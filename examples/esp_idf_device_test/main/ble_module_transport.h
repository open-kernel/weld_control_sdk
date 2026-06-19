#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ble_module_transport_rx_cb_t)(const uint8_t *data, size_t len,
                                             void *ctx);

esp_err_t ble_module2_transport_start(ble_module_transport_rx_cb_t rx_cb,
                                      void *ctx);
esp_err_t ble_module2_transport_send(const uint8_t *data, size_t len);
const char *ble_module2_transport_serial_suffix(void);
bool ble_module2_transport_uses_status_led_gpio(void);
