#include "settings.h"

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

static void write_fixed_string(uint8_t *buf, uint16_t offset, const char *value,
                               uint16_t fixed_len) {
    memset(buf + offset, 0, fixed_len);
    if (!value) return;
    size_t len = strnlen(value, fixed_len);
    memcpy(buf + offset, value, len);
}

static void read_fixed_string(const uint8_t *data, uint16_t offset,
                              uint16_t fixed_len, char *out) {
    memcpy(out, data + offset, fixed_len);
    out[fixed_len] = '\0';
}

static void settings_fault_log_pack_into(const settings_fault_log_t *fields,
                                         uint8_t *buf, uint16_t offset) {
    write_u16_le(buf, offset, fields->id);
    buf[offset + 2] = fields->type;
    write_u32_le(buf, offset + 3, fields->time_sec);
    write_fixed_string(buf, offset + 7, fields->title, SETTINGS_FAULT_TITLE_LEN);
    write_fixed_string(buf, offset + 39, fields->message, SETTINGS_FAULT_MESSAGE_LEN);
}

static void settings_fault_log_unpack_from(const uint8_t *data, uint16_t offset,
                                           settings_fault_log_t *out) {
    out->id = read_u16_le(data, offset);
    out->type = data[offset + 2];
    out->time_sec = read_u32_le(data, offset + 3);
    read_fixed_string(data, offset + 7, SETTINGS_FAULT_TITLE_LEN, out->title);
    read_fixed_string(data, offset + 39, SETTINGS_FAULT_MESSAGE_LEN, out->message);
}

static void settings_runtime_profile_pack_into(const settings_runtime_profile_t *fields,
                                               uint8_t *buf, uint16_t offset) {
    write_u16_le(buf, offset, fields->target_voltage_mv);
    write_u16_le(buf, offset + 2, fields->target_current_a10);
    write_u16_le(buf, offset + 4, fields->preheat_pulse_ms10);
    write_u16_le(buf, offset + 6, fields->cool_time_ms10);
    write_u16_le(buf, offset + 8, fields->main_pulse_ms10);
    buf[offset + 10] = fields->trigger_mode;
    write_u16_le(buf, offset + 11, fields->auto_delay_ms);
}

static void settings_runtime_profile_unpack_from(const uint8_t *data, uint16_t offset,
                                                 settings_runtime_profile_t *out) {
    out->target_voltage_mv = read_u16_le(data, offset);
    out->target_current_a10 = read_u16_le(data, offset + 2);
    out->preheat_pulse_ms10 = read_u16_le(data, offset + 4);
    out->cool_time_ms10 = read_u16_le(data, offset + 6);
    out->main_pulse_ms10 = read_u16_le(data, offset + 8);
    out->trigger_mode = data[offset + 10];
    out->auto_delay_ms = read_u16_le(data, offset + 11);
}

static void settings_limits_max_pack_into(const settings_limits_max_t *fields,
                                          uint8_t *buf, uint16_t offset) {
    write_u16_le(buf, offset, fields->target_voltage_mv_max);
    write_u16_le(buf, offset + 2, fields->target_current_a10_max);
    write_u16_le(buf, offset + 4, fields->preheat_pulse_ms10_max);
    write_u16_le(buf, offset + 6, fields->cool_time_ms10_max);
    write_u16_le(buf, offset + 8, fields->main_pulse_ms10_max);
    write_u16_le(buf, offset + 10, fields->auto_delay_ms_max);
}

static void settings_limits_max_unpack_from(const uint8_t *data, uint16_t offset,
                                            settings_limits_max_t *out) {
    out->target_voltage_mv_max = read_u16_le(data, offset);
    out->target_current_a10_max = read_u16_le(data, offset + 2);
    out->preheat_pulse_ms10_max = read_u16_le(data, offset + 4);
    out->cool_time_ms10_max = read_u16_le(data, offset + 6);
    out->main_pulse_ms10_max = read_u16_le(data, offset + 8);
    out->auto_delay_ms_max = read_u16_le(data, offset + 10);
}

bool settings_current_pack(const settings_current_t *fields, uint8_t *buf,
                           uint16_t buf_size, uint16_t *out_len) {
    if (!fields || !buf || !out_len || buf_size < SETTINGS_CURRENT_PAYLOAD_SIZE) return false;
    buf[0] = fields->profile_id;
    settings_runtime_profile_pack_into(&fields->current_profile, buf, 1);
    settings_limits_max_pack_into(&fields->limits_max, buf, 1 + SETTINGS_RUNTIME_PROFILE_PAYLOAD_SIZE);
    *out_len = SETTINGS_CURRENT_PAYLOAD_SIZE;
    return true;
}

bool settings_current_unpack(const uint8_t *data, uint16_t len,
                             settings_current_t *out_fields) {
    if (!data || !out_fields || len < SETTINGS_CURRENT_PAYLOAD_SIZE) return false;
    out_fields->profile_id = data[0];
    settings_runtime_profile_unpack_from(data, 1, &out_fields->current_profile);
    settings_limits_max_unpack_from(data, 1 + SETTINGS_RUNTIME_PROFILE_PAYLOAD_SIZE, &out_fields->limits_max);
    return true;
}

bool settings_self_check_pack(const settings_self_check_t *fields, uint8_t *buf,
                              uint16_t buf_size, uint16_t *out_len) {
    if (!fields || !buf || !out_len) return false;
    if (fields->esr_size > 0 && !fields->esrs_mohm10) return false;
    if (fields->fault_log_size > 0 && !fields->fault_logs) return false;
    uint16_t base_size = (uint16_t)(1u + 2u * fields->esr_size + 1u + 1u);
    uint16_t needed = (uint16_t)(base_size + SETTINGS_FAULT_LOG_PAYLOAD_SIZE * fields->fault_log_size);
    if (needed > SETTINGS_SELF_CHECK_MAX_PAYLOAD_SIZE) return false;
    if (buf_size < needed) return false;
    uint16_t offset = 0;
    buf[offset++] = fields->esr_size;
    for (uint8_t i = 0; i < fields->esr_size; i++) {
        write_u16_le(buf, offset, fields->esrs_mohm10[i]);
        offset += 2;
    }
    buf[offset++] = fields->esr_quality;
    buf[offset++] = fields->fault_log_size;
    for (uint8_t i = 0; i < fields->fault_log_size; i++) {
        settings_fault_log_pack_into(&fields->fault_logs[i], buf, offset);
        offset += SETTINGS_FAULT_LOG_PAYLOAD_SIZE;
    }
    *out_len = needed;
    return true;
}

bool settings_self_check_unpack(const uint8_t *data, uint16_t len,
                                settings_self_check_t *out_fields,
                                uint16_t *esrs_mohm10,
                                uint8_t esr_capacity,
                                settings_fault_log_t *fault_logs,
                                uint8_t fault_log_capacity) {
    if (!data || !out_fields || len < 3) return false;
    uint16_t offset = 0;
    uint8_t esr_size = data[offset++];
    if (esr_size > esr_capacity || (esr_size > 0 && !esrs_mohm10)) return false;
    if (len < offset + 2 * esr_size + 2) return false;
    for (uint8_t i = 0; i < esr_size; i++) {
        esrs_mohm10[i] = read_u16_le(data, offset);
        offset += 2;
    }
    uint8_t esr_quality = data[offset++];
    uint8_t fault_log_size = data[offset++];
    if (fault_log_size > fault_log_capacity || (fault_log_size > 0 && !fault_logs)) return false;
    if (len < offset + SETTINGS_FAULT_LOG_PAYLOAD_SIZE * fault_log_size) return false;
    for (uint8_t i = 0; i < fault_log_size; i++) {
        settings_fault_log_unpack_from(data, offset, &fault_logs[i]);
        offset += SETTINGS_FAULT_LOG_PAYLOAD_SIZE;
    }
    out_fields->esrs_mohm10 = esrs_mohm10;
    out_fields->esr_size = esr_size;
    out_fields->esr_quality = esr_quality;
    out_fields->fault_logs = fault_logs;
    out_fields->fault_log_size = fault_log_size;
    return true;
}

bool settings_apply_profile_pack(const settings_apply_profile_t *fields, uint8_t *buf,
                                 uint16_t buf_size, uint16_t *out_len) {
    if (!fields || !buf || !out_len || buf_size < SETTINGS_APPLY_PROFILE_PAYLOAD_SIZE) return false;
    buf[0] = fields->profile_id;
    settings_runtime_profile_pack_into(&fields->profile, buf, 1);
    *out_len = SETTINGS_APPLY_PROFILE_PAYLOAD_SIZE;
    return true;
}

bool settings_apply_profile_unpack(const uint8_t *data, uint16_t len,
                                   settings_apply_profile_t *out_fields) {
    if (!data || !out_fields || len < SETTINGS_APPLY_PROFILE_PAYLOAD_SIZE) return false;
    out_fields->profile_id = data[0];
    settings_runtime_profile_unpack_from(data, 1, &out_fields->profile);
    return true;
}

bool settings_reset_pack(const settings_reset_t *fields, uint8_t *buf,
                         uint16_t buf_size, uint16_t *out_len) {
    if (!fields || !buf || !out_len || buf_size < SETTINGS_RESET_PAYLOAD_SIZE) return false;
    buf[0] = fields->flags;
    *out_len = SETTINGS_RESET_PAYLOAD_SIZE;
    return true;
}

bool settings_reset_unpack(const uint8_t *data, uint16_t len,
                           settings_reset_t *out_fields) {
    if (!data || !out_fields || len < SETTINGS_RESET_PAYLOAD_SIZE) return false;
    out_fields->flags = data[0];
    return true;
}
