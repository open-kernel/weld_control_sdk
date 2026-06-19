import struct
import time
import ubinascii
import json
import os
from micropython import const
import bluetooth
import uasyncio as asyncio
import machine
import neopixel
from sdk_codec import (
    sdk_device_create, sdk_device_reset_parser, sdk_decode,
    CMD_DEVICE_GET_INFO, CMD_DEVICE_GET_INFO_ACK,
    CMD_DEVICE_PAIR_REQUEST, CMD_DEVICE_PAIR_REQUEST_ACK, CMD_PING, CMD_PONG, CMD_DEVICE_PAIR_UNPAIR, CMD_DEVICE_PAIR_UNPAIR_ACK, CMD_DEVICE_AUTH, CMD_DEVICE_AUTH_ACK, CMD_DEVICE_AUTH_REQUIRED,
    CMD_DEVICE_RESET, CMD_DEVICE_RESET_ACK,
    CMD_OTA_START, CMD_OTA_START_ACK, CMD_OTA_DATA, CMD_OTA_DATA_ACK, CMD_OTA_VERIFY, CMD_OTA_VERIFY_ACK,
    CMD_OTA_ABORT, CMD_OTA_ABORT_ACK,
    CMD_READ_DASHBOARD_INIT, CMD_READ_DASHBOARD_INIT_ACK, CMD_DASHBOARD_START, CMD_DASHBOARD_START_ACK,
    CMD_DASHBOARD_STOP, CMD_DASHBOARD_STOP_ACK,
    CMD_DASHBOARD_COMPACT, CMD_DASHBOARD_WELD_RECORDS, CMD_DASHBOARD_LOGS,
    CMD_SETTINGS_READ_CURRENT, CMD_SETTINGS_READ_CURRENT_ACK, CMD_SETTINGS_READ_SELF_CHECK, CMD_SETTINGS_READ_SELF_CHECK_ACK,
    CMD_SETTINGS_PROFILE, CMD_SETTINGS_PROFILE_ACK,
    CMD_SETTINGS_RESET, CMD_SETTINGS_RESET_ACK, CMD_MANUAL_TRIGGER, CMD_MANUAL_TRIGGER_ACK,
    CMD_SAFE_DISCHARGE, CMD_SAFE_DISCHARGE_ACK, CMD_SAFE_DISCHARGE_STOP, CMD_SAFE_DISCHARGE_STOP_ACK,
    CMD_CHARGE_START, CMD_CHARGE_START_ACK, CMD_CHARGE_PAUSE, CMD_CHARGE_PAUSE_ACK,
    SDK_PROTOCOL_VERSION, SDK_MIN_PROTOCOL_VERSION, SDK_OTA_MAX_FW_SIZE, SDK_OTA_CHUNK_MAX,
    SDK_AUTH_FREE_COMMANDS,
    OTA_CODE_NONE, OTA_CODE_SIZE, OTA_CODE_SEQ, OTA_CODE_CRC, OTA_CODE_BUSY, OTA_CODE_COMPANY, OTA_CODE_TYPE,
    SDK_RESULT_STATUS_OK, SDK_RESULT_STATUS_FAIL, SDK_RESULT_STATUS_BUSY, SDK_RESULT_STATUS_AUTH_INVALID,
    SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_STATUS_DEVICE_ERROR,
    SDK_RESULT_STATUS_PENDING,
    SDK_RESULT_CODE_AUTH_INVALID_TOKEN, SDK_RESULT_CODE_AUTH_REJECTED_COMMAND,
    SDK_RESULT_CODE_COMMON_NONE, SDK_RESULT_CODE_COMMON_UNKNOWN, SDK_RESULT_CODE_COMMON_PROTOCOL_UNSUPPORTED,
    SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT, SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE,
    SDK_RESULT_CODE_PAIR_REJECTED, SDK_RESULT_CODE_PAIR_WAIT_CONFIRM,
    SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID,
    SDK_TOKEN_LEN, sdk_result_t, pair_token_t, pair_request_t, auth_request_t, sdk_packet_t,
    device_info_t,
    ota_start_t, ota_data_hdr_t, ota_verify_t, ota_ack_data_t,
    SDK_SVC_UUID, SDK_CHR_TX_UUID, SDK_CHR_RX_UUID,
    sdk_build_adv_data)
from sdk_frame_chunk import sdk_frame_chunk_with_encode_iter
from sdk_compat import (
    SDK_COMPAT_CODE_APP_TOO_OLD,
    sdk_check_protocol_compat,
)
from payloads.settings import (
    SETTINGS_TRIGGER_MODE_UNSET, SETTINGS_TRIGGER_MODE_MANUAL, SETTINGS_TRIGGER_MODE_AUTO,
    SETTINGS_ESR_QUALITY_EXCELLENT, SETTINGS_FAULT_LOG_TYPE_WARN, SETTINGS_FAULT_LOG_TYPE_ERROR,
    SETTINGS_RESET_FLAG_CLEAR_TOKENS,
    settings_apply_profile_t, settings_current_t, settings_reset_t, settings_self_check_t)
from payloads.dashboard import (
    DASHBOARD_LOG_CODE_SYSTEM_READY, DASHBOARD_LOG_CODE_CHARGE_STARTED, DASHBOARD_LOG_CODE_WELD_COMPLETE,
    DASHBOARD_CHARGE_MODE_CONSTANT_CURRENT, DASHBOARD_CHARGE_MODE_CONSTANT_VOLTAGE,
    DASHBOARD_CHARGE_MODE_PRECHARGE, DASHBOARD_CHARGE_MODE_FLOAT, DASHBOARD_CHARGE_MODE_PAUSED,
    DASHBOARD_LOG_CODE_CONFIG_UPDATED, dashboard_compact_t, dashboard_init_t,
    dashboard_init_request_t, dashboard_logs_t, dashboard_weld_records_t)

_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)

SDK_NOTIFY_CHUNK_SIZE = const(20)
SDK_NOTIFY_RETRY_DELAY_MS = const(30)
SDK_PARTIAL_FRAME_TIMEOUT_MS = const(1000)
OTA_SESSION_TIMEOUT_MS = const(15000)

# WS2812 灯效控制器：管理设备上的三色 RGB LED (如指示灯)
class WS2812Controller:
    def __init__(self, pin=48, num_leds=1):
        self.np = neopixel.NeoPixel(machine.Pin(pin, machine.Pin.OUT), num_leds)
        self.state = 'OFF'
        self.task = None
        self.off()

    # 设置 LED 的 RGB 颜色值
    def set_color(self, r, g, b):
        self.np[0] = (r, g, b)
        self.np.write()

    # 关闭 LED 并取消当前闪烁等异步任务
    def off(self):
        self.state = 'OFF'
        self.set_color(0, 0, 0)
        self._cancel_task()

    # 设为常亮指定颜色
    def solid(self, r, g, b):
        self.state = 'SOLID'
        self._cancel_task()
        self.set_color(r, g, b)

    # 设为交替闪烁指定颜色
    def blink(self, r, g, b, interval_ms=500):
        self.state = 'BLINK'
        self._cancel_task()
        self.task = asyncio.create_task(self._blink_loop(r, g, b, interval_ms))

    # 安全地取消当前的异步闪烁任务
    def _cancel_task(self):
        if self.task:
            self.task.cancel()
            self.task = None

    # 异步闪烁循环
    async def _blink_loop(self, r, g, b, interval_ms):
        on = True
        try:
            while True:
                if on:
                    self.set_color(r, g, b)
                else:
                    self.set_color(0, 0, 0)
                on = not on
                await asyncio.sleep_ms(interval_ms)
        except asyncio.CancelledError:
            pass


# 按键监控类：通过定时轮询按键引脚状态，识别单击、双/三击、长按
class Button:
    def __init__(self, pin=0):
        self.pin = machine.Pin(pin, machine.Pin.IN, machine.Pin.PULL_UP)
        self.on_single_click = None # 单击回调函数
        self.on_long_press = None   # 长按回调函数
        self.on_triple_click = None # 三击回调函数
        asyncio.create_task(self._monitor())

    async def _monitor(self):
        last_state = 1
        press_time = 0
        click_count = 0
        last_click_time = 0
        long_press_triggered = False

        while True:
            state = self.pin.value()
            now = time.ticks_ms()

            if state == 0 and last_state == 1:
                # Pressed
                press_time = now
                long_press_triggered = False
                await asyncio.sleep_ms(20) # debounce
            elif state == 1 and last_state == 0:
                # Released
                if not long_press_triggered:
                    duration = time.ticks_diff(now, press_time)
                    if 20 < duration < 1000:
                        click_count += 1
                        last_click_time = now
                await asyncio.sleep_ms(20) # debounce
            elif state == 0 and last_state == 0:
                # Held
                duration = time.ticks_diff(now, press_time)
                if duration >= 3000 and not long_press_triggered:
                    long_press_triggered = True
                    click_count = 0
                    if self.on_long_press:
                        self.on_long_press()

            # Handle multi-click timeout
            if click_count > 0 and time.ticks_diff(now, last_click_time) > 400:
                if click_count == 1:
                    if self.on_single_click:
                        self.on_single_click()
                elif click_count >= 3:
                    if self.on_triple_click:
                        self.on_triple_click()
                click_count = 0

            last_state = state
            await asyncio.sleep_ms(10)


class BLEDevice:
    def __init__(self, name):
        self.ble = bluetooth.BLE()
        self.ble.active(True)
        self.ble.irq(self._irq)
        
        # Adjust MTU if possible (optional, ESP32 defaults to 23, but can be negotiated)
        try:
            # 配置 BLE Gap 名称、MTU 大小、绑定使能以及接收缓冲区大小
            self.ble.config(gap_name=name, mtu=517, bond=True, rxbuf=2048)
        except Exception:
            pass

        # Get MAC
        self.mac = self.ble.config('mac')[1]
        self.name = name

        self.connections = set()
        self.sdk_devices = {}
        self.partial_frame_started_ms = {}

        # Register SDK communication service. Device info is read by CMD_DEVICE_GET_INFO,
        # not by BLE Device Information Service.
        ((self.tx, self.rx),) = self.ble.gatts_register_services((
            (SDK_SVC_UUID, (
                (SDK_CHR_TX_UUID, bluetooth.FLAG_NOTIFY),
                (SDK_CHR_RX_UUID, bluetooth.FLAG_WRITE | bluetooth.FLAG_WRITE_NO_RESPONSE),
            )),
        ))

        self.manufacturer_name = '焊机大厂'
        self.model_number = 'SC-01'
        self.serial_number = ubinascii.hexlify(self.mac).decode('ascii')
        self.firmware_version = (1, 0, 0)
        self.firmware_build_id = 20260606
        self.hardware_version = (1, 0, 0)
        self.company_id = 0xAAAA
        self.product_id = 0x0001
        
        import feature_mask
        feature_mask.feature_mask_add(feature_mask.FEATURE_01 | feature_mask.FEATURE_02 | feature_mask.FEATURE_03 | feature_mask.FEATURE_12)
        self.feature_mask = feature_mask.feature_mask_get()

        # 初始化 LED 和按键，绑定按键事件处理函数
        self.led = WS2812Controller(pin=48)
        self.btn = Button(pin=0)
        self.btn.on_single_click = self.handle_single_click
        self.btn.on_long_press = self.handle_long_press
        self.btn.on_triple_click = self.handle_triple_click

        # 状态管理变量
        self.pairing_mode = False           # 是否处于允许新配对的模式
        self.pairing_timeout_task = None    # 允许配对模式的超时定时任务
        self.confirm_timeout_task = None    # 等待按键确认配对的超时定时任务
        self.waiting_confirm_conn = None    # 正在等待用户按键确认配对的连接句柄
        self.waiting_confirm_seq = None     # 配对请求的序列号(SEQ)，用于回复时匹配

        self.active_conn = None             # 当前已成功配对或通过身份验证(鉴权)的唯一活跃连接句柄
        self.conn_macs = {}                 # 物理连接句柄与对端 MAC 地址的映射表
        self.conn_tokens = {}               # 物理连接句柄与当前会话/绑定 Token 的映射表
        self.auth_timeout_tasks = {}        # 各连接的鉴权超时定时任务(连接后3秒内需完成鉴权，否则断开)
        self.dashboard_stream_task = None   # Dashboard 实时上报任务，连接断开或重新 init 时取消
        self.dashboard_tick = 0
        self.dashboard_weld_id = 100
        self.safe_discharge_active = False
        self.charge_paused = False
        self.ota_state = None
        self.ota_timeout_task = None
        # Demo only: 保存多个已配对上位机/App 的 Token。
        # 正式设备建议提供已配对上位机管理入口，用于查看和移除指定 Token。
        self.g_bonded = set()
        self.load_bonded()                  # 从本地文件加载已绑定的 Token
        self.settings_state = self.create_default_settings_state()

        # 开启 RX 特征值的 append 模式 (True)。
        # 这对于处理"沾包"和"半包"至关重要。如果不开启，连续快速的 Write 无响应会被底层覆盖，
        # gatts_read 只能读到最后一包，导致丢包。开启后 gatts_read 会返回累积的流数据并清空。
        self.ble.gatts_set_buffer(self.rx, 2048, True)

        print("BLE initialized. Device IDLE. Long press BOOT to start pairing.")
        self.led.off()
        self.start_advertising()

    # 从闪存文件 bonded.json 中读取多个已绑定 Token
    def load_bonded(self):
        try:
            if 'bonded.json' in os.listdir():
                with open('bonded.json', 'r') as f:
                    data = json.load(f)
                    self.g_bonded = set(bytes.fromhex(token_hex) for token_hex in data)
                print("Loaded bonded devices:", data)
            else:
                print("No bonded devices found.")
        except Exception as e:
            print("Failed to load bonded devices:", e)

    # 将当前绑定的多个 Token 序列化并保存到本地闪存中
    def save_bonded(self):
        try:
            data = [ubinascii.hexlify(token).decode() for token in self.g_bonded]
            with open('bonded.json', 'w') as f:
                json.dump(data, f)
            print("Saved bonded devices:", data)
        except Exception as e:
            print("Failed to save bonded devices:", e)

    def create_default_settings_state(self):
        now = int(time.time())
        log_base_time = now if now > 1700000000 else 1718000000
        return {
            'current_profile': {
                'id': 1,
                'name': '设备当前参数',
                'target_voltage_mv': 3850,
                'target_current_a10': 100,
                'preheat_pulse_ms10': 125,
                'cool_time_ms10': 450,
                'main_pulse_ms10': 280,
                'trigger_mode': SETTINGS_TRIGGER_MODE_MANUAL,
                'auto_delay_ms': 500,
                'updated_at_sec': now,
            },
            'limits_max': {
                'target_voltage_mv_max': 12000,
                'target_current_a10_max': 200,
                'preheat_pulse_ms10_max': 2000,
                'cool_time_ms10_max': 2000,
                'main_pulse_ms10_max': 2000,
                'auto_delay_ms_max': 8000,
            },
            'esrs_mohm10': [83, 87, 85, 89],
            'esr_quality': SETTINGS_ESR_QUALITY_EXCELLENT,
            'fault_logs': [
                {
                    'id': 1,
                    'type': SETTINGS_FAULT_LOG_TYPE_WARN,
                    'time_sec': log_base_time - 180,
                    'title': '充电曲线观察',
                    'message': '电容充电斜率存在短暂波动，当前仍处于安全阈值内。',
                },
                {
                    'id': 2,
                    'type': SETTINGS_FAULT_LOG_TYPE_ERROR,
                    'time_sec': log_base_time - 960,
                    'title': '历史保护记录',
                    'message': '上一次测试触发过压保护，已自动复位并等待人工确认。',
                },
            ],
        }

    def get_current_runtime_profile(self):
        profile = self.settings_state.get('current_profile') or self.create_default_settings_state()['current_profile']
        return {
            'target_voltage_mv': profile['target_voltage_mv'],
            'target_current_a10': profile['target_current_a10'],
            'preheat_pulse_ms10': profile['preheat_pulse_ms10'],
            'cool_time_ms10': profile['cool_time_ms10'],
            'main_pulse_ms10': profile['main_pulse_ms10'],
            'trigger_mode': profile['trigger_mode'],
            'auto_delay_ms': profile['auto_delay_ms'],
        }

    def get_settings_limits_max(self):
        limits = self.settings_state.get('limits_max') or self.create_default_settings_state()['limits_max']
        return {
            'target_voltage_mv_max': limits['target_voltage_mv_max'],
            'target_current_a10_max': limits['target_current_a10_max'],
            'preheat_pulse_ms10_max': limits['preheat_pulse_ms10_max'],
            'cool_time_ms10_max': limits['cool_time_ms10_max'],
            'main_pulse_ms10_max': limits['main_pulse_ms10_max'],
            'auto_delay_ms_max': limits['auto_delay_ms_max'],
        }

    def get_current_profile_id(self):
        profile = self.settings_state.get('current_profile') or {}
        profile_id = profile.get('id', 0)
        return profile_id if 1 <= profile_id <= 10 else 0

    def apply_settings_profile(self, profile_id, runtime_profile):
        current = self.settings_state.get('current_profile')
        if not current:
            current = self.create_default_settings_state()['current_profile']
            self.settings_state['current_profile'] = current
        current['id'] = profile_id if 1 <= profile_id <= 10 else 0
        current['target_voltage_mv'] = runtime_profile['target_voltage_mv']
        current['target_current_a10'] = runtime_profile['target_current_a10']
        current['preheat_pulse_ms10'] = runtime_profile['preheat_pulse_ms10']
        current['cool_time_ms10'] = runtime_profile['cool_time_ms10']
        current['main_pulse_ms10'] = runtime_profile['main_pulse_ms10']
        current['trigger_mode'] = runtime_profile['trigger_mode']
        current['auto_delay_ms'] = runtime_profile['auto_delay_ms']
        current['updated_at_sec'] = int(time.time())

    async def handle_device_get_info(self, conn_handle, seq):
        payload = device_info_t.pack({
            'company_id': self.company_id,
            'product_id': self.product_id,
            'feature_mask': self.feature_mask,
            'protocol_version': SDK_PROTOCOL_VERSION,
            'protocol_min_version': SDK_MIN_PROTOCOL_VERSION,
            'ota_max_kb': SDK_OTA_MAX_FW_SIZE // 1024,
            'ota_chunk_max': SDK_OTA_CHUNK_MAX,
            'firmware_major': self.firmware_version[0],
            'firmware_minor': self.firmware_version[1],
            'firmware_patch': self.firmware_version[2],
            'firmware_build_id': self.firmware_build_id,
            'hardware_major': self.hardware_version[0],
            'hardware_minor': self.hardware_version[1],
            'hardware_patch': self.hardware_version[2],
            'manufacturer': self.manufacturer_name,
            'model': self.model_number,
            'serial': self.serial_number,
        })
        await self.send_packet_async(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload))

    def _cancel_task_safe(self, task):
        if task:
            try:
                # Avoid cancelling the currently running task to prevent RuntimeError
                if hasattr(asyncio, 'current_task') and task == asyncio.current_task():
                    return
                task.cancel()
            except Exception:
                pass

    # 开启或关闭新设备配对模式
    def set_pairing_mode(self, active):
        self.pairing_mode = active
        
        self._cancel_task_safe(self.pairing_timeout_task)
        self.pairing_timeout_task = None
        
        self._cancel_task_safe(self.confirm_timeout_task)
        self.confirm_timeout_task = None

        if active:
            print("Entering Pairing Mode")
            self.led.solid(0, 0, 50) # 配对模式下 LED 亮蓝色
            # 开启 60s 的配对窗口期，超时自动退出配对模式
            self.pairing_timeout_task = asyncio.create_task(self._pairing_timeout())
        else:
            print("Exiting Pairing Mode")
            self.led.off()
            # 退出配对模式时不需要关闭广播。如果未连接，广播本就在一直运行；如果已连接，底层协议栈会自动停止广播。
            self.waiting_confirm_conn = None
            self.waiting_confirm_seq = None

    # 配对窗口期超时（60s）任务
    async def _pairing_timeout(self):
        try:
            await asyncio.sleep(60)
            print("Pairing timeout 60s reached.")
            self.set_pairing_mode(False)
        except asyncio.CancelledError:
            pass

    # 3秒鉴权超时：如果设备连接后3秒内未通过身份验证，则发送鉴权失败 ACK 并断开连接
    async def _auth_timeout(self, conn_handle):
        try:
            await asyncio.sleep(3)
            # 发送鉴权超时的原因：status=AUTH_INVALID, code=INVALID_TOKEN。
            # 因为没有收到主机的指令，SEQ 可以默认填 0
            self.send_packet(conn_handle, CMD_DEVICE_AUTH_ACK, 0, sdk_result_t.pack(SDK_RESULT_STATUS_AUTH_INVALID, SDK_RESULT_CODE_AUTH_INVALID_TOKEN))
            
            # 延迟 200ms 等待蓝牙底层将这包数据真实发送到空中
            await asyncio.sleep_ms(200)
            
            print(f"Auth timeout for handle {conn_handle}. Disconnecting.")
            self.ble.gap_disconnect(conn_handle)
        except asyncio.CancelledError:
            pass
        finally:
            self.auth_timeout_tasks.pop(conn_handle, None)

    async def _disconnect_pair_conn_after_result(self, conn_handle, delay_ms=500):
        await asyncio.sleep_ms(delay_ms)
        try:
            self.ble.gap_disconnect(conn_handle)
            print(f"Disconnected pairing connection: handle={conn_handle}")
        except Exception as e:
            print("Error disconnecting pairing connection:", e)

    # 60秒按键确认超时：收到配对请求后，如果用户在 60s 内未按下物理按键确认，则回复失败并断开该连接
    async def _confirm_timeout(self):
        try:
            await asyncio.sleep(60)
            print("Confirm timeout 60s reached. No confirmation received.")
            conn = self.waiting_confirm_conn
            seq = self.waiting_confirm_seq
            if conn is not None and seq is not None:
                self.waiting_confirm_conn = None
                self.waiting_confirm_seq = None
                resp_payload = sdk_result_t.pack(SDK_RESULT_STATUS_FAIL, SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT)
                self.send_packet(conn, CMD_DEVICE_PAIR_REQUEST_ACK, seq, resp_payload)
                await self._disconnect_pair_conn_after_result(conn)
            self.set_pairing_mode(False)
        except asyncio.CancelledError:
            pass

    # 处理按键长按 (3s)：确认接受配对，或者进入配对状态
    def handle_long_press(self):
        print("Button Long Press 3s")
        if self.waiting_confirm_conn is not None:
            # 接受配对请求
            print("Accepting Pair Request")
            
            # 生成随机 Token，用于后续的断线重连鉴权
            token = os.urandom(SDK_TOKEN_LEN)
            self.g_bonded.add(token)
            self.save_bonded()

            # 回复配对成功数据包，data 固定为 Token
            resp_payload = sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, pair_token_t.pack(token))
            self.send_packet(self.waiting_confirm_conn, CMD_DEVICE_PAIR_REQUEST_ACK, self.waiting_confirm_seq, resp_payload)
            
            # 将该连接标记为当前的活跃连接
            self.active_conn = self.waiting_confirm_conn
            self.conn_tokens[self.waiting_confirm_conn] = token
            self.waiting_confirm_conn = None
            self.waiting_confirm_seq = None
            
            self.led.solid(0, 50, 0) # 配对并连接成功后 LED 亮绿色
            self.pairing_mode = False # 配对完成，退出配对模式
            # 此处无需显式操作广播，底层协议栈在物理连接建立时已自动停止广播
            
            self._cancel_task_safe(self.confirm_timeout_task)
            self.confirm_timeout_task = None
        else:
            # 未在等待确认配对时，长按进入配对模式
            self.set_pairing_mode(True)

    # 处理按键单击：拒绝当前的配对请求
    def handle_single_click(self):
        print("Button Single Click")
        if self.waiting_confirm_conn is not None:
            # 拒绝配对请求
            print("Rejecting Pair Request")
            resp_payload = sdk_result_t.pack(SDK_RESULT_STATUS_FAIL, SDK_RESULT_CODE_PAIR_REJECTED)
            self.send_packet(self.waiting_confirm_conn, CMD_DEVICE_PAIR_REQUEST_ACK, self.waiting_confirm_seq, resp_payload)
            
            conn_to_drop = self.waiting_confirm_conn
            self.waiting_confirm_conn = None
            self.waiting_confirm_seq = None
            
            # 延迟断开连接，确保拒绝配对的最终 ACK 成功送出
            asyncio.create_task(self._disconnect_pair_conn_after_result(conn_to_drop))
                
            # 重新回到配对广播状态，等待其他设备的连接与配对
            self.set_pairing_mode(True)

    # 处理按键三击：强制断开所有当前连接，并退出配对模式
    def handle_triple_click(self):
        print("Button Triple Click")
        if self.connections:
            for conn in list(self.connections):
                self.ble.gap_disconnect(conn)
        self.set_pairing_mode(False)

    # 蓝牙底层事件回调处理函数
    def _irq(self, event, data):
        if event == _IRQ_CENTRAL_CONNECT:
            # 收到主机连接成功事件
            conn_handle, addr_type, addr = data
            mac_bytes = bytes(addr)
            print(f"Connected: handle={conn_handle}, physical MAC={ubinascii.hexlify(mac_bytes)}")
            
            self.connections.add(conn_handle)
            self.conn_macs[conn_handle] = mac_bytes
            
            # 1. 独占性校验：如果当前已经有活跃的已鉴权连接，则直接断开新进来的物理连接
            if self.active_conn is not None:
                print("Already connected. Rejecting new connection.")
                self.ble.gap_disconnect(conn_handle)
                return

            self.get_sdk_device(conn_handle)
            
            # 注意：此处不启动鉴权定时器，为配对/鉴权流程预留时间

            
            self._cancel_task_safe(self.pairing_timeout_task)
            self.pairing_timeout_task = None

        elif event == _IRQ_CENTRAL_DISCONNECT:
            # 主机断开连接事件
            conn_handle, addr_type, addr = data
            print(f"Disconnected: handle={conn_handle}")
            if conn_handle in self.connections:
                self.connections.remove(conn_handle)
            self.conn_macs.pop(conn_handle, None)
            self.conn_tokens.pop(conn_handle, None)
            self.sdk_devices.pop(conn_handle, None)
            self.partial_frame_started_ms.pop(conn_handle, None)
            
            task = self.auth_timeout_tasks.pop(conn_handle, None)
            self._cancel_task_safe(task)
                
            if self.active_conn == conn_handle:
                self.active_conn = None
                self._cancel_task_safe(self.dashboard_stream_task)
                self.dashboard_stream_task = None
                self.clear_ota_state("disconnect")
                
            if self.waiting_confirm_conn == conn_handle:
                self.waiting_confirm_conn = None
                self.waiting_confirm_seq = None
                self._cancel_task_safe(self.confirm_timeout_task)
                self.confirm_timeout_task = None

            # 若已无活跃连接，根据是否在配对模式来决定：重新开启配对模式广播，或普通白名单广播
            if self.active_conn is None:
                self.start_advertising() # 恢复常规广播
                if self.pairing_mode:
                    self.set_pairing_mode(True)
                else:
                    self.led.off()

        elif event == _IRQ_GATTS_WRITE:
            conn_handle, value_handle = data
            if value_handle == self.rx:
                rx_data = self.ble.gatts_read(self.rx)
                sdk_dev = self.get_sdk_device(conn_handle)
                self.reset_stale_sdk_parser_if_needed(conn_handle, "before-rx")
                offset = 0
                while offset < len(rx_data):
                    result = sdk_decode(sdk_dev, rx_data[offset:])
                    if result['kind'] == 'packet':
                        self.clear_partial_frame_timer(conn_handle)
                        self.handle_packet(conn_handle, result['packet'])
                        offset += result['consumed']
                    elif result['kind'] == 'need-more':
                        self.start_partial_frame_timer_if_needed(conn_handle)
                        break
                    else:
                        self.clear_partial_frame_timer(conn_handle)
                        offset += result['consumed']

    def start_advertising(self):
        adv_data, resp_data = sdk_build_adv_data(self.mac, self.name)
        print("Starting advertising with MAC:", ubinascii.hexlify(self.mac))
        self.ble.gap_advertise(100000, adv_data=adv_data, resp_data=resp_data)

    def get_sdk_device(self, conn_handle):
        sdk_dev = self.sdk_devices.get(conn_handle)
        if sdk_dev is None:
            sdk_dev = sdk_device_create(
                mac=self.conn_macs.get(conn_handle, self.mac),
                name="%s:%s" % (self.name, conn_handle),
            )
            self.sdk_devices[conn_handle] = sdk_dev
        return sdk_dev

    def start_partial_frame_timer_if_needed(self, conn_handle):
        if conn_handle not in self.partial_frame_started_ms:
            self.partial_frame_started_ms[conn_handle] = time.ticks_ms()

    def clear_partial_frame_timer(self, conn_handle):
        self.partial_frame_started_ms.pop(conn_handle, None)

    def reset_stale_sdk_parser_if_needed(self, conn_handle, reason):
        started_ms = self.partial_frame_started_ms.get(conn_handle)
        if started_ms is None:
            return
        if time.ticks_diff(time.ticks_ms(), started_ms) <= SDK_PARTIAL_FRAME_TIMEOUT_MS:
            return
        sdk_dev = self.sdk_devices.get(conn_handle)
        if sdk_dev:
            print("[SDK] parser partial frame timeout:", reason, "conn", conn_handle)
            sdk_device_reset_parser(sdk_dev)
        self.clear_partial_frame_timer(conn_handle)

    def send_packet(self, conn_handle, cmd, seq, payload):
        pkt = sdk_packet_t(cmd=cmd, seq=seq, payload=payload)
        sdk_dev = self.get_sdk_device(conn_handle)
        logged = False
        for chunk in sdk_frame_chunk_with_encode_iter(sdk_dev, pkt, SDK_NOTIFY_CHUNK_SIZE):
            if not logged:
                print(f"[{time.ticks_ms()}] Sending Packet: CMD=0x{cmd:02X}, SEQ={seq}, LEN={chunk['total_len']}")
                logged = True
            self.ble.gatts_notify(conn_handle, self.tx, chunk['data'])
            # 如果发现微信端接收丢包，可以取消下面这行延时的注释
            time.sleep_ms(5)

    async def send_packet_async(self, conn_handle, cmd, seq, payload):
        pkt = sdk_packet_t(cmd=cmd, seq=seq, payload=payload)
        sdk_dev = self.get_sdk_device(conn_handle)
        logged = False
        for chunk in sdk_frame_chunk_with_encode_iter(sdk_dev, pkt, SDK_NOTIFY_CHUNK_SIZE):
            if not logged:
                print(f"[{time.ticks_ms()}] Sending Packet Async: CMD=0x{cmd:02X}, SEQ={seq}, LEN={chunk['total_len']}")
                logged = True
            while True:
                try:
                    self.ble.gatts_notify(conn_handle, self.tx, chunk['data'])
                    break
                except OSError as err:
                    errno = getattr(err, 'errno', None)
                    if errno is None and getattr(err, 'args', None):
                        errno = err.args[0]
                    if errno != 12:
                        raise
                    await asyncio.sleep_ms(SDK_NOTIFY_RETRY_DELAY_MS)
            await asyncio.sleep_ms(5)

    def is_authenticated_conn(self, conn_handle):
        return self.active_conn == conn_handle and conn_handle in self.conn_tokens

    def requires_auth(self, cmd):
        return cmd not in SDK_AUTH_FREE_COMMANDS

    def send_auth_required_kick(self, conn_handle, pkt):
        self.send_packet(
            conn_handle,
            CMD_DEVICE_AUTH_REQUIRED,
            pkt.seq,
            sdk_result_t.pack(SDK_RESULT_STATUS_AUTH_INVALID, SDK_RESULT_CODE_AUTH_REJECTED_COMMAND, bytes([pkt.cmd & 0xFF])),
        )

        async def delayed_disconnect(conn):
            await asyncio.sleep_ms(200)
            try:
                self.ble.gap_disconnect(conn)
            except Exception:
                pass
        asyncio.create_task(delayed_disconnect(conn_handle))

    async def handle_settings_read_current(self, conn_handle, seq):
        payload = settings_current_t.pack({
            'profile_id': self.get_current_profile_id(),
            'current_profile': self.get_current_runtime_profile(),
            'limits_max': self.get_settings_limits_max(),
        })
        await self.send_packet_async(conn_handle, CMD_SETTINGS_READ_CURRENT_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload))

    async def handle_settings_read_self_check(self, conn_handle, seq):
        payload = settings_self_check_t.pack({
            'esrs_mohm10': self.settings_state.get('esrs_mohm10', []),
            'esr_quality': self.settings_state.get('esr_quality', 0),
            'fault_logs': self.settings_state.get('fault_logs', []),
        })
        if payload is None:
            await self.send_packet_async(
                conn_handle,
                CMD_SETTINGS_READ_SELF_CHECK_ACK,
                seq,
                sdk_result_t.pack(SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN),
            )
            return
        await self.send_packet_async(conn_handle, CMD_SETTINGS_READ_SELF_CHECK_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload))

    async def handle_settings_apply_profile(self, conn_handle, seq, payload):
        submit = settings_apply_profile_t.unpack(payload)
        if submit is None:
            await self.send_packet_async(conn_handle, CMD_SETTINGS_PROFILE_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID))
            return
        self.apply_settings_profile(submit['profile_id'], submit['profile'])
        await self.send_packet_async(conn_handle, CMD_SETTINGS_PROFILE_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))

    async def handle_settings_reset(self, conn_handle, seq, payload):
        request = settings_reset_t.unpack(payload)
        if request is None:
            await self.send_packet_async(conn_handle, CMD_SETTINGS_RESET_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID))
            return
        self.settings_state = self.create_default_settings_state()
        clear_tokens = bool(request.get('flags', 0) & SETTINGS_RESET_FLAG_CLEAR_TOKENS)
        if clear_tokens:
            self.g_bonded.clear()
            self.conn_tokens.clear()
            self.save_bonded()
        await self.send_packet_async(conn_handle, CMD_SETTINGS_RESET_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))
        if clear_tokens:
            async def delayed_disconnect(conn):
                await asyncio.sleep_ms(200)
                try:
                    self.ble.gap_disconnect(conn)
                except Exception:
                    pass
            asyncio.create_task(delayed_disconnect(conn_handle))

    def build_ota_result(self, status, code=OTA_CODE_NONE, next_chunk=0):
        return sdk_result_t.pack(status, code, ota_ack_data_t.pack(next_chunk))

    async def handle_ota_start(self, conn_handle, seq, payload):
        start = ota_start_t.unpack(payload)
        if start is None:
            print("[OTA] START invalid payload_len", len(payload), "expected 14")
            await self.send_packet_async(conn_handle, CMD_OTA_START_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, 0))
            return
        print("[OTA] START", start, "max_fw", SDK_OTA_MAX_FW_SIZE, "max_chunk", SDK_OTA_CHUNK_MAX)
        if start['company_id'] != self.company_id:
            print("[OTA] START reject company got", hex(start['company_id']), "expected", hex(self.company_id))
            await self.send_packet_async(conn_handle, CMD_OTA_START_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_COMPANY, 0))
            return
        if start['fw_type'] != 0x01:
            print("[OTA] START reject type got", start['fw_type'], "expected", 0x01)
            await self.send_packet_async(conn_handle, CMD_OTA_START_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_TYPE, 0))
            return
        if start['fw_size'] <= 0 or start['fw_size'] > SDK_OTA_MAX_FW_SIZE or start['chunk_size'] <= 0 or start['chunk_size'] > SDK_OTA_CHUNK_MAX:
            print("[OTA] START reject size/chunk fw_size", start['fw_size'], "max_fw", SDK_OTA_MAX_FW_SIZE, "chunk", start['chunk_size'], "max_chunk", SDK_OTA_CHUNK_MAX)
            await self.send_packet_async(conn_handle, CMD_OTA_START_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, 0))
            return

        self.ota_state = {
            'fw_size': start['fw_size'],
            'fw_crc16': start['fw_crc16'],
            'chunk_size': start['chunk_size'],
            'next_chunk': 0,
            'received_size': 0,
            'crc16': 0xFFFF,
            'updated_ms': time.ticks_ms(),
        }
        self.refresh_ota_timeout()
        await self.send_packet_async(conn_handle, CMD_OTA_START_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_OK, OTA_CODE_NONE, 0))

    async def handle_ota_data(self, conn_handle, seq, payload):
        state = self.ota_state
        if self.is_ota_expired():
            self.clear_ota_state("timeout-before-data")
            state = None
        if not state:
            await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_BUSY, OTA_CODE_BUSY, 0))
            return
        header = ota_data_hdr_t.unpack(payload)
        if header is None:
            await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, state['next_chunk']))
            return
        data = payload[4:4 + header['data_len']]
        if header['chunk_index'] != state['next_chunk']:
            print("[OTA] SEQ mismatch, got", header['chunk_index'], "expected", state['next_chunk'])
            await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SEQ, state['next_chunk']))
            return
        if len(data) != header['data_len'] or header['data_len'] <= 0 or header['data_len'] > state['chunk_size']:
            await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, state['next_chunk']))
            return
        if state['received_size'] + header['data_len'] > state['fw_size']:
            await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, state['next_chunk']))
            return

        state['crc16'] = self.crc16_ccitt_false_update(state['crc16'], data)
        state['received_size'] += header['data_len']
        state['next_chunk'] += 1
        state['updated_ms'] = time.ticks_ms()
        self.refresh_ota_timeout()
        if state['next_chunk'] % 8 == 0 or state['received_size'] >= state['fw_size']:
            print("[OTA] DATA received", state['received_size'], "/", state['fw_size'], "next", state['next_chunk'])
        await self.send_packet_async(conn_handle, CMD_OTA_DATA_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_OK, OTA_CODE_NONE, state['next_chunk']))

    async def handle_ota_verify(self, conn_handle, seq, payload):
        state = self.ota_state
        if self.is_ota_expired():
            self.clear_ota_state("timeout-before-verify")
            state = None
        verify = ota_verify_t.unpack(payload)
        if not state or verify is None:
            await self.send_packet_async(conn_handle, CMD_OTA_VERIFY_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_BUSY, OTA_CODE_BUSY, 0))
            return
        print("[OTA] VERIFY", verify, "received", state['received_size'], "crc", state['crc16'])
        if verify['fw_size'] != state['fw_size'] or state['received_size'] != state['fw_size']:
            await self.send_packet_async(conn_handle, CMD_OTA_VERIFY_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, state['next_chunk']))
            return
        if verify['fw_crc16'] != state['fw_crc16'] or state['crc16'] != state['fw_crc16']:
            await self.send_packet_async(conn_handle, CMD_OTA_VERIFY_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_FAIL, OTA_CODE_CRC, state['next_chunk']))
            return
        print("[OTA] Simulated firmware update complete. No flash operation performed.")
        self.clear_ota_state("verify-ok")
        await self.send_packet_async(conn_handle, CMD_OTA_VERIFY_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_OK, OTA_CODE_NONE, state['next_chunk']))

    async def handle_ota_abort(self, conn_handle, seq):
        self.clear_ota_state("abort")
        await self.send_packet_async(conn_handle, CMD_OTA_ABORT_ACK, seq, self.build_ota_result(SDK_RESULT_STATUS_OK, OTA_CODE_NONE, 0))

    def refresh_ota_timeout(self):
        self._cancel_task_safe(self.ota_timeout_task)
        self.ota_timeout_task = asyncio.create_task(self.ota_timeout_loop())

    async def ota_timeout_loop(self):
        await asyncio.sleep_ms(OTA_SESSION_TIMEOUT_MS)
        if self.is_ota_expired():
            self.clear_ota_state("timeout")

    def is_ota_expired(self):
        state = self.ota_state
        if not state:
            return False
        return time.ticks_diff(time.ticks_ms(), state.get('updated_ms', 0)) > OTA_SESSION_TIMEOUT_MS

    def clear_ota_state(self, reason):
        if self.ota_state:
            print("[OTA] Clear state:", reason)
        self.ota_state = None
        task = self.ota_timeout_task
        self.ota_timeout_task = None
        self._cancel_task_safe(task)

    def crc16_ccitt_false_update(self, crc, data):
        for b in data:
            crc ^= (b << 8)
            for _ in range(8):
                if crc & 0x8000:
                    crc = ((crc << 1) ^ 0x1021) & 0xFFFF
                else:
                    crc = (crc << 1) & 0xFFFF
        return crc & 0xFFFF

    async def handle_dashboard_init(self, conn_handle, seq, request_payload):
        request = dashboard_init_request_t.unpack(request_payload)
        if not request:
            await self.send_packet_async(
                conn_handle,
                CMD_READ_DASHBOARD_INIT_ACK,
                seq,
                sdk_result_t.pack(SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_COMMON_UNKNOWN),
            )
            return
        print(
            "[DASHBOARD INIT] app_protocol={}, app_min_protocol={}, app_version={}.{}.{}, build_id={}".format(
                request['app_protocol_version'],
                request['app_min_protocol_version'],
                request['app_version_major'],
                request['app_version_minor'],
                request['app_version_patch'],
                request['app_build_id'],
            )
        )
        compat = sdk_check_protocol_compat(
            SDK_PROTOCOL_VERSION,
            SDK_MIN_PROTOCOL_VERSION,
            request['app_protocol_version'],
            request['app_min_protocol_version'],
        )
        if compat['code'] == SDK_COMPAT_CODE_APP_TOO_OLD:
            print("[DASHBOARD INIT] App protocol too old. Rejecting dashboard init.")
            await self.send_packet_async(
                conn_handle,
                CMD_READ_DASHBOARD_INIT_ACK,
                seq,
                sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_PROTOCOL_UNSUPPORTED),
            )
            return
        profile = self.get_current_runtime_profile()
        payload = dashboard_init_t.pack({
            'setting_voltage_max_mv': int(profile.get('target_voltage_mv', 2700)),
            'stat_welds_total': 1280,
            'stat_welds_task': 18,
            'setting_weld_pre_ms10': int(profile.get('preheat_pulse_ms10', 0)),
            'setting_weld_cooling_ms10': int(profile.get('cool_time_ms10', 0)),
            'setting_weld_main_ms10': int(profile.get('main_pulse_ms10', 0)),
            'protocol_version': SDK_PROTOCOL_VERSION,
            'protocol_min_version': SDK_MIN_PROTOCOL_VERSION,
            'firmware_major': self.firmware_version[0],
            'firmware_minor': self.firmware_version[1],
            'firmware_patch': self.firmware_version[2],
            'firmware_build_id': self.firmware_build_id,
        })
        await self.send_packet_async(conn_handle, CMD_READ_DASHBOARD_INIT_ACK, seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload))

    def start_dashboard_stream(self, conn_handle):
        self._cancel_task_safe(self.dashboard_stream_task)
        self.dashboard_stream_task = asyncio.create_task(self.dashboard_stream_loop(conn_handle))

    def stop_dashboard_stream(self):
        self._cancel_task_safe(self.dashboard_stream_task)
        self.dashboard_stream_task = None

    async def dashboard_stream_loop(self, conn_handle):
        try:
            while self.is_authenticated_conn(conn_handle):
                self.dashboard_tick += 1
                tick = self.dashboard_tick
                profile = self.get_current_runtime_profile()
                voltage_max_mv = int(profile.get('target_voltage_mv', 2700))
                phase = tick % 34
                is_welding = phase < 8
                if is_welding:
                    weld_current_a = 650 + phase * 210
                    voltage_mv = max(0, voltage_max_mv - 180 - phase * 48)
                    charge_current_ma = 900
                else:
                    weld_current_a = 35 + (tick % 6) * 8
                    recharge = min(26, phase - 8)
                    voltage_mv = min(voltage_max_mv, 2120 + recharge * max(1, (voltage_max_mv - 2120) // 26))
                    charge_current_ma = 0 if self.charge_paused else 2600 + (26 - recharge) * 120

                if self.charge_paused:
                    charge_mode_code = DASHBOARD_CHARGE_MODE_PAUSED
                elif tick % 132 < 12:
                    charge_mode_code = DASHBOARD_CHARGE_MODE_PRECHARGE
                elif tick % 132 > 118:
                    charge_mode_code = DASHBOARD_CHARGE_MODE_FLOAT
                elif tick % 44 > 20:
                    charge_mode_code = DASHBOARD_CHARGE_MODE_CONSTANT_VOLTAGE
                else:
                    charge_mode_code = DASHBOARD_CHARGE_MODE_CONSTANT_CURRENT

                compact_payload = dashboard_compact_t.pack({
                    'voltage_mv': voltage_mv,
                    'weld_current_a': weld_current_a,
                    'charge_current_ma': charge_current_ma,
                    'est_time_full_sec': max(0, 86 - (tick % 86)),
                    'temperature_capacitor_c10': 315 + (tick % 22) * 2,
                    'temperature_mos_c10': 452 + (tick % 25) * 3,
                    'machine_status': 2,
                    'charge_mode_code': charge_mode_code,
                    'discharge_status': 1 if self.safe_discharge_active else 0,
                    'undefined_status': 0,
                })
                await self.send_packet_async(conn_handle, CMD_DASHBOARD_COMPACT, tick & 0xFF, compact_payload)

                if tick % 34 == 0:
                    self.dashboard_weld_id += 1
                    weld_payload = dashboard_weld_records_t.from_array([{
                        'id': self.dashboard_weld_id,
                        'peak_current_a': 1680 + (tick % 9) * 24,
                        'post_voltage_mv': voltage_mv,
                    }])
                    await self.send_packet_async(conn_handle, CMD_DASHBOARD_WELD_RECORDS, tick & 0xFF, weld_payload)

                if tick % 12 == 0:
                    log_code = DASHBOARD_LOG_CODE_WELD_COMPLETE if tick % 34 == 0 else DASHBOARD_LOG_CODE_SYSTEM_READY
                    log_payload = dashboard_logs_t.from_array([{
                        'id': tick,
                        'level': 0,
                        'time_sec': tick // 10,
                        'code': log_code,
                    }])
                    await self.send_packet_async(conn_handle, CMD_DASHBOARD_LOGS, tick & 0xFF, log_payload)

                await asyncio.sleep_ms(100)
        except asyncio.CancelledError:
            pass

    def handle_packet(self, conn_handle, pkt):
        print(f"[{time.ticks_ms()}] Received Packet: CMD=0x{pkt.cmd:02X}, SEQ={pkt.seq}, PAYLOAD={ubinascii.hexlify(pkt.payload)}")

        if self.requires_auth(pkt.cmd) and not self.is_authenticated_conn(conn_handle):
            print("Auth required for CMD=0x%02X, kicking connection." % pkt.cmd)
            self.send_auth_required_kick(conn_handle, pkt)
            return

        if pkt.cmd == CMD_DEVICE_GET_INFO:
            print("=== Received DEVICE GET INFO ===")
            asyncio.create_task(self.handle_device_get_info(conn_handle, pkt.seq))

        elif pkt.cmd == CMD_DEVICE_AUTH:
            print("=== Received AUTH ===")
            token = auth_request_t.unpack(pkt.payload)
            if token and token in self.g_bonded:
                print("Auth success.")
                self.send_packet(conn_handle, CMD_DEVICE_AUTH_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))
                self.active_conn = conn_handle
                self.conn_tokens[conn_handle] = token
                self.led.solid(0, 50, 0)
                self._cancel_task_safe(self.auth_timeout_tasks.pop(conn_handle, None))
                # 鉴权成功后无需显式操作广播，底层协议栈在物理连接建立时已自动停止广播
            else:
                print("Auth failed.")
                self.send_packet(conn_handle, CMD_DEVICE_AUTH_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_AUTH_INVALID, SDK_RESULT_CODE_AUTH_INVALID_TOKEN))
                async def delayed_disconnect(conn):
                    await asyncio.sleep_ms(200)
                    try: self.ble.gap_disconnect(conn)
                    except Exception: pass
                asyncio.create_task(delayed_disconnect(conn_handle))

        elif pkt.cmd == CMD_DEVICE_PAIR_REQUEST:
            print("=== Received PAIR REQUEST ===")
            
            # 如果设备不处于配对广播状态下，立即拒绝配对并延迟断开连接
            if not self.pairing_mode:
                print("Not in pairing mode. Rejecting PAIR REQUEST.")
                resp_payload = sdk_result_t.pack(SDK_RESULT_STATUS_FAIL, SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE)
                self.send_packet(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt.seq, resp_payload)
                return

            pair_request = pair_request_t.unpack(pkt.payload)
            if not pair_request:
                print("Invalid PAIR REQUEST payload.")
                resp_payload = sdk_result_t.pack(SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_COMMON_UNKNOWN)
                self.send_packet(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt.seq, resp_payload)
                return

            client_id = pair_request['client_id']
            host_name = pair_request['name']
            print("Client ID: 0x%016X" % client_id)
            print(f"Host Name: {host_name}")

            self.waiting_confirm_conn = conn_handle
            self.waiting_confirm_seq = pkt.seq

            pending_payload = sdk_result_t.pack(SDK_RESULT_STATUS_PENDING, SDK_RESULT_CODE_PAIR_WAIT_CONFIRM)
            self.send_packet(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt.seq, pending_payload)
            
            # 对端请求配对，取消其 3 秒鉴权超时断线任务（若有）
            self._cancel_task_safe(self.auth_timeout_tasks.pop(conn_handle, None))
            
            # LED 闪烁黄色（红绿混合），指示等待用户按下物理按键确认
            self.led.blink(50, 50, 0, interval_ms=200)

            # 启动 60 秒确认配对的超时倒计时
            self._cancel_task_safe(self.confirm_timeout_task)
            self.confirm_timeout_task = asyncio.create_task(self._confirm_timeout())
            
        elif pkt.cmd == CMD_DEVICE_PAIR_UNPAIR:
            print("=== Received UNPAIR ===")
            token = self.conn_tokens.get(conn_handle)
            if token and token in self.g_bonded:
                self.g_bonded.remove(token)
                self.save_bonded()
            self.send_packet(conn_handle, CMD_DEVICE_PAIR_UNPAIR_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))
            
            # Delayed disconnect after unpairing
            async def delayed_disconnect(conn):
                await asyncio.sleep_ms(500)
                try:
                    self.ble.gap_disconnect(conn)
                except Exception: pass
            asyncio.create_task(delayed_disconnect(conn_handle))

        elif pkt.cmd == CMD_PING:
            print(">>> Received PING ===")
            print(">>> PONG", pkt.payload.decode('utf-8').rstrip('\x00'))
            # Echo back heartbeat with ACK flag set
            self.send_packet(conn_handle, CMD_PONG, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_DEVICE_RESET:
            print("=== Received RESET ===")
            print("[ACTION] Device reset requested after firmware update. Simulated only.")
            self.send_packet(conn_handle, CMD_DEVICE_RESET_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_OTA_START:
            print("=== Received OTA START ===")
            asyncio.create_task(self.handle_ota_start(conn_handle, pkt.seq, pkt.payload))

        elif pkt.cmd == CMD_OTA_DATA:
            print("=== Received OTA DATA ===")
            asyncio.create_task(self.handle_ota_data(conn_handle, pkt.seq, pkt.payload))

        elif pkt.cmd == CMD_OTA_VERIFY:
            print("=== Received OTA VERIFY ===")
            asyncio.create_task(self.handle_ota_verify(conn_handle, pkt.seq, pkt.payload))

        elif pkt.cmd == CMD_OTA_ABORT:
            print("=== Received OTA ABORT ===")
            asyncio.create_task(self.handle_ota_abort(conn_handle, pkt.seq))

        elif pkt.cmd == CMD_READ_DASHBOARD_INIT:
            print("=== Received DASHBOARD INIT ===")
            asyncio.create_task(self.handle_dashboard_init(conn_handle, pkt.seq, pkt.payload))

        elif pkt.cmd == CMD_DASHBOARD_START:
            print("=== Received DASHBOARD START ===")
            self.start_dashboard_stream(conn_handle)
            self.send_packet(conn_handle, CMD_DASHBOARD_START_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_DASHBOARD_STOP:
            print("=== Received DASHBOARD STOP ===")
            self.stop_dashboard_stream()
            self.send_packet(conn_handle, CMD_DASHBOARD_STOP_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_MANUAL_TRIGGER:
            print("=== Received MANUAL TRIGGER ===")
            print("[ACTION] Manual weld trigger is not supported by this reference firmware.")
            self.send_packet(conn_handle, CMD_MANUAL_TRIGGER_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_SAFE_DISCHARGE:
            print("=== Received SAFE DISCHARGE ===")
            print("[ACTION] Safe discharge is not supported by this reference firmware.")
            self.send_packet(conn_handle, CMD_SAFE_DISCHARGE_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_SAFE_DISCHARGE_STOP:
            print("=== Received SAFE DISCHARGE STOP ===")
            print("[ACTION] Safe discharge stop is not supported by this reference firmware.")
            self.send_packet(conn_handle, CMD_SAFE_DISCHARGE_STOP_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_CHARGE_START:
            print("=== Received CHARGE START ===")
            print("[ACTION] Charge start is not supported by this reference firmware.")
            self.send_packet(conn_handle, CMD_CHARGE_START_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_CHARGE_PAUSE:
            print("=== Received CHARGE PAUSE ===")
            print("[ACTION] Charge pause is not supported by this reference firmware.")
            self.send_packet(conn_handle, CMD_CHARGE_PAUSE_ACK, pkt.seq, sdk_result_t.pack(SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE))

        elif pkt.cmd == CMD_SETTINGS_READ_CURRENT:
            print("=== Received SETTINGS READ CURRENT ===")
            asyncio.create_task(self.handle_settings_read_current(conn_handle, pkt.seq))

        elif pkt.cmd == CMD_SETTINGS_READ_SELF_CHECK:
            print("=== Received SETTINGS READ SELF CHECK ===")
            asyncio.create_task(self.handle_settings_read_self_check(conn_handle, pkt.seq))

        elif pkt.cmd == CMD_SETTINGS_PROFILE:
            print("=== Received SETTINGS APPLY PROFILE ===")
            asyncio.create_task(self.handle_settings_apply_profile(conn_handle, pkt.seq, bytes(pkt.payload)))

        elif pkt.cmd == CMD_SETTINGS_RESET:
            print("=== Received SETTINGS RESET ===")
            asyncio.create_task(self.handle_settings_reset(conn_handle, pkt.seq, bytes(pkt.payload)))


async def main():
    print("Starting BLE Application with asyncio")
    device = BLEDevice("Weld_Control")
    while True:
        await asyncio.sleep(1)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Exiting")
