#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "led_strip.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_module_at_test.h"
#include "ble_module_transport.h"
#include "esp_mac.h"
#include "feature_mask.h"
#include "payloads/dashboard.h"
#include "payloads/settings.h"
#include "sdk_codec.h"
#include "sdk_compat.h"
#include "sdk_frame_chunk.h"

#define WELD_DEVICE_NAME "Weld_Control"
#define WELD_NVS_NAMESPACE "weld_test"
#define WELD_NVS_TOKEN_KEY "tokens"
#define WELD_MAX_BONDED_TOKENS 8u

#define WELD_DEVICE_COMPANY_ID 0xAAAAu
#define WELD_DEVICE_PRODUCT_ID 0x0001u
#define WELD_FW_TYPE_BLE 0x01u
#define WELD_FIRMWARE_BUILD_ID 20260614u
#define WELD_HARDWARE_BUILD_ID 1u

#define WELD_NOTIFY_CHUNK_SIZE 20u
#define WELD_RESULT_PAYLOAD_MAX 768u
#define WELD_OTA_SESSION_TIMEOUT_US (15LL * 1000LL * 1000LL)
#define WELD_DECODE_PARTIAL_TIMEOUT_US (1000LL * 1000LL)
#define WELD_BOOT_BUTTON_GPIO GPIO_NUM_9
#define WELD_STATUS_LED_GPIO GPIO_NUM_48
#define WELD_STATUS_LED_COUNT 1u
#define WELD_LED_PAIR_VALUE 50u
#define WELD_BUTTON_POLL_MS 10u
#define WELD_BUTTON_DEBOUNCE_MS 20u
#define WELD_BUTTON_LONG_PRESS_MS 3000u
#define WELD_BUTTON_CLICK_MIN_MS 20u
#define WELD_BUTTON_CLICK_MAX_MS 1000u
#define WELD_BUTTON_MULTI_CLICK_TIMEOUT_MS 400u
#define WELD_PAIRING_TIMEOUT_MS 60000u
#define WELD_PAIR_CONFIRM_TIMEOUT_MS 60000u
#define WELD_PAIR_CONFIRM_BLINK_MS 200u
#define WELD_BUTTON_TASK_STACK 8192u
#define WELD_CONFIRM_TASK_STACK 8192u
#define WELD_MODULE2_CONN_HANDLE 1u
#define WELD_FAULT_LOG_BASE_TIME_SEC 1718000000u

static const char *TAG = "weld_device_test";

typedef enum {
  WELD_RUNTIME_NIMBLE = 0,
  WELD_RUNTIME_MODULE_AT_TEST = 1,
  WELD_RUNTIME_MODULE2_TRANSPORT = 2,
} weld_runtime_mode_t;

static const weld_runtime_mode_t s_runtime_mode =
    WELD_RUNTIME_MODULE2_TRANSPORT;

static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0xA5, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x00, 0x00,
                     0x5A, 0x4B, 0x01, 0x00, 0xCA, 0x9E, 0x00, 0x00);
static const ble_uuid128_t s_tx_uuid =
    BLE_UUID128_INIT(0xA5, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x00, 0x00,
                     0x5A, 0x4B, 0x02, 0x00, 0xCA, 0x9E, 0x00, 0x00);
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0xA5, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x00, 0x00,
                     0x5A, 0x4B, 0x03, 0x00, 0xCA, 0x9E, 0x00, 0x00);

typedef struct {
  bool active;
  uint32_t fw_size;
  uint16_t fw_crc16;
  uint16_t chunk_size;
  uint16_t next_chunk;
  uint32_t received_size;
  uint16_t crc16;
  int64_t updated_us;
} weld_ota_state_t;

static uint8_t s_own_addr_type;
static uint8_t s_own_addr[6];
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;
static uint16_t s_rx_val_handle;
static bool s_notify_enabled;
static bool s_authenticated;
static bool s_pairing_mode;
static uint8_t s_waiting_confirm_seq;

static sdk_device_t *s_sdk_dev;
static led_strip_handle_t s_led_strip;
static SemaphoreHandle_t s_tx_mutex;
static TaskHandle_t s_dashboard_task;
static TaskHandle_t s_pairing_timeout_task;
static TaskHandle_t s_confirm_timeout_task;
static TaskHandle_t s_button_task;
static TaskHandle_t s_led_blink_task;
static weld_ota_state_t s_ota;
static uint8_t s_rx_write_buf[512];

/*
 * Demo only: store multiple paired App / host tokens in NVS.
 * Production firmware should expose a management path to list and remove
 * paired hosts instead of relying on this simple FIFO replacement policy.
 */
static uint8_t s_bonded_tokens[WELD_MAX_BONDED_TOKENS][SDK_TOKEN_LEN];
static uint8_t s_bonded_token_count;
static uint8_t s_active_token[SDK_TOKEN_LEN];
static bool s_active_token_valid;

static settings_current_t s_settings_current;
static uint16_t s_esrs_mohm10[] = {83, 87, 85, 89};
static settings_fault_log_t s_fault_logs[2];

static bool s_safe_discharge_active;
static bool s_charge_paused;
static uint32_t s_dashboard_tick;
static uint16_t s_dashboard_weld_id = 100;
static uint16_t s_dashboard_mock_post_voltage_mv;
static int64_t s_decode_wait_started_us;

void ble_store_config_init(void);

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event_cb(struct ble_gap_event *event, void *arg);
static bool is_module2_transport_mode(void);
static void handle_rx_bytes(uint16_t conn_handle, const uint8_t *data,
                            uint16_t len);
static void module2_transport_rx_cb(const uint8_t *data, size_t len,
                                    void *ctx);
static void transport_disconnect(uint16_t conn_handle);
static void reset_sdk_parser(const char *reason);
static void reset_stale_sdk_parser_if_needed(const char *reason);
static void start_advertising(void);
static void stop_dashboard_stream(void);
static void clear_ota_state(const char *reason);
static void set_pairing_mode(bool active);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_tx_val_handle,
            },
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_rx_val_handle,
            },
            {0},
        },
    },
    {0},
};

static void copy_fixed(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static void reset_settings_state(void) {
  s_settings_current.profile_id = 1;
  s_settings_current.current_profile = (settings_runtime_profile_t){
      .target_voltage_mv = 3850,
      .single_cap_voltage_mv = 2100,
      .weld_disable_voltage_mv = 3200,
      .target_current_a10 = 100,
      .preheat_pulse_ms10 = 125,
      .cool_time_ms10 = 450,
      .main_pulse_ms10 = 280,
      .trigger_mode = SETTINGS_TRIGGER_MODE_MANUAL,
      .auto_delay_ms = 500,
  };
  s_settings_current.limits_max = (settings_limits_max_t){
      .target_voltage_mv_max = 12000,
      .single_cap_voltage_mv_max = 6000,
      .weld_disable_voltage_mv_max = 12000,
      .target_current_a10_max = 200,
      .preheat_pulse_ms10_max = 2000,
      .cool_time_ms10_max = 2000,
      .main_pulse_ms10_max = 2000,
      .auto_delay_ms_max = 8000,
  };

  s_fault_logs[0] = (settings_fault_log_t){
      .id = 1,
      .type = SETTINGS_FAULT_LOG_TYPE_WARN,
      .time_sec = WELD_FAULT_LOG_BASE_TIME_SEC - 180u,
  };
  copy_fixed(s_fault_logs[0].title, sizeof(s_fault_logs[0].title),
             "充电曲线观察");
  copy_fixed(s_fault_logs[0].message, sizeof(s_fault_logs[0].message),
             "电容充电斜率存在短暂波动，当前仍处于安全阈值内。");

  s_fault_logs[1] = (settings_fault_log_t){
      .id = 2,
      .type = SETTINGS_FAULT_LOG_TYPE_ERROR,
      .time_sec = WELD_FAULT_LOG_BASE_TIME_SEC - 960u,
  };
  copy_fixed(s_fault_logs[1].title, sizeof(s_fault_logs[1].title),
             "历史保护记录");
  copy_fixed(s_fault_logs[1].message, sizeof(s_fault_logs[1].message),
             "上一次测试触发过压保护，已自动复位并等待人工确认。");
}

static void reset_runtime_state(void) {
  s_authenticated = false;
  s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  s_active_token_valid = false;
  memset(s_active_token, 0, sizeof(s_active_token));
  s_notify_enabled = false;
  s_ota = (weld_ota_state_t){0};
  s_safe_discharge_active = false;
  s_charge_paused = false;
}

static void reset_sdk_parser(const char *reason) {
  if (s_sdk_dev) sdk_device_reset_parser(s_sdk_dev);
  s_decode_wait_started_us = 0;
  ESP_LOGI(TAG, "SDK parser reset: %s", reason ? reason : "");
}

static void reset_stale_sdk_parser_if_needed(const char *reason) {
  if (s_decode_wait_started_us == 0) return;

  int64_t elapsed_us = esp_timer_get_time() - s_decode_wait_started_us;
  if (elapsed_us < WELD_DECODE_PARTIAL_TIMEOUT_US) return;

  ESP_LOGW(TAG, "SDK parser partial frame timeout after %lld ms: %s",
           (long long)(elapsed_us / 1000LL), reason ? reason : "");
  reset_sdk_parser("partial-frame-timeout");
}

static bool find_bonded_token(const uint8_t token[SDK_TOKEN_LEN],
                              uint8_t *out_index) {
  if (!token) return false;
  for (uint8_t i = 0; i < s_bonded_token_count; i++) {
    if (memcmp(s_bonded_tokens[i], token, SDK_TOKEN_LEN) == 0) {
      if (out_index) *out_index = i;
      return true;
    }
  }
  return false;
}

static esp_err_t persist_bonded_tokens(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(WELD_NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  if (s_bonded_token_count == 0) {
    err = nvs_erase_key(handle, WELD_NVS_TOKEN_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  } else {
    err = nvs_set_blob(handle, WELD_NVS_TOKEN_KEY, s_bonded_tokens,
                       (size_t)s_bonded_token_count * SDK_TOKEN_LEN);
  }
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

static esp_err_t load_tokens(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(WELD_NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    s_bonded_token_count = 0;
    return ESP_OK;
  }
  if (err != ESP_OK) return err;

  size_t len = sizeof(s_bonded_tokens);
  err = nvs_get_blob(handle, WELD_NVS_TOKEN_KEY, s_bonded_tokens, &len);
  nvs_close(handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    s_bonded_token_count = 0;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    s_bonded_token_count = 0;
    return err;
  }
  if ((len % SDK_TOKEN_LEN) != 0 ||
      len > (size_t)WELD_MAX_BONDED_TOKENS * SDK_TOKEN_LEN) {
    memset(s_bonded_tokens, 0, sizeof(s_bonded_tokens));
    s_bonded_token_count = 0;
    return ESP_ERR_INVALID_SIZE;
  }
  s_bonded_token_count = (uint8_t)(len / SDK_TOKEN_LEN);
  return err;
}

static esp_err_t add_bonded_token(const uint8_t token[SDK_TOKEN_LEN]) {
  if (!token) return ESP_ERR_INVALID_ARG;
  if (find_bonded_token(token, NULL)) return ESP_OK;

  if (s_bonded_token_count >= WELD_MAX_BONDED_TOKENS) {
    /*
     * Demo policy: keep pairing usable when storage is full by dropping the
     * oldest token. Real devices should let users remove a selected paired App.
     */
    memmove(s_bonded_tokens, s_bonded_tokens + 1,
            (WELD_MAX_BONDED_TOKENS - 1u) * SDK_TOKEN_LEN);
    s_bonded_token_count = WELD_MAX_BONDED_TOKENS - 1u;
    ESP_LOGW(TAG, "bonded token list full, removed oldest token");
  }
  memcpy(s_bonded_tokens[s_bonded_token_count], token, SDK_TOKEN_LEN);
  s_bonded_token_count++;
  return persist_bonded_tokens();
}

static esp_err_t remove_bonded_token(const uint8_t token[SDK_TOKEN_LEN]) {
  uint8_t index = 0;
  if (!find_bonded_token(token, &index)) return ESP_OK;

  if (index + 1u < s_bonded_token_count) {
    memmove(s_bonded_tokens[index], s_bonded_tokens[index + 1u],
            (size_t)(s_bonded_token_count - index - 1u) * SDK_TOKEN_LEN);
  }
  s_bonded_token_count--;
  memset(s_bonded_tokens[s_bonded_token_count], 0, SDK_TOKEN_LEN);
  return persist_bonded_tokens();
}

static esp_err_t clear_bonded_tokens(void) {
  memset(s_bonded_tokens, 0, sizeof(s_bonded_tokens));
  s_bonded_token_count = 0;
  memset(s_active_token, 0, sizeof(s_active_token));
  s_active_token_valid = false;
  return persist_bonded_tokens();
}

static void recreate_sdk_device(const uint8_t mac[6]) {
  if (s_sdk_dev) sdk_device_destroy(s_sdk_dev);
  s_sdk_dev = sdk_device_create(mac, WELD_DEVICE_NAME);
  s_decode_wait_started_us = 0;
  if (!s_sdk_dev) {
    ESP_LOGE(TAG, "failed to create sdk_device");
  }
}

static bool is_module2_transport_mode(void) {
  return s_runtime_mode == WELD_RUNTIME_MODULE2_TRANSPORT;
}

static bool is_connected(uint16_t conn_handle) {
  return s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_conn_handle == conn_handle;
}

static bool is_authenticated_conn(uint16_t conn_handle) {
  return s_authenticated && s_active_conn_handle == conn_handle;
}

static bool is_auth_free_command(uint8_t cmd) {
  for (uint8_t i = 0; i < SDK_AUTH_FREE_COMMANDS_COUNT; i++) {
    if (SDK_AUTH_FREE_COMMANDS[i] == cmd) return true;
  }
  return false;
}

static uint16_t crc16_ccitt_false_update(uint16_t crc, const uint8_t *data,
                                         uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static void send_packet_locked(uint16_t conn_handle, uint8_t cmd, uint8_t seq,
                               const uint8_t *payload, uint16_t payload_len) {
  if (!is_connected(conn_handle) || !s_notify_enabled || !s_sdk_dev) return;
  if (payload_len > SDK_MAX_PAYLOAD) return;

  sdk_packet_t pkt = {
      .cmd = cmd,
      .seq = seq,
      .payload_len = payload_len,
  };
  if (payload_len > 0 && payload) {
    memcpy(pkt.payload, payload, payload_len);
  }

  uint8_t frame[SDK_HEAD_SIZE + SDK_MAX_PAYLOAD + SDK_CRC_SIZE];
  sdk_frame_chunk_with_encode_iter_t iter = sdk_frame_chunk_with_encode_iter(
      s_sdk_dev, &pkt, frame, sizeof(frame), WELD_NOTIFY_CHUNK_SIZE);
  if (!iter.ok) {
    ESP_LOGE(TAG, "sdk_encode failed cmd=0x%02X len=%u", cmd, payload_len);
    return;
  }

  ESP_LOGI(TAG, "TX cmd=0x%02X seq=%u payload=%u frame=%u",
           cmd, seq, payload_len, iter.frame_len);
  sdk_frame_chunk_t chunk;
  while (sdk_frame_chunk_with_encode_next(&iter, &chunk)) {

    if (is_module2_transport_mode()) {
      esp_err_t err = ble_module2_transport_send(chunk.data, chunk.len);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "module2 UART TX failed: %s cmd=0x%02X",
                 esp_err_to_name(err), cmd);
        return;
      }
    } else {
      struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk.data, chunk.len);
      if (!om) {
        ESP_LOGE(TAG, "notify mbuf alloc failed");
        return;
      }
      int rc = ble_gatts_notify_custom(conn_handle, s_tx_val_handle, om);
      if (rc != 0) {
        ESP_LOGW(TAG, "notify failed rc=%d cmd=0x%02X", rc, cmd);
        return;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void send_packet(uint16_t conn_handle, uint8_t cmd, uint8_t seq,
                        const uint8_t *payload, uint16_t payload_len) {
  if (!s_tx_mutex) return;
  xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
  send_packet_locked(conn_handle, cmd, seq, payload, payload_len);
  xSemaphoreGive(s_tx_mutex);
}

static void send_result(uint16_t conn_handle, uint8_t cmd, uint8_t seq,
                        uint8_t status, uint8_t code, const uint8_t *data,
                        uint16_t data_len) {
  uint8_t result_payload[WELD_RESULT_PAYLOAD_MAX];
  uint16_t result_len = sdk_result_pack(status, code, data, data_len,
                                        result_payload, sizeof(result_payload));
  if (result_len == 0) {
    ESP_LOGE(TAG, "result pack failed cmd=0x%02X data_len=%u", cmd, data_len);
    return;
  }
  send_packet(conn_handle, cmd, seq, result_payload, result_len);
}

static void reset_module2_session(const char *reason) {
  ESP_LOGW(TAG, "module2 transport session reset: %s", reason ? reason : "");
  stop_dashboard_stream();
  clear_ota_state(reason ? reason : "module2-session-reset");
  reset_sdk_parser(reason ? reason : "module2-session-reset");
  reset_runtime_state();
  s_conn_handle = WELD_MODULE2_CONN_HANDLE;
  s_notify_enabled = true;
}

static void transport_disconnect(uint16_t conn_handle) {
  if (!is_connected(conn_handle)) return;

  if (is_module2_transport_mode()) {
    reset_module2_session("disconnect-request");
    return;
  }

  ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void disconnect_later_task(void *arg) {
  uintptr_t packed = (uintptr_t)arg;
  uint16_t conn_handle = (uint16_t)(packed & 0xFFFFu);
  uint16_t delay_ms = (uint16_t)(packed >> 16);
  if (delay_ms == 0) delay_ms = 250;
  vTaskDelay(pdMS_TO_TICKS(delay_ms));
  transport_disconnect(conn_handle);
  vTaskDelete(NULL);
}

static void schedule_disconnect_after(uint16_t conn_handle, uint16_t delay_ms) {
  uintptr_t packed = ((uintptr_t)delay_ms << 16) | conn_handle;
  xTaskCreate(disconnect_later_task, "weld_disc", 3072,
              (void *)packed, 5, NULL);
}

static void schedule_disconnect(uint16_t conn_handle) {
  schedule_disconnect_after(conn_handle, 250);
}

static void cancel_task(TaskHandle_t *task) {
  if (!task || !*task) return;
  TaskHandle_t current = xTaskGetCurrentTaskHandle();
  if (*task != current) {
    vTaskDelete(*task);
  }
  *task = NULL;
}

static uint8_t s_led_blink_r;
static uint8_t s_led_blink_g;
static uint8_t s_led_blink_b;
static uint16_t s_led_blink_interval_ms;

static void status_led_write(uint8_t r, uint8_t g, uint8_t b) {
  if (!s_led_strip) return;
  if (r == 0 && g == 0 && b == 0) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_clear(s_led_strip));
    return;
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_set_pixel(s_led_strip, 0, r, g, b));
  ESP_ERROR_CHECK_WITHOUT_ABORT(led_strip_refresh(s_led_strip));
}

static void status_led_blink_task(void *arg) {
  (void)arg;
  bool on = true;
  while (true) {
    if (on) {
      status_led_write(s_led_blink_r, s_led_blink_g, s_led_blink_b);
    } else {
      status_led_write(0, 0, 0);
    }
    on = !on;
    vTaskDelay(pdMS_TO_TICKS(s_led_blink_interval_ms));
  }
}

static void status_led_off(void) {
  cancel_task(&s_led_blink_task);
  status_led_write(0, 0, 0);
}

static void status_led_solid(uint8_t r, uint8_t g, uint8_t b) {
  cancel_task(&s_led_blink_task);
  status_led_write(r, g, b);
}

static void status_led_blink(uint8_t r, uint8_t g, uint8_t b,
                             uint16_t interval_ms) {
  cancel_task(&s_led_blink_task);
  s_led_blink_r = r;
  s_led_blink_g = g;
  s_led_blink_b = b;
  s_led_blink_interval_ms = interval_ms ? interval_ms : 500;
  BaseType_t ok = xTaskCreate(status_led_blink_task, "status_led", 3072, NULL,
                              5, &s_led_blink_task);
  if (ok != pdPASS) {
    s_led_blink_task = NULL;
    ESP_LOGE(TAG, "failed to start status LED blink task");
  }
}

static void init_status_led(void) {
  led_strip_config_t strip_config = {
      .strip_gpio_num = WELD_STATUS_LED_GPIO,
      .max_leds = WELD_STATUS_LED_COUNT,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };
  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config,
                                           &s_led_strip);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "status LED init failed: %s", esp_err_to_name(err));
    return;
  }
  status_led_off();
}

static void pairing_timeout_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(WELD_PAIRING_TIMEOUT_MS));
  if (s_pairing_mode && s_waiting_confirm_conn == BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGI(TAG, "pairing mode timeout");
    set_pairing_mode(false);
  }
  if (s_pairing_timeout_task == xTaskGetCurrentTaskHandle()) {
    s_pairing_timeout_task = NULL;
  }
  vTaskDelete(NULL);
}

static void confirm_timeout_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(WELD_PAIR_CONFIRM_TIMEOUT_MS));

  uint16_t conn_handle = s_waiting_confirm_conn;
  uint8_t seq = s_waiting_confirm_seq;
  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGI(TAG, "pair confirm timeout conn=%u seq=%u", conn_handle, seq);
    s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
    s_waiting_confirm_seq = 0;
    send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, seq,
                SDK_RESULT_STATUS_FAIL,
                SDK_RESULT_CODE_PAIR_CONFIRM_TIMEOUT, NULL, 0);
    schedule_disconnect_after(conn_handle, 500);
  }

  if (s_confirm_timeout_task == xTaskGetCurrentTaskHandle()) {
    s_confirm_timeout_task = NULL;
  }
  set_pairing_mode(false);
  vTaskDelete(NULL);
}

static void start_confirm_timeout(void) {
  cancel_task(&s_confirm_timeout_task);
  BaseType_t ok = xTaskCreate(confirm_timeout_task, "pair_confirm",
                              WELD_CONFIRM_TASK_STACK, NULL, 5,
                              &s_confirm_timeout_task);
  if (ok != pdPASS) {
    s_confirm_timeout_task = NULL;
    ESP_LOGE(TAG, "failed to start confirm timeout task");
  }
}

static void set_pairing_mode(bool active) {
  s_pairing_mode = active;

  cancel_task(&s_pairing_timeout_task);
  if (!active) {
    cancel_task(&s_confirm_timeout_task);
    s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
    s_waiting_confirm_seq = 0;
    status_led_off();
    ESP_LOGI(TAG, "pairing mode off");
    return;
  }

  status_led_solid(0, 0, WELD_LED_PAIR_VALUE);
  ESP_LOGI(TAG, "pairing mode on: wait for pair request");
  if (!is_connected(s_conn_handle)) {
    if (is_module2_transport_mode()) {
      s_conn_handle = WELD_MODULE2_CONN_HANDLE;
      s_notify_enabled = true;
    } else {
      start_advertising();
    }
  }
  BaseType_t ok = xTaskCreate(pairing_timeout_task, "pair_window", 3072, NULL,
                              5, &s_pairing_timeout_task);
  if (ok != pdPASS) {
    s_pairing_timeout_task = NULL;
    ESP_LOGE(TAG, "failed to start pairing timeout task");
  }
}

static void accept_waiting_pair_request(void) {
  uint16_t conn_handle = s_waiting_confirm_conn;
  uint8_t seq = s_waiting_confirm_seq;
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    set_pairing_mode(true);
    return;
  }

  uint8_t token[SDK_TOKEN_LEN];
  esp_fill_random(token, sizeof(token));
  esp_err_t err = add_bonded_token(token);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "save token failed: %s", esp_err_to_name(err));
    send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, seq,
                SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    schedule_disconnect_after(conn_handle, 500);
    set_pairing_mode(false);
    return;
  }

  s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
  s_waiting_confirm_seq = 0;
  s_authenticated = true;
  s_active_conn_handle = conn_handle;
  memcpy(s_active_token, token, SDK_TOKEN_LEN);
  s_active_token_valid = true;
  send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, token,
              sizeof(token));
  ESP_LOGI(TAG, "pair accepted conn=%u", conn_handle);
  s_pairing_mode = false;
  cancel_task(&s_pairing_timeout_task);
  cancel_task(&s_confirm_timeout_task);
  status_led_solid(0, WELD_LED_PAIR_VALUE, 0);
}

static void reject_waiting_pair_request(void) {
  uint16_t conn_handle = s_waiting_confirm_conn;
  uint8_t seq = s_waiting_confirm_seq;
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE) return;

  s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
  s_waiting_confirm_seq = 0;
  send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, seq,
              SDK_RESULT_STATUS_FAIL, SDK_RESULT_CODE_PAIR_REJECTED, NULL, 0);
  ESP_LOGI(TAG, "pair rejected conn=%u", conn_handle);
  schedule_disconnect_after(conn_handle, 500);
  set_pairing_mode(true);
}

static void handle_triple_click(void) {
  ESP_LOGI(TAG, "BOOT triple click: disconnect and exit pairing mode");
  uint16_t conn_handle = s_conn_handle;
  set_pairing_mode(false);
  s_authenticated = false;
  s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    transport_disconnect(conn_handle);
  }
}

static void button_task(void *arg) {
  (void)arg;
  int last_state = 1;
  int64_t press_time_ms = 0;
  int64_t last_click_time_ms = 0;
  uint8_t click_count = 0;
  bool long_press_triggered = false;

  while (true) {
    int state = gpio_get_level(WELD_BOOT_BUTTON_GPIO);
    int64_t now_ms = esp_timer_get_time() / 1000LL;

    if (state == 0 && last_state == 1) {
      press_time_ms = now_ms;
      long_press_triggered = false;
      vTaskDelay(pdMS_TO_TICKS(WELD_BUTTON_DEBOUNCE_MS));
    } else if (state == 1 && last_state == 0) {
      if (!long_press_triggered) {
        int64_t duration_ms = now_ms - press_time_ms;
        if (duration_ms > WELD_BUTTON_CLICK_MIN_MS &&
            duration_ms < WELD_BUTTON_CLICK_MAX_MS) {
          click_count++;
          last_click_time_ms = now_ms;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(WELD_BUTTON_DEBOUNCE_MS));
    } else if (state == 0 && last_state == 0) {
      int64_t duration_ms = now_ms - press_time_ms;
      if (duration_ms >= WELD_BUTTON_LONG_PRESS_MS &&
          !long_press_triggered) {
        long_press_triggered = true;
        click_count = 0;
        ESP_LOGI(TAG, "BOOT long press");
        accept_waiting_pair_request();
      }
    }

    if (click_count > 0 &&
        now_ms - last_click_time_ms > WELD_BUTTON_MULTI_CLICK_TIMEOUT_MS) {
      if (click_count == 1) {
        ESP_LOGI(TAG, "BOOT single click");
        reject_waiting_pair_request();
      } else if (click_count >= 3) {
        handle_triple_click();
      }
      click_count = 0;
    }

    last_state = state;
    vTaskDelay(pdMS_TO_TICKS(WELD_BUTTON_POLL_MS));
  }
}

static void init_boot_button(void) {
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << WELD_BOOT_BUTTON_GPIO,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&cfg));
  BaseType_t ok = xTaskCreate(button_task, "boot_button",
                              WELD_BUTTON_TASK_STACK, NULL, 5,
                              &s_button_task);
  if (ok != pdPASS) {
    s_button_task = NULL;
    ESP_LOGE(TAG, "failed to start boot button task");
  }
}

static const char *device_serial_suffix(void) {
  if (is_module2_transport_mode()) {
    return ble_module2_transport_serial_suffix();
  }
  return "ESP";
}

static void format_serial(char *serial, size_t serial_size) {
  snprintf(serial, serial_size, "%02X%02X%02X%02X%02X%02X-%s",
           s_own_addr[5], s_own_addr[4], s_own_addr[3],
           s_own_addr[2], s_own_addr[1], s_own_addr[0],
           device_serial_suffix());
}

static void handle_device_get_info(uint16_t conn_handle, uint8_t seq) {
  char serial[20];
  format_serial(serial, sizeof(serial));

  const char *manufacturer = "Weld Test";
  const char *model = "SC-01";
  device_info_t info = {
      .company_id = WELD_DEVICE_COMPANY_ID,
      .product_id = WELD_DEVICE_PRODUCT_ID,
      .feature_mask = feature_mask_get(),
      .protocol_version = SDK_PROTOCOL_VERSION,
      .protocol_min_version = SDK_MIN_PROTOCOL_VERSION,
      .ota_max_kb = SDK_OTA_MAX_FW_SIZE / 1024u,
      .ota_chunk_max = SDK_OTA_CHUNK_MAX,
      .firmware_major = 1,
      .firmware_minor = 0,
      .firmware_patch = 0,
      .firmware_build_id = WELD_FIRMWARE_BUILD_ID,
      .hardware_major = 1,
      .hardware_minor = 0,
      .hardware_patch = WELD_HARDWARE_BUILD_ID,
      .manufacturer = manufacturer,
      .manufacturer_len = (uint8_t)strlen(manufacturer),
      .model = model,
      .model_len = (uint8_t)strlen(model),
      .serial = serial,
      .serial_len = (uint8_t)strlen(serial),
  };

  uint8_t payload[128];
  uint16_t payload_len = 0;
  if (!device_info_pack(&info, payload, sizeof(payload), &payload_len)) {
    send_result(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq,
                SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }
  send_result(conn_handle, CMD_DEVICE_GET_INFO_ACK, seq, SDK_RESULT_STATUS_OK,
              SDK_RESULT_CODE_COMMON_NONE, payload, payload_len);
}

static void handle_pair_request(uint16_t conn_handle, const sdk_packet_t *pkt) {
  if (!s_pairing_mode) {
    ESP_LOGI(TAG, "PAIR rejected: not in pairing mode");
    send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL,
                SDK_RESULT_CODE_PAIR_NOT_IN_PAIRING_MODE, NULL, 0);
    return;
  }

  pair_request_t request;
  if (!pair_request_unpack(pkt->payload, pkt->payload_len, &request)) {
    send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }

  char host_name[SDK_PAIR_NAME_MAX + 1];
  uint8_t name_len = request.name_len;
  if (name_len > SDK_PAIR_NAME_MAX) name_len = SDK_PAIR_NAME_MAX;
  memcpy(host_name, request.name, name_len);
  host_name[name_len] = '\0';
  ESP_LOGI(TAG, "PAIR client_id=0x%016" PRIX64 " name=%s", request.client_id,
           host_name);

  s_waiting_confirm_conn = conn_handle;
  s_waiting_confirm_seq = pkt->seq;
  send_result(conn_handle, CMD_DEVICE_PAIR_REQUEST_ACK, pkt->seq,
              SDK_RESULT_STATUS_PENDING,
              SDK_RESULT_CODE_PAIR_WAIT_CONFIRM, NULL, 0);
  status_led_blink(WELD_LED_PAIR_VALUE, WELD_LED_PAIR_VALUE, 0,
                   WELD_PAIR_CONFIRM_BLINK_MS);
  start_confirm_timeout();
  ESP_LOGI(TAG, "PAIR pending: long press BOOT to accept, single click to reject");
}

static void handle_auth(uint16_t conn_handle, const sdk_packet_t *pkt) {
  bool ok = pkt->payload_len >= SDK_TOKEN_LEN &&
            find_bonded_token(pkt->payload, NULL);
  if (ok) {
    s_authenticated = true;
    s_active_conn_handle = conn_handle;
    memcpy(s_active_token, pkt->payload, SDK_TOKEN_LEN);
    s_active_token_valid = true;
    status_led_solid(0, WELD_LED_PAIR_VALUE, 0);
    send_result(conn_handle, CMD_DEVICE_AUTH_ACK, pkt->seq, SDK_RESULT_STATUS_OK,
                SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
    return;
  }

  send_result(conn_handle, CMD_DEVICE_AUTH_ACK, pkt->seq,
              SDK_RESULT_STATUS_AUTH_INVALID,
              SDK_RESULT_CODE_AUTH_INVALID_TOKEN, NULL, 0);
  schedule_disconnect(conn_handle);
}

static void send_auth_required(uint16_t conn_handle, const sdk_packet_t *pkt) {
  uint8_t rejected_cmd = pkt->cmd;
  send_result(conn_handle, CMD_DEVICE_AUTH_REQUIRED, pkt->seq,
              SDK_RESULT_STATUS_AUTH_INVALID,
              SDK_RESULT_CODE_AUTH_REJECTED_COMMAND, &rejected_cmd, 1);
  schedule_disconnect(conn_handle);
}

static void handle_settings_read_current(uint16_t conn_handle, uint8_t seq) {
  uint8_t payload[SETTINGS_CURRENT_PAYLOAD_SIZE];
  uint16_t payload_len = 0;
  if (!settings_current_pack(&s_settings_current, payload, sizeof(payload),
                             &payload_len)) {
    send_result(conn_handle, CMD_SETTINGS_READ_CURRENT_ACK, seq,
                SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }
  send_result(conn_handle, CMD_SETTINGS_READ_CURRENT_ACK, seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload,
              payload_len);
}

static void handle_settings_read_self_check(uint16_t conn_handle, uint8_t seq) {
  settings_self_check_t self_check = {
      .esrs_mohm10 = s_esrs_mohm10,
      .esr_size = sizeof(s_esrs_mohm10) / sizeof(s_esrs_mohm10[0]),
      .esr_quality = SETTINGS_ESR_QUALITY_EXCELLENT,
      .fault_logs = s_fault_logs,
      .fault_log_size = sizeof(s_fault_logs) / sizeof(s_fault_logs[0]),
  };
  uint8_t payload[512];
  uint16_t payload_len = 0;
  if (!settings_self_check_pack(&self_check, payload, sizeof(payload),
                                &payload_len)) {
    send_result(conn_handle, CMD_SETTINGS_READ_SELF_CHECK_ACK, seq,
                SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }
  send_result(conn_handle, CMD_SETTINGS_READ_SELF_CHECK_ACK, seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload,
              payload_len);
}

static void handle_settings_profile(uint16_t conn_handle,
                                    const sdk_packet_t *pkt) {
  settings_apply_profile_t profile;
  if (!settings_apply_profile_unpack(pkt->payload, pkt->payload_len, &profile)) {
    send_result(conn_handle, CMD_SETTINGS_PROFILE_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM,
                SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID, NULL, 0);
    return;
  }
  s_settings_current.profile_id =
      profile.profile_id >= 1 && profile.profile_id <= 10 ? profile.profile_id
                                                          : 0;
  s_settings_current.current_profile = profile.profile;
  send_result(conn_handle, CMD_SETTINGS_PROFILE_ACK, pkt->seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
}

static bool quick_value_in_range(int32_t value, uint16_t max) {
  return value >= 0 && value <= (int32_t)max;
}

static uint8_t apply_settings_quick_set(const settings_quick_set_t *quick) {
  if (!quick) return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
  settings_runtime_profile_t *profile = &s_settings_current.current_profile;
  const settings_limits_max_t *limits = &s_settings_current.limits_max;

  switch (quick->item) {
    case SETTINGS_QUICK_SET_ITEM_CHARGE_VOLTAGE:
      if (quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      if (!quick_value_in_range(quick->primary, limits->target_voltage_mv_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      profile->target_voltage_mv = (uint16_t)quick->primary;
      return SDK_RESULT_CODE_COMMON_NONE;

    case SETTINGS_QUICK_SET_ITEM_CHARGE_CURRENT:
      if (quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      if (!quick_value_in_range(quick->primary, limits->target_current_a10_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      profile->target_current_a10 = (uint16_t)quick->primary;
      return SDK_RESULT_CODE_COMMON_NONE;

    case SETTINGS_QUICK_SET_ITEM_PREHEAT_PULSE:
      if (quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      if (!quick_value_in_range(quick->primary, limits->preheat_pulse_ms10_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      profile->preheat_pulse_ms10 = (uint16_t)quick->primary;
      return SDK_RESULT_CODE_COMMON_NONE;

    case SETTINGS_QUICK_SET_ITEM_COOL_TIME:
      if (quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      if (!quick_value_in_range(quick->primary, limits->cool_time_ms10_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      profile->cool_time_ms10 = (uint16_t)quick->primary;
      return SDK_RESULT_CODE_COMMON_NONE;

    case SETTINGS_QUICK_SET_ITEM_MAIN_PULSE:
      if (quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      if (!quick_value_in_range(quick->primary, limits->main_pulse_ms10_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      profile->main_pulse_ms10 = (uint16_t)quick->primary;
      return SDK_RESULT_CODE_COMMON_NONE;

    case SETTINGS_QUICK_SET_ITEM_TRIGGER_MODE:
      if ((quick->primary != SETTINGS_TRIGGER_MODE_MANUAL &&
           quick->primary != SETTINGS_TRIGGER_MODE_AUTO) ||
          !quick_value_in_range(quick->secondary, limits->auto_delay_ms_max)) {
        return SDK_RESULT_CODE_SETTINGS_VALUE_OUT_OF_RANGE;
      }
      if (quick->primary == SETTINGS_TRIGGER_MODE_MANUAL && quick->secondary != 0) {
        return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
      }
      profile->trigger_mode = (uint8_t)quick->primary;
      profile->auto_delay_ms = (uint16_t)quick->secondary;
      return SDK_RESULT_CODE_COMMON_NONE;

    default:
      return SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID;
  }
}

static void handle_settings_quick_set(uint16_t conn_handle,
                                      const sdk_packet_t *pkt) {
  settings_quick_set_t quick;
  if (!settings_quick_set_unpack(pkt->payload, pkt->payload_len, &quick)) {
    send_result(conn_handle, CMD_SETTINGS_QUICK_SET_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM,
                SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID, NULL, 0);
    return;
  }
  uint8_t code = apply_settings_quick_set(&quick);
  if (code != SDK_RESULT_CODE_COMMON_NONE) {
    send_result(conn_handle, CMD_SETTINGS_QUICK_SET_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM, code, NULL, 0);
    return;
  }
  send_result(conn_handle, CMD_SETTINGS_QUICK_SET_ACK, pkt->seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
}

static void handle_settings_reset(uint16_t conn_handle, const sdk_packet_t *pkt) {
  settings_reset_t reset;
  if (!settings_reset_unpack(pkt->payload, pkt->payload_len, &reset)) {
    send_result(conn_handle, CMD_SETTINGS_RESET_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM,
                SDK_RESULT_CODE_SETTINGS_PAYLOAD_INVALID, NULL, 0);
    return;
  }
  reset_settings_state();
  bool clear_tokens = (reset.flags & SETTINGS_RESET_FLAG_CLEAR_TOKENS) != 0;
  if (clear_tokens) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(clear_bonded_tokens());
    s_authenticated = false;
  }
  send_result(conn_handle, CMD_SETTINGS_RESET_ACK, pkt->seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
  if (clear_tokens) schedule_disconnect(conn_handle);
}

static void handle_dashboard_init(uint16_t conn_handle, const sdk_packet_t *pkt) {
  dashboard_init_request_t request;
  if (!dashboard_init_request_unpack(pkt->payload, pkt->payload_len, &request)) {
    send_result(conn_handle, CMD_READ_DASHBOARD_INIT_ACK, pkt->seq,
                SDK_RESULT_STATUS_INVALID_PARAM, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }

  sdk_compat_result_t compat = sdk_check_protocol_compat(
      SDK_PROTOCOL_VERSION, SDK_MIN_PROTOCOL_VERSION,
      request.app_protocol_version, request.app_min_protocol_version);
  if (compat.code == SDK_COMPAT_CODE_APP_TOO_OLD) {
    send_result(conn_handle, CMD_READ_DASHBOARD_INIT_ACK, pkt->seq,
                SDK_RESULT_STATUS_NOT_SUPPORTED,
                SDK_RESULT_CODE_COMMON_PROTOCOL_UNSUPPORTED, NULL, 0);
    return;
  }

  dashboard_init_t init = {
      .setting_voltage_max_mv =
          s_settings_current.current_profile.target_voltage_mv,
      .stat_welds_total = 1280,
      .stat_welds_task = 18,
      .setting_weld_pre_ms10 =
          s_settings_current.current_profile.preheat_pulse_ms10,
      .setting_weld_cooling_ms10 =
          s_settings_current.current_profile.cool_time_ms10,
      .setting_weld_main_ms10 =
          s_settings_current.current_profile.main_pulse_ms10,
      .protocol_version = SDK_PROTOCOL_VERSION,
      .firmware_major = 1,
      .firmware_minor = 0,
      .firmware_patch = 0,
      .firmware_build_id = WELD_FIRMWARE_BUILD_ID,
      .protocol_min_version = SDK_MIN_PROTOCOL_VERSION,
      .setting_single_cap_voltage_mv =
          s_settings_current.current_profile.single_cap_voltage_mv,
      .setting_trigger_mode = s_settings_current.current_profile.trigger_mode,
      .setting_auto_delay_ms = s_settings_current.current_profile.auto_delay_ms,
  };
  uint8_t payload[DASHBOARD_INIT_PAYLOAD_SIZE];
  if (!dashboard_init_pack(&init, payload, sizeof(payload))) {
    send_result(conn_handle, CMD_READ_DASHBOARD_INIT_ACK, pkt->seq,
                SDK_RESULT_STATUS_DEVICE_ERROR, SDK_RESULT_CODE_COMMON_UNKNOWN,
                NULL, 0);
    return;
  }
  send_result(conn_handle, CMD_READ_DASHBOARD_INIT_ACK, pkt->seq,
              SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, payload,
              sizeof(payload));
}

static void dashboard_stream_task(void *arg) {
  uint16_t conn_handle = (uint16_t)(uintptr_t)arg;
  while (is_connected(conn_handle) && s_authenticated) {
    s_dashboard_tick++;
    uint32_t tick = s_dashboard_tick;
    uint16_t voltage_max_mv =
        s_settings_current.current_profile.target_voltage_mv;
    uint8_t phase = tick % 34;
    bool is_welding = phase < 8;

    uint16_t weld_current_a;
    uint16_t voltage_mv;
    uint16_t charge_current_ma;
    if (is_welding) {
      weld_current_a = (uint16_t)(650 + phase * 210);
      uint16_t drop = (uint16_t)(180 + phase * 48);
      voltage_mv = voltage_max_mv > drop ? (uint16_t)(voltage_max_mv - drop) : 0;
      charge_current_ma = 900;
    } else {
      weld_current_a = (uint16_t)(35 + (tick % 6) * 8);
      uint8_t recharge = (uint8_t)(phase > 8 ? phase - 8 : 0);
      if (recharge > 26) recharge = 26;
      uint16_t step = voltage_max_mv > 2120 ? (uint16_t)((voltage_max_mv - 2120) / 26) : 1;
      if (step == 0) step = 1;
      voltage_mv = (uint16_t)(2120 + recharge * step);
      if (voltage_mv > voltage_max_mv) voltage_mv = voltage_max_mv;
      charge_current_ma =
          s_charge_paused ? 0 : (uint16_t)(2600 + (26 - recharge) * 120);
    }

    uint8_t charge_mode_code;
    if (s_charge_paused) {
      charge_mode_code = DASHBOARD_CHARGE_MODE_PAUSED;
    } else if (tick % 132 < 12) {
      charge_mode_code = DASHBOARD_CHARGE_MODE_PRECHARGE;
    } else if (tick % 132 > 118) {
      charge_mode_code = DASHBOARD_CHARGE_MODE_FLOAT;
    } else if (tick % 44 > 20) {
      charge_mode_code = DASHBOARD_CHARGE_MODE_CONSTANT_VOLTAGE;
    } else {
      charge_mode_code = DASHBOARD_CHARGE_MODE_CONSTANT_CURRENT;
    }

    uint16_t voltage_cap_1_mv = (uint16_t)(voltage_mv / 2u + (tick % 5u));
    uint16_t voltage_cap_2_mv =
        voltage_mv > voltage_cap_1_mv ? (uint16_t)(voltage_mv - voltage_cap_1_mv) : 0;
    uint8_t machine_status =
        voltage_mv < s_settings_current.current_profile.weld_disable_voltage_mv
            ? DASHBOARD_MACHINE_STATUS_VOLTAGE_LOW
            : DASHBOARD_MACHINE_STATUS_READY;
    dashboard_compact_t compact = {
        .voltage_mv = voltage_mv,
        .weld_current_a = weld_current_a,
        .charge_current_ma = charge_current_ma,
        .voltage_cap_1_mv = voltage_cap_1_mv,
        .voltage_cap_2_mv = voltage_cap_2_mv,
        .temperature_capacitor_c = (int8_t)(31 + (tick % 6)),
        .temperature_mos_c = (int8_t)(45 + (tick % 8)),
        .machine_status = machine_status,
        .charge_mode_code = charge_mode_code,
        .discharge_status = s_safe_discharge_active ? 1 : 0,
        .undefined_status = 0,
    };
    uint8_t compact_payload[DASHBOARD_COMPACT_PAYLOAD_SIZE];
    if (dashboard_compact_pack(&compact, compact_payload,
                               sizeof(compact_payload))) {
      send_packet(conn_handle, CMD_DASHBOARD_COMPACT, tick & 0xFF,
                  compact_payload, sizeof(compact_payload));
    }

    if (tick % 34 == 0) {
      s_dashboard_weld_id++;
      if (s_dashboard_mock_post_voltage_mv == 0 ||
          s_dashboard_mock_post_voltage_mv > voltage_mv) {
        s_dashboard_mock_post_voltage_mv = voltage_mv;
      }
      uint16_t mock_drop_mv = (uint16_t)(10u + (esp_random() % 61u));
      if (s_dashboard_mock_post_voltage_mv <= mock_drop_mv + 1800u) {
        s_dashboard_mock_post_voltage_mv = voltage_mv;
      }
      uint16_t post_voltage_mv =
          s_dashboard_mock_post_voltage_mv > mock_drop_mv
              ? (uint16_t)(s_dashboard_mock_post_voltage_mv - mock_drop_mv)
              : 0;
      s_dashboard_mock_post_voltage_mv = post_voltage_mv;
      dashboard_weld_record_t record = {
          .id = s_dashboard_weld_id,
          .peak_current_a = (uint16_t)(1680 + (tick % 9) * 24),
          .post_voltage_mv = post_voltage_mv,
      };
      uint8_t payload[1 + DASHBOARD_WELD_RECORD_PAYLOAD_SIZE];
      uint16_t payload_len = 0;
      if (dashboard_weld_records_from_array(&record, 1, payload,
                                            sizeof(payload), &payload_len)) {
        send_packet(conn_handle, CMD_DASHBOARD_WELD_RECORDS,
                    tick & 0xFF, payload, payload_len);
      }
    }

    if (tick % 12 == 0) {
      dashboard_log_t log = {
          .id = (uint16_t)tick,
          .level = 0,
          .time_sec = tick / 10,
          .code = tick % 34 == 0 ? DASHBOARD_LOG_CODE_WELD_COMPLETE
                                 : DASHBOARD_LOG_CODE_SYSTEM_READY,
      };
      uint8_t payload[1 + DASHBOARD_LOG_PAYLOAD_SIZE];
      uint16_t payload_len = 0;
      if (dashboard_logs_from_array(&log, 1, payload, sizeof(payload),
                                    &payload_len)) {
        send_packet(conn_handle, CMD_DASHBOARD_LOGS, tick & 0xFF,
                    payload, payload_len);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
  s_dashboard_task = NULL;
  vTaskDelete(NULL);
}

static void start_dashboard_stream(uint16_t conn_handle) {
  stop_dashboard_stream();
  xTaskCreate(dashboard_stream_task, "weld_dash", 4096,
              (void *)(uintptr_t)conn_handle, 5, &s_dashboard_task);
}

static void stop_dashboard_stream(void) {
  if (s_dashboard_task) {
    TaskHandle_t task = s_dashboard_task;
    s_dashboard_task = NULL;
    vTaskDelete(task);
  }
}

static void build_ota_ack(uint16_t next_chunk, uint8_t out[OTA_ACK_DATA_PAYLOAD_SIZE]) {
  ota_ack_data_t ack = {.next_chunk = next_chunk};
  ota_ack_data_pack(&ack, out, OTA_ACK_DATA_PAYLOAD_SIZE);
}

static bool ota_expired(void) {
  return s_ota.active &&
         esp_timer_get_time() - s_ota.updated_us > WELD_OTA_SESSION_TIMEOUT_US;
}

static void clear_ota_state(const char *reason) {
  if (s_ota.active) ESP_LOGI(TAG, "OTA clear: %s", reason);
  s_ota = (weld_ota_state_t){0};
}

static void handle_ota_start(uint16_t conn_handle, const sdk_packet_t *pkt) {
  ota_start_t start;
  uint8_t ack[OTA_ACK_DATA_PAYLOAD_SIZE];
  if (!ota_start_unpack(pkt->payload, pkt->payload_len, &start)) {
    ESP_LOGW(TAG, "OTA START invalid payload_len=%u expected=%u",
             (unsigned)pkt->payload_len, (unsigned)OTA_START_PAYLOAD_SIZE);
    build_ota_ack(0, ack);
    send_result(conn_handle, CMD_OTA_START_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, ack, sizeof(ack));
    return;
  }
  ESP_LOGI(TAG,
           "OTA START company=0x%04X type=%u version=%u.%u.%u fw_size=%" PRIu32
           " crc=0x%04X chunk=%u max_fw=%" PRIu32 " max_chunk=%u payload_len=%u",
           (unsigned)start.company_id, (unsigned)start.fw_type,
           (unsigned)start.fw_ver[0], (unsigned)start.fw_ver[1],
           (unsigned)start.fw_ver[2], start.fw_size, (unsigned)start.fw_crc16,
           (unsigned)start.chunk_size, (uint32_t)SDK_OTA_MAX_FW_SIZE,
           (unsigned)SDK_OTA_CHUNK_MAX, (unsigned)pkt->payload_len);
  if (start.company_id != WELD_DEVICE_COMPANY_ID) {
    ESP_LOGW(TAG, "OTA START reject: company mismatch got=0x%04X expected=0x%04X",
             (unsigned)start.company_id, (unsigned)WELD_DEVICE_COMPANY_ID);
    build_ota_ack(0, ack);
    send_result(conn_handle, CMD_OTA_START_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_COMPANY, ack, sizeof(ack));
    return;
  }
  if (start.fw_type != WELD_FW_TYPE_BLE) {
    ESP_LOGW(TAG, "OTA START reject: type mismatch got=%u expected=%u",
             (unsigned)start.fw_type, (unsigned)WELD_FW_TYPE_BLE);
    build_ota_ack(0, ack);
    send_result(conn_handle, CMD_OTA_START_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_TYPE, ack, sizeof(ack));
    return;
  }
  if (start.fw_size == 0 || start.fw_size > SDK_OTA_MAX_FW_SIZE ||
      start.chunk_size == 0 || start.chunk_size > SDK_OTA_CHUNK_MAX) {
    ESP_LOGW(TAG,
             "OTA START reject: invalid size/chunk fw_size=%" PRIu32
             " max_fw=%" PRIu32 " chunk=%u max_chunk=%u",
             start.fw_size, (uint32_t)SDK_OTA_MAX_FW_SIZE,
             (unsigned)start.chunk_size, (unsigned)SDK_OTA_CHUNK_MAX);
    build_ota_ack(0, ack);
    send_result(conn_handle, CMD_OTA_START_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, ack, sizeof(ack));
    return;
  }

  s_ota = (weld_ota_state_t){
      .active = true,
      .fw_size = start.fw_size,
      .fw_crc16 = start.fw_crc16,
      .chunk_size = start.chunk_size,
      .next_chunk = 0,
      .received_size = 0,
      .crc16 = 0xFFFF,
      .updated_us = esp_timer_get_time(),
  };
  build_ota_ack(0, ack);
  send_result(conn_handle, CMD_OTA_START_ACK, pkt->seq, SDK_RESULT_STATUS_OK,
              OTA_CODE_NONE, ack, sizeof(ack));
}

static void handle_ota_data(uint16_t conn_handle, const sdk_packet_t *pkt) {
  uint8_t ack[OTA_ACK_DATA_PAYLOAD_SIZE];
  if (ota_expired()) clear_ota_state("timeout-before-data");
  if (!s_ota.active) {
    build_ota_ack(0, ack);
    send_result(conn_handle, CMD_OTA_DATA_ACK, pkt->seq,
                SDK_RESULT_STATUS_BUSY, OTA_CODE_BUSY, ack, sizeof(ack));
    return;
  }

  ota_data_hdr_t header;
  if (!ota_data_hdr_unpack(pkt->payload, pkt->payload_len, &header) ||
      pkt->payload_len < OTA_DATA_HDR_PAYLOAD_SIZE + header.data_len) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_DATA_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, ack, sizeof(ack));
    return;
  }
  if (header.chunk_index != s_ota.next_chunk) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_DATA_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SEQ, ack, sizeof(ack));
    return;
  }
  if (header.data_len == 0 || header.data_len > s_ota.chunk_size ||
      s_ota.received_size + header.data_len > s_ota.fw_size) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_DATA_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, ack, sizeof(ack));
    return;
  }

  const uint8_t *data = pkt->payload + OTA_DATA_HDR_PAYLOAD_SIZE;
  s_ota.crc16 = crc16_ccitt_false_update(s_ota.crc16, data, header.data_len);
  s_ota.received_size += header.data_len;
  s_ota.next_chunk++;
  s_ota.updated_us = esp_timer_get_time();
  build_ota_ack(s_ota.next_chunk, ack);
  send_result(conn_handle, CMD_OTA_DATA_ACK, pkt->seq, SDK_RESULT_STATUS_OK,
              OTA_CODE_NONE, ack, sizeof(ack));
}

static void handle_ota_verify(uint16_t conn_handle, const sdk_packet_t *pkt) {
  uint8_t ack[OTA_ACK_DATA_PAYLOAD_SIZE];
  if (ota_expired()) clear_ota_state("timeout-before-verify");

  ota_verify_t verify;
  if (!s_ota.active ||
      !ota_verify_unpack(pkt->payload, pkt->payload_len, &verify)) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_VERIFY_ACK, pkt->seq,
                SDK_RESULT_STATUS_BUSY, OTA_CODE_BUSY, ack, sizeof(ack));
    return;
  }
  if (verify.fw_size != s_ota.fw_size ||
      s_ota.received_size != s_ota.fw_size) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_VERIFY_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_SIZE, ack, sizeof(ack));
    return;
  }
  if (verify.fw_crc16 != s_ota.fw_crc16 ||
      s_ota.crc16 != s_ota.fw_crc16) {
    build_ota_ack(s_ota.next_chunk, ack);
    send_result(conn_handle, CMD_OTA_VERIFY_ACK, pkt->seq,
                SDK_RESULT_STATUS_FAIL, OTA_CODE_CRC, ack, sizeof(ack));
    return;
  }

  build_ota_ack(s_ota.next_chunk, ack);
  clear_ota_state("verify-ok");
  send_result(conn_handle, CMD_OTA_VERIFY_ACK, pkt->seq, SDK_RESULT_STATUS_OK,
              OTA_CODE_NONE, ack, sizeof(ack));
}

static void handle_ota_abort(uint16_t conn_handle, uint8_t seq) {
  uint8_t ack[OTA_ACK_DATA_PAYLOAD_SIZE];
  clear_ota_state("abort");
  build_ota_ack(0, ack);
  send_result(conn_handle, CMD_OTA_ABORT_ACK, seq, SDK_RESULT_STATUS_OK,
              OTA_CODE_NONE, ack, sizeof(ack));
}

static void handle_packet(uint16_t conn_handle, sdk_packet_t *pkt) {
  ESP_LOGI(TAG, "RX cmd=0x%02X seq=%u len=%u",
           pkt->cmd, pkt->seq, pkt->payload_len);

  if (!is_auth_free_command(pkt->cmd) && !is_authenticated_conn(conn_handle)) {
    send_auth_required(conn_handle, pkt);
    return;
  }

  switch (pkt->cmd) {
    case CMD_DEVICE_GET_INFO:
      handle_device_get_info(conn_handle, pkt->seq);
      break;
    case CMD_DEVICE_PAIR_REQUEST:
      handle_pair_request(conn_handle, pkt);
      break;
    case CMD_DEVICE_AUTH:
      handle_auth(conn_handle, pkt);
      break;
    case CMD_DEVICE_PAIR_UNPAIR:
      if (s_active_token_valid) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(remove_bonded_token(s_active_token));
      }
      s_active_token_valid = false;
      memset(s_active_token, 0, sizeof(s_active_token));
      s_authenticated = false;
      s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      send_result(conn_handle, CMD_DEVICE_PAIR_UNPAIR_ACK, pkt->seq,
                  SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
      schedule_disconnect_after(conn_handle, 500);
      break;
    case CMD_PING:
      send_result(conn_handle, CMD_PONG, pkt->seq, SDK_RESULT_STATUS_OK,
                  SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
      break;
    case CMD_DEVICE_RESET:
      send_result(conn_handle, CMD_DEVICE_RESET_ACK, pkt->seq,
                  SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
      break;
    case CMD_SETTINGS_READ_CURRENT:
      handle_settings_read_current(conn_handle, pkt->seq);
      break;
    case CMD_SETTINGS_READ_SELF_CHECK:
      handle_settings_read_self_check(conn_handle, pkt->seq);
      break;
    case CMD_SETTINGS_PROFILE:
      handle_settings_profile(conn_handle, pkt);
      break;
    case CMD_SETTINGS_RESET:
      handle_settings_reset(conn_handle, pkt);
      break;
    case CMD_SETTINGS_QUICK_SET:
      handle_settings_quick_set(conn_handle, pkt);
      break;
    case CMD_READ_DASHBOARD_INIT:
      handle_dashboard_init(conn_handle, pkt);
      break;
    case CMD_DASHBOARD_START:
      send_result(conn_handle, CMD_DASHBOARD_START_ACK, pkt->seq,
                  SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
      start_dashboard_stream(conn_handle);
      break;
    case CMD_DASHBOARD_STOP:
      stop_dashboard_stream();
      send_result(conn_handle, CMD_DASHBOARD_STOP_ACK, pkt->seq,
                  SDK_RESULT_STATUS_OK, SDK_RESULT_CODE_COMMON_NONE, NULL, 0);
      break;
    case CMD_MANUAL_TRIGGER:
      send_result(conn_handle, CMD_MANUAL_TRIGGER_ACK, pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE,
                  NULL, 0);
      break;
    case CMD_SAFE_DISCHARGE:
      send_result(conn_handle, CMD_SAFE_DISCHARGE_ACK, pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE,
                  NULL, 0);
      break;
    case CMD_SAFE_DISCHARGE_STOP:
      send_result(conn_handle, CMD_SAFE_DISCHARGE_STOP_ACK, pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE,
                  NULL, 0);
      break;
    case CMD_CHARGE_START:
      send_result(conn_handle, CMD_CHARGE_START_ACK, pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE,
                  NULL, 0);
      break;
    case CMD_CHARGE_PAUSE:
      send_result(conn_handle, CMD_CHARGE_PAUSE_ACK, pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED, SDK_RESULT_CODE_COMMON_NONE,
                  NULL, 0);
      break;
    case CMD_OTA_START:
      handle_ota_start(conn_handle, pkt);
      break;
    case CMD_OTA_DATA:
      handle_ota_data(conn_handle, pkt);
      break;
    case CMD_OTA_VERIFY:
      handle_ota_verify(conn_handle, pkt);
      break;
    case CMD_OTA_ABORT:
      handle_ota_abort(conn_handle, pkt->seq);
      break;
    default:
      send_result(conn_handle, (uint8_t)(pkt->cmd | 0x80), pkt->seq,
                  SDK_RESULT_STATUS_NOT_SUPPORTED,
                  SDK_RESULT_CODE_COMMON_UNKNOWN, NULL, 0);
      break;
  }
}

static void handle_rx_bytes(uint16_t conn_handle, const uint8_t *data,
                            uint16_t len) {
  if (!data || len == 0) return;
  if (!s_sdk_dev) {
    ESP_LOGW(TAG, "RX dropped: SDK device is not ready");
    return;
  }
  reset_stale_sdk_parser_if_needed("before-rx");

  ESP_LOGI(TAG, "RX data len=%u", len);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

  uint16_t offset = 0;
  while (offset < len) {
    sdk_decode_result_t result =
        sdk_decode(s_sdk_dev, data + offset, (uint16_t)(len - offset));
    if (result.kind == SDK_DECODE_PACKET) {
      s_decode_wait_started_us = 0;
      handle_packet(conn_handle, &result.packet);
      offset = (uint16_t)(offset + result.consumed);
    } else if (result.kind == SDK_DECODE_ERROR) {
      s_decode_wait_started_us = 0;
      ESP_LOGW(TAG, "decode error consumed=%u", result.consumed);
      offset = (uint16_t)(offset + (result.consumed ? result.consumed : 1));
    } else {
      if (s_decode_wait_started_us == 0) {
        s_decode_wait_started_us = esp_timer_get_time();
      }
      reset_stale_sdk_parser_if_needed("after-rx");
      break;
    }
  }
}

static void module2_transport_rx_cb(const uint8_t *data, size_t len,
                                    void *ctx) {
  (void)ctx;
  if (!data || len == 0) return;
  if (len > UINT16_MAX) {
    ESP_LOGW(TAG, "module2 RX chunk too large len=%u", (unsigned)len);
    return;
  }

  if (!is_connected(WELD_MODULE2_CONN_HANDLE)) {
    s_conn_handle = WELD_MODULE2_CONN_HANDLE;
    s_notify_enabled = true;
  }
  handle_rx_bytes(WELD_MODULE2_CONN_HANDLE, data, (uint16_t)len);
}

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "GATT access op=%d attr=%u rx_attr=%u", ctxt->op, attr_handle,
           s_rx_val_handle);
  if (attr_handle != s_rx_val_handle ||
      ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    ESP_LOGW(TAG, "unexpected GATT access op=%d attr=%u", ctxt->op,
             attr_handle);
    return BLE_ATT_ERR_UNLIKELY;
  }

  uint16_t len = os_mbuf_len(ctxt->om);
  if (len == 0 || len > sizeof(s_rx_write_buf)) {
    ESP_LOGW(TAG, "invalid RX write len=%u", len);
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  int rc = ble_hs_mbuf_to_flat(ctxt->om, s_rx_write_buf,
                               sizeof(s_rx_write_buf), &len);
  if (rc != 0) {
    ESP_LOGW(TAG, "mbuf flatten failed rc=%d len=%u", rc, len);
    return BLE_ATT_ERR_UNLIKELY;
  }
  ESP_LOGI(TAG, "RX write len=%u", len);
  handle_rx_bytes(conn_handle, s_rx_write_buf, len);
  return 0;
}

static int gatt_init(void) {
  int rc;
  ble_svc_gap_init();
  ble_svc_gatt_init();

  rc = ble_gatts_count_cfg(s_gatt_svcs);
  if (rc != 0) return rc;
  rc = ble_gatts_add_svcs(s_gatt_svcs);
  if (rc != 0) return rc;
  return 0;
}

static void start_advertising(void) {
  if (ble_gap_adv_active()) return;

  struct ble_hs_adv_fields fields = {0};
  const char *name = ble_svc_gap_device_name();
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t *)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;
  fields.appearance = 0x14C0;
  fields.appearance_is_present = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "set adv fields failed rc=%d", rc);
    return;
  }

  struct ble_hs_adv_fields rsp_fields = {0};
  rsp_fields.uuids128 = &s_svc_uuid;
  rsp_fields.num_uuids128 = 1;
  rsp_fields.uuids128_is_complete = 0;
  rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "set scan rsp failed rc=%d", rc);
    return;
  }

  struct ble_gap_adv_params params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
      .itvl_min = BLE_GAP_ADV_ITVL_MS(120),
      .itvl_max = BLE_GAP_ADV_ITVL_MS(150),
  };
  rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                         gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "start advertising failed rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "advertising as %s", name);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        if (rc == 0) {
          recreate_sdk_device(desc.peer_id_addr.val);
        }
        s_conn_handle = event->connect.conn_handle;
        reset_runtime_state();
        cancel_task(&s_pairing_timeout_task);
        ESP_LOGI(TAG, "connected handle=%u", s_conn_handle);
      } else {
        ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
        start_advertising();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "disconnect reason=%d", event->disconnect.reason);
      stop_dashboard_stream();
      clear_ota_state("disconnect");
      reset_sdk_parser("disconnect");
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      reset_runtime_state();
      start_advertising();
      if (s_pairing_mode) {
        set_pairing_mode(true);
      } else {
        cancel_task(&s_confirm_timeout_task);
        s_waiting_confirm_conn = BLE_HS_CONN_HANDLE_NONE;
        s_waiting_confirm_seq = 0;
        status_led_off();
      }
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "advertise complete reason=%d", event->adv_complete.reason);
      start_advertising();
      return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == s_tx_val_handle) {
        s_notify_enabled = event->subscribe.cur_notify;
        ESP_LOGI(TAG, "notify %s", s_notify_enabled ? "enabled" : "disabled");
      }
      return 0;
    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "mtu conn=%u mtu=%u", event->mtu.conn_handle,
               event->mtu.value);
      return 0;
    case BLE_GAP_EVENT_NOTIFY_TX:
      if (event->notify_tx.status != 0 &&
          event->notify_tx.status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "notify tx status=%d", event->notify_tx.status);
      }
      return 0;
    default:
      return 0;
  }
}

static void on_reset(int reason) {
  ESP_LOGE(TAG, "nimble reset reason=%d", reason);
}

static void on_sync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  assert(rc == 0);
  rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
  assert(rc == 0);
  rc = ble_hs_id_copy_addr(s_own_addr_type, s_own_addr, NULL);
  assert(rc == 0);
  recreate_sdk_device(s_own_addr);
  start_advertising();
}

static void host_task(void *param) {
  (void)param;
  ESP_LOGI(TAG, "NimBLE host task started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void app_main(void) {
  if (s_runtime_mode == WELD_RUNTIME_MODULE_AT_TEST) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(ble_module_at_test_run());
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  feature_mask_set(SDK_FEATURE_SETTINGS_CHARGE_TARGET_VOLTAGE |
                   SDK_FEATURE_SETTINGS_CHARGE_TARGET_CURRENT |
                   SDK_FEATURE_SETTINGS_SINGLE_CAP_VOLTAGE_LIMIT |
                   SDK_FEATURE_SETTINGS_TRIGGER_MODE |
                   SDK_FEATURE_DASHBOARD_CHARGE_CURRENT |
                   SDK_FEATURE_DASHBOARD_WELD_CURRENT |
                   SDK_FEATURE_DASHBOARD_CAPACITOR_TEMPERATURE |
                   SDK_FEATURE_DASHBOARD_MOS_TEMPERATURE |
                   SDK_FEATURE_DASHBOARD_LOGS |
                   SDK_FEATURE_MAINTENANCE_FIRMWARE_UPDATE |
                   SDK_FEATURE_MAINTENANCE_FACTORY_RESET |
                   SDK_FEATURE_DIAGNOSTIC_ESR_SELF_CHECK |
                   SDK_FEATURE_DIAGNOSTIC_FAULT_LOG_READ);
  reset_settings_state();
  ESP_ERROR_CHECK_WITHOUT_ABORT(load_tokens());
  ESP_LOGI(TAG, "bonded token count=%u", s_bonded_token_count);

  s_tx_mutex = xSemaphoreCreateMutex();
  assert(s_tx_mutex != NULL);
  if (is_module2_transport_mode() &&
      ble_module2_transport_uses_status_led_gpio()) {
    ESP_LOGW(TAG, "status LED disabled: external BLE module uses GPIO%d",
             WELD_STATUS_LED_GPIO);
  } else {
    init_status_led();
  }

  if (is_module2_transport_mode()) {
    esp_err_t mac_err = esp_read_mac(s_own_addr, ESP_MAC_BT);
    if (mac_err != ESP_OK) {
      ESP_LOGW(TAG, "read BT MAC failed: %s, fallback to base MAC",
               esp_err_to_name(mac_err));
      ESP_ERROR_CHECK(esp_read_mac(s_own_addr, ESP_MAC_BASE));
    }
    recreate_sdk_device(s_own_addr);
    s_conn_handle = WELD_MODULE2_CONN_HANDLE;
    s_notify_enabled = true;
  }

  init_boot_button();

  if (is_module2_transport_mode()) {
    ESP_LOGW(TAG, "module2 transport mode: ESP NimBLE is disabled");
    esp_err_t err =
        ble_module2_transport_start(module2_transport_rx_cb, NULL);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "module2 transport start failed: %s", esp_err_to_name(err));
      return;
    }
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
    return;
  }

  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  int rc = gatt_init();
  assert(rc == 0);
  rc = ble_svc_gap_device_name_set(WELD_DEVICE_NAME);
  assert(rc == 0);
  ble_store_config_init();

  nimble_port_freertos_init(host_task);
}
