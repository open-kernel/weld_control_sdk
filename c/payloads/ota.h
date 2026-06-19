#ifndef SDK_PAYLOADS_OTA_H
#define SDK_PAYLOADS_OTA_H

/**
 * @file ota.h
 * @brief OTA payload 编解码。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(SDK_PACKED_BEGIN) || !defined(SDK_PACKED_END) || !defined(SDK_PACKED)
#undef SDK_PACKED_BEGIN
#undef SDK_PACKED_END
#undef SDK_PACKED
#if defined(_MSC_VER)
#define SDK_PACKED_BEGIN __pragma(pack(push, 1))
#define SDK_PACKED_END __pragma(pack(pop))
#define SDK_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define SDK_PACKED_BEGIN
#define SDK_PACKED_END
#define SDK_PACKED __attribute__((packed))
#else
#define SDK_PACKED_BEGIN
#define SDK_PACKED_END
#define SDK_PACKED
#endif
#endif

#define SDK_OTA_JOIN_IMPL(a, b) a##b
#define SDK_OTA_JOIN(a, b) SDK_OTA_JOIN_IMPL(a, b)
#if defined(__cplusplus) && __cplusplus >= 201103L
#define SDK_OTA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define SDK_OTA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define SDK_OTA_STATIC_ASSERT(cond, msg) \
  typedef char SDK_OTA_JOIN(sdk_ota_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

/* OTA 业务码，放在 sdk_result_t.code 中 */
#define OTA_CODE_NONE 0x00    /**< 无错误 */
#define OTA_CODE_SIZE 0x64    /**< 固件大小超限 */
#define OTA_CODE_SEQ 0x65     /**< 分片序号错误 */
#define OTA_CODE_CRC 0x66     /**< CRC 校验失败 */
#define OTA_CODE_BUSY 0x67    /**< 设备忙 */
#define OTA_CODE_COMPANY 0x68 /**< 厂商ID不匹配（防止上传错固件） */
#define OTA_CODE_TYPE 0x69    /**< 固件类型不匹配（BLE/UART 混淆） */

/** OTA 最大固件大小（字节），默认 256 KB */
#ifndef SDK_OTA_MAX_FW_SIZE
#define SDK_OTA_MAX_FW_SIZE (256 * 1024u)
#endif
/** OTA 单包数据最大长度 */
#define SDK_OTA_CHUNK_MAX 240u
#define OTA_START_PAYLOAD_SIZE 14     /**< OTA 开始请求 payload 长度 */
#define OTA_DATA_HDR_PAYLOAD_SIZE 4   /**< OTA 数据片头 payload 长度，不含固件数据 */
#define OTA_VERIFY_PAYLOAD_SIZE 6     /**< OTA 校验请求 payload 长度 */
#define OTA_ACK_DATA_PAYLOAD_SIZE 2   /**< OTA ACK data payload 长度，不含 sdk_result_t 头 */

SDK_PACKED_BEGIN
/** OTA 开始载荷（上位机→设备） */
typedef struct SDK_PACKED {
  uint16_t company_id; /**< 厂商ID，必须与设备端匹配 */
  uint8_t fw_type;     /**< 固件类型 FW_TYPE_BLE=0x01 / FW_TYPE_UART=0x02 */
  uint8_t fw_ver[3];   /**< 目标固件版本 [主, 次, 修订] */
  uint32_t fw_size;    /**< 固件总字节数（不含头部） */
  uint16_t fw_crc16;   /**< 整固件 CRC16-CCITT-FALSE */
  uint16_t chunk_size; /**< 每包数据字节数（不含协议头） */
} ota_start_t;
SDK_PACKED_END

SDK_PACKED_BEGIN
/** OTA 数据包头（上位机→设备，后紧跟 chunk_size 字节固件数据） */
typedef struct SDK_PACKED {
  uint16_t chunk_index; /**< 分片索引（0-based） */
  uint16_t data_len;    /**< 本片实际数据长度 */
} ota_data_hdr_t;
SDK_PACKED_END

SDK_PACKED_BEGIN
/** OTA 校验载荷（上位机→设备） */
typedef struct SDK_PACKED {
  uint32_t fw_size;  /**< 固件总字节数（再次确认） */
  uint16_t fw_crc16; /**< 整固件 CRC16 */
} ota_verify_t;
SDK_PACKED_END

SDK_PACKED_BEGIN
/** OTA 应答 data 载荷，外层 status/code 由 sdk_result_t 承载。 */
typedef struct SDK_PACKED {
  uint16_t next_chunk; /**< 期望下一分片索引（用于续传） */
} ota_ack_data_t;
SDK_PACKED_END

SDK_OTA_STATIC_ASSERT(sizeof(ota_start_t) == OTA_START_PAYLOAD_SIZE,
                      "ota_start_t size must match wire payload");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, company_id) == 0,
                      "ota_start_t company_id offset mismatch");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, fw_type) == 2,
                      "ota_start_t fw_type offset mismatch");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, fw_ver) == 3,
                      "ota_start_t fw_ver offset mismatch");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, fw_size) == 6,
                      "ota_start_t fw_size offset mismatch");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, fw_crc16) == 10,
                      "ota_start_t fw_crc16 offset mismatch");
SDK_OTA_STATIC_ASSERT(offsetof(ota_start_t, chunk_size) == 12,
                      "ota_start_t chunk_size offset mismatch");
SDK_OTA_STATIC_ASSERT(sizeof(ota_data_hdr_t) == OTA_DATA_HDR_PAYLOAD_SIZE,
                      "ota_data_hdr_t size must match wire payload");
SDK_OTA_STATIC_ASSERT(sizeof(ota_verify_t) == OTA_VERIFY_PAYLOAD_SIZE,
                      "ota_verify_t size must match wire payload");
SDK_OTA_STATIC_ASSERT(sizeof(ota_ack_data_t) == OTA_ACK_DATA_PAYLOAD_SIZE,
                      "ota_ack_data_t size must match wire payload");

/**
 * @brief 打包 OTA 开始请求。
 *
 * 注意：fw_size 不含固件包头，只表示 payload 固件体大小。
 */
bool ota_start_pack(const ota_start_t *fields, uint8_t *buf, uint16_t buf_size);
/** @brief 解包 OTA 开始请求。 */
bool ota_start_unpack(const uint8_t *data, uint16_t len, ota_start_t *out_fields);
/** @brief 打包 OTA 数据片头。 */
bool ota_data_hdr_pack(const ota_data_hdr_t *fields, uint8_t *buf, uint16_t buf_size);
/** @brief 解包 OTA 数据片头。 */
bool ota_data_hdr_unpack(const uint8_t *data, uint16_t len, ota_data_hdr_t *out_fields);
/** @brief 打包 OTA 校验请求。 */
bool ota_verify_pack(const ota_verify_t *fields, uint8_t *buf, uint16_t buf_size);
/** @brief 解包 OTA 校验请求。 */
bool ota_verify_unpack(const uint8_t *data, uint16_t len, ota_verify_t *out_fields);
/** @brief 打包 OTA 应答 data。 */
bool ota_ack_data_pack(const ota_ack_data_t *fields, uint8_t *buf, uint16_t buf_size);
/** @brief 解包 OTA 应答 data。 */
bool ota_ack_data_unpack(const uint8_t *data, uint16_t len, ota_ack_data_t *out_fields);

#ifdef __cplusplus
}
#endif

#endif
