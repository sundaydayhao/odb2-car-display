#include "device_services.h"

#include <stdatomic.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool s_connected, s_reconfigure, s_connecting;
static const char *TAG = "network";

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_connected, true);
        atomic_store(&s_connecting, false);
        device_status_set(SERVICE_NETWORK, SERVICE_READY, "热点已连接");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&s_connected, false);
        atomic_store(&s_connecting, false);
        device_status_set(SERVICE_NETWORK, SERVICE_WAITING, "等待手机热点");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        atomic_store(&s_connected, false);
        atomic_store(&s_reconfigure, true);
    }
}

bool device_network_ready(void) { return atomic_load(&s_connected); }
void device_network_reconfigure(void) { atomic_store(&s_reconfigure, true); }

static void network_task(void *arg)
{
    (void)arg;
    esp_netif_create_default_wifi_sta();
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) err = esp_wifi_init(&init);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Radio setup failed: %s", esp_err_to_name(err));
        device_status_set(SERVICE_NETWORK, SERVICE_ERROR, "无线模块未就绪");
        vTaskDelete(NULL);
        return;
    }
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
    time_config.start = false;
    time_config.server_from_dhcp = false;
    esp_netif_sntp_init(&time_config);
    unsigned backoff = 1, connecting_seconds = 0;
    bool clock_started = false;
    for (;;) {
        if (atomic_exchange(&s_reconfigure, false)) {
            esp_wifi_disconnect();
            atomic_store(&s_connected, false);
            atomic_store(&s_connecting, false);
            backoff = 1;
        }
        if (device_network_ready()) {
            if (!clock_started) { esp_netif_sntp_start(); clock_started = true; }
            backoff = 1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (atomic_load(&s_connecting)) {
            if (++connecting_seconds >= 20) {
                esp_wifi_disconnect();
                atomic_store(&s_connecting, false);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        device_settings_t settings;
        device_settings_get(&settings);
        if (!*settings.ssid) {
            device_status_set(SERVICE_NETWORK, SERVICE_OFF, "尚未设置热点");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        wifi_config_t config = {0};
        memcpy(config.sta.ssid, settings.ssid, strlen(settings.ssid));
        memcpy(config.sta.password, settings.password, strlen(settings.password));
        config.sta.pmf_cfg.capable = true;
        config.sta.threshold.authmode = *settings.password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        device_status_set(SERVICE_NETWORK, SERVICE_BUSY, "正在连接热点");
        atomic_store(&s_connecting, true);
        connecting_seconds = 0;
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&settings, 0, sizeof(settings));
        memset(&config, 0, sizeof(config));
        if (err == ESP_OK) err = esp_wifi_connect();
        if (err != ESP_OK) atomic_store(&s_connecting, false);
        vTaskDelay(pdMS_TO_TICKS(backoff * 1000 + esp_random() % 500));
        if (backoff < 32) backoff *= 2;
    }
}

esp_err_t device_network_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    return xTaskCreate(network_task, "hotspot", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
