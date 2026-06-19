"""协议版本兼容性判断工具。

说明：兼容判断只使用双方声明的协议版本范围，不解析 feature_mask。
"""

from sdk_version import SDK_MIN_PROTOCOL_VERSION, SDK_PROTOCOL_VERSION

SDK_COMPAT_LEVEL_OK = 0
SDK_COMPAT_LEVEL_WARN = 1
SDK_COMPAT_LEVEL_LIMITED = 2
SDK_COMPAT_LEVEL_BLOCK = 3

SDK_COMPAT_CODE_NONE = 0
SDK_COMPAT_CODE_APP_TOO_OLD = 1
SDK_COMPAT_CODE_DEVICE_TOO_OLD = 2
SDK_COMPAT_CODE_DEVICE_NEWER = 3


def sdk_is_app_protocol_supported_by_device(device_protocol_min_version, app_protocol_version=SDK_PROTOCOL_VERSION):
    """判断当前 App 协议版本是否满足设备最低要求。"""
    return device_protocol_min_version <= app_protocol_version


def sdk_check_protocol_compat(
    device_protocol_version,
    device_protocol_min_version,
    app_protocol_version=SDK_PROTOCOL_VERSION,
    app_min_protocol_version=SDK_MIN_PROTOCOL_VERSION,
):
    """检查 App 与设备协议版本是否兼容。

    注意：设备版本较新只返回 WARN；是否限制新功能由上层按 feature 或
    业务能力决定。
    """
    result = {
        'level': SDK_COMPAT_LEVEL_OK,
        'code': SDK_COMPAT_CODE_NONE,
        'app_protocol_version': app_protocol_version,
        'app_min_protocol_version': app_min_protocol_version,
        'device_protocol_version': device_protocol_version,
        'device_protocol_min_version': device_protocol_min_version,
    }

    if device_protocol_min_version > app_protocol_version:
        result['level'] = SDK_COMPAT_LEVEL_BLOCK
        result['code'] = SDK_COMPAT_CODE_APP_TOO_OLD
        return result

    if device_protocol_version < app_min_protocol_version:
        result['level'] = SDK_COMPAT_LEVEL_BLOCK
        result['code'] = SDK_COMPAT_CODE_DEVICE_TOO_OLD
        return result

    if device_protocol_version > app_protocol_version:
        result['level'] = SDK_COMPAT_LEVEL_WARN
        result['code'] = SDK_COMPAT_CODE_DEVICE_NEWER
        return result

    return result
