/**
 * @file sdk_codec.h
 * @brief BLE 数据层 SDK - 协议编解码器
 * @version 1.0
 * @date 2026-05-16
 *
 * 说明：
 *   本库为纯数据层编解码库，不提供连接管理、扫描、蓝牙协议栈等实现。
 *   提供帧格式定义、组包、解包、校验和命令集规范。
 *   支持单片机（STM32 等）与上位机（Windows/Linux/macOS）共用。
 *
 * 使用方式：
 *   每建立一个物理连接，创建一个 sdk_device_t 实例。
 *   从蓝牙/UART 收到原始字节后，调用 sdk_decode() 循环解析。
 *   发送数据时，调用 sdk_encode() 组帧。
 */

#ifndef SDK_CODEC_H
#define SDK_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdk_commands.h"
#include "sdk_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 跨平台兼容宏
 * ============================================================ */

/** 结构体紧凑排布（消除填充字节） */
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

#include "payloads/device_info.h"
#include "payloads/ota.h"

/** 内存分配钩子（单片机可重定向到 pvPortMalloc / 内存池） */
#ifndef SDK_MALLOC
#include <stdlib.h>
#define SDK_MALLOC(size) malloc(size)
#endif
#ifndef SDK_FREE
#include <stdlib.h>
#define SDK_FREE(ptr) free(ptr)
#endif

/* ============================================================
 * 可配置宏
 * ============================================================ */

#ifndef SDK_MAX_PAYLOAD
/** 当前 SDK 实现允许缓存/解析的最大 payload 长度，可按设备内存调整 */
#define SDK_MAX_PAYLOAD 1024
#endif

/** 线协议单帧 payload 理论上限（字节），v1 使用 16-bit little-endian 长度字段 */
#define SDK_WIRE_MAX_PAYLOAD 0xFFFFu

#ifndef SDK_TOKEN_LEN
/** 配对/鉴权 Token 长度（字节） */
#define SDK_TOKEN_LEN 12
#endif

/** 帧头固定长度：SOF + LEN_LE16 + CMD + SEQ */
#define SDK_HEAD_SIZE 5
/** CRC8 校验长度 */
#define SDK_CRC_SIZE 1
/** 帧固定开销（头 + CRC8），v1 固定 6 字节 */
#define SDK_FRAME_OVERHEAD (SDK_HEAD_SIZE + SDK_CRC_SIZE)
/** 广播设备名最大长度 */
#define SDK_ADV_NAME_MAX 16
/** 默认 BLE 单次写入字节数 */
#define SDK_DEFAULT_TRANSPORT_MTU_PAYLOAD 20

/* ============================================================
 * 帧常量
 * ============================================================ */

/** 帧起始字节 */
#define SDK_SOF 0xAA

/* ============================================================
 * UUID 定义
 * ============================================================ */

/** 主服务 UUID */
#define SDK_SVC_UUID                                                           \
  {0x75, 0x2F, 0xF3, 0xFD, 0xD4, 0xE8, 0xB6, 0xB2,                             \
   0xF3, 0x53, 0x7F, 0x87, 0x3F, 0xA2, 0x68, 0x58}
/** TX 特征 UUID（设备→上位机，Notify） */
#define SDK_CHR_TX_UUID                                                        \
  {0xDE, 0xAD, 0x1B, 0xE1, 0xA8, 0xF5, 0xFC, 0x8C,                             \
   0x10, 0x55, 0x2F, 0x83, 0x7C, 0xDA, 0x3C, 0x37}
/** RX 特征 UUID（上位机→设备，Write） */
#define SDK_CHR_RX_UUID                                                        \
  {0x4B, 0xEE, 0xCF, 0xE8, 0x99, 0x1A, 0x06, 0x93,                             \
   0xAD, 0x57, 0xA1, 0x72, 0x00, 0x52, 0x25, 0xA5}
/** 16-bit 兼容主服务 UUID，用于外置 BLE 模块 */
#define SDK_SVC_UUID16 "A23F"
/** 16-bit 兼容 TX 特征 UUID（设备→上位机，Notify） */
#define SDK_CHR_TX_UUID16 "DA7C"
/** 16-bit 兼容 RX 特征 UUID（上位机→设备，Write） */
#define SDK_CHR_RX_UUID16 "5200"

/* ============================================================
 * 数据结构
 * ============================================================ */

/** 配对请求载荷：wire 格式为 client_id[8] + name_len[1] + name[name_len] */
#define SDK_PAIR_CLIENT_ID_SIZE 8u
#define SDK_PAIR_REQUEST_HEADER_SIZE (SDK_PAIR_CLIENT_ID_SIZE + 1u)
/** 配对请求名称最大字节数，按 12 个常见中文 UTF-8 字符预留 */
#define SDK_PAIR_NAME_MAX 36u
typedef struct {
  uint64_t client_id;   /**< 上位机 ID，wire 为 u64 little-endian */
  const uint8_t *name;  /**< 指向 payload 内 name 数据，不复制，不保证 NUL 结尾 */
  uint8_t name_len;     /**< UTF-8 name 字节数 */
} pair_request_t;

typedef enum {
  SDK_RESULT_STATUS_RESERVED = 0,      /**< 保留/未初始化，收到后视为协议错误 */
  SDK_RESULT_STATUS_OK = 1,            /**< 成功 */
  SDK_RESULT_STATUS_FAIL = 2,          /**< 普通失败 */
  SDK_RESULT_STATUS_BUSY = 3,          /**< 设备忙 */
  SDK_RESULT_STATUS_AUTH_INVALID = 4,  /**< 鉴权失效 */
  SDK_RESULT_STATUS_INVALID_PARAM = 5, /**< 参数非法 */
  SDK_RESULT_STATUS_NOT_SUPPORTED = 6, /**< 不支持 */
  SDK_RESULT_STATUS_DEVICE_ERROR = 7,  /**< 设备内部异常 */
  SDK_RESULT_STATUS_PENDING = 8,       /**< 请求已受理，等待后续最终应答 */
} sdk_result_status_t;

typedef enum {
  SDK_RESULT_CODE_COMMON_NONE = 0,
  SDK_RESULT_CODE_COMMON_UNKNOWN = 1,
  SDK_RESULT_CODE_COMMON_PROTOCOL_UNSUPPORTED = 2,
  SDK_RESULT_CODE_PAIR_REJECTED = 1,
  SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE = 2,
  SDK_RESULT_CODE_PAIR_WAIT_CONFIRM = 3,
  SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT = 4,
  SDK_RESULT_CODE_AUTH_INVALID_TOKEN = 1,
  SDK_RESULT_CODE_AUTH_REJECTED_COMMAND = 2,
  SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID = 1,
  SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE = 2,
  SDK_RESULT_CODE_DASHBOARD_START_FAILED = 1,
  SDK_RESULT_CODE_DASHBOARD_STOP_FAILED = 2,
  SDK_RESULT_CODE_DASHBOARD_CONTROL_FAILED = 3,
} sdk_result_code_t;

#define SDK_RESULT_HEADER_SIZE 2u

/** 统一应答载荷：status/code/data，data 始终是该 CMD 固定实体或空。 */
typedef struct {
  uint8_t status;      /**< sdk_result_status_t */
  uint8_t code;        /**< SDK_RESULT_CODE_* 分组中的业务 code */
  const uint8_t *data; /**< 指向 payload 内 data 区域，不复制 */
  uint16_t data_len;   /**< data 长度 */
} sdk_result_t;

SDK_PACKED_BEGIN
/** 配对成功 data，固定 Token。 */
typedef struct SDK_PACKED {
  uint8_t token[SDK_TOKEN_LEN];
} pair_token_t;
SDK_PACKED_END

SDK_PACKED_BEGIN
/** 设备鉴权请求载荷 */
typedef struct SDK_PACKED {
  uint8_t token[SDK_TOKEN_LEN]; /**< 上次配对保存的 Token */
} auth_request_t;
SDK_PACKED_END

/** 解码后的数据包（面向业务层） */
typedef struct {
  uint8_t cmd;                      /**< 完整命令字（0-255，不复用 bit7） */
  uint8_t seq;                      /**< 序列号 */
  uint16_t payload_len;             /**< 载荷长度 */
  uint8_t payload[SDK_MAX_PAYLOAD]; /**< 载荷数据 */
} sdk_packet_t;

typedef enum {
  SDK_DECODE_NEED_MORE = 0,
  SDK_DECODE_PACKET = 1,
  SDK_DECODE_ERROR = -1,
} sdk_decode_kind_t;

typedef struct {
  sdk_decode_kind_t kind;
  uint16_t consumed;
  sdk_packet_t packet;
} sdk_decode_result_t;

/** 设备上下文（每个物理连接对应一个实例） */
typedef struct sdk_device sdk_device_t;

/* ============================================================
 * API 接口
 * ============================================================ */

/**
 * @brief 创建设备上下文
 * @param mac   设备/上位机 MAC 地址（6 字节），仅用于标识和日志
 * @param name  设备名称（调试用），可为 NULL
 * @return      设备上下文指针，失败返回 NULL
 *
 * 说明：每建立一个物理连接（如蓝牙连接、串口连接），都应创建一个实例。
 *       该实例内部维护独立的流式解析状态机，防止多连接数据串扰。
 */
sdk_device_t *sdk_device_create(const uint8_t mac[6], const char *name);

/**
 * @brief 销毁设备上下文
 * @param dev 由 sdk_device_create 创建的指针
 */
void sdk_device_destroy(sdk_device_t *dev);

/**
 * @brief 获取设备 MAC（调试用）
 * @param dev 设备上下文
 * @return  指向内部 MAC 数组的指针，dev 为 NULL 时返回 NULL
 */
const uint8_t *sdk_device_get_mac(const sdk_device_t *dev);

/**
 * @brief 重置设备上下文内的流式解码状态。
 * @param dev 设备上下文，可为 NULL
 *
 * 说明：物理连接断开、传输层会话重建，或调用方判定半帧已超时时，
 *       可调用本函数丢弃当前未完成帧，避免旧半帧吞掉后续命令。
 */
void sdk_device_reset_parser(sdk_device_t *dev);

/**
 * @brief 编码：将数据包组帧为原始字节流
 * @param dev         设备上下文（用于统计）
 * @param pkt         待编码的数据包
 * @param buf         输出缓冲区
 * @param buf_size    输出缓冲区大小
 * @return            组帧后的总长度；0 表示错误（载荷超长或缓冲区不足）
 */
uint16_t sdk_encode(sdk_device_t *dev, const sdk_packet_t *pkt, uint8_t *buf,
                    uint16_t buf_size);

/**
 * @brief 流式解码：从原始字节流中解析出一帧数据
 * @param dev       设备上下文（内部维护解析状态）
 * @param data      输入数据缓冲区
 * @param len       输入数据长度
 * @return 解码结果对象：packet / need-more / error
 *
 * 说明：
 *   本函数为流式状态机，支持粘包、拆包、错包场景。
 *   多连接场景下，每个连接必须使用独立的 dev 实例。
 *   调用者通常在主循环或任务中循环调用本函数，直到返回 0。
 */
sdk_decode_result_t sdk_decode(sdk_device_t *dev, const uint8_t *data,
                               uint16_t len);

/**
 * @brief 打包统一应答 payload。
 * @param status 通用状态码。
 * @param code 当前 CMD 的业务 code。
 * @param data 当前 CMD 固定 data 实体或 NULL。
 * @param data_len data 长度。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区长度。
 * @return 写入 payload 长度；0 表示参数或缓冲区错误。
 */
uint16_t sdk_result_pack(uint8_t status, uint8_t code, const uint8_t *data,
                         uint16_t data_len, uint8_t *buf, uint16_t buf_size);

/**
 * @brief 解包统一应答 payload。
 * @param data payload 字节。
 * @param len payload 长度。
 * @param out_result 输出结果。
 * @return 成功返回 true。
 *
 * 注意：out_result->data 指向输入 data 内部，调用方需保证 data 生命周期。
 */
bool sdk_result_unpack(const uint8_t *data, uint16_t len,
                       sdk_result_t *out_result);

/**
 * @brief 打包配对请求 payload。
 * @param client_id 上位机 ID，wire 固定 u64 little-endian。
 * @param name 上位机显示名称，UTF-8 最多 SDK_PAIR_NAME_MAX 字节。
 * @param buf 输出缓冲区。
 * @param buf_size 输出缓冲区长度。
 * @return 写入 payload 长度；0 表示参数或缓冲区错误。
 *
 * 注意：name 会按字节长度截断；C 端不校验 UTF-8 字符边界。
 */
uint16_t pair_request_pack(uint64_t client_id, const char *name,
                           uint8_t *buf, uint16_t buf_size);

/**
 * @brief 解包配对请求 payload。
 * @param data payload 字节。
 * @param len payload 长度。
 * @param out_request 输出结果。
 * @return 成功返回 true。
 *
 * 注意：out_request->name 指向输入 data 内部，不保证 NUL 结尾。
 */
bool pair_request_unpack(const uint8_t *data, uint16_t len,
                         pair_request_t *out_request);

/**
 * @brief 计算指定传输单次写入大小下可承载的业务 payload 字节数。
 * @param transport_payload_size BLE/UART 单次传输可写入字节数。
 * @return 扣除 SDK 帧头和 CRC 后的 payload 容量。
 */
uint16_t sdk_get_payload_capacity(uint16_t transport_payload_size);

/**
 * @brief CRC8/ATM 计算（多项式 0x07，初始值 0x00）
 * @param data  数据指针
 * @param len   数据长度
 * @return      8 位 CRC 值
 */
uint8_t sdk_crc8(const uint8_t *data, uint16_t len);

/* ============================================================
 * 广播数据构建 API (Advertising Data Builder)
 * ============================================================ */

/**
 * @brief 构建 BLE Flags AD Structure。
 * @param flags BLE flags。
 * @param buf 输出缓冲区。
 * @param max_len 输出缓冲区长度。
 * @return 写入长度；0 表示缓冲区不足。
 */
uint16_t sdk_adv_build_flags(uint8_t flags, uint8_t *buf, uint16_t max_len);

/**
 * @brief 构建 Complete Local Name AD Structure。
 * @param name 设备广播名称。
 * @param buf 输出缓冲区。
 * @param max_len 输出缓冲区长度。
 * @return 写入长度；0 表示缓冲区不足。
 *
 * 注意：名称按字节截断到 SDK_ADV_NAME_MAX，C 端不校验 UTF-8 字符边界。
 */
uint16_t sdk_adv_build_name(const char *name, uint8_t *buf, uint16_t max_len);

/**
 * @brief 构建 Appearance AD Structure。
 * @param appearance BLE Appearance 值。
 * @param buf 输出缓冲区。
 * @param max_len 输出缓冲区长度。
 * @return 写入长度；0 表示缓冲区不足。
 */
uint16_t sdk_adv_build_appearance(uint16_t appearance, uint8_t *buf,
                                  uint16_t max_len);

/**
 * @brief 构建 128-bit Service UUID 不完整列表 AD Structure。
 * @param uuid 16 字节 UUID，小端序广播格式。
 * @param buf 输出缓冲区。
 * @param max_len 输出缓冲区长度。
 * @return 写入长度；0 表示缓冲区不足。
 */
uint16_t sdk_adv_build_service_uuid128_incomplete(const uint8_t uuid[16],
                                                  uint8_t *buf,
                                                  uint16_t max_len);

/**
 * @brief 构建 128-bit Service UUID 完整列表 AD Structure。
 * @param uuid 16 字节 UUID，小端序广播格式。
 * @param buf 输出缓冲区。
 * @param max_len 输出缓冲区长度。
 * @return 写入长度；0 表示缓冲区不足。
 */
uint16_t sdk_adv_build_service_uuid128_complete(const uint8_t uuid[16],
                                                uint8_t *buf, uint16_t max_len);

/**
 * @brief 统一构建广播包 (ADV) 与扫描响应包 (SCAN_RSP)
 * @param mac          设备 MAC (可选，保留给特殊业务)
 * @param name         设备名称
 * @param adv_buf      输出广播包的缓冲区 (通常 31 字节)
 * @param adv_max      广播包缓冲区最大长度
 * @param out_adv_len  实际写入的广播包长度
 * @param rsp_buf      输出扫描响应的缓冲区 (通常 31 字节)
 * @param rsp_max      扫描响应缓冲区最大长度
 * @param out_rsp_len  实际写入的扫描响应长度
 * @return true 成功, false 缓冲区不足
 *
 * 注意：BLE 广播和扫描响应各自通常限制 31 字节，修改字段时需重新核算长度。
 */
bool sdk_build_adv_data(const uint8_t mac[6], const char *name,
                        uint8_t *adv_buf, uint16_t adv_max,
                        uint16_t *out_adv_len, uint8_t *rsp_buf,
                        uint16_t rsp_max, uint16_t *out_rsp_len);

#ifdef __cplusplus
}
#endif

#endif /* SDK_CODEC_H */
