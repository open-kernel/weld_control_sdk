/**
 * @file sdk_codec.c
 * @brief BLE 数据层 SDK - 协议编解码器实现
 *
 * 特性：
 *   - 零动态内存分配（热路径无 malloc）
 *   - 流式状态机解析，自动处理粘包/拆包/错包
 *   - 每连接独立解析上下文，多设备不串扰
 */

#include "sdk_codec.h"
#include <string.h>

/* ============================================================
 * CRC8/ATM
 * ============================================================ */

uint8_t sdk_crc8(const uint8_t *data, uint16_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ============================================================
 * 设备上下文定义
 * ============================================================ */

/** 解析状态机状态 */
enum {
    ST_WAIT_SOF = 0,    /**< 等待帧头 */
    ST_LEN_LO,          /**< 接收载荷长度低字节 */
    ST_LEN_HI,          /**< 接收载荷长度高字节 */
    ST_CMD,             /**< 接收命令字 */
    ST_SEQ,             /**< 接收序列号 */
    ST_PAYLOAD,         /**< 接收载荷 */
    ST_CRC,             /**< 接收 CRC8 */
};

/** 设备上下文（每连接一个实例） */
struct sdk_device {
    uint8_t  mac[6];            /**< MAC 地址（调试用） */
    char     name[32];          /**< 设备名（调试用） */
    uint32_t rx_frames;         /**< 接收帧计数 */
    uint32_t rx_errors;         /**< 接收错误计数 */
    uint32_t tx_frames;         /**< 发送帧计数 */

    /** 流式解析状态机（每个连接独立） */
    struct {
        uint8_t  state;         /**< 当前解析状态 */
        uint16_t payload_len;   /**< 当前帧的载荷长度 */
        uint16_t idx;           /**< 内部缓冲区写入索引 */
        uint8_t  buf[SDK_HEAD_SIZE + SDK_MAX_PAYLOAD + SDK_CRC_SIZE];
    } parser;
};

/* ============================================================
 * 生命周期管理
 * ============================================================ */

sdk_device_t* sdk_device_create(const uint8_t mac[6], const char *name) {
    sdk_device_t *dev = (sdk_device_t *)SDK_MALLOC(sizeof(sdk_device_t));
    if (!dev) return NULL;

    memset(dev, 0, sizeof(sdk_device_t));

    if (mac) {
        memcpy(dev->mac, mac, 6);
    }
    if (name) {
        strncpy(dev->name, name, 31);
        dev->name[31] = '\0';
    }
    return dev;
}

void sdk_device_destroy(sdk_device_t *dev) {
    if (dev) {
        SDK_FREE(dev);
    }
}

const uint8_t* sdk_device_get_mac(const sdk_device_t *dev) {
    return dev ? dev->mac : NULL;
}

/* ============================================================
 * 解码器（流式状态机）
 * ============================================================ */

/** 重置解析状态机 */
static inline void parser_reset(sdk_device_t *dev) {
    if (!dev) return;
    dev->parser.state = ST_WAIT_SOF;
    dev->parser.idx = 0;
    dev->parser.payload_len = 0;
}

void sdk_device_reset_parser(sdk_device_t *dev) {
    parser_reset(dev);
}

/* ============================================================
 * 编码器
 * ============================================================ */

uint16_t sdk_encode(sdk_device_t *dev,
                     const sdk_packet_t *pkt,
                     uint8_t *buf,
                     uint16_t buf_size) {
    if (!pkt || !buf) return 0;
    if (pkt->payload_len > SDK_MAX_PAYLOAD) return 0;
    if (buf_size < SDK_HEAD_SIZE + pkt->payload_len + SDK_CRC_SIZE) return 0;

    uint16_t pos = 0;
    buf[pos++] = SDK_SOF;
    buf[pos++] = pkt->payload_len & 0xFF;
    buf[pos++] = (uint8_t)((pkt->payload_len >> 8) & 0xFF);
    buf[pos++] = pkt->cmd;
    buf[pos++] = pkt->seq;

    if (pkt->payload_len > 0) {
        memcpy(buf + pos, pkt->payload, pkt->payload_len);
        pos += pkt->payload_len;
    }

    uint8_t crc = sdk_crc8(buf, pos);
    buf[pos++] = crc;

    if (dev) dev->tx_frames++;
    return pos;
}

uint16_t sdk_result_pack(uint8_t status, uint8_t code, const uint8_t *data,
                         uint16_t data_len, uint8_t *buf, uint16_t buf_size) {
    if (!buf || buf_size < SDK_RESULT_HEADER_SIZE + data_len) return 0;
    if (data_len > 0 && !data) return 0;
    buf[0] = status;
    buf[1] = code;
    if (data_len > 0) {
        memcpy(buf + SDK_RESULT_HEADER_SIZE, data, data_len);
    }
    return SDK_RESULT_HEADER_SIZE + data_len;
}

bool sdk_result_unpack(const uint8_t *data, uint16_t len,
                       sdk_result_t *out_result) {
    if (!data || !out_result || len < SDK_RESULT_HEADER_SIZE) return false;
    out_result->status = data[0];
    out_result->code = data[1];
    out_result->data = data + SDK_RESULT_HEADER_SIZE;
    out_result->data_len = len - SDK_RESULT_HEADER_SIZE;
    return true;
}

uint16_t pair_request_pack(uint64_t client_id, const char *name,
                           uint8_t *buf, uint16_t buf_size) {
    if (!buf || buf_size < SDK_PAIR_REQUEST_HEADER_SIZE) return 0;
    uint16_t max_name_len = (uint16_t)(buf_size - SDK_PAIR_REQUEST_HEADER_SIZE);
    if (max_name_len > SDK_PAIR_NAME_MAX) max_name_len = SDK_PAIR_NAME_MAX;

    uint16_t name_len = 0;
    if (name) {
        while (name[name_len] != '\0' && name_len < max_name_len) {
            name_len++;
        }
    }

    for (uint8_t i = 0; i < SDK_PAIR_CLIENT_ID_SIZE; i++) {
        buf[i] = (uint8_t)((client_id >> (8u * i)) & 0xFFu);
    }
    buf[SDK_PAIR_CLIENT_ID_SIZE] = (uint8_t)name_len;
    if (name_len > 0) {
        memcpy(buf + SDK_PAIR_REQUEST_HEADER_SIZE, name, name_len);
    }
    return (uint16_t)(SDK_PAIR_REQUEST_HEADER_SIZE + name_len);
}

bool pair_request_unpack(const uint8_t *data, uint16_t len,
                         pair_request_t *out_request) {
    if (!data || !out_request || len < SDK_PAIR_REQUEST_HEADER_SIZE) return false;
    uint8_t name_len = data[SDK_PAIR_CLIENT_ID_SIZE];
    if (name_len > SDK_PAIR_NAME_MAX) return false;
    if (len < SDK_PAIR_REQUEST_HEADER_SIZE + name_len) return false;

    uint64_t client_id = 0;
    for (uint8_t i = 0; i < SDK_PAIR_CLIENT_ID_SIZE; i++) {
        client_id |= ((uint64_t)data[i]) << (8u * i);
    }
    out_request->client_id = client_id;
    out_request->name = data + SDK_PAIR_REQUEST_HEADER_SIZE;
    out_request->name_len = name_len;
    return true;
}

sdk_decode_result_t sdk_decode(sdk_device_t *dev,
                               const uint8_t *data,
                               uint16_t len) {
    sdk_decode_result_t result;
    memset(&result, 0, sizeof(result));
    result.kind = SDK_DECODE_NEED_MORE;
    if (!dev || !data) return result;

    for (uint16_t i = 0; i < len; i++) {  /* i 为 uint16_t，最大 65534，i+1 安全 */
        uint8_t b = data[i];

        switch (dev->parser.state) {
            case ST_WAIT_SOF:
                if (b == SDK_SOF) {
                    dev->parser.buf[0] = b;
                    dev->parser.idx = 1;
                    dev->parser.state = ST_LEN_LO;
                }
                break;

            case ST_LEN_LO:
                dev->parser.buf[dev->parser.idx++] = b;
                dev->parser.payload_len = b;
                dev->parser.state = ST_LEN_HI;
                break;

            case ST_LEN_HI:
                dev->parser.buf[dev->parser.idx++] = b;
                dev->parser.payload_len |= (uint16_t)b << 8;
                if (dev->parser.payload_len > SDK_MAX_PAYLOAD) {
                    dev->rx_errors++;
                    parser_reset(dev);
                    result.kind = SDK_DECODE_ERROR;
                    result.consumed = (uint16_t)(i + 1);
                    return result;
                }
                dev->parser.state = ST_CMD;
                break;

            case ST_CMD:
            case ST_SEQ:
                dev->parser.buf[dev->parser.idx++] = b;
                dev->parser.state++;
                if (dev->parser.state == ST_PAYLOAD && dev->parser.payload_len == 0) {
                    dev->parser.state = ST_CRC;
                }
                break;

            case ST_PAYLOAD:
                dev->parser.buf[dev->parser.idx++] = b;
                if (dev->parser.idx >= SDK_HEAD_SIZE + dev->parser.payload_len) {
                    dev->parser.state = ST_CRC;
                }
                break;

            case ST_CRC: {
                dev->parser.buf[dev->parser.idx++] = b;
                uint16_t frame_len = SDK_HEAD_SIZE + dev->parser.payload_len + SDK_CRC_SIZE;
                uint8_t calc_crc = sdk_crc8(dev->parser.buf, frame_len - SDK_CRC_SIZE);
                uint8_t recv_crc = dev->parser.buf[frame_len - 1];
                uint16_t consumed = (uint16_t)(i + 1);

                if (calc_crc != recv_crc) {
                    dev->rx_errors++;
                    parser_reset(dev);
                    result.kind = SDK_DECODE_ERROR;
                    result.consumed = consumed;
                    return result;
                }

                result.kind = SDK_DECODE_PACKET;
                result.consumed = consumed;
                result.packet.cmd = dev->parser.buf[3];
                result.packet.seq = dev->parser.buf[4];
                result.packet.payload_len = dev->parser.payload_len;
                if (result.packet.payload_len > 0) {
                    memcpy(result.packet.payload,
                           dev->parser.buf + SDK_HEAD_SIZE,
                           result.packet.payload_len);
                }

                parser_reset(dev);
                dev->rx_frames++;
                return result;
            }

            default:
                dev->rx_errors++;
                parser_reset(dev);
                result.kind = SDK_DECODE_ERROR;
                result.consumed = (uint16_t)(i + 1);
                return result;
        }
    }

    return result;
}

uint16_t sdk_get_payload_capacity(uint16_t transport_payload_size) {
    return transport_payload_size > SDK_FRAME_OVERHEAD
        ? (uint16_t)(transport_payload_size - SDK_FRAME_OVERHEAD)
        : 0;
}

/* ============================================================
 * 广播数据构建 API (Advertising Data Builder) 实现
 * ============================================================ */

uint16_t sdk_adv_build_flags(uint8_t flags, uint8_t *buf, uint16_t max_len) {
    if (max_len < 3) return 0;
    buf[0] = 2;         // Length
    buf[1] = 0x01;      // Type: Flags
    buf[2] = flags;
    return 3;
}

uint16_t sdk_adv_build_name(const char *name, uint8_t *buf, uint16_t max_len) {
    if (!name || max_len < 2) return 0;
    uint16_t name_len = 0;
    while (name[name_len] != '\0' && name_len < SDK_ADV_NAME_MAX) {
        name_len++;
    }
    if (max_len < name_len + 2) return 0;
    
    buf[0] = name_len + 1; // Length
    buf[1] = 0x09;         // Type: Complete Local Name
    for (uint16_t i = 0; i < name_len; i++) {
        buf[2 + i] = (uint8_t)name[i];
    }
    return name_len + 2;
}

uint16_t sdk_adv_build_appearance(uint16_t appearance, uint8_t *buf, uint16_t max_len) {
    if (max_len < 4) return 0;
    buf[0] = 3;         // Length
    buf[1] = 0x19;      // Type: Appearance
    buf[2] = appearance & 0xFF;         // LSB
    buf[3] = (appearance >> 8) & 0xFF;  // MSB
    return 4;
}

uint16_t sdk_adv_build_service_uuid128_incomplete(const uint8_t uuid[16], uint8_t *buf, uint16_t max_len) {
    if (max_len < 18) return 0;
    buf[0] = 17;        // Length
    buf[1] = 0x06;      // Type: Incomplete List of 128-bit Service Class UUIDs
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = uuid[i]; // 假设传入已经是小端序
    }
    return 18;
}

uint16_t sdk_adv_build_service_uuid128_complete(const uint8_t uuid[16], uint8_t *buf, uint16_t max_len) {
    if (max_len < 18) return 0;
    buf[0] = 17;        // Length
    buf[1] = 0x07;      // Type: Complete List of 128-bit Service Class UUIDs
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = uuid[i]; 
    }
    return 18;
}

bool sdk_build_adv_data(const uint8_t mac[6], const char *name, 
                        uint8_t *adv_buf, uint16_t adv_max, uint16_t *out_adv_len,
                        uint8_t *rsp_buf, uint16_t rsp_max, uint16_t *out_rsp_len) {
    (void)mac; /* mac 参数保留，以备未来将 MAC 混入厂商自定义字段 */
    uint16_t offset_adv = 0;
    uint16_t offset_rsp = 0;
    uint16_t written;

    // 1. 标志位 (Flags)
    written = sdk_adv_build_flags(0x06, adv_buf + offset_adv, adv_max - offset_adv);
    if (written == 0) return false;
    offset_adv += written;

    // 2. 设备名称 (Name)
    written = sdk_adv_build_name(name, adv_buf + offset_adv, adv_max - offset_adv);
    if (written == 0) return false;
    offset_adv += written;

    // 3. 外观 (Appearance) - 示例设为 0x14C0
    written = sdk_adv_build_appearance(0x14C0, adv_buf + offset_adv, adv_max - offset_adv);
    if (written == 0) return false;
    offset_adv += written;

    *out_adv_len = offset_adv;

    // ==== 构建 Scan Response ====
    const uint8_t svc_uuid[16] = SDK_SVC_UUID;
    written = sdk_adv_build_service_uuid128_incomplete(svc_uuid, rsp_buf + offset_rsp, rsp_max - offset_rsp);
    if (written == 0) return false;
    offset_rsp += written;

    *out_rsp_len = offset_rsp;

    return true;
}
