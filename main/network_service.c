#include "device_services.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool s_connected, s_reconfigure, s_connecting, s_radio_ready, s_scan_requested;
static atomic_int s_scan_done;
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_networks_t s_networks;
static char s_associated_ssid[33]; /* Owned by the event loop. */
static network_security_t s_associated_security;
static network_security_t security(wifi_auth_mode_t auth);
static const char *TAG = "network";
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void scan_state(network_scan_state_t state, const char *message)
{
    portENTER_CRITICAL(&s_scan_lock);
    s_networks.state = state;
    snprintf(s_networks.message, sizeof(s_networks.message), "%s", message);
    ++s_networks.revision;
    portEXIT_CRITICAL(&s_scan_lock);
}
void device_network_scan_get(wifi_networks_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_scan_lock);
    *out = s_networks;
    portEXIT_CRITICAL(&s_scan_lock);
}
esp_err_t device_network_scan_request(void)
{
    if (!atomic_load(&s_radio_ready)) {
        scan_state(NETWORK_SCAN_ERROR, "无线模块未就绪，请稍后重试");
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_scan_lock);
    bool busy = s_networks.state == NETWORK_SCAN_BUSY;
    if (!busy) {
        s_networks.state = NETWORK_SCAN_BUSY;
        snprintf(s_networks.message, sizeof(s_networks.message), "正在查找附近的 Wi-Fi");
        ++s_networks.revision;
    }
    portEXIT_CRITICAL(&s_scan_lock);
    if (!busy) atomic_store(&s_scan_requested, true);
    return ESP_OK;
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *scan = data;
        atomic_store(&s_scan_done, scan && scan->status == 0 ? 1 : -1);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *ap = data;
        memset(s_associated_ssid, 0, sizeof(s_associated_ssid));
        if (ap) memcpy(s_associated_ssid, ap->ssid, ap->ssid_len > 32 ? 32 : ap->ssid_len);
        s_associated_security = ap ? security(ap->authmode) : NETWORK_UNSUPPORTED;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_connected, true);
        atomic_store(&s_connecting, false);
        portENTER_CRITICAL(&s_scan_lock);
        s_networks.connected = true;
        memcpy(s_networks.connected_ssid, s_associated_ssid, sizeof(s_associated_ssid));
        s_networks.connected_security = s_associated_security;
        ++s_networks.revision;
        portEXIT_CRITICAL(&s_scan_lock);
        device_status_set(SERVICE_NETWORK, SERVICE_READY, "已连接 Wi-Fi");
    } else if ((base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        atomic_store(&s_connected, false);
        atomic_store(&s_connecting, false);
        portENTER_CRITICAL(&s_scan_lock);
        s_networks.connected = false;
        s_networks.connected_ssid[0] = '\0';
        ++s_networks.revision;
        portEXIT_CRITICAL(&s_scan_lock);
        unsigned reason = base == WIFI_EVENT && data ? ((wifi_event_sta_disconnected_t *)data)->reason : 0;
        const char *message = "连接已断开，正在等待重连";
        if (reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_HANDSHAKE_TIMEOUT)
            message = "认证失败，请检查热点密码";
        else if (reason == WIFI_REASON_NO_AP_FOUND) message = "未找到已保存的热点";
        else if (reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY || reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD)
            message = "热点加密方式不兼容";
        device_status_set(SERVICE_NETWORK, SERVICE_WAITING, message);
        if (base == IP_EVENT) atomic_store(&s_reconfigure, true);
    }
}

bool device_network_ready(void) { return atomic_load(&s_connected); }
void device_network_reconfigure(void) { atomic_store(&s_reconfigure, true); }

static network_security_t security(wifi_auth_mode_t auth)
{
    if (auth == WIFI_AUTH_OPEN) return NETWORK_OPEN;
    if (auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK) return NETWORK_WPA2;
    if (auth == WIFI_AUTH_WPA3_PSK) return NETWORK_WPA3;
    return NETWORK_UNSUPPORTED;
}
static void collect_scan(void)
{
    /* Both arrays live outside the task stack; only the network task uses them. */
    static wifi_ap_record_t records[32];
    static wifi_networks_t result;
    memset(&result, 0, sizeof(result));
    uint16_t count = sizeof(records) / sizeof(records[0]);
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list();
        scan_state(NETWORK_SCAN_ERROR, "读取热点失败，请重新扫描");
        ESP_LOGW(TAG, "Scan results: %s", esp_err_to_name(err));
        return;
    }
    for (unsigned i = 0; i < count; ++i)
        wifi_networks_add(&result, records[i].ssid, records[i].rssi, security(records[i].authmode));
    portENTER_CRITICAL(&s_scan_lock);
    s_networks.count = result.count;
    memcpy(s_networks.items, result.items, sizeof(result.items));
    s_networks.state = NETWORK_SCAN_READY;
    snprintf(s_networks.message, sizeof(s_networks.message), "%s", result.count ? "选择一个热点连接" : "未发现热点，请开启手机的 2.4 GHz 热点");
    ++s_networks.revision;
    portEXIT_CRITICAL(&s_scan_lock);
    ESP_LOGI(TAG, "Scan complete: %u records, %u named networks", count, result.count);
}

static void network_task(void *arg)
{
    (void)arg;
    esp_netif_create_default_wifi_sta();
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    if (err == ESP_OK) err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) err = esp_wifi_init(&init);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    /* STA only, including scans and connection failures. */
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Radio setup failed: %s", esp_err_to_name(err));
        device_status_set(SERVICE_NETWORK, SERVICE_ERROR, "无线模块未就绪");
        scan_state(NETWORK_SCAN_ERROR, "无线模块未就绪，请稍后重试");
        vTaskDelete(NULL);
        return;
    }
    atomic_store(&s_radio_ready, true);
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.cloudflare.com"));
    time_config.start = false;
    time_config.server_from_dhcp = false;
    esp_netif_sntp_init(&time_config);
    unsigned backoff = 1;
    bool clock_started = false, scanning = false;
    int64_t connect_deadline = 0, attempt_at = 0, scan_deadline = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = now_ms();
        if (!scanning && atomic_exchange(&s_scan_requested, false)) {
            /* Connecting and scanning cannot run together; established links stay up. */
            if (!device_network_ready() && atomic_load(&s_connecting)) {
                esp_wifi_disconnect();
                atomic_store(&s_connecting, false);
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            atomic_store(&s_scan_done, 0);
            err = esp_wifi_scan_start(NULL, false);
            if (err == ESP_OK) { scanning = true; scan_deadline = now_ms() + 10000; }
            else {
                ESP_LOGW(TAG, "Scan start: %s", esp_err_to_name(err));
                scan_state(NETWORK_SCAN_ERROR, "扫描失败，请稍后重新扫描");
            }
        }
        if (scanning) {
            int done = atomic_exchange(&s_scan_done, 0);
            if (!done && now < scan_deadline) continue;
            if (done > 0) collect_scan();
            else {
                esp_wifi_scan_stop();
                esp_wifi_clear_ap_list();
                scan_state(NETWORK_SCAN_ERROR, "扫描超时或中断，请重新扫描");
            }
            scanning = false;
            attempt_at = now + 500;
        }
        if (atomic_exchange(&s_reconfigure, false)) {
            esp_wifi_disconnect();
            atomic_store(&s_connected, false);
            atomic_store(&s_connecting, false);
            backoff = 1; attempt_at = now + 300;
        }
        if (device_network_ready()) {
            if (!clock_started) { esp_netif_sntp_start(); clock_started = true; }
            backoff = 1;
            continue;
        }
        if (atomic_load(&s_connecting)) {
            if (now >= connect_deadline) {
                esp_wifi_disconnect();
                atomic_store(&s_connecting, false);
                device_status_set(SERVICE_NETWORK, SERVICE_WAITING, "连接超时，请检查热点后重试");
            }
            continue;
        }
        if (now < attempt_at) continue;
        device_settings_t settings;
        device_settings_get(&settings);
        if (!*settings.ssid) {
            memset(&settings, 0, sizeof(settings));
            device_status_set(SERVICE_NETWORK, SERVICE_OFF, "请选择附近的 Wi-Fi");
            attempt_at = now + 1000;
            continue;
        }
        wifi_config_t config = {0};
        memcpy(config.sta.ssid, settings.ssid, strlen(settings.ssid));
        memcpy(config.sta.password, settings.password, strlen(settings.password));
        config.sta.pmf_cfg.capable = true;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        config.sta.threshold.authmode = *settings.password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        device_status_set(SERVICE_NETWORK, SERVICE_BUSY, "正在连接热点");
        atomic_store(&s_connecting, true);
        connect_deadline = now + 20000;
        attempt_at = now + backoff * 1000;
        if (backoff < 32) backoff *= 2;
        err = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&settings, 0, sizeof(settings)); memset(&config, 0, sizeof(config));
        if (err == ESP_OK) err = esp_wifi_connect();
        if (err != ESP_OK) {
            atomic_store(&s_connecting, false);
            device_status_set(SERVICE_NETWORK, SERVICE_WAITING, "连接未成功，请重试");
        }
    }
}

esp_err_t device_network_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    return xTaskCreate(network_task, "hotspot", 6144, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
