#include "ble_module_transport.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdk_codec.h"

#define BLE_MODULE2_UART UART_NUM_1
#define BLE_MODULE2_BAUD 115200
#define BLE_MODULE2_TX_GPIO 35
#define BLE_MODULE2_RX_GPIO 36
#define BLE_MODULE2_RX_BUF_SIZE 1024
#define BLE_MODULE2_CMD_TIMEOUT_MS 900u
#define BLE_MODULE2_RX_TASK_STACK 12288u

#define BLE_MODULE_TRANSPORT_TARGET_MODULE1 1
#define BLE_MODULE_TRANSPORT_TARGET_MODULE2 2
#define BLE_MODULE_TRANSPORT_TARGET_MODULE3 3
#define BLE_MODULE_TRANSPORT_TARGET BLE_MODULE_TRANSPORT_TARGET_MODULE1

#define BLE_MODULE1_TX_GPIO 42
#define BLE_MODULE1_RX_GPIO 41
#define BLE_MODULE1_RST_GPIO 40

#define BLE_MODULE3_TX_GPIO 48
#define BLE_MODULE3_RX_GPIO 47

#define BLE_MODULE3_EXTRA_UUID16 "F605"

static const char *TAG = "ble_module_transport";

typedef struct {
  const char *command;
  uint32_t settle_ms;
} module2_at_command_t;

typedef struct {
  const char *name;
  const char *serial_suffix;
  int tx_gpio;
  int rx_gpio;
  int rst_gpio;
  const module2_at_command_t *commands;
  size_t command_count;
} module_transport_config_t;

typedef struct {
  ble_module_transport_rx_cb_t rx_cb;
  void *rx_ctx;
  TaskHandle_t rx_task;
  bool started;
} module2_transport_state_t;

static module2_transport_state_t s_transport;

static const module2_at_command_t s_module1_init_commands[] = {
    {.command = "AT+CMD", .settle_ms = 300},
    {.command = "AT+MASTER=01", .settle_ms = 1800},
    {.command = "AT+NAMB=Weld-MD1", .settle_ms = 1800},
    {.command = "AT+UUID=" SDK_SVC_UUID16, .settle_ms = 1800},
    {.command = "AT+CHAR=" SDK_CHR_TX_UUID16, .settle_ms = 1800},
    {.command = "AT+WRITE=" SDK_CHR_RX_UUID16, .settle_ms = 1800},
    {.command = "AT+RESET", .settle_ms = 2200},
    {.command = "AT+MASTER", .settle_ms = 300},
    {.command = "AT+NAMB", .settle_ms = 300},
    {.command = "AT+UUID", .settle_ms = 300},
    {.command = "AT+CHAR", .settle_ms = 300},
    {.command = "AT+WRITE", .settle_ms = 300},
};

static const module2_at_command_t s_module2_init_commands[] = {
    {.command = "AT+UUIDS=" SDK_SVC_UUID16, .settle_ms = 500},
    {.command = "AT+UUIDN=" SDK_CHR_TX_UUID16, .settle_ms = 500},
    {.command = "AT+UUIDW=" SDK_CHR_RX_UUID16, .settle_ms = 500},
    {.command = "AT+NAME=Weld-MD2", .settle_ms = 500},
    {.command = "AT+TXPOWER=0", .settle_ms = 500},
    {.command = "AT+REBOOT=1", .settle_ms = 1800},
    {.command = "AT+TXPOWER?", .settle_ms = 300},
    {.command = "AT+UUIDS?", .settle_ms = 300},
    {.command = "AT+UUIDN?", .settle_ms = 300},
    {.command = "AT+UUIDW?", .settle_ms = 300},
};

static const module2_at_command_t s_module3_init_commands[] = {
    {.command = "AT", .settle_ms = 200},
    {.command = "AT+MODE=0", .settle_ms = 500},
    {.command = "AT+NAME=Weld-MD3", .settle_ms = 500},
    {.command = "AT+UUID=" SDK_SVC_UUID16 ","
                BLE_MODULE3_EXTRA_UUID16 "," SDK_CHR_TX_UUID16 ","
                SDK_CHR_RX_UUID16,
     .settle_ms = 500},
    {.command = "AT+RESET", .settle_ms = 2200},
    {.command = "AT+CONNMODE?", .settle_ms = 300},
    {.command = "AT+CONNMODE=0", .settle_ms = 2500},
    {.command = "AT+CONNMODE?", .settle_ms = 300},
    {.command = "AT+ADVDATA?", .settle_ms = 300},
    {.command = "AT+SRDATA?", .settle_ms = 300},
};

#define ARRAY_SIZE(v) (sizeof(v) / sizeof((v)[0]))

static const module_transport_config_t s_module1_config = {
    .name = "module1 UM1AT",
    .serial_suffix = "M1",
    .tx_gpio = BLE_MODULE1_TX_GPIO,
    .rx_gpio = BLE_MODULE1_RX_GPIO,
    .rst_gpio = BLE_MODULE1_RST_GPIO,
    .commands = s_module1_init_commands,
    .command_count = ARRAY_SIZE(s_module1_init_commands),
};

static const module_transport_config_t s_module2_config = {
    .name = "module2 MX-01P",
    .serial_suffix = "M2",
    .tx_gpio = BLE_MODULE2_TX_GPIO,
    .rx_gpio = BLE_MODULE2_RX_GPIO,
    .rst_gpio = -1,
    .commands = s_module2_init_commands,
    .command_count = ARRAY_SIZE(s_module2_init_commands),
};

static const module_transport_config_t s_module3_config = {
    .name = "module3",
    .serial_suffix = "M3",
    .tx_gpio = BLE_MODULE3_TX_GPIO,
    .rx_gpio = BLE_MODULE3_RX_GPIO,
    .rst_gpio = -1,
    .commands = s_module3_init_commands,
    .command_count = ARRAY_SIZE(s_module3_init_commands),
};

static const module_transport_config_t *active_config(void) {
  if (BLE_MODULE_TRANSPORT_TARGET == BLE_MODULE_TRANSPORT_TARGET_MODULE1) {
    return &s_module1_config;
  }
  if (BLE_MODULE_TRANSPORT_TARGET == BLE_MODULE_TRANSPORT_TARGET_MODULE3) {
    return &s_module3_config;
  }
  return &s_module2_config;
}

static void log_at_response(const uint8_t *data, int len) {
  char line[BLE_MODULE2_RX_BUF_SIZE + 1];
  int out = 0;
  for (int i = 0; i < len && out < (int)sizeof(line) - 1; i++) {
    uint8_t ch = data[i];
    if (ch == '\r') {
      if (out < (int)sizeof(line) - 2) {
        line[out++] = '\\';
        line[out++] = 'r';
      }
    } else if (ch == '\n') {
      if (out < (int)sizeof(line) - 2) {
        line[out++] = '\\';
        line[out++] = 'n';
      }
    } else if (ch >= 0x20 && ch <= 0x7E) {
      line[out++] = (char)ch;
    } else if (out < (int)sizeof(line) - 4) {
      out += snprintf(line + out, sizeof(line) - out, "\\x%02X", ch);
    }
  }
  line[out] = '\0';
  ESP_LOGI(TAG, "AT RX %s", line);
}

static bool read_at_response(uint32_t timeout_ms) {
  uint8_t data[BLE_MODULE2_RX_BUF_SIZE];
  int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;
  bool received = false;

  while (esp_timer_get_time() < deadline_us) {
    int len = uart_read_bytes(BLE_MODULE2_UART, data, sizeof(data),
                              pdMS_TO_TICKS(80));
    if (len > 0) {
      received = true;
      log_at_response(data, len);
    }
  }
  return received;
}

static esp_err_t send_at_command(const module2_at_command_t *command) {
  static const char line_ending[] = "\r\n";

  ESP_LOGI(TAG, "AT TX %s", command->command);
  uart_flush_input(BLE_MODULE2_UART);

  int written = uart_write_bytes(BLE_MODULE2_UART, command->command,
                                 strlen(command->command));
  if (written < 0) return ESP_FAIL;
  written = uart_write_bytes(BLE_MODULE2_UART, line_ending,
                             strlen(line_ending));
  if (written < 0) return ESP_FAIL;

  esp_err_t err = uart_wait_tx_done(BLE_MODULE2_UART, pdMS_TO_TICKS(300));
  if (err != ESP_OK) return err;

  bool received = read_at_response(BLE_MODULE2_CMD_TIMEOUT_MS);
  if (!received) {
    ESP_LOGW(TAG, "no response for %s", command->command);
  }
  vTaskDelay(pdMS_TO_TICKS(command->settle_ms));
  return ESP_OK;
}

static esp_err_t configure_uart(const module_transport_config_t *config) {
  const uart_config_t uart_config = {
      .baud_rate = BLE_MODULE2_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_param_config(BLE_MODULE2_UART, &uart_config);
  if (err != ESP_OK) return err;

  err = uart_set_pin(BLE_MODULE2_UART, config->tx_gpio,
                     config->rx_gpio, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE);
  if (err != ESP_OK) return err;

  uart_flush_input(BLE_MODULE2_UART);
  ESP_LOGI(TAG, "%s UART%d tx=%d rx=%d baud=%d", config->name,
           BLE_MODULE2_UART, config->tx_gpio, config->rx_gpio,
           BLE_MODULE2_BAUD);
  return ESP_OK;
}

static void reset_module(const module_transport_config_t *config) {
  if (config->rst_gpio < 0) return;

  gpio_reset_pin((gpio_num_t)config->rst_gpio);
  gpio_set_direction((gpio_num_t)config->rst_gpio, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)config->rst_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level((gpio_num_t)config->rst_gpio, 1);
  ESP_LOGI(TAG, "%s reset gpio=%d", config->name, config->rst_gpio);
  read_at_response(1800);
}

static void rx_task(void *arg) {
  (void)arg;
  uint8_t data[BLE_MODULE2_RX_BUF_SIZE];

  while (true) {
    int len = uart_read_bytes(BLE_MODULE2_UART, data, sizeof(data),
                              pdMS_TO_TICKS(100));
    if (len <= 0) continue;

    ESP_LOGI(TAG, "UART RX data len=%d", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    int sdk_offset = -1;
    for (int i = 0; i < len; i++) {
      if (data[i] == SDK_SOF) {
        sdk_offset = i;
        break;
      }
    }

    if (sdk_offset < 0) {
      log_at_response(data, len);
      continue;
    }

    if (sdk_offset > 0) {
      ESP_LOGI(TAG, "module event before SDK frame len=%d", sdk_offset);
      log_at_response(data, sdk_offset);
    }

    if (s_transport.rx_cb) {
      s_transport.rx_cb(data + sdk_offset, (size_t)(len - sdk_offset),
                        s_transport.rx_ctx);
    }
  }
}

esp_err_t ble_module2_transport_start(ble_module_transport_rx_cb_t rx_cb,
                                      void *ctx) {
  if (s_transport.started) return ESP_OK;
  const module_transport_config_t *config = active_config();

  uart_driver_delete(BLE_MODULE2_UART);
  esp_err_t err = uart_driver_install(BLE_MODULE2_UART,
                                      BLE_MODULE2_RX_BUF_SIZE * 2, 0, 0,
                                      NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
    return err;
  }

  err = configure_uart(config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART config failed: %s", esp_err_to_name(err));
    return err;
  }

  reset_module(config);

  for (size_t i = 0; i < config->command_count; i++) {
    err = send_at_command(&config->commands[i]);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "AT command failed: %s", esp_err_to_name(err));
      return err;
    }
  }

  uart_flush_input(BLE_MODULE2_UART);
  s_transport.rx_cb = rx_cb;
  s_transport.rx_ctx = ctx;

  BaseType_t ok = xTaskCreate(rx_task, "ble_mod2_rx",
                              BLE_MODULE2_RX_TASK_STACK, NULL, 5,
                              &s_transport.rx_task);
  if (ok != pdPASS) {
    s_transport.rx_task = NULL;
    ESP_LOGE(TAG, "failed to start RX task");
    return ESP_FAIL;
  }

  s_transport.started = true;
  ESP_LOGW(TAG, "%s UART transparent transport started", config->name);
  return ESP_OK;
}

esp_err_t ble_module2_transport_send(const uint8_t *data, size_t len) {
  if (!s_transport.started || !data || len == 0) return ESP_ERR_INVALID_STATE;

  ESP_LOGI(TAG, "UART TX data len=%u", (unsigned)len);
  ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

  int written = uart_write_bytes(BLE_MODULE2_UART, data, len);
  if (written < 0 || (size_t)written != len) {
    ESP_LOGW(TAG, "UART write failed written=%d len=%u", written, (unsigned)len);
    return ESP_FAIL;
  }

  esp_err_t err = uart_wait_tx_done(BLE_MODULE2_UART, pdMS_TO_TICKS(500));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "UART wait tx failed: %s", esp_err_to_name(err));
  }
  return err;
}

const char *ble_module2_transport_serial_suffix(void) {
  return active_config()->serial_suffix;
}

bool ble_module2_transport_uses_status_led_gpio(void) {
  const module_transport_config_t *config = active_config();
  return config->tx_gpio == 48 || config->rx_gpio == 48 ||
         config->rst_gpio == 48;
}
