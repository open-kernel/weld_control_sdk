#include "dashboard.h"

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

uint8_t dashboard_pack_status_flags(uint8_t machine_status,
                                    uint8_t charge_mode_code) {
    return (uint8_t)(((charge_mode_code & DASHBOARD_CHARGE_MODE_MASK)
                      << DASHBOARD_CHARGE_MODE_SHIFT) |
                     (machine_status & DASHBOARD_MACHINE_STATUS_MASK));
}

uint8_t dashboard_get_machine_status(uint8_t status_flags) {
    return status_flags & DASHBOARD_MACHINE_STATUS_MASK;
}

uint8_t dashboard_get_charge_mode_code(uint8_t status_flags) {
    return (status_flags >> DASHBOARD_CHARGE_MODE_SHIFT) & DASHBOARD_CHARGE_MODE_MASK;
}

uint8_t dashboard_pack_flags(uint8_t discharge_status,
                             uint8_t undefined_status) {
    return (uint8_t)(((undefined_status & DASHBOARD_UNDEFINED_STATUS_MASK)
                      << DASHBOARD_UNDEFINED_STATUS_SHIFT) |
                     (discharge_status & DASHBOARD_DISCHARGE_STATUS_MASK));
}

uint8_t dashboard_get_discharge_status(uint8_t flags) {
    return flags & DASHBOARD_DISCHARGE_STATUS_MASK;
}

uint8_t dashboard_get_undefined_status(uint8_t flags) {
    return (flags >> DASHBOARD_UNDEFINED_STATUS_SHIFT) & DASHBOARD_UNDEFINED_STATUS_MASK;
}

bool dashboard_init_pack(const dashboard_init_t *fields, uint8_t *buf,
                         uint16_t buf_size) {
    if (!fields || !buf || buf_size < DASHBOARD_INIT_PAYLOAD_SIZE) return false;
    if (DASHBOARD_INIT_PAYLOAD_SIZE >= DASHBOARD_INIT_PAYLOAD_SIZE_V1) {
        write_u16_le(buf, 0, fields->setting_voltage_max_mv);
        write_u32_le(buf, 2, fields->stat_welds_total);
        write_u32_le(buf, 6, fields->stat_welds_task);
        write_u16_le(buf, 10, fields->setting_weld_pre_ms10);
        write_u16_le(buf, 12, fields->setting_weld_cooling_ms10);
        write_u16_le(buf, 14, fields->setting_weld_main_ms10);
        write_u16_le(buf, 16, fields->protocol_version);
        buf[18] = fields->firmware_major;
        buf[19] = fields->firmware_minor;
        buf[20] = fields->firmware_patch;
        write_u32_le(buf, 21, fields->firmware_build_id);
        write_u16_le(buf, 25, fields->protocol_min_version);
        write_u16_le(buf, 27, fields->setting_single_cap_voltage_mv);
        buf[29] = fields->setting_trigger_mode;
        write_u16_le(buf, 30, fields->setting_auto_delay_ms);
    }
    return true;
}

bool dashboard_init_unpack(const uint8_t *data, uint16_t len,
                           dashboard_init_t *out_fields) {
    if (!data || !out_fields || len < DASHBOARD_INIT_PAYLOAD_SIZE_V1) return false;
    if (len >= DASHBOARD_INIT_PAYLOAD_SIZE_V1) {
        out_fields->setting_voltage_max_mv = read_u16_le(data, 0);
        out_fields->stat_welds_total = read_u32_le(data, 2);
        out_fields->stat_welds_task = read_u32_le(data, 6);
        out_fields->setting_weld_pre_ms10 = read_u16_le(data, 10);
        out_fields->setting_weld_cooling_ms10 = read_u16_le(data, 12);
        out_fields->setting_weld_main_ms10 = read_u16_le(data, 14);
        out_fields->protocol_version = read_u16_le(data, 16);
        out_fields->firmware_major = data[18];
        out_fields->firmware_minor = data[19];
        out_fields->firmware_patch = data[20];
        out_fields->firmware_build_id = read_u32_le(data, 21);
        out_fields->protocol_min_version = read_u16_le(data, 25);
        out_fields->setting_single_cap_voltage_mv = read_u16_le(data, 27);
        out_fields->setting_trigger_mode = data[29];
        out_fields->setting_auto_delay_ms = read_u16_le(data, 30);
    }
    return true;
}

bool dashboard_init_request_pack(const dashboard_init_request_t *fields,
                                 uint8_t *buf, uint16_t buf_size) {
    if (!fields || !buf || buf_size < DASHBOARD_INIT_REQUEST_PAYLOAD_SIZE) return false;
    write_u16_le(buf, 0, fields->app_protocol_version);
    write_u16_le(buf, 2, fields->app_min_protocol_version);
    buf[4] = fields->app_version_major;
    buf[5] = fields->app_version_minor;
    buf[6] = fields->app_version_patch;
    write_u32_le(buf, 7, fields->app_build_id);
    return true;
}

bool dashboard_init_request_unpack(const uint8_t *data, uint16_t len,
                                   dashboard_init_request_t *out_fields) {
    if (!data || !out_fields || len < DASHBOARD_INIT_REQUEST_PAYLOAD_SIZE) return false;
    out_fields->app_protocol_version = read_u16_le(data, 0);
    out_fields->app_min_protocol_version = read_u16_le(data, 2);
    out_fields->app_version_major = data[4];
    out_fields->app_version_minor = data[5];
    out_fields->app_version_patch = data[6];
    out_fields->app_build_id = read_u32_le(data, 7);
    return true;
}

bool dashboard_compact_pack(const dashboard_compact_t *fields, uint8_t *buf,
                            uint16_t buf_size) {
    if (!fields || !buf || buf_size < DASHBOARD_COMPACT_PAYLOAD_SIZE) return false;
    write_u16_le(buf, 0, fields->voltage_mv);
    write_u16_le(buf, 2, fields->weld_current_a);
    write_u16_le(buf, 4, fields->charge_current_ma);
    write_u16_le(buf, 6, fields->voltage_cap_1_mv);
    write_u16_le(buf, 8, fields->voltage_cap_2_mv);
    buf[10] = (uint8_t)fields->temperature_capacitor_c;
    buf[11] = (uint8_t)fields->temperature_mos_c;
    buf[12] = dashboard_pack_status_flags(fields->machine_status,
                                          fields->charge_mode_code);
    buf[13] = dashboard_pack_flags(fields->discharge_status,
                                   fields->undefined_status);
    return true;
}

bool dashboard_compact_unpack(const uint8_t *data, uint16_t len,
                              dashboard_compact_t *out_fields) {
    if (!data || !out_fields || len < DASHBOARD_COMPACT_PAYLOAD_SIZE) return false;
    out_fields->voltage_mv = read_u16_le(data, 0);
    out_fields->weld_current_a = read_u16_le(data, 2);
    out_fields->charge_current_ma = read_u16_le(data, 4);
    out_fields->voltage_cap_1_mv = read_u16_le(data, 6);
    out_fields->voltage_cap_2_mv = read_u16_le(data, 8);
    out_fields->temperature_capacitor_c = (int8_t)data[10];
    out_fields->temperature_mos_c = (int8_t)data[11];
    out_fields->machine_status = dashboard_get_machine_status(data[12]);
    out_fields->charge_mode_code = dashboard_get_charge_mode_code(data[12]);
    out_fields->discharge_status = dashboard_get_discharge_status(data[13]);
    out_fields->undefined_status = dashboard_get_undefined_status(data[13]);
    return true;
}

bool dashboard_weld_records_from_array(const dashboard_weld_record_t *records,
                                       uint8_t size, uint8_t *buf,
                                       uint16_t buf_size, uint16_t *out_len) {
    if (!buf || !out_len) return false;
    if (size > 0 && !records) return false;
    if (size > DASHBOARD_WELD_RECORD_MAX_ITEMS) size = DASHBOARD_WELD_RECORD_MAX_ITEMS;
    uint16_t needed = (uint16_t)(1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE * size);
    if (buf_size < needed) return false;

    buf[0] = size;
    for (uint8_t i = 0; i < size; i++) {
        uint16_t offset = (uint16_t)(1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE * i);
        write_u16_le(buf, offset, records[i].id);
        write_u16_le(buf, offset + 2, records[i].peak_current_a);
        write_u16_le(buf, offset + 4, records[i].post_voltage_mv);
    }
    *out_len = needed;
    return true;
}

bool dashboard_weld_records_peek_size(const uint8_t *data, uint16_t len,
                                      uint8_t *out_size) {
    if (!data || !out_size || len < 1) return false;
    uint8_t size = data[0];
    uint16_t needed = (uint16_t)(1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE * size);
    if (len < needed) return false;
    *out_size = size;
    return true;
}

bool dashboard_weld_records_unpack_item(const uint8_t *data, uint16_t len,
                                        uint8_t index,
                                        dashboard_weld_record_t *out_record) {
    if (!data || !out_record || len < 1) return false;
    uint8_t size = data[0];
    if (index >= size) return false;
    uint16_t needed = (uint16_t)(1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE * size);
    if (len < needed) return false;

    uint16_t offset = (uint16_t)(1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE * index);
    out_record->id = read_u16_le(data, offset);
    out_record->peak_current_a = read_u16_le(data, offset + 2);
    out_record->post_voltage_mv = read_u16_le(data, offset + 4);
    return true;
}

bool dashboard_logs_from_array(const dashboard_log_t *logs, uint8_t size,
                               uint8_t *buf, uint16_t buf_size,
                               uint16_t *out_len) {
    if (!buf || !out_len) return false;
    if (size > 0 && !logs) return false;
    if (size > DASHBOARD_LOG_MAX_ITEMS) size = DASHBOARD_LOG_MAX_ITEMS;
    uint16_t needed = (uint16_t)(1 + DASHBOARD_LOG_PAYLOAD_SIZE * size);
    if (buf_size < needed) return false;

    buf[0] = size;
    for (uint8_t i = 0; i < size; i++) {
        uint16_t offset = (uint16_t)(1 + DASHBOARD_LOG_PAYLOAD_SIZE * i);
        write_u16_le(buf, offset, logs[i].id);
        buf[offset + 2] = logs[i].level;
        write_u32_le(buf, offset + 3, logs[i].time_sec);
        write_u16_le(buf, offset + 7, logs[i].code);
    }
    *out_len = needed;
    return true;
}

bool dashboard_logs_peek_size(const uint8_t *data, uint16_t len,
                              uint8_t *out_size) {
    if (!data || !out_size || len < 1) return false;
    uint8_t size = data[0];
    uint16_t needed = (uint16_t)(1 + DASHBOARD_LOG_PAYLOAD_SIZE * size);
    if (len < needed) return false;
    *out_size = size;
    return true;
}

bool dashboard_logs_unpack_item(const uint8_t *data, uint16_t len,
                                uint8_t index, dashboard_log_t *out_log) {
    if (!data || !out_log || len < 1) return false;
    uint8_t size = data[0];
    if (index >= size) return false;
    uint16_t needed = (uint16_t)(1 + DASHBOARD_LOG_PAYLOAD_SIZE * size);
    if (len < needed) return false;

    uint16_t offset = (uint16_t)(1 + DASHBOARD_LOG_PAYLOAD_SIZE * index);
    out_log->id = read_u16_le(data, offset);
    out_log->level = data[offset + 2];
    out_log->time_sec = read_u32_le(data, offset + 3);
    out_log->code = read_u16_le(data, offset + 7);
    return true;
}
