"""Settings payload 编解码。"""

import struct
from sdk_codec import SDK_MAX_PAYLOAD

SETTINGS_TRIGGER_MODE_UNSET = 0  # 未设置，设备不得静默当作默认值
SETTINGS_TRIGGER_MODE_MANUAL = 1 # 手动触发
SETTINGS_TRIGGER_MODE_AUTO = 2   # 自动触发

SETTINGS_ESR_QUALITY_UNKNOWN = 0   # 未知质量
SETTINGS_ESR_QUALITY_EXCELLENT = 1 # 优良
SETTINGS_ESR_QUALITY_GOOD = 2      # 良好
SETTINGS_ESR_QUALITY_HEALTHY = 3   # 健康
SETTINGS_ESR_QUALITY_POOR = 4      # 较差
SETTINGS_ESR_QUALITY_BAD = 5       # 很差

SETTINGS_FAULT_LOG_TYPE_NORMAL = 0 # 普通信息
SETTINGS_FAULT_LOG_TYPE_WARN = 1   # 警告
SETTINGS_FAULT_LOG_TYPE_ERROR = 2  # 错误

SETTINGS_FAULT_TITLE_LEN = 32            # 故障日志标题固定长度，单位 byte
SETTINGS_FAULT_MESSAGE_LEN = 128         # 故障日志消息固定长度，单位 byte
SETTINGS_RESET_FLAG_CLEAR_TOKENS = 0x01  # 恢复出厂时同时清除配对 token


def _encode_fixed_string(value, length):
    """将字符串编码为固定长度 UTF-8 字节，不足补 0。"""
    text = value or ''
    data = text.encode('utf-8')
    while len(data) > length and text:
        text = text[:-1]
        data = text.encode('utf-8')
    return data[:length] + b'\x00' * (length - len(data[:length]))


def _decode_fixed_string(data):
    """解码固定长度 UTF-8 字符串，并移除尾部 0。"""
    return data.decode('utf-8', 'replace').rstrip('\x00')


class settings_fault_log_t:
    """单条故障日志 payload。"""

    BYTE_LENGTH = 167

    @staticmethod
    def pack(fields):
        """打包单条故障日志。

        注意：title/message 会按 UTF-8 字符边界截断。
        """
        return b''.join((
            struct.pack('<HBI', fields['id'], fields['type'], fields['time_sec']),
            _encode_fixed_string(fields.get('title', ''), SETTINGS_FAULT_TITLE_LEN),
            _encode_fixed_string(fields.get('message', ''), SETTINGS_FAULT_MESSAGE_LEN),
        ))

    @staticmethod
    def unpack(data):
        """解包单条故障日志；长度不足时返回 None。"""
        if len(data) < settings_fault_log_t.BYTE_LENGTH:
            return None
        values = struct.unpack('<HBI', data[0:7])
        return {
            'id': values[0],
            'type': values[1],
            'time_sec': values[2],
            'title': _decode_fixed_string(data[7:39]),
            'message': _decode_fixed_string(data[39:167]),
        }


class settings_runtime_profile_t:
    """设备当前生效运行参数 payload。

    注意：single_cap_voltage_mv 为必填项，设备端不应接受 0 作为有效配置。
    """

    BYTE_LENGTH = 17

    @staticmethod
    def pack(fields):
        """打包设备运行参数。"""
        return struct.pack(
            '<HHHHHHHBH',
            fields['target_voltage_mv'],
            fields['single_cap_voltage_mv'],
            fields['weld_disable_voltage_mv'],
            fields['target_current_a10'],
            fields['preheat_pulse_ms10'],
            fields['cool_time_ms10'],
            fields['main_pulse_ms10'],
            fields['trigger_mode'],
            fields['auto_delay_ms'],
        )

    @staticmethod
    def unpack(data):
        """解包设备运行参数；长度不足时返回 None。"""
        if len(data) < settings_runtime_profile_t.BYTE_LENGTH:
            return None
        values = struct.unpack('<HHHHHHHBH', data[:settings_runtime_profile_t.BYTE_LENGTH])
        return {
            'target_voltage_mv': values[0],
            'single_cap_voltage_mv': values[1],
            'weld_disable_voltage_mv': values[2],
            'target_current_a10': values[3],
            'preheat_pulse_ms10': values[4],
            'cool_time_ms10': values[5],
            'main_pulse_ms10': values[6],
            'trigger_mode': values[7],
            'auto_delay_ms': values[8],
        }


class settings_limits_max_t:
    """设备侧 settings 动态上限 payload。"""

    BYTE_LENGTH = 16

    @staticmethod
    def pack(fields):
        """打包 settings 动态上限。"""
        return struct.pack(
            '<HHHHHHHH',
            fields['target_voltage_mv_max'],
            fields['single_cap_voltage_mv_max'],
            fields['weld_disable_voltage_mv_max'],
            fields['target_current_a10_max'],
            fields['preheat_pulse_ms10_max'],
            fields['cool_time_ms10_max'],
            fields['main_pulse_ms10_max'],
            fields['auto_delay_ms_max'],
        )

    @staticmethod
    def unpack(data):
        """解包 settings 动态上限；长度不足时返回 None。"""
        if len(data) < settings_limits_max_t.BYTE_LENGTH:
            return None
        values = struct.unpack('<HHHHHHHH', data[:settings_limits_max_t.BYTE_LENGTH])
        return {
            'target_voltage_mv_max': values[0],
            'single_cap_voltage_mv_max': values[1],
            'weld_disable_voltage_mv_max': values[2],
            'target_current_a10_max': values[3],
            'preheat_pulse_ms10_max': values[4],
            'cool_time_ms10_max': values[5],
            'main_pulse_ms10_max': values[6],
            'auto_delay_ms_max': values[7],
        }


class settings_current_t:
    """设置页首包：当前参数 + 动态上限。"""

    BYTE_LENGTH = 1 + settings_runtime_profile_t.BYTE_LENGTH + settings_limits_max_t.BYTE_LENGTH

    @staticmethod
    def pack(fields):
        """打包设置页首包。"""
        return (
            bytes([fields.get('profile_id', 0) & 0xFF])
            + settings_runtime_profile_t.pack(fields['current_profile'])
            + settings_limits_max_t.pack(fields['limits_max'])
        )

    @staticmethod
    def unpack(data):
        """解包设置页首包；长度不足时返回 None。"""
        if len(data) < settings_current_t.BYTE_LENGTH:
            return None
        profile = settings_runtime_profile_t.unpack(data[1:])
        limits_offset = 1 + settings_runtime_profile_t.BYTE_LENGTH
        limits_max = settings_limits_max_t.unpack(data[limits_offset:])
        if profile is None or limits_max is None:
            return None
        return {'profile_id': data[0], 'current_profile': profile, 'limits_max': limits_max}


class settings_self_check_t:
    """设备自检数据：ESR 数组、质量等级和故障日志。"""

    @staticmethod
    def pack(fields):
        """打包设备自检数据。

        注意：ESR 和故障日志较多导致超过 SDK_MAX_PAYLOAD 时返回 None，
        不会静默截断。
        """
        esrs = fields.get('esrs_mohm10', [])
        fault_logs = fields.get('fault_logs', [])
        if len(esrs) > 0xFF or len(fault_logs) > 0xFF:
            return None
        base_size = 1 + len(esrs) * 2 + 1 + 1
        needed = base_size + settings_fault_log_t.BYTE_LENGTH * len(fault_logs)
        if needed > SDK_MAX_PAYLOAD:
            return None
        buf = bytearray()
        buf.append(len(esrs))
        for esr in esrs:
            buf.extend(struct.pack('<H', esr))
        buf.append(fields['esr_quality'])
        buf.append(len(fault_logs))
        for log in fault_logs:
            buf.extend(settings_fault_log_t.pack(log))
        return bytes(buf)

    @staticmethod
    def unpack(data):
        """解包设备自检数据；长度不足时返回 None。"""
        if len(data) < 3:
            return None
        offset = 0
        esr_size = data[offset]
        offset += 1
        if len(data) < offset + esr_size * 2 + 2:
            return None
        esrs = []
        for _ in range(esr_size):
            esrs.append(struct.unpack('<H', data[offset: offset + 2])[0])
            offset += 2
        esr_quality = data[offset]
        offset += 1
        fault_log_size = data[offset]
        offset += 1
        if len(data) < offset + fault_log_size * settings_fault_log_t.BYTE_LENGTH:
            return None
        fault_logs = []
        for _ in range(fault_log_size):
            log = settings_fault_log_t.unpack(data[offset: offset + settings_fault_log_t.BYTE_LENGTH])
            if log is None:
                return None
            fault_logs.append(log)
            offset += settings_fault_log_t.BYTE_LENGTH
        return {'esrs_mohm10': esrs, 'esr_quality': esr_quality, 'fault_logs': fault_logs}


class settings_apply_profile_t:
    """App 要应用到设备的参数 payload。"""

    BYTE_LENGTH = 1 + settings_runtime_profile_t.BYTE_LENGTH

    @staticmethod
    def pack(fields):
        """打包 App 要应用到设备的参数。"""
        return bytes([fields.get('profile_id', 0) & 0xFF]) + settings_runtime_profile_t.pack(fields['profile'])

    @staticmethod
    def unpack(data):
        """解包 App 要应用到设备的参数；长度不足时返回 None。"""
        if len(data) < settings_apply_profile_t.BYTE_LENGTH:
            return None
        profile = settings_runtime_profile_t.unpack(data[1:])
        if profile is None:
            return None
        return {'profile_id': data[0], 'profile': profile}


class settings_reset_t:
    """恢复出厂设置选项 payload。"""

    BYTE_LENGTH = 1

    @staticmethod
    def pack(fields):
        """打包恢复出厂设置选项。"""
        return bytes([fields.get('flags', 0) & 0xFF])

    @staticmethod
    def unpack(data):
        """解包恢复出厂设置选项；长度不足时返回 None。"""
        if len(data) < settings_reset_t.BYTE_LENGTH:
            return None
        return {'flags': data[0]}
