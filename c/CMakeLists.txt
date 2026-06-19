cmake_minimum_required(VERSION 3.14)
project(weld_sdk C)

add_library(weld_sdk STATIC
    sdk_codec.c
    sdk_compat.c
    feature_mask.c
    payloads/device_info.c
    payloads/dashboard.c
    payloads/ota.c
    payloads/settings.c
)

target_include_directories(weld_sdk PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/payloads
)
