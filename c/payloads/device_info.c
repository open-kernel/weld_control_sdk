#include "device_info.h"

#include <string.h>

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

static uint8_t bounded_len(uint8_t len) {
    return len;
}

bool device_info_pack(const device_info_t *fields, uint8_t *buf,
                      uint16_t buf_size, uint16_t *out_len) {
    if (!fields || !buf || !out_len) return false;
    uint8_t manufacturer_len = bounded_len(fields->manufacturer_len);
    uint8_t model_len = bounded_len(fields->model_len);
    uint8_t serial_len = bounded_len(fields->serial_len);
    if ((manufacturer_len > 0 && !fields->manufacturer) ||
        (model_len > 0 && !fields->model) ||
        (serial_len > 0 && !fields->serial)) {
        return false;
    }
    uint16_t needed = (uint16_t)(DEVICE_INFO_FIXED_SIZE + manufacturer_len + model_len + serial_len);
    if (buf_size < needed) return false;

    if (DEVICE_INFO_FIXED_SIZE >= DEVICE_INFO_FIXED_SIZE_V1) {
        write_u16_le(buf, 0, fields->company_id);
        write_u16_le(buf, 2, fields->product_id);
        write_u32_le(buf, 4, fields->feature_mask);
        write_u16_le(buf, 8, fields->protocol_version);
        write_u16_le(buf, 10, fields->protocol_min_version);
        write_u16_le(buf, 12, fields->ota_max_kb);
        write_u16_le(buf, 14, fields->ota_chunk_max);
        buf[16] = fields->firmware_major;
        buf[17] = fields->firmware_minor;
        buf[18] = fields->firmware_patch;
        write_u32_le(buf, 19, fields->firmware_build_id);
        buf[23] = fields->hardware_major;
        buf[24] = fields->hardware_minor;
        buf[25] = fields->hardware_patch;
        buf[26] = manufacturer_len;
        buf[27] = model_len;
        buf[28] = serial_len;
    }

    uint16_t offset = DEVICE_INFO_FIXED_SIZE;
    if (manufacturer_len > 0) {
        memcpy(buf + offset, fields->manufacturer, manufacturer_len);
        offset += manufacturer_len;
    }
    if (model_len > 0) {
        memcpy(buf + offset, fields->model, model_len);
        offset += model_len;
    }
    if (serial_len > 0) {
        memcpy(buf + offset, fields->serial, serial_len);
    }
    *out_len = needed;
    return true;
}

bool device_info_unpack(const uint8_t *data, uint16_t len,
                        device_info_t *out_fields) {
    if (!data || !out_fields || len < DEVICE_INFO_FIXED_SIZE_V1) return false;
    uint8_t manufacturer_len = data[26];
    uint8_t model_len = data[27];
    uint8_t serial_len = data[28];
    uint16_t needed = (uint16_t)(DEVICE_INFO_FIXED_SIZE_V1 + manufacturer_len + model_len + serial_len);
    if (len < needed) return false;

    if (len >= DEVICE_INFO_FIXED_SIZE_V1) {
        out_fields->company_id = read_u16_le(data, 0);
        out_fields->product_id = read_u16_le(data, 2);
        out_fields->feature_mask = read_u32_le(data, 4);
        out_fields->protocol_version = read_u16_le(data, 8);
        out_fields->protocol_min_version = read_u16_le(data, 10);
        out_fields->ota_max_kb = read_u16_le(data, 12);
        out_fields->ota_chunk_max = read_u16_le(data, 14);
        out_fields->firmware_major = data[16];
        out_fields->firmware_minor = data[17];
        out_fields->firmware_patch = data[18];
        out_fields->firmware_build_id = read_u32_le(data, 19);
        out_fields->hardware_major = data[23];
        out_fields->hardware_minor = data[24];
        out_fields->hardware_patch = data[25];
        out_fields->manufacturer_len = manufacturer_len;
        out_fields->model_len = model_len;
        out_fields->serial_len = serial_len;
    }

    uint16_t offset = DEVICE_INFO_FIXED_SIZE_V1;
    out_fields->manufacturer = (const char *)(data + offset);
    offset += manufacturer_len;
    out_fields->model = (const char *)(data + offset);
    offset += model_len;
    out_fields->serial = (const char *)(data + offset);
    return true;
}
