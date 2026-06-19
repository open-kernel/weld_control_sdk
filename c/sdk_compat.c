#include "sdk_compat.h"

bool sdk_is_app_protocol_supported_by_device(uint16_t device_protocol_min_version,
                                             uint16_t app_protocol_version) {
    return device_protocol_min_version <= app_protocol_version;
}

sdk_compat_result_t sdk_check_protocol_compat(uint16_t device_protocol_version,
                                              uint16_t device_protocol_min_version,
                                              uint16_t app_protocol_version,
                                              uint16_t app_min_protocol_version) {
    sdk_compat_result_t result = {
        SDK_COMPAT_LEVEL_OK,
        SDK_COMPAT_CODE_NONE,
        app_protocol_version,
        app_min_protocol_version,
        device_protocol_version,
        device_protocol_min_version,
    };

    if (device_protocol_min_version > app_protocol_version) {
        result.level = SDK_COMPAT_LEVEL_BLOCK;
        result.code = SDK_COMPAT_CODE_APP_TOO_OLD;
        return result;
    }

    if (device_protocol_version < app_min_protocol_version) {
        result.level = SDK_COMPAT_LEVEL_BLOCK;
        result.code = SDK_COMPAT_CODE_DEVICE_TOO_OLD;
        return result;
    }

    if (device_protocol_version > app_protocol_version) {
        result.level = SDK_COMPAT_LEVEL_WARN;
        result.code = SDK_COMPAT_CODE_DEVICE_NEWER;
        return result;
    }

    return result;
}

sdk_compat_result_t sdk_check_protocol_compat_default(uint16_t device_protocol_version,
                                                      uint16_t device_protocol_min_version) {
    return sdk_check_protocol_compat(device_protocol_version,
                                     device_protocol_min_version,
                                     SDK_PROTOCOL_VERSION,
                                     SDK_MIN_PROTOCOL_VERSION);
}

