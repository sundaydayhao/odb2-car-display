#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "obd2.h"
#include "ui.h"
#include "device_services.h"
#include "recorder.h"
#include "cloud_service.h"
#include "voice_service.h"
#include "camera_service.h"
#include "media_service.h"
#include "media_library.h"

static const char *TAG = "app";

static void transport_task(void *arg)
{
    (void)arg;
    esp_err_t err = obd2_init();
    if (err != ESP_OK) ESP_LOGE(TAG, "Vehicle radio unavailable: %s", esp_err_to_name(err));
    /* Both stacks configure the same Hosted SDIO bus; finish BLE setup first. */
    err = device_network_start();
    if (err != ESP_OK) ESP_LOGE(TAG, "Network task unavailable: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
}

static void verify_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        bool healthy = true;
        for (int i = 0; i < 30; ++i) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (!ui_healthy() || heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 16384 ||
                !heap_caps_check_integrity_all(false)) healthy = false;
        }
        if (healthy) ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
        else ESP_ERROR_CHECK(esp_ota_mark_app_invalid_rollback_and_reboot());
    }
}

static void vehicle_task(void *arg)
{
    (void)arg;
    int64_t last_record = 0;

    while (1) {
        obd2_vehicle_data_t data;
        esp_err_t err = obd2_read_vehicle_data(&data);
        if (err == ESP_OK) {
            ui_render_vehicle_data(&data);
            if (esp_timer_get_time() - last_record >= 1000000) {
                recorder_metrics(&data);
                last_record = esp_timer_get_time();
            }
        } else {
            ESP_LOGW(TAG, "OBD-II refresh failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    /* Never erase identity or network credentials implicitly after an update. */
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(device_services_init());
    ESP_ERROR_CHECK(ui_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(recorder_start());
    ESP_ERROR_CHECK(media_library_start());
    ESP_ERROR_CHECK(voice_service_start());
    ESP_ERROR_CHECK(media_service_start());
    ESP_ERROR_CHECK(camera_service_start());
    ESP_ERROR_CHECK(cloud_service_start());
    ESP_ERROR_CHECK(device_console_start());
    ESP_ERROR_CHECK(xTaskCreate(transport_task, "vehicle_init", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(vehicle_task, "vehicle_task", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Vehicle P4 services started");
    /* Reuse the startup task's DRAM stack for flash metadata and probation.
     * UI/radio tasks already run independently, including during OTA probation. */
    verify_boot();
}
