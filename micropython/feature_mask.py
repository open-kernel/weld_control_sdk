"""设备功能位辅助工具。

说明：feature mask 只描述设备能力，不用于协议版本兼容判断。
"""

# 设置类功能。
SDK_FEATURE_SETTINGS_CHARGE_TARGET_VOLTAGE = 1 << 0
SDK_FEATURE_SETTINGS_CHARGE_TARGET_CURRENT = 1 << 1
SDK_FEATURE_SETTINGS_SINGLE_CAP_VOLTAGE_LIMIT = 1 << 2
SDK_FEATURE_SETTINGS_TRIGGER_MODE = 1 << 3

# Dashboard 读取类功能。
SDK_FEATURE_DASHBOARD_CHARGE_CURRENT = 1 << 4
SDK_FEATURE_DASHBOARD_WELD_CURRENT = 1 << 5
SDK_FEATURE_DASHBOARD_CAPACITOR_TEMPERATURE = 1 << 6
SDK_FEATURE_DASHBOARD_MOS_TEMPERATURE = 1 << 7
SDK_FEATURE_DASHBOARD_LOGS = 1 << 8

# 控制类功能。
SDK_FEATURE_CONTROL_SAFE_DISCHARGE = 1 << 9
SDK_FEATURE_CONTROL_CHARGE_START_PAUSE = 1 << 10

# 维护类功能。
SDK_FEATURE_MAINTENANCE_FIRMWARE_UPDATE = 1 << 11
SDK_FEATURE_MAINTENANCE_FACTORY_RESET = 1 << 12

# 诊断类功能。
SDK_FEATURE_DIAGNOSTIC_ESR_SELF_CHECK = 1 << 13
SDK_FEATURE_DIAGNOSTIC_FAULT_LOG_READ = 1 << 14

# 预留功能位。
SDK_FEATURE_RESERVED_16 = 1 << 15
SDK_FEATURE_RESERVED_17 = 1 << 16
SDK_FEATURE_RESERVED_18 = 1 << 17
SDK_FEATURE_RESERVED_19 = 1 << 18
SDK_FEATURE_RESERVED_20 = 1 << 19
SDK_FEATURE_RESERVED_21 = 1 << 20
SDK_FEATURE_RESERVED_22 = 1 << 21
SDK_FEATURE_RESERVED_23 = 1 << 22
SDK_FEATURE_RESERVED_24 = 1 << 23
SDK_FEATURE_RESERVED_25 = 1 << 24
SDK_FEATURE_RESERVED_26 = 1 << 25
SDK_FEATURE_RESERVED_27 = 1 << 26
SDK_FEATURE_RESERVED_28 = 1 << 27
SDK_FEATURE_RESERVED_29 = 1 << 28
SDK_FEATURE_RESERVED_30 = 1 << 29

_g_feature_mask = 0

def feature_mask_set(mask):
    """覆盖当前全局功能掩码。

    注意：该值是模块级全局状态，多设备场景建议直接使用设备信息中的
    feature_mask。
    """
    global _g_feature_mask
    _g_feature_mask = mask & 0xFFFFFFFF

def feature_mask_add(features):
    """向当前全局功能掩码追加功能位。"""
    global _g_feature_mask
    _g_feature_mask = (_g_feature_mask | features) & 0xFFFFFFFF

def feature_mask_get():
    """获取当前全局功能掩码。"""
    return _g_feature_mask

def feature_mask_has(mask, feature):
    """判断指定功能位是否存在。"""
    return (mask & feature) != 0

def feature_mask_to_hex(mask):
    """将功能掩码编码为 8 位大写十六进制字符串。"""
    return "{:08X}".format(mask & 0xFFFFFFFF)

def feature_mask_from_hex(hex_str):
    """从十六进制字符串解析功能掩码。"""
    return int(hex_str, 16) & 0xFFFFFFFF
