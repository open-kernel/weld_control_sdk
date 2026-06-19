"""设备功能位辅助工具。

说明：feature mask 只描述设备能力，不用于协议版本兼容判断。
"""

# 30 个预留功能标志位，具体含义由设备型号文档定义。
FEATURE_01 = 1 << 0
FEATURE_02 = 1 << 1
FEATURE_03 = 1 << 2
FEATURE_04 = 1 << 3
FEATURE_05 = 1 << 4
FEATURE_06 = 1 << 5
FEATURE_07 = 1 << 6
FEATURE_08 = 1 << 7
FEATURE_09 = 1 << 8
FEATURE_10 = 1 << 9
FEATURE_11 = 1 << 10
FEATURE_12 = 1 << 11
FEATURE_13 = 1 << 12
FEATURE_14 = 1 << 13
FEATURE_15 = 1 << 14
FEATURE_16 = 1 << 15
FEATURE_17 = 1 << 16
FEATURE_18 = 1 << 17
FEATURE_19 = 1 << 18
FEATURE_20 = 1 << 19
FEATURE_21 = 1 << 20
FEATURE_22 = 1 << 21
FEATURE_23 = 1 << 22
FEATURE_24 = 1 << 23
FEATURE_25 = 1 << 24
FEATURE_26 = 1 << 25
FEATURE_27 = 1 << 26
FEATURE_28 = 1 << 27
FEATURE_29 = 1 << 28
FEATURE_30 = 1 << 29

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
