#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "obd2.h"
#include "ui.h"

static const char *TAG = "app";

static void vehicle_task(void *arg)
{
    (void)arg;

    while (1) {
        obd2_vehicle_data_t data;
        esp_err_t err = obd2_read_vehicle_data(&data);
        if (err == ESP_OK) {
            ui_render_vehicle_data(&data);
        } else {
            ESP_LOGW(TAG, "OBD-II refresh failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_OBD2_POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ui_init();
    ESP_ERROR_CHECK(obd2_init());

    xTaskCreate(vehicle_task, "vehicle_task", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "ODB2 dashboard started");
}
