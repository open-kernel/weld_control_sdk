# ============================================================
# 文件名: sdk_codec.py
# 简述: BLE 数据层 SDK - 协议编解码器
# 版本: 1.0
#
# 说明：
#   本库为纯数据层编解码库，不提供连接管理、扫描、蓝牙协议栈等实现。
#   提供帧格式定义、组包、解包、校验和命令集规范。
#   支持单片机（STM32 等）与上位机（Windows/Linux/macOS）共用。
#
# 使用方式：
#   每建立一个物理连接，创建一个 sdk_device_t 实例。
#   从蓝牙/UART 收到原始字节后，调用 sdk_decode() 循环解析。
#   发送数据时，调用 sdk_encode() 组帧。
# ============================================================

import struct
import bluetooth
from sdk_commands import *
from sdk_version import *
from payloads.device_info import *
from payloads.ota import *

# ============================================================
# 可配置宏 / 常量
# ============================================================

# 线协议单帧 payload 理论上限（字节），v1 使用 16-bit little-endian 长度字段
SDK_WIRE_MAX_PAYLOAD = 0xFFFF

# 当前 SDK 实现允许缓存/解析的最大 payload 长度，可按设备内存调整
SDK_MAX_PAYLOAD = 1024

# 配对/鉴权 Token 长度（字节）
SDK_TOKEN_LEN = 12

# 帧头固定长度：SOF + LEN_LE16 + CMD + SEQ
SDK_HEAD_SIZE = 5
# CRC8 校验长度
SDK_CRC_SIZE = 1
# 帧固定开销（头 + CRC8），v1 固定 6 字节
SDK_FRAME_OVERHEAD = SDK_HEAD_SIZE + SDK_CRC_SIZE
# 广播设备名最大长度
SDK_ADV_NAME_MAX = 16
# 默认 BLE 单次写入字节数
SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD = 20
# 配对请求上位机 ID 长度：u64 little-endian
SDK_PAIR_CLIENT_ID_SIZE = 8
# 配对请求固定头长度：client_id[8] + name_len[1]
SDK_PAIR_REQUEST_HEADER_SIZE = SDK_PAIR_CLIENT_ID_SIZE + 1
# 配对请求名称最大字节数，按 12 个常见中文 UTF-8 字符预留
SDK_PAIR_NAME_MAX = 36

# ============================================================
# 帧常量
# ============================================================

# 帧起始字节
SDK_SOF = 0xAA

def sdk_get_payload_capacity(transport_payload_size=SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD):
    """计算指定传输单次写入大小下可承载的业务 payload 字节数。"""
    return max(0, transport_payload_size - SDK_FRAME_OVERHEAD)

# ============================================================
# UUID 定义
# ============================================================

# 主服务 UUID
SDK_SVC_UUID = bluetooth.UUID('5868A23F-877F-53F3-B2B6-E8D4FDF32F75')
# TX 特征 UUID（设备→上位机，Notify）
SDK_CHR_TX_UUID = bluetooth.UUID('373CDA7C-832F-5510-8CFC-F5A8E11BADDE')
# RX 特征 UUID（上位机→设备，Write）
SDK_CHR_RX_UUID = bluetooth.UUID('A5255200-72A1-57AD-9306-1A99E8CFEE4B')
# 16-bit 兼容主服务 UUID，用于外置 BLE 模块
SDK_SVC_UUID16 = bluetooth.UUID(0xA23F)
# 16-bit 兼容 TX 特征 UUID（设备→上位机，Notify）
SDK_CHR_TX_UUID16 = bluetooth.UUID(0xDA7C)
# 16-bit 兼容 RX 特征 UUID（上位机→设备，Write）
SDK_CHR_RX_UUID16 = bluetooth.UUID(0x5200)

# ============================================================
# 数据结构
# ============================================================



# 配对请求载荷
class pair_request_t:
    """配对请求 payload：client_id[8] + name_len[1] + name。"""

    @staticmethod
    def _pack_client_id(client_id):
        if isinstance(client_id, (bytes, bytearray)):
            raw = bytes(client_id)
            return raw[:SDK_PAIR_CLIENT_ID_SIZE] + b'\x00' * max(0, SDK_PAIR_CLIENT_ID_SIZE - len(raw))
        value = int(client_id or 0)
        return bytes((value >> (8 * i)) & 0xFF for i in range(SDK_PAIR_CLIENT_ID_SIZE))

    @staticmethod
    def _encode_name(name):
        result = bytearray()
        for ch in str(name or ''):
            encoded = ch.encode('utf-8')
            if len(result) + len(encoded) > SDK_PAIR_NAME_MAX:
                break
            result.extend(encoded)
        return bytes(result)

    @staticmethod
    def pack(client_id, name):
        """打包配对请求 payload。

        注意：name 会按 UTF-8 字符边界截断，不保证保留完整原始字符串。
        """
        name_bytes = pair_request_t._encode_name(name)
        return pair_request_t._pack_client_id(client_id) + bytes([len(name_bytes) & 0xFF]) + name_bytes
        
    @staticmethod
    def unpack(data):
        """解包配对请求 payload；长度或名称非法时返回 None。"""
        if len(data) < SDK_PAIR_REQUEST_HEADER_SIZE: return None
        name_len = data[SDK_PAIR_CLIENT_ID_SIZE]
        if name_len > SDK_PAIR_NAME_MAX: return None
        if len(data) < SDK_PAIR_REQUEST_HEADER_SIZE + name_len: return None
        try:
            name = bytes(data[SDK_PAIR_REQUEST_HEADER_SIZE:SDK_PAIR_REQUEST_HEADER_SIZE + name_len]).decode('utf-8')
        except UnicodeError:
            return None
        client_id = 0
        for i in range(SDK_PAIR_CLIENT_ID_SIZE):
            client_id |= int(data[i]) << (8 * i)
        return {
            'client_id': client_id,
            'name': name,
        }

SDK_RESULT_STATUS_RESERVED = 0      # 保留/未初始化，收到后视为协议错误
SDK_RESULT_STATUS_OK = 1            # 成功
SDK_RESULT_STATUS_FAIL = 2          # 普通失败
SDK_RESULT_STATUS_BUSY = 3          # 设备忙
SDK_RESULT_STATUS_AUTH_INVALID = 4  # 鉴权失效，需要重新配对/鉴权
SDK_RESULT_STATUS_INVALID_PARAM = 5 # 参数非法
SDK_RESULT_STATUS_NOT_SUPPORTED = 6 # 当前设备或固件不支持
SDK_RESULT_STATUS_DEVICE_ERROR = 7  # 设备内部异常
SDK_RESULT_STATUS_PENDING = 8       # 请求已受理，等待后续最终应答

SDK_RESULT_CODE_COMMON_NONE = 0                 # 通用业务码：无错误
SDK_RESULT_CODE_COMMON_UNKNOWN = 1              # 通用业务码：未知错误
SDK_RESULT_CODE_COMMON_PROTOCOL_UNSUPPORTED = 2 # 通用业务码：协议版本不支持

SDK_RESULT_CODE_PAIR_REJECTED = 1             # 配对业务码：设备拒绝配对
SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE = 2  # 配对业务码：设备未处于配对模式
SDK_RESULT_CODE_PAIR_WAIT_CONFIRM = 3         # 配对业务码：等待设备端确认
SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT = 4      # 配对业务码：设备端确认超时

SDK_RESULT_CODE_AUTH_INVALID_TOKEN = 1       # 鉴权业务码：token 无效或已被设备清除
SDK_RESULT_CODE_AUTH_REJECTED_COMMAND = 2    # 鉴权业务码：未授权命令被拒绝

SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID = 1 # 设置业务码：payload 非法

SDK_RESULT_CODE_DASHBOARD_START_FAILED = 1   # Dashboard 业务码：启动实时上报失败
SDK_RESULT_CODE_DASHBOARD_STOP_FAILED = 2    # Dashboard 业务码：停止实时上报失败
SDK_RESULT_CODE_DASHBOARD_CONTROL_FAILED = 3 # Dashboard 业务码：控制命令执行失败

SDK_RESULT_HEADER_SIZE = 2 # 统一应答 payload 固定头长度：status[1] + code[1]

# 统一应答载荷：status/code/data，data 始终是该 CMD 固定实体或空。
class sdk_result_t:
    """统一应答 payload：status/code/data。"""

    @staticmethod
    def pack(status, code=SDK_RESULT_CODE_COMMON_NONE, data=b''):
        """打包统一应答 payload。"""
        return bytes([status & 0xFF, code & 0xFF]) + bytes(data)

    @staticmethod
    def unpack(data):
        """解包统一应答 payload；长度不足时返回 None。"""
        if len(data) < SDK_RESULT_HEADER_SIZE: return None
        return {'status': data[0], 'code': data[1], 'data': bytes(data[SDK_RESULT_HEADER_SIZE:])}

# 配对成功数据，固定 Token。
class pair_token_t:
    """配对成功 data，固定 token 长度。"""

    BYTE_LENGTH = SDK_TOKEN_LEN

    @staticmethod
    def pack(token):
        """打包配对成功 token。"""
        token = bytes(token)
        return token[:SDK_TOKEN_LEN] + b'\x00' * max(0, SDK_TOKEN_LEN - len(token))

    @staticmethod
    def unpack(data):
        """解包配对成功 token；长度不足时返回 None。"""
        if len(data) < SDK_TOKEN_LEN: return None
        return bytes(data[:SDK_TOKEN_LEN])

# 设备鉴权请求载荷
class auth_request_t:
    """设备鉴权请求 payload。"""

    @staticmethod
    def pack(token):
        """打包鉴权请求 token。"""
        token = bytes(token)
        return token[:SDK_TOKEN_LEN] + b'\x00' * max(0, SDK_TOKEN_LEN - len(token))
        
    @staticmethod
    def unpack(data):
        """解包鉴权请求 token；长度不足时返回 None。"""
        if len(data) < SDK_TOKEN_LEN: return None
        return bytes(data[:SDK_TOKEN_LEN])

# 解码后的数据包（面向业务层）
class sdk_packet_t:
    """解码后的数据包，面向业务层。"""

    def __init__(self, cmd=0, seq=0, payload=b''):
        self.cmd = cmd
        self.seq = seq
        self.payload = bytearray(payload)
    
    @property
    def payload_len(self):
        return len(self.payload)

# Parser States（内部使用）
ST_WAIT_SOF = 0
ST_LEN_LO   = 1
ST_LEN_HI   = 2
ST_CMD      = 3
ST_SEQ      = 4
ST_PAYLOAD  = 5
ST_CRC      = 6

class sdk_device_parser_t:
    """连接级流式解析状态机。"""

    def __init__(self):
        self.state = ST_WAIT_SOF
        self.payload_len = 0
        self.idx = 0
        self.buf = bytearray(SDK_HEAD_SIZE + SDK_MAX_PAYLOAD + SDK_CRC_SIZE)

    def reset(self):
        self.state = ST_WAIT_SOF
        self.idx = 0
        self.payload_len = 0

# 设备上下文（每个物理连接对应一个实例）
class sdk_device_t:
    """设备连接上下文，每个物理连接对应一个实例。"""

    def __init__(self, mac=None, name=None):
        self.mac = mac if mac else bytearray(6)
        self.name = name if name else ""
        self.rx_frames = 0
        self.rx_errors = 0
        self.tx_frames = 0
        self.parser = sdk_device_parser_t()

# ============================================================
# API 接口
# ============================================================

# 创建设备上下文
def sdk_device_create(mac=None, name=None):
    """创建设备连接上下文。

    注意：每个物理连接必须独立创建上下文，不能跨连接复用解析状态机。
    """
    return sdk_device_t(mac, name)

def sdk_device_reset_parser(dev):
    """重置设备上下文内的流式解码状态。

    物理连接断开、传输层会话重建，或调用方判定半帧已超时时，
    可调用本函数丢弃当前未完成帧，避免旧半帧吞掉后续命令。
    """
    if dev:
        dev.parser.reset()

def sdk_encode(dev, pkt):
    """将数据包组帧为原始字节流。

    注意：payload 超过当前 SDK_MAX_PAYLOAD 或线协议上限时返回空 bytes。
    """
    if not pkt:
        return b''
    payload_len = len(pkt.payload)
    if payload_len > SDK_MAX_PAYLOAD or payload_len > SDK_WIRE_MAX_PAYLOAD:
        return b''
    
    buf = bytearray()
    buf.append(SDK_SOF)
    buf.append(payload_len & 0xFF)
    buf.append((payload_len >> 8) & 0xFF)
    buf.append(pkt.cmd & 0xFF)
    buf.append(pkt.seq & 0xFF)
    
    if payload_len > 0:
        buf.extend(pkt.payload)
        
    buf.append(sdk_crc8(buf))
    
    if dev:
        dev.tx_frames += 1
    return bytes(buf)

def sdk_decode(dev, data):
    """从原始字节流中流式解析一帧数据。

    返回 {'kind': 'packet'|'need-more'|'error', ...}。
    注意：收到 packet 后如 data 仍有剩余，需要继续传入剩余字节。
    """
    if not dev or not data:
        return {'kind': 'need-more'}

    parser = dev.parser
    data_len = len(data)

    for i in range(data_len):
        b = data[i]

        if parser.state == ST_WAIT_SOF:
            if b == SDK_SOF:
                parser.buf[0] = b
                parser.idx = 1
                parser.state = ST_LEN_LO
        elif parser.state == ST_LEN_LO:
            parser.buf[parser.idx] = b
            parser.idx += 1
            parser.payload_len = b
            parser.state = ST_LEN_HI
        elif parser.state == ST_LEN_HI:
            parser.buf[parser.idx] = b
            parser.idx += 1
            parser.payload_len |= b << 8
            if parser.payload_len > SDK_MAX_PAYLOAD:
                dev.rx_errors += 1
                parser.reset()
                return {'kind': 'error', 'consumed': i + 1}
            parser.state = ST_CMD
        elif parser.state in (ST_CMD, ST_SEQ):
            parser.buf[parser.idx] = b
            parser.idx += 1
            parser.state += 1
            if parser.state == ST_PAYLOAD and parser.payload_len == 0:
                parser.state = ST_CRC
        elif parser.state == ST_PAYLOAD:
            parser.buf[parser.idx] = b
            parser.idx += 1
            if parser.idx >= SDK_HEAD_SIZE + parser.payload_len:
                parser.state = ST_CRC
        elif parser.state == ST_CRC:
            parser.buf[parser.idx] = b
            parser.idx += 1
            
            calc_crc = sdk_crc8(parser.buf[:SDK_HEAD_SIZE + parser.payload_len])
            frame_crc = parser.buf[SDK_HEAD_SIZE + parser.payload_len]

            consumed = i + 1
            
            if calc_crc != frame_crc:
                dev.rx_errors += 1
                parser.reset()
                return {'kind': 'error', 'consumed': consumed}
                
            pkt = sdk_packet_t()
            pkt.cmd = parser.buf[3]
            pkt.seq = parser.buf[4]
            if parser.payload_len > 0:
                pkt.payload = parser.buf[SDK_HEAD_SIZE : SDK_HEAD_SIZE + parser.payload_len]
                
            parser.reset()
            dev.rx_frames += 1
            return {'kind': 'packet', 'consumed': consumed, 'packet': pkt}
        else:
            dev.rx_errors += 1
            parser.reset()
            return {'kind': 'error', 'consumed': i + 1}

    return {'kind': 'need-more'}

# ============================================================
# CRC8/ATM
# ============================================================

def sdk_crc8(data):
    """计算 CRC8/ATM（多项式 0x07，初始值 0x00）。"""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc

# ============================================================
# 广播数据构建 API (Advertising Data Builder)
# ============================================================

def sdk_adv_build_flags(flags=0x06):
    """构建 BLE Flags AD Structure。"""
    return bytes([2, 0x01, flags])

def sdk_adv_build_name(name):
    """构建 Complete Local Name AD Structure。

    注意：名称按字节截断到 SDK_ADV_NAME_MAX，MicroPython 端不校验 UTF-8
    字符边界。
    """
    name_bytes = name.encode('utf-8')
    if len(name_bytes) > SDK_ADV_NAME_MAX:
        name_bytes = name_bytes[:SDK_ADV_NAME_MAX]
    adv = bytearray()
    adv.append(len(name_bytes) + 1)
    adv.append(0x09) # Complete Local Name
    adv.extend(name_bytes)
    return bytes(adv)

def sdk_adv_build_appearance(appearance):
    """构建 Appearance AD Structure。"""
    adv = bytearray(4)
    adv[0] = 3
    adv[1] = 0x19 # Appearance
    adv[2] = appearance & 0xFF
    adv[3] = (appearance >> 8) & 0xFF
    return bytes(adv)

def sdk_adv_build_service_uuid128_incomplete(uuid_bytes):
    """构建 128-bit Service UUID 不完整列表 AD Structure。"""
    adv = bytearray()
    adv.append(17)
    adv.append(0x06)
    adv.extend(uuid_bytes)
    return bytes(adv)

def sdk_adv_build_service_uuid128_complete(uuid_bytes):
    """构建 128-bit Service UUID 完整列表 AD Structure。"""
    adv = bytearray()
    adv.append(17)
    adv.append(0x07)
    adv.extend(uuid_bytes)
    return bytes(adv)

def sdk_build_adv_data(mac, name):
    """统一构建广播包 (ADV) 与扫描响应包 (SCAN_RSP)。

    注意：BLE 广播和扫描响应各自通常限制 31 字节，修改字段时需重新
    核算长度。
    """
    # mac 参数当前暂未使用，保留以备未来将 MAC 混入厂商自定义字段
    # 广播包: Flags + Name + Appearance
    adv_data = bytearray()
    adv_data.extend(sdk_adv_build_flags(0x06))
    adv_data.extend(sdk_adv_build_name(name))
    adv_data.extend(sdk_adv_build_appearance(0x14C0))

    # 扫描响应包: Service UUID (由于我们用的是 128-bit UUID, 放进 SCAN_RSP)
    scan_rsp = bytearray()
    uuid_bytes = bytes(SDK_SVC_UUID)
    scan_rsp.extend(sdk_adv_build_service_uuid128_incomplete(uuid_bytes))

    print(f"[ADV INFO] ADV Data Len: {len(adv_data)} bytes, Hex: {''.join('%02X' % x for x in adv_data)}")
    print(f"[ADV INFO] SCAN_RSP Len: {len(scan_rsp)} bytes, Hex: {''.join('%02X' % x for x in scan_rsp)}")

    return bytes(adv_data), bytes(scan_rsp)
