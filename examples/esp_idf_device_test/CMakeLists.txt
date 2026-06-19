cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3 CACHE STRING "ESP-IDF target")
set(EXTRA_COMPONENT_DIRS components/weld_sdk)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(weld_sdk_device_test)
