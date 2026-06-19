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

/** 30 个预留功能标志位，具体含义由设备型号文档定义。 */
#define FEATURE_01 (1U << 0)
#define FEATURE_02 (1U << 1)
#define FEATURE_03 (1U << 2)
#define FEATURE_04 (1U << 3)
#define FEATURE_05 (1U << 4)
#define FEATURE_06 (1U << 5)
#define FEATURE_07 (1U << 6)
#define FEATURE_08 (1U << 7)
#define FEATURE_09 (1U << 8)
#define FEATURE_10 (1U << 9)
#define FEATURE_11 (1U << 10)
#define FEATURE_12 (1U << 11)
#define FEATURE_13 (1U << 12)
#define FEATURE_14 (1U << 13)
#define FEATURE_15 (1U << 14)
#define FEATURE_16 (1U << 15)
#define FEATURE_17 (1U << 16)
#define FEATURE_18 (1U << 17)
#define FEATURE_19 (1U << 18)
#define FEATURE_20 (1U << 19)
#define FEATURE_21 (1U << 20)
#define FEATURE_22 (1U << 21)
#define FEATURE_23 (1U << 22)
#define FEATURE_24 (1U << 23)
#define FEATURE_25 (1U << 24)
#define FEATURE_26 (1U << 25)
#define FEATURE_27 (1U << 26)
#define FEATURE_28 (1U << 27)
#define FEATURE_29 (1U << 28)
#define FEATURE_30 (1U << 29)

/**
 * @brief 覆盖当前全局功能掩码。
 * @param mask 新功能掩码。
 *
 * 注意：该值是模块级全局状态，多设备场景建议直接使用设备信息中的 feature_mask。
 */
void feature_mask_set(uint32_t mask);

/**
 * @brief 向当前全局功能掩码追加功能位。
 * @param features 要追加的 FEATURE_* 位，可用按位或组合。
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
 * @param feature 单个 FEATURE_* 位。
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
