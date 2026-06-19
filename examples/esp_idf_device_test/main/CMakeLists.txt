idf_component_register(
    SRCS "main.c" "ble_module_at_test.c" "ble_module_transport.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_timer esp_driver_gpio esp_driver_uart led_strip weld_sdk
)
