#include "device_services.h"
#include "device_http.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "obd2.h"
#include "sdkconfig.h"

static SemaphoreHandle_t s_lock;
static device_status_t s_status;
static device_settings_t s_settings;
static char s_id[33], s_boot[17];

bool device_url_valid(const char *url)
{
    if (!url || strncmp(url, "https://", 8) || !url[8] || strlen(url) >= 256) return false;
    for (const char *p = url + 8; *p; ++p) {
        if ((unsigned char)*p <= 32 || *p == '@' || *p == '#' || *p == '\\') return false;
    }
    return url[8] != '/';
}

esp_err_t device_services_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("vehicle", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    size_t length = sizeof(s_settings);
    if (nvs_get_blob(nvs, "settings_v1", &s_settings, &length) != ESP_OK || length != sizeof(s_settings)) {
        memset(&s_settings, 0, sizeof(s_settings));
        strlcpy(s_settings.ssid, CONFIG_DEVICE_WIFI_SSID, sizeof(s_settings.ssid));
        strlcpy(s_settings.password, CONFIG_DEVICE_WIFI_PASSWORD, sizeof(s_settings.password));
    }
    s_settings.ssid[32] = 0;
    s_settings.password[64] = 0;
    s_settings.upload_url[255] = s_settings.ota_url[255] = s_settings.token[128] = 0;
    length = sizeof(s_id);
    if (nvs_get_str(nvs, "device_id", s_id, &length) != ESP_OK) {
        snprintf(s_id, sizeof(s_id), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
                 (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
        err = nvs_set_str(nvs, "device_id", s_id);
        if (err == ESP_OK) err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    snprintf(s_boot, sizeof(s_boot), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    return err;
}

void device_status_set(service_id_t id, service_state_t state, const char *detail)
{
    if (!s_lock || id >= SERVICE_COUNT) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state[id] = state;
    strlcpy(s_status.detail[id], detail ? detail : "", sizeof(s_status.detail[id]));
    xSemaphoreGive(s_lock);
}

void device_status_get(device_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

bool device_ui_snapshot(device_status_t *status, device_settings_t *settings)
{
    if (!status || !settings || !s_lock || xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
    *status = s_status;
    *settings = s_settings;
    xSemaphoreGive(s_lock);
    return true;
}

void device_settings_get(device_settings_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_settings;
    xSemaphoreGive(s_lock);
}

esp_err_t device_settings_save(const device_settings_t *settings)
{
    if (!settings || (*settings->upload_url && !device_url_valid(settings->upload_url)) ||
        (*settings->ota_url && !device_url_valid(settings->ota_url))) return ESP_ERR_INVALID_ARG;
    for (const char *p = settings->token; *p; ++p) {
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127) return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("vehicle", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = nvs_set_blob(nvs, "settings_v1", settings, sizeof(*settings));
    if (err == ESP_OK) err = nvs_commit(nvs);
    if (err == ESP_OK) s_settings = *settings;
    xSemaphoreGive(s_lock);
    nvs_close(nvs);
    return err;
}

const char *device_id(void) { return s_id; }
const char *device_boot_id(void) { return s_boot; }
bool device_clock_ready(void) { return time(NULL) >= 1735689600; }

void device_http_config(esp_http_client_config_t *config, const char *url)
{
    *config = (esp_http_client_config_t){.url = url, .timeout_ms = 12000,
        .buffer_size = 4096, .disable_auto_redirect = true, .crt_bundle_attach = esp_crt_bundle_attach};
#ifdef DEVICE_CUSTOM_TRUST
    extern const char device_service_ca_start[] asm("_binary_service_ca_pem_start");
    config->cert_pem = device_service_ca_start;
    config->crt_bundle_attach = NULL;
#endif
}

bool device_maintenance_allowed(bool bench)
{
    obd2_vehicle_data_t data;
    if (obd2_read_vehicle_data(&data) != ESP_OK) return false;
    if (data.speed_valid) return data.speed_kph == 0;
#if CONFIG_DEVICE_BENCH_MAINTENANCE
    return bench;
#else
    (void)bench;
    return false;
#endif
}
