idf_component_register(
    SRCS
        "app_main.c"
        "obd2.c"
        "obd2_decode.c"
        "elm327_protocol.c"
        "elm327_ble.c"
        "ble_devices.c"
        "ui.c"
        "lvgl_memory.c"
        "audio_device.c"
        "media_model.c"
        "rda5807.c"
        "fm_service.c"
        "media_panel.c"
        "media_service.c"
        "media_library.c"
        "photo_bmp.c"
        "dashboard.c"
        "gps_model.c"
        "gps_service.c"
        "map_model.c"
        "map_render.c"
        "map_geometry.c"
        "map_panel.c"
        "ui_assets.c"
        "service_panel.c"
        "device_services.c"
        "network_service.c"
        "network_retry.c"
        "wifi_networks.c"
        "device_console.c"
        "voice_service.c"
        "voice_tools.c"
        "vehicle_metrics.c"
        "alarm_model.c"
        "alarm_service.c"
        "xiaozhi_avatar.c"
        "voice_codec_memory.c"
        "voice_frontend.c"
        "camera_service.c"
        "recorder.c"
        "record_storage.c"
        "sdmmc_guard.c"
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
        esp_driver_uart
        esp_event
        esp_lcd
        esp_timer
        esp_driver_i2c
        esp_driver_i2s
        lvgl
        nvs_flash
        esp32_p4_function_ev_board
        esp_wifi_remote
        esp_xiaozhi
        mcp-c-sdk
        esp-sr
        esp_audio_codec
        esp_audio_effects
        esp_codec_dev
        esp_video
        esp_netif
        esp_http_client
        app_update
        mbedtls
        cjson
        console
        fatfs
        sdmmc
        esp_driver_sdmmc
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    get_filename_component(REPO_DIR "${PROJECT_DIR}" DIRECTORY)
    idf_component_get_property(sr_dir espressif__esp-sr COMPONENT_DIR)
    file(GLOB voice_model_files
        "${sr_dir}/model/wakenet_model/wn9_nihaoxiaozhi_tts/*"
        "${sr_dir}/model/vadnet_model/vadnet1_medium/*")
    set(voice_models "${CMAKE_CURRENT_BINARY_DIR}/voice_models.bin")
    add_custom_command(OUTPUT "${voice_models}"
        COMMAND ${PYTHON} "${REPO_DIR}/tools/pack_voice_models.py" "${sr_dir}" "${voice_models}"
        DEPENDS "${REPO_DIR}/tools/pack_voice_models.py" ${voice_model_files}
        VERBATIM)
    add_custom_target(voice_models_bin DEPENDS "${voice_models}")
    add_dependencies(${COMPONENT_LIB} voice_models_bin)
    target_add_binary_data(${COMPONENT_LIB} "${voice_models}" BINARY)
endif()

if(CONFIG_LV_USE_CUSTOM_MALLOC)
    # LVGL calls these hooks from another archive; retain the project's allocator.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_mem_init")
endif()

# SD hotplug reconfigures slot 0 while Hosted uses slot 1. Serialize driver
# configuration and transactions without changing the shared ESP-IDF sources.
foreach(sdmmc_function
    init init_slot deinit_slot deinit do_transaction set_card_clk set_bus_width
    set_bus_ddr_mode set_input_delay set_cclk_always_on io_int_enable
    get_real_freq is_slot_set_to_uhs1 get_state get_slot_width)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_host_${sdmmc_function}")
endforeach()
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_init_host_frequency")

if(CONFIG_DEVICE_TRUST_DIR)
    foreach(asset service_ca.pem ota_public.pem)
        if(NOT EXISTS "${CONFIG_DEVICE_TRUST_DIR}/${asset}")
            message(FATAL_ERROR "Missing public trust file: ${CONFIG_DEVICE_TRUST_DIR}/${asset}")
        endif()
        target_add_binary_data(${COMPONENT_LIB} "${CONFIG_DEVICE_TRUST_DIR}/${asset}" TEXT)
    endforeach()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DEVICE_CUSTOM_TRUST=1)
endif()
