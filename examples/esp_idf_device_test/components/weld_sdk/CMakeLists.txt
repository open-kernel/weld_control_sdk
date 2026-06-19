set(WELD_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../c")

idf_component_register(
    SRCS
        "${WELD_SDK_DIR}/sdk_codec.c"
        "${WELD_SDK_DIR}/sdk_compat.c"
        "${WELD_SDK_DIR}/feature_mask.c"
        "${WELD_SDK_DIR}/payloads/device_info.c"
        "${WELD_SDK_DIR}/payloads/ota.c"
        "${WELD_SDK_DIR}/payloads/settings.c"
        "${WELD_SDK_DIR}/payloads/dashboard.c"
    INCLUDE_DIRS
        "${WELD_SDK_DIR}"
        "${WELD_SDK_DIR}/payloads"
)
