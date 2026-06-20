"""Dashboard payload 编解码。"""

import struct
from sdk_codec import SDK_MAX_PAYLOAD

DASHBOARD_MACHINE_STATUS_MASK = 0x0F      # status_flags 低 4 bit：设备运行状态
DASHBOARD_CHARGE_MODE_MASK = 0x0F         # status_flags 高 4 bit：充电模式
DASHBOARD_CHARGE_MODE_SHIFT = 4           # status_flags 中充电模式的位移
DASHBOARD_DISCHARGE_STATUS_MASK = 0x0F    # flags 低 4 bit：放电状态
DASHBOARD_UNDEFINED_STATUS_MASK = 0x0F    # flags 高 4 bit：预留状态
DASHBOARD_UNDEFINED_STATUS_SHIFT = 4      # flags 中预留状态的位移

DASHBOARD_MACHINE_STATUS_WAITING_DATA = 0 # 尚未收到有效运行数据
DASHBOARD_MACHINE_STATUS_NOT_READY = 1    # 设备未就绪
DASHBOARD_MACHINE_STATUS_READY = 2        # 设备运行就绪
DASHBOARD_MACHINE_STATUS_FAULT = 3        # 设备异常
DASHBOARD_MACHINE_STATUS_VOLTAGE_LOW = 4  # 储能电压低于禁焊电压

DASHBOARD_CHARGE_MODE_UNKNOWN = 0          # 未知状态
DASHBOARD_CHARGE_MODE_STANDBY = 1          # 待机状态
DASHBOARD_CHARGE_MODE_CONSTANT_CURRENT = 2 # 恒流阶段
DASHBOARD_CHARGE_MODE_CONSTANT_VOLTAGE = 3 # 恒压阶段
DASHBOARD_CHARGE_MODE_PRECHARGE = 4        # 预充阶段
DASHBOARD_CHARGE_MODE_FLOAT = 5            # 浮充阶段
DASHBOARD_CHARGE_MODE_PAUSED = 6           # 暂停充电
DASHBOARD_CHARGE_MODE_FAULT = 7            # 充电故障
DASHBOARD_CHARGE_MODE_BALANCE_FAULT = 8    # 均衡异常
DASHBOARD_CHARGE_MODE_INPUT_FAULT = 9      # 输入异常
DASHBOARD_CHARGE_MODE_OUTPUT_FAULT = 10    # 输出异常
DASHBOARD_CHARGE_MODE_PROTECTION_MODE = 11 # 保护模式

DASHBOARD_LOG_CODE_SYSTEM_READY = 1       # 系统就绪
DASHBOARD_LOG_CODE_CHARGE_STARTED = 2     # 开始充电
DASHBOARD_LOG_CODE_CHARGE_STOPPED = 3     # 停止充电
DASHBOARD_LOG_CODE_WELD_TRIGGERED = 4     # 点焊已触发
DASHBOARD_LOG_CODE_WELD_COMPLETE = 5      # 点焊完成
DASHBOARD_LOG_CODE_TEMPERATURE_WARN = 6   # 温度警告
DASHBOARD_LOG_CODE_TEMPERATURE_ERROR = 7  # 温度异常
DASHBOARD_LOG_CODE_BLE_CONNECTED = 8      # BLE 已连接
DASHBOARD_LOG_CODE_BLE_DISCONNECTED = 9   # BLE 已断开
DASHBOARD_LOG_CODE_CONFIG_UPDATED = 10    # 参数已更新


def dashboard_pack_status_flags(machine_status, charge_mode_code):
    """打包设备运行状态和充电模式到 status_flags。"""
    return ((charge_mode_code & DASHBOARD_CHARGE_MODE_MASK) << DASHBOARD_CHARGE_MODE_SHIFT) | (
        machine_status & DASHBOARD_MACHINE_STATUS_MASK
    )


def dashboard_get_machine_status(status_flags):
    """从 status_flags 读取设备运行状态。"""
    return status_flags & DASHBOARD_MACHINE_STATUS_MASK


def dashboard_get_charge_mode_code(status_flags):
    """从 status_flags 读取充电模式码。"""
    return (status_flags >> DASHBOARD_CHARGE_MODE_SHIFT) & DASHBOARD_CHARGE_MODE_MASK


def dashboard_pack_flags(discharge_status, undefined_status):
    """打包放电状态和预留状态到 flags。"""
    return ((undefined_status & DASHBOARD_UNDEFINED_STATUS_MASK) << DASHBOARD_UNDEFINED_STATUS_SHIFT) | (
        discharge_status & DASHBOARD_DISCHARGE_STATUS_MASK
    )


def dashboard_get_discharge_status(flags):
    """从 flags 读取放电状态。"""
    return flags & DASHBOARD_DISCHARGE_STATUS_MASK


def dashboard_get_undefined_status(flags):
    """从 flags 读取预留状态。"""
    return (flags >> DASHBOARD_UNDEFINED_STATUS_SHIFT) & DASHBOARD_UNDEFINED_STATUS_MASK


class dashboard_init_t:
    """Dashboard 初始化响应 data。"""

    FORMAT = "<HIIHHHHBBBIHHBH"
    BYTE_LENGTH_V1 = 32
    BYTE_LENGTH = BYTE_LENGTH_V1

    @staticmethod
    def pack(fields):
        """打包 Dashboard 初始化响应 data。

        注意：新增字段只能尾部追加，已发布字段 offset/长度/单位不可变。
        """
        values = ()
        if dashboard_init_t.BYTE_LENGTH >= dashboard_init_t.BYTE_LENGTH_V1:
            values = (
                fields['setting_voltage_max_mv'],
                fields['stat_welds_total'],
                fields['stat_welds_task'],
                fields['setting_weld_pre_ms10'],
                fields['setting_weld_cooling_ms10'],
                fields['setting_weld_main_ms10'],
                fields['protocol_version'],
                fields['firmware_major'],
                fields['firmware_minor'],
                fields['firmware_patch'],
                fields['firmware_build_id'],
                fields['protocol_min_version'],
                fields['setting_single_cap_voltage_mv'],
                fields['setting_trigger_mode'],
                fields['setting_auto_delay_ms'],
            )
        return struct.pack(
            dashboard_init_t.FORMAT,
            *values,
        )

    @staticmethod
    def unpack(data):
        """解包 Dashboard 初始化响应 data。

        注意：按当前 SDK 已知长度读取前缀，不根据协议版本切换解析结构。
        """
        if len(data) < dashboard_init_t.BYTE_LENGTH_V1:
            return None
        values = struct.unpack(dashboard_init_t.FORMAT, data[:dashboard_init_t.BYTE_LENGTH_V1])
        result = {
            'setting_voltage_max_mv': values[0],
            'stat_welds_total': values[1],
            'stat_welds_task': values[2],
            'setting_weld_pre_ms10': values[3],
            'setting_weld_cooling_ms10': values[4],
            'setting_weld_main_ms10': values[5],
            'protocol_version': values[6],
            'firmware_major': values[7],
            'firmware_minor': values[8],
            'firmware_patch': values[9],
            'firmware_build_id': values[10],
            'protocol_min_version': values[11],
            'setting_single_cap_voltage_mv': values[12],
            'setting_trigger_mode': values[13],
            'setting_auto_delay_ms': values[14],
        }
        return result


class dashboard_init_request_t:
    """Dashboard 初始化请求 payload。"""

    FORMAT = "<HHBBBI"
    BYTE_LENGTH = 11

    @staticmethod
    def pack(fields):
        """打包 Dashboard 初始化请求。"""
        return struct.pack(
            dashboard_init_request_t.FORMAT,
            fields['app_protocol_version'],
            fields['app_min_protocol_version'],
            fields['app_version_major'],
            fields['app_version_minor'],
            fields['app_version_patch'],
            fields['app_build_id'],
        )

    @staticmethod
    def unpack(data):
        """解包 Dashboard 初始化请求；长度不足时返回 None。"""
        if len(data) < dashboard_init_request_t.BYTE_LENGTH:
            return None
        values = struct.unpack(dashboard_init_request_t.FORMAT, data[:dashboard_init_request_t.BYTE_LENGTH])
        return {
            'app_protocol_version': values[0],
            'app_min_protocol_version': values[1],
            'app_version_major': values[2],
            'app_version_minor': values[3],
            'app_version_patch': values[4],
            'app_build_id': values[5],
        }


class dashboard_compact_t:
    """Dashboard 高频紧凑状态 payload。"""

    FORMAT = "<HHHHHbbBB"
    BYTE_LENGTH = 14

    @staticmethod
    def pack(fields):
        """打包 Dashboard 高频紧凑状态。"""
        return struct.pack(
            dashboard_compact_t.FORMAT,
            fields['voltage_mv'],
            fields['weld_current_a'],
            fields['charge_current_ma'],
            fields['voltage_cap_1_mv'],
            fields['voltage_cap_2_mv'],
            fields['temperature_capacitor_c'],
            fields['temperature_mos_c'],
            dashboard_pack_status_flags(fields['machine_status'], fields['charge_mode_code']),
            dashboard_pack_flags(fields['discharge_status'], fields['undefined_status']),
        )

    @staticmethod
    def unpack(data):
        """解包 Dashboard 高频紧凑状态；长度不足时返回 None。"""
        if len(data) < dashboard_compact_t.BYTE_LENGTH:
            return None
        values = struct.unpack(dashboard_compact_t.FORMAT, data[:dashboard_compact_t.BYTE_LENGTH])
        return {
            'voltage_mv': values[0],
            'weld_current_a': values[1],
            'charge_current_ma': values[2],
            'voltage_cap_1_mv': values[3],
            'voltage_cap_2_mv': values[4],
            'temperature_capacitor_c': values[5],
            'temperature_mos_c': values[6],
            'machine_status': dashboard_get_machine_status(values[7]),
            'charge_mode_code': dashboard_get_charge_mode_code(values[7]),
            'discharge_status': dashboard_get_discharge_status(values[8]),
            'undefined_status': dashboard_get_undefined_status(values[8]),
        }


class dashboard_weld_records_t:
    """Dashboard 点焊记录批量 payload。"""

    RECORD_FORMAT = "<HHH"
    RECORD_LENGTH = 6
    MAX_ITEMS = min(0xFF, (SDK_MAX_PAYLOAD - 1) // RECORD_LENGTH)

    @staticmethod
    def from_array(records):
        """将点焊记录数组打包为批量上报 payload。"""
        size = min(len(records), dashboard_weld_records_t.MAX_ITEMS)
        buf = bytearray([size])
        for index in range(size):
            record = records[index]
            buf.extend(struct.pack(
                dashboard_weld_records_t.RECORD_FORMAT,
                record['id'],
                record['peak_current_a'],
                record['post_voltage_mv'],
            ))
        return bytes(buf)

    @staticmethod
    def peek_size(data):
        """预读批量点焊记录数量；长度不足时返回 None。"""
        if len(data) < 1:
            return None
        size = data[0]
        expected_length = 1 + dashboard_weld_records_t.RECORD_LENGTH * size
        if len(data) < expected_length:
            return None
        return size

    @staticmethod
    def unpack_item(data, index):
        """解包指定下标的点焊记录；越界或长度不足时返回 None。"""
        size = dashboard_weld_records_t.peek_size(data)
        if size is None or index < 0 or index >= size:
            return None
        offset = 1 + dashboard_weld_records_t.RECORD_LENGTH * index
        values = struct.unpack(
            dashboard_weld_records_t.RECORD_FORMAT,
            data[offset: offset + dashboard_weld_records_t.RECORD_LENGTH],
        )
        return {
            'id': values[0],
            'peak_current_a': values[1],
            'post_voltage_mv': values[2],
        }


class dashboard_logs_t:
    """Dashboard 诊断日志批量 payload。"""

    LOG_FORMAT = "<HBIH"
    LOG_LENGTH = 9
    MAX_ITEMS = min(0xFF, (SDK_MAX_PAYLOAD - 1) // LOG_LENGTH)

    @staticmethod
    def from_array(logs):
        """将诊断日志数组打包为批量上报 payload。"""
        size = min(len(logs), dashboard_logs_t.MAX_ITEMS)
        buf = bytearray([size])
        for index in range(size):
            log = logs[index]
            buf.extend(struct.pack(
                dashboard_logs_t.LOG_FORMAT,
                log['id'],
                log['level'],
                log['time_sec'],
                log['code'],
            ))
        return bytes(buf)

    @staticmethod
    def peek_size(data):
        """预读批量诊断日志数量；长度不足时返回 None。"""
        if len(data) < 1:
            return None
        size = data[0]
        expected_length = 1 + dashboard_logs_t.LOG_LENGTH * size
        if len(data) < expected_length:
            return None
        return size

    @staticmethod
    def unpack_item(data, index):
        """解包指定下标的诊断日志；越界或长度不足时返回 None。"""
        size = dashboard_logs_t.peek_size(data)
        if size is None or index < 0 or index >= size:
            return None
        offset = 1 + dashboard_logs_t.LOG_LENGTH * index
        values = struct.unpack(
            dashboard_logs_t.LOG_FORMAT,
            data[offset: offset + dashboard_logs_t.LOG_LENGTH],
        )
        return {
            'id': values[0],
            'level': values[1],
            'time_sec': values[2],
            'code': values[3],
        }
