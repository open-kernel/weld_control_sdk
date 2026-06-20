#ifndef SDK_PAYLOADS_SETTINGS_H
#define SDK_PAYLOADS_SETTINGS_H

/**
 * @file settings.h
 * @brief Settings payload 编解码。
 */

#include <stdbool.h>
#include <stdint.h>

#include "../sdk_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_TRIGGER_MODE_UNSET 0  /**< 未设置，设备不得静默当作默认值 */
#define SETTINGS_TRIGGER_MODE_MANUAL 1 /**< 手动触发 */
#define SETTINGS_TRIGGER_MODE_AUTO 2   /**< 自动触发 */

#define SETTINGS_ESR_QUALITY_UNKNOWN 0   /**< 未知质量 */
#define SETTINGS_ESR_QUALITY_EXCELLENT 1 /**< 优良 */
#define SETTINGS_ESR_QUALITY_GOOD 2      /**< 良好 */
#define SETTINGS_ESR_QUALITY_HEALTHY 3   /**< 健康 */
#define SETTINGS_ESR_QUALITY_POOR 4      /**< 较差 */
#define SETTINGS_ESR_QUALITY_BAD 5       /**< 很差 */

#define SETTINGS_FAULT_LOG_TYPE_NORMAL 0 /**< 普通信息 */
#define SETTINGS_FAULT_LOG_TYPE_WARN 1   /**< 警告 */
#define SETTINGS_FAULT_LOG_TYPE_ERROR 2  /**< 错误 */

#define SETTINGS_FAULT_TITLE_LEN 32         /**< 故障日志标题固定长度，单位 byte */
#define SETTINGS_FAULT_MESSAGE_LEN 128      /**< 故障日志消息固定长度，单位 byte */
#define SETTINGS_FAULT_LOG_PAYLOAD_SIZE 167 /**< 单条故障日志 payload 长度 */
#define SETTINGS_RUNTIME_PROFILE_PAYLOAD_SIZE 17 /**< 运行参数 payload 长度 */
#define SETTINGS_LIMITS_MAX_PAYLOAD_SIZE 16       /**< 动态上限 payload 长度 */
#define SETTINGS_CURRENT_PAYLOAD_SIZE 34          /**< 设置页首包 payload 长度 */
#define SETTINGS_APPLY_PROFILE_PAYLOAD_SIZE 18    /**< 应用参数 payload 长度 */
#define SETTINGS_RESET_PAYLOAD_SIZE 1             /**< 恢复出厂选项 payload 长度 */
#define SETTINGS_RESET_FLAG_CLEAR_TOKENS 0x01     /**< 恢复出厂时同时清除配对 token */
#define SETTINGS_SELF_CHECK_MAX_PAYLOAD_SIZE SDK_MAX_PAYLOAD /**< 自检单包上限 */

typedef struct {
  uint16_t id;                                      /**< 故障日志 ID */
  uint8_t type;                                     /**< 类型：0 正常，1 警告，2 错误 */
  uint32_t time_sec;                                /**< 日志时间，Unix 秒 */
  char title[SETTINGS_FAULT_TITLE_LEN + 1];         /**< 设备侧标题，固定 32 字节 UTF-8 */
  char message[SETTINGS_FAULT_MESSAGE_LEN + 1];     /**< 设备侧消息，固定 128 字节 UTF-8 */
} settings_fault_log_t;

typedef struct {
  uint16_t target_voltage_mv;                       /**< 目标储能电压，单位 mV */
  uint16_t single_cap_voltage_mv;                   /**< 单电容限压，单位 mV，必填 */
  uint16_t weld_disable_voltage_mv;                 /**< 禁焊电压，单位 mV */
  uint16_t target_current_a10;                      /**< 目标输出电流，单位 0.1A */
  uint16_t preheat_pulse_ms10;                      /**< 预热脉冲时间，单位 0.1ms */
  uint16_t cool_time_ms10;                          /**< 冷却时间，单位 0.1ms */
  uint16_t main_pulse_ms10;                         /**< 主脉冲时间，单位 0.1ms */
  uint8_t trigger_mode;                             /**< 触发模式：0 未选择/未知，1 手动，2 自动 */
  uint16_t auto_delay_ms;                           /**< 自动触发延迟，单位 ms */
} settings_runtime_profile_t;

typedef struct {
  uint16_t target_voltage_mv_max;                   /**< 充电目标电压上限，单位 mV */
  uint16_t single_cap_voltage_mv_max;               /**< 单电容限压上限，单位 mV */
  uint16_t weld_disable_voltage_mv_max;             /**< 禁焊电压上限，单位 mV */
  uint16_t target_current_a10_max;                  /**< 充电电流上限，单位 0.1A */
  uint16_t preheat_pulse_ms10_max;                  /**< 预热脉冲时间上限，单位 0.1ms */
  uint16_t cool_time_ms10_max;                      /**< 冷却时间上限，单位 0.1ms */
  uint16_t main_pulse_ms10_max;                     /**< 主脉冲时间上限，单位 0.1ms */
  uint16_t auto_delay_ms_max;                       /**< 自动触发延迟上限，单位 ms */
} settings_limits_max_t;

typedef struct {
  uint8_t profile_id;                               /**< 设备当前生效预设 ID，1-10；0 表示未知 */
  settings_runtime_profile_t current_profile;       /**< 设备当前生效参数 */
  settings_limits_max_t limits_max;                 /**< 设备侧 settings 动态上限 */
} settings_current_t;

typedef struct {
  const uint16_t *esrs_mohm10;                      /**< ESR 测量值数组，单位 0.1mΩ */
  uint8_t esr_size;                                 /**< ESR 数量 */
  uint8_t esr_quality;                              /**< ESR 质量等级 */
  const settings_fault_log_t *fault_logs;           /**< 历史故障日志数组 */
  uint8_t fault_log_size;                           /**< 历史故障日志数量 */
} settings_self_check_t;

typedef struct {
  uint8_t profile_id;                               /**< App 要应用到设备的预设 ID，1-10 */
  settings_runtime_profile_t profile;               /**< App 要应用到设备的当前参数 */
} settings_apply_profile_t;

typedef struct {
  uint8_t flags;                                    /**< 恢复出厂选项；bit0=同时清除配对 token */
} settings_reset_t;

/** @brief 打包设置页首包：当前参数 + 动态上限。 */
bool settings_current_pack(const settings_current_t *fields, uint8_t *buf,
                           uint16_t buf_size, uint16_t *out_len);
/** @brief 解包设置页首包：当前参数 + 动态上限。 */
bool settings_current_unpack(const uint8_t *data, uint16_t len,
                             settings_current_t *out_fields);
/**
 * @brief 打包设备自检数据。
 *
 * 注意：ESR 和故障日志较多导致超过单帧上限时返回 false，不会静默截断。
 */
bool settings_self_check_pack(const settings_self_check_t *fields, uint8_t *buf,
                              uint16_t buf_size, uint16_t *out_len);
/**
 * @brief 解包设备自检数据。
 *
 * 注意：ESR 数组和故障日志由调用方提供缓冲区，容量不足会返回 false。
 */
bool settings_self_check_unpack(const uint8_t *data, uint16_t len,
                                settings_self_check_t *out_fields,
                                uint16_t *esrs_mohm10,
                                uint8_t esr_capacity,
                                settings_fault_log_t *fault_logs,
                                uint8_t fault_log_capacity);
/** @brief 打包 App 要应用到设备的参数。 */
bool settings_apply_profile_pack(const settings_apply_profile_t *fields, uint8_t *buf,
                                 uint16_t buf_size, uint16_t *out_len);
/** @brief 解包 App 要应用到设备的参数。 */
bool settings_apply_profile_unpack(const uint8_t *data, uint16_t len,
                                   settings_apply_profile_t *out_fields);
/** @brief 打包恢复出厂设置选项。 */
bool settings_reset_pack(const settings_reset_t *fields, uint8_t *buf,
                         uint16_t buf_size, uint16_t *out_len);
/** @brief 解包恢复出厂设置选项。 */
bool settings_reset_unpack(const uint8_t *data, uint16_t len,
                           settings_reset_t *out_fields);

#ifdef __cplusplus
}
#endif

#endif /* SDK_PAYLOADS_SETTINGS_H */
