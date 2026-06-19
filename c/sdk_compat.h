#ifndef SDK_COMPAT_H
#define SDK_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#include "sdk_version.h"

/**
 * @file sdk_compat.h
 * @brief 协议版本兼容性判断工具。
 *
 * 说明：兼容判断只使用双方声明的协议版本范围，不解析 feature_mask。
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDK_COMPAT_LEVEL_OK = 0,      /**< 完全兼容 */
  SDK_COMPAT_LEVEL_WARN = 1,    /**< 可继续使用，但可能缺少新功能 */
  SDK_COMPAT_LEVEL_LIMITED = 2, /**< 功能受限，预留等级 */
  SDK_COMPAT_LEVEL_BLOCK = 3,   /**< 阻断连接或核心流程 */
} sdk_compat_level_t;

typedef enum {
  SDK_COMPAT_CODE_NONE = 0,           /**< 无兼容问题 */
  SDK_COMPAT_CODE_APP_TOO_OLD = 1,    /**< App 协议版本低于设备要求的最低版本 */
  SDK_COMPAT_CODE_DEVICE_TOO_OLD = 2, /**< 设备协议版本低于 App 可解析的最低版本 */
  SDK_COMPAT_CODE_DEVICE_NEWER = 3,   /**< 设备协议版本高于 App 当前版本 */
} sdk_compat_code_t;

/** 协议兼容性检查结果。 */
typedef struct {
  sdk_compat_level_t level;
  sdk_compat_code_t code;
  uint16_t app_protocol_version;
  uint16_t app_min_protocol_version;
  uint16_t device_protocol_version;
  uint16_t device_protocol_min_version;
} sdk_compat_result_t;

/**
 * @brief 判断当前 App 协议版本是否满足设备最低要求。
 * @param device_protocol_min_version 设备要求的最低 App 协议版本。
 * @param app_protocol_version App 当前协议版本。
 * @return 满足返回 true。
 */
bool sdk_is_app_protocol_supported_by_device(uint16_t device_protocol_min_version,
                                             uint16_t app_protocol_version);

/**
 * @brief 检查 App 与设备协议版本是否兼容。
 * @param device_protocol_version 设备当前协议版本。
 * @param device_protocol_min_version 设备要求的最低 App 协议版本。
 * @param app_protocol_version App 当前协议版本。
 * @param app_min_protocol_version App 可解析的最低设备协议版本。
 * @return 兼容性等级和业务码。
 *
 * 注意：设备版本较新只返回 WARN；是否限制新功能由上层按 feature 或业务能力决定。
 */
sdk_compat_result_t sdk_check_protocol_compat(uint16_t device_protocol_version,
                                              uint16_t device_protocol_min_version,
                                              uint16_t app_protocol_version,
                                              uint16_t app_min_protocol_version);

/**
 * @brief 使用 SDK 默认版本检查协议兼容性。
 * @param device_protocol_version 设备当前协议版本。
 * @param device_protocol_min_version 设备要求的最低 App 协议版本。
 * @return 兼容性等级和业务码。
 */
sdk_compat_result_t sdk_check_protocol_compat_default(uint16_t device_protocol_version,
                                                      uint16_t device_protocol_min_version);

#ifdef __cplusplus
}
#endif

#endif /* SDK_COMPAT_H */
