idf_component_register(
    SRCS
        "app_main.c"
        "obd2.c"
        "obd2_decode.c"
        "elm327_protocol.c"
        "elm327_ble.c"
        "ui.c"
        "lvgl_memory.c"
        "audio_device.c"
        "media_model.c"
        "media_panel.c"
        "media_service.c"
        "dashboard.c"
        "service_panel.c"
        "device_services.c"
        "network_service.c"
        "wifi_networks.c"
        "device_console.c"
        "voice_service.c"
        "camera_service.c"
        "recorder.c"
        "cloud_service.c"
        "ota_policy.c"
        "fonts/dashboard_text_18.c"
        "fonts/dashboard_text_22.c"
        "fonts/dashboard_wifi_18.c"
        "fonts/dashboard_digits_144.c"
        "fonts/dashboard_digits_72.c"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        bt
        esp_driver_gpio
        esp_event
        esp_lcd
        esp_timer
        esp_driver_i2c
        lvgl
        nvs_flash
        esp32_p4_function_ev_board
        esp_wifi_remote
        esp_xiaozhi
        esp_audio_codec
        esp_codec_dev
        esp_video
        esp_netif
        esp_http_client
        app_update
        mbedtls
        cjson
        console
)

if(CONFIG_LV_USE_CUSTOM_MALLOC)
    # LVGL calls these hooks from another archive; retain the project's allocator.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init")
endif()

if(CONFIG_DEVICE_TRUST_DIR)
    foreach(asset service_ca.pem ota_public.pem)
        if(NOT EXISTS "${CONFIG_DEVICE_TRUST_DIR}/${asset}")
            message(FATAL_ERROR "Missing public trust file: ${CONFIG_DEVICE_TRUST_DIR}/${asset}")
        endif()
        target_add_binary_data(${COMPONENT_LIB} "${CONFIG_DEVICE_TRUST_DIR}/${asset}" TEXT)
    endforeach()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_CUSTOM_TRUST=1)
endif()
