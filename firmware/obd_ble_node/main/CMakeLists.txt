idf_component_register(SRCS "app_main.c" "node_core.c" "node_ble.c" "node_capture.c" "node_power.c"
    INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_driver_gpio driver
    esp_adc esp_timer esp_driver_usb_serial_jtag spiffs esp_hw_support)
