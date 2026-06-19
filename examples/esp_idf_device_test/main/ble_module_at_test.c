#include "ble_module_at_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdk_codec.h"

#define WELD_BT_MODULE_TEST_UART UART_NUM_1
#define WELD_BT_MODULE_TEST_BAUD 115200
#define WELD_BT_MODULE_RX_BUF_SIZE 1024
#define WELD_BT_MODULE_CMD_TIMEOUT_MS 800u

#define WELD_COMPAT_EXTRA_UUID16 "F605"

static const char *TAG = "ble_module_at_test";

typedef struct {
  const char *command;
  uint32_t settle_ms;
} at_command_t;

typedef struct {
  const char *name;
  bool enabled;
  int tx_gpio;
  int rx_gpio;
  int rst_gpio;
  const at_command_t *commands;
  size_t command_count;
} module_spec_t;

#define ARRAY_SIZE(v) (sizeof(v) / sizeof((v)[0]))

static const at_command_t s_module_1_commands[] = {
    {.command = "AT+CMD", .settle_ms = 200},
    {.command = "AT+MASTER=01", .settle_ms = 500},
    {.command = "AT+NAMB=DX32-UM1AT", .settle_ms = 500},
    {.command = "AT+UUID=" SDK_SVC_UUID16, .settle_ms = 500},
    {.command = "AT+CHAR=" SDK_CHR_TX_UUID16, .settle_ms = 500},
    {.command = "AT+WRITE=" SDK_CHR_RX_UUID16, .settle_ms = 500},
    {.command = "AT+RESET", .settle_ms = 1800},
};

static const at_command_t s_module_2_commands[] = {
    {.command = "AT", .settle_ms = 200},
    {.command = "AT+UUIDS=" SDK_SVC_UUID16, .settle_ms = 500},
    {.command = "AT+UUIDN=" SDK_CHR_TX_UUID16, .settle_ms = 500},
    {.command = "AT+UUIDW=" SDK_CHR_RX_UUID16, .settle_ms = 500},
    {.command = "AT+NAME=WeldControl", .settle_ms = 500},
    {.command = "AT+TXPOWER=0", .settle_ms = 500},
    {.command = "AT+REBOOT=1", .settle_ms = 1800},
    {.command = "AT+TXPOWER?", .settle_ms = 300},
    {.command = "AT+UUIDS?", .settle_ms = 300},
    {.command = "AT+UUIDN?", .settle_ms = 300},
    {.command = "AT+UUIDW?", .settle_ms = 300},
};

static const at_command_t s_module_3_commands[] = {
    {.command = "AT", .settle_ms = 200},
    {.command = "AT+MODE=0", .settle_ms = 500},
    {.command = "AT+NAME=DX32-UM3AT", .settle_ms = 500},
    {.command = "AT+UUID=" SDK_SVC_UUID16 ","
                WELD_COMPAT_EXTRA_UUID16 "," SDK_CHR_TX_UUID16 ","
                SDK_CHR_RX_UUID16,
     .settle_ms = 500},
    {.command = "AT+RESET", .settle_ms = 2200},
    {.command = "AT+CONNMODE?", .settle_ms = 300},
    {.command = "AT+CONNMODE=0", .settle_ms = 2500},
    {.command = "AT+CONNMODE?", .settle_ms = 300},
    {.command = "AT+ADVDATA?", .settle_ms = 300},
    {.command = "AT+SRDATA?", .settle_ms = 300},
};

static const module_spec_t s_modules[] = {
    {
        .name = "module1 UM1AT",
        .enabled = true,
        .tx_gpio = 42,
        .rx_gpio = 41,
        .rst_gpio = 40,
        .commands = s_module_1_commands,
        .command_count = ARRAY_SIZE(s_module_1_commands),
    },
    {
        .name = "module2",
        .enabled = true,
        .tx_gpio = 35,
        .rx_gpio = 36,
        .rst_gpio = GPIO_NUM_NC,
        .commands = s_module_2_commands,
        .command_count = ARRAY_SIZE(s_module_2_commands),
    },
    {
        .name = "module3",
        .enabled = true,
        .tx_gpio = 48,
        .rx_gpio = 47,
        .rst_gpio = GPIO_NUM_NC,
        .commands = s_module_3_commands,
        .command_count = ARRAY_SIZE(s_module_3_commands),
    },
};

static void log_uart_response(const module_spec_t *module, const uint8_t *data,
                              int len) {
  char line[WELD_BT_MODULE_RX_BUF_SIZE + 1];
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
  ESP_LOGI(TAG, "[%s] RX %s", module->name, line);
}

static bool read_module_response(const module_spec_t *module, uint32_t timeout_ms) {
  uint8_t data[WELD_BT_MODULE_RX_BUF_SIZE];
  int64_t deadline_us = esp_timer_get_time() + (int64_t)timeout_ms * 1000LL;
  bool received = false;

  while (esp_timer_get_time() < deadline_us) {
    int len = uart_read_bytes(WELD_BT_MODULE_TEST_UART, data, sizeof(data),
                              pdMS_TO_TICKS(80));
    if (len > 0) {
      received = true;
      log_uart_response(module, data, len);
    }
  }
  return received;
}

static esp_err_t configure_test_uart(const module_spec_t *module) {
  const uart_config_t uart_config = {
      .baud_rate = WELD_BT_MODULE_TEST_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_param_config(WELD_BT_MODULE_TEST_UART, &uart_config);
  if (err != ESP_OK) return err;

  err = uart_set_pin(WELD_BT_MODULE_TEST_UART, module->tx_gpio, module->rx_gpio,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) return err;

  uart_flush_input(WELD_BT_MODULE_TEST_UART);
  ESP_LOGI(TAG, "[%s] UART%d tx=%d rx=%d baud=%d", module->name,
           WELD_BT_MODULE_TEST_UART, module->tx_gpio, module->rx_gpio,
           WELD_BT_MODULE_TEST_BAUD);
  return ESP_OK;
}

static void pulse_reset_if_present(const module_spec_t *module) {
  if (module->rst_gpio == GPIO_NUM_NC) return;

  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << module->rst_gpio,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&cfg));

  ESP_LOGI(TAG, "[%s] pulse reset gpio=%d", module->name, module->rst_gpio);
  gpio_set_level(module->rst_gpio, 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(module->rst_gpio, 0);
  vTaskDelay(pdMS_TO_TICKS(120));
  gpio_set_level(module->rst_gpio, 1);
  vTaskDelay(pdMS_TO_TICKS(800));
}

static esp_err_t send_at_command(const module_spec_t *module,
                                 const at_command_t *command) {
  static const char line_ending[] = "\r\n";
  ESP_LOGI(TAG, "[%s] TX %s", module->name, command->command);
  uart_flush_input(WELD_BT_MODULE_TEST_UART);

  int written = uart_write_bytes(WELD_BT_MODULE_TEST_UART, command->command,
                                 strlen(command->command));
  if (written < 0) return ESP_FAIL;
  written = uart_write_bytes(WELD_BT_MODULE_TEST_UART, line_ending,
                             strlen(line_ending));
  if (written < 0) return ESP_FAIL;

  esp_err_t err = uart_wait_tx_done(WELD_BT_MODULE_TEST_UART,
                                    pdMS_TO_TICKS(300));
  if (err != ESP_OK) return err;

  bool received = read_module_response(module, WELD_BT_MODULE_CMD_TIMEOUT_MS);
  if (!received) {
    ESP_LOGW(TAG, "[%s] no response for %s", module->name, command->command);
  }
  vTaskDelay(pdMS_TO_TICKS(command->settle_ms));
  return ESP_OK;
}

static esp_err_t run_module_script(const module_spec_t *module) {
  esp_err_t err = configure_test_uart(module);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "[%s] UART config failed: %s", module->name,
             esp_err_to_name(err));
    return err;
  }

  pulse_reset_if_present(module);

  for (size_t i = 0; i < module->command_count; i++) {
    err = send_at_command(module, &module->commands[i]);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "[%s] command failed: %s", module->name,
               esp_err_to_name(err));
      return err;
    }
  }
  ESP_LOGI(TAG, "[%s] script complete", module->name);
  return ESP_OK;
}

esp_err_t ble_module_at_test_run(void) {
  ESP_LOGW(TAG, "external BLE module AT test mode");
  ESP_LOGI(TAG, "compat UUID16 service=%s tx_notify=%s rx_write=%s extra=%s",
           SDK_SVC_UUID16, SDK_CHR_TX_UUID16,
           SDK_CHR_RX_UUID16, WELD_COMPAT_EXTRA_UUID16);

  uart_driver_delete(WELD_BT_MODULE_TEST_UART);
  esp_err_t err = uart_driver_install(WELD_BT_MODULE_TEST_UART,
                                      WELD_BT_MODULE_RX_BUF_SIZE * 2, 0, 0,
                                      NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
    return err;
  }

  esp_err_t result = ESP_OK;
  for (size_t i = 0; i < ARRAY_SIZE(s_modules); i++) {
    if (!s_modules[i].enabled) {
      ESP_LOGW(TAG, "[%s] skipped", s_modules[i].name);
      continue;
    }
    err = run_module_script(&s_modules[i]);
    if (err != ESP_OK && result == ESP_OK) {
      result = err;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  ESP_LOGW(TAG, "external BLE module AT test finished: %s",
           esp_err_to_name(result));
  ESP_LOGW(TAG, "set s_runtime_mode to select NimBLE, AT test, or module2 transport");
  return result;
}
