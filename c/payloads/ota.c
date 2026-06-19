#include "ota.h"

static void write_u16_le(uint8_t *buf, uint16_t offset, uint16_t value) {
    buf[offset] = (uint8_t)(value & 0xFF);
    buf[offset + 1] = (uint8_t)(value >> 8);
}

static uint16_t read_u16_le(const uint8_t *buf, uint16_t offset) {
    return (uint16_t)buf[offset] | ((uint16_t)buf[offset + 1] << 8);
}

static void write_u32_le(uint8_t *buf, uint16_t offset, uint32_t value) {
    buf[offset] = (uint8_t)(value & 0xFF);
    buf[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    buf[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    buf[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t read_u32_le(const uint8_t *buf, uint16_t offset) {
    return (uint32_t)buf[offset] |
           ((uint32_t)buf[offset + 1] << 8) |
           ((uint32_t)buf[offset + 2] << 16) |
           ((uint32_t)buf[offset + 3] << 24);
}

bool ota_start_pack(const ota_start_t *fields, uint8_t *buf, uint16_t buf_size) {
    if (!fields || !buf || buf_size < OTA_START_PAYLOAD_SIZE) return false;
    write_u16_le(buf, 0, fields->company_id);
    buf[2] = fields->fw_type;
    buf[3] = fields->fw_ver[0];
    buf[4] = fields->fw_ver[1];
    buf[5] = fields->fw_ver[2];
    write_u32_le(buf, 6, fields->fw_size);
    write_u16_le(buf, 10, fields->fw_crc16);
    write_u16_le(buf, 12, fields->chunk_size);
    return true;
}

bool ota_start_unpack(const uint8_t *data, uint16_t len, ota_start_t *out_fields) {
    if (!data || !out_fields || len < OTA_START_PAYLOAD_SIZE) return false;
    out_fields->company_id = read_u16_le(data, 0);
    out_fields->fw_type = data[2];
    out_fields->fw_ver[0] = data[3];
    out_fields->fw_ver[1] = data[4];
    out_fields->fw_ver[2] = data[5];
    out_fields->fw_size = read_u32_le(data, 6);
    out_fields->fw_crc16 = read_u16_le(data, 10);
    out_fields->chunk_size = read_u16_le(data, 12);
    return true;
}

bool ota_data_hdr_pack(const ota_data_hdr_t *fields, uint8_t *buf, uint16_t buf_size) {
    if (!fields || !buf || buf_size < OTA_DATA_HDR_PAYLOAD_SIZE) return false;
    write_u16_le(buf, 0, fields->chunk_index);
    write_u16_le(buf, 2, fields->data_len);
    return true;
}

bool ota_data_hdr_unpack(const uint8_t *data, uint16_t len, ota_data_hdr_t *out_fields) {
    if (!data || !out_fields || len < OTA_DATA_HDR_PAYLOAD_SIZE) return false;
    out_fields->chunk_index = read_u16_le(data, 0);
    out_fields->data_len = read_u16_le(data, 2);
    return true;
}

bool ota_verify_pack(const ota_verify_t *fields, uint8_t *buf, uint16_t buf_size) {
    if (!fields || !buf || buf_size < OTA_VERIFY_PAYLOAD_SIZE) return false;
    write_u32_le(buf, 0, fields->fw_size);
    write_u16_le(buf, 4, fields->fw_crc16);
    return true;
}

bool ota_verify_unpack(const uint8_t *data, uint16_t len, ota_verify_t *out_fields) {
    if (!data || !out_fields || len < OTA_VERIFY_PAYLOAD_SIZE) return false;
    out_fields->fw_size = read_u32_le(data, 0);
    out_fields->fw_crc16 = read_u16_le(data, 4);
    return true;
}

bool ota_ack_data_pack(const ota_ack_data_t *fields, uint8_t *buf, uint16_t buf_size) {
    if (!fields || !buf || buf_size < OTA_ACK_DATA_PAYLOAD_SIZE) return false;
    write_u16_le(buf, 0, fields->next_chunk);
    return true;
}

bool ota_ack_data_unpack(const uint8_t *data, uint16_t len, ota_ack_data_t *out_fields) {
    if (!data || !out_fields || len < OTA_ACK_DATA_PAYLOAD_SIZE) return false;
    out_fields->next_chunk = read_u16_le(data, 0);
    return true;
}
