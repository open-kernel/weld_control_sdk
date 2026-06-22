#ifndef FEATURE_MASK_H
#define FEATURE_MASK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @file feature_mask.h
 * @brief 设备功能位辅助工具。
 *
 * 说明：feature mask 只描述设备能力，不用于协议版本兼容判断。
 */

/** 充电目标电压设置。 */
#define SDK_FEATURE_SETTINGS_CHARGE_TARGET_VOLTAGE (1U << 0)
/** 充电目标电流设置。 */
#define SDK_FEATURE_SETTINGS_CHARGE_TARGET_CURRENT (1U << 1)
/** 单电容限压设置。 */
#define SDK_FEATURE_SETTINGS_SINGLE_CAP_VOLTAGE_LIMIT (1U << 2)
/** 手动/自动触发模式设置。 */
#define SDK_FEATURE_SETTINGS_TRIGGER_MODE (1U << 3)

/** 充电电流读取。 */
#define SDK_FEATURE_DASHBOARD_CHARGE_CURRENT (1U << 4)
/** 点焊电流读取。 */
#define SDK_FEATURE_DASHBOARD_WELD_CURRENT (1U << 5)
/** 电容温度读取。 */
#define SDK_FEATURE_DASHBOARD_CAPACITOR_TEMPERATURE (1U << 6)
/** MOS 温度读取。 */
#define SDK_FEATURE_DASHBOARD_MOS_TEMPERATURE (1U << 7)
/** Dashboard 日志读取。 */
#define SDK_FEATURE_DASHBOARD_LOGS (1U << 8)

/** 安全放电。 */
#define SDK_FEATURE_CONTROL_SAFE_DISCHARGE (1U << 9)
/** 开始/暂停充电控制。 */
#define SDK_FEATURE_CONTROL_CHARGE_START_PAUSE (1U << 10)

/** 固件升级。 */
#define SDK_FEATURE_MAINTENANCE_FIRMWARE_UPDATE (1U << 11)
/** 恢复出厂设置。 */
#define SDK_FEATURE_MAINTENANCE_FACTORY_RESET (1U << 12)

/** 设备内阻自检。 */
#define SDK_FEATURE_DIAGNOSTIC_ESR_SELF_CHECK (1U << 13)
/** 故障日志读取。 */
#define SDK_FEATURE_DIAGNOSTIC_FAULT_LOG_READ (1U << 14)

/** 预留功能位。 */
#define SDK_FEATURE_RESERVED_16 (1U << 15)
#define SDK_FEATURE_RESERVED_17 (1U << 16)
#define SDK_FEATURE_RESERVED_18 (1U << 17)
#define SDK_FEATURE_RESERVED_19 (1U << 18)
#define SDK_FEATURE_RESERVED_20 (1U << 19)
#define SDK_FEATURE_RESERVED_21 (1U << 20)
#define SDK_FEATURE_RESERVED_22 (1U << 21)
#define SDK_FEATURE_RESERVED_23 (1U << 22)
#define SDK_FEATURE_RESERVED_24 (1U << 23)
#define SDK_FEATURE_RESERVED_25 (1U << 24)
#define SDK_FEATURE_RESERVED_26 (1U << 25)
#define SDK_FEATURE_RESERVED_27 (1U << 26)
#define SDK_FEATURE_RESERVED_28 (1U << 27)
#define SDK_FEATURE_RESERVED_29 (1U << 28)
#define SDK_FEATURE_RESERVED_30 (1U << 29)

/**
 * @brief 覆盖当前全局功能掩码。
 * @param mask 新功能掩码。
 *
 * 注意：该值是模块级全局状态，多设备场景建议直接使用设备信息中的 feature_mask。
 */
void feature_mask_set(uint32_t mask);

/**
 * @brief 向当前全局功能掩码追加功能位。
 * @param features 要追加的 SDK_FEATURE_* 位，可用按位或组合。
 */
void feature_mask_add(uint32_t features);

/**
 * @brief 获取当前全局功能掩码。
 * @return 无符号 32 位功能掩码。
 */
uint32_t feature_mask_get(void);

/**
 * @brief 判断指定功能位是否存在。
 * @param mask 待检查的功能掩码。
 * @param feature 单个 SDK_FEATURE_* 位。
 * @return 支持返回 true。
 */
bool feature_mask_has(uint32_t mask, uint32_t feature);

/**
 * @brief 将功能掩码编码为 8 位大写十六进制字符串。
 * @param mask 功能掩码。
 * @param buf 输出缓冲区。
 *
 * 注意：buf 长度至少为 9，用于容纳 8 个字符和结尾 '\0'。
 */
void feature_mask_to_hex(uint32_t mask, char *buf);

/**
 * @brief 从十六进制字符串解析功能掩码。
 * @param hex 十六进制字符串。
 * @return 无符号 32 位功能掩码；非法字符按 0 处理。
 */
uint32_t feature_mask_from_hex(const char *hex);

#endif
