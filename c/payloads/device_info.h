#ifndef SDK_PAYLOADS_DEVICE_INFO_H
#define SDK_PAYLOADS_DEVICE_INFO_H

/**
 * @file device_info.h
 * @brief 设备信息 payload 编解码。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_INFO_FIXED_SIZE_V1 29 /**< 设备信息 V1 固定头 payload 长度，不含变长字符串 */
#define DEVICE_INFO_FIXED_SIZE DEVICE_INFO_FIXED_SIZE_V1 /**< 当前设备信息固定头 payload 长度 */

/** 设备信息字段；字符串指针均由调用方管理，SDK 不复制。 */
typedef struct {
  uint16_t company_id;             /**< 厂商 ID，用于校验设备归属 */
  uint16_t product_id;             /**< 产品/型号 ID */
  uint32_t feature_mask;           /**< 功能集合 bitmask */
  uint16_t protocol_version;       /**< 协议版本，当前为 0x1 */
  uint16_t protocol_min_version;   /**< 设备要求的最低 App 协议版本 */
  uint16_t ota_max_kb;             /**< 最大固件大小，单位 KB */
  uint16_t ota_chunk_max;          /**< 推荐 OTA chunk size，单位 byte */
  uint8_t firmware_major;          /**< 固件版本 major */
  uint8_t firmware_minor;          /**< 固件版本 minor */
  uint8_t firmware_patch;          /**< 固件版本 patch */
  uint32_t firmware_build_id;      /**< 固件 build id */
  uint8_t hardware_major;          /**< 硬件版本 major */
  uint8_t hardware_minor;          /**< 硬件版本 minor */
  uint8_t hardware_patch;          /**< 硬件版本 patch */
  const char *manufacturer;        /**< 厂商字符串，UTF-8 */
  uint8_t manufacturer_len;        /**< 厂商字符串长度 */
  const char *model;               /**< 型号字符串，UTF-8 */
  uint8_t model_len;               /**< 型号字符串长度 */
  const char *serial;              /**< 序列号字符串，UTF-8 */
  uint8_t serial_len;              /**< 序列号字符串长度 */
} device_info_t;

/**
 * @brief 打包设备信息 payload。
 * @param fields 设备信息字段。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区长度。
 * @param out_len 实际写入长度。
 * @return 成功返回 true。
 *
 * 注意：manufacturer/model/serial 均按各自 len 写入，serial 不应为空。
 */
bool device_info_pack(const device_info_t *fields, uint8_t *buf,
                      uint16_t buf_size, uint16_t *out_len);
/**
 * @brief 解包设备信息 payload。
 * @param data payload 字节。
 * @param len payload 长度。
 * @param out_fields 输出字段。
 * @return 成功返回 true。
 *
 * 注意：字符串指针指向输入 data 内部，不保证 NUL 结尾。
 */
bool device_info_unpack(const uint8_t *data, uint16_t len,
                        device_info_t *out_fields);

#ifdef __cplusplus
}
#endif

#endif /* SDK_PAYLOADS_DEVICE_INFO_H */
