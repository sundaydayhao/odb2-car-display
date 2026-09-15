#include "device_services.h"
#include "network_retry.h"

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

static atomic_bool s_connected, s_reconfigure, s_radio_ready, s_scan_requested;
static atomic_int s_scan_done;
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_networks_t s_networks;
static esp_netif_t *s_sta;
typedef struct {
    bool associated, has_ip, radio_started;
    uint32_t link_revision, starts, stops;
    unsigned reason;
    char ssid[33];
    uint8_t bssid[6];
    network_security_t security;
} link_facts_t;
static link_facts_t s_link;
static network_security_t security(wifi_auth_mode_t auth);
static const char *TAG = "network";
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static link_facts_t link_facts(void)
{
    portENTER_CRITICAL(&s_scan_lock);
    link_facts_t facts=s_link;
    portEXIT_CRITICAL(&s_scan_lock);
    return facts;
}

static bool has_address(const link_facts_t *facts)
{
    esp_netif_ip_info_t ip={0};
    return facts->associated && facts->has_ip && s_sta && esp_netif_is_netif_up(s_sta) &&
        esp_netif_get_ip_info(s_sta,&ip)==ESP_OK && ip.ip.addr!=0;
}

static void publish_connected(bool connected, const link_facts_t *facts)
{
    if (atomic_exchange(&s_connected,connected)==connected) return;
    portENTER_CRITICAL(&s_scan_lock);
    s_networks.connected=connected;
    snprintf(s_networks.connected_ssid,sizeof(s_networks.connected_ssid),"%s",connected ? facts->ssid : "");
    if (connected) s_networks.connected_security=facts->security;
    ++s_networks.revision;
    portEXIT_CRITICAL(&s_scan_lock);
    if (connected) device_status_set(SERVICE_NETWORK,SERVICE_READY,"已连接 Wi-Fi");
}

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
    } else if (base==WIFI_EVENT && (id==WIFI_EVENT_STA_START || id==WIFI_EVENT_STA_STOP)) {
        portENTER_CRITICAL(&s_scan_lock);
        if (id==WIFI_EVENT_STA_START) ++s_link.starts; else ++s_link.stops;
        s_link.radio_started=id==WIFI_EVENT_STA_START;
        s_link.associated=false; s_link.has_ip=false; ++s_link.link_revision;
        portEXIT_CRITICAL(&s_scan_lock);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *ap = data;
        portENTER_CRITICAL(&s_scan_lock);
        bool same_link=s_link.associated && ap && !memcmp(s_link.bssid,ap->bssid,sizeof(s_link.bssid));
        memset(s_link.ssid,0,sizeof(s_link.ssid));
        if (ap) memcpy(s_link.ssid,ap->ssid,ap->ssid_len>32 ? 32 : ap->ssid_len);
        if (ap) memcpy(s_link.bssid,ap->bssid,sizeof(s_link.bssid));
        s_link.security=ap ? security(ap->authmode) : NETWORK_UNSUPPORTED;
        s_link.associated=true;
        if (!same_link) s_link.has_ip=false;
        s_link.reason=0; ++s_link.link_revision;
        portEXIT_CRITICAL(&s_scan_lock);
        ESP_LOGI(TAG, "Station associated; waiting for DHCP address");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got=data;
        if (!got || got->esp_netif!=s_sta || !got->ip_info.ip.addr) return;
        portENTER_CRITICAL(&s_scan_lock);
        if (s_link.associated) s_link.has_ip=true;
        portEXIT_CRITICAL(&s_scan_lock);
        ESP_LOGI(TAG, "DHCP address acquired");
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        unsigned reason=data ? ((wifi_event_sta_disconnected_t *)data)->reason : 0;
        portENTER_CRITICAL(&s_scan_lock);
        s_link.associated=false; s_link.has_ip=false; s_link.reason=reason; ++s_link.link_revision;
        portEXIT_CRITICAL(&s_scan_lock);
        ESP_LOGW(TAG, "Network disconnected: event=%ld reason=%u", (long)id, reason);
    } else if (base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        /* This delayed notification may belong to an earlier disconnect or our
         * DHCP restart. The task checks the current address; it is not a retry command. */
        ESP_LOGW(TAG,"IP-lost notification; checking current interface state");
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
    snprintf(s_networks.message, sizeof(s_networks.message), "%s", result.count ? "选择一个 Wi-Fi 连接" : "未发现 Wi-Fi，请检查 2.4 GHz 网络");
    ++s_networks.revision;
    portEXIT_CRITICAL(&s_scan_lock);
    ESP_LOGI(TAG, "Scan complete: %u records, %u named networks", count, result.count);
}

static bool wait_radio_event(bool started, uint32_t previous)
{
    int64_t deadline=now_ms()+5000;
    do {
        link_facts_t facts=link_facts();
        if ((started ? facts.starts : facts.stops)!=previous) return true;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (now_ms()<deadline);
    ESP_LOGE(TAG,"Timed out waiting for STA_%s",started ? "START" : "STOP");
    return false;
}

static esp_err_t disconnect_station(void)
{
    link_facts_t before=link_facts();
    esp_err_t err=esp_wifi_disconnect();
    if (err==ESP_ERR_WIFI_NOT_CONNECT) return ESP_OK;
    if (err!=ESP_OK) { ESP_LOGW(TAG,"Disconnect failed: %s",esp_err_to_name(err)); return err; }
    /* Cancelling an unassociated connection does not guarantee DISCONNECTED.
     * The RPC has completed; confirm the remote station is also unassociated. */
    if (!before.associated) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap)==ESP_ERR_WIFI_NOT_CONNECT) return ESP_OK;
    }
    int64_t deadline=now_ms()+3000;
    do {
        link_facts_t facts=link_facts();
        if (!facts.associated && facts.link_revision!=before.link_revision) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (now_ms()<deadline);
    ESP_LOGE(TAG,"Timed out waiting for station disconnect");
    return ESP_ERR_TIMEOUT;
}

static esp_err_t radio_start(void)
{
    uint32_t start=link_facts().starts;
    esp_err_t err=esp_wifi_start();
    if (err!=ESP_OK || !wait_radio_event(true,start)) return err==ESP_OK ? ESP_ERR_TIMEOUT : err;
    /* Vehicle power and continuous audio favor immediate packet reception. */
    err=esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_ps_type_t mode=WIFI_PS_MIN_MODEM;
    esp_err_t checked=esp_wifi_get_ps(&mode);
    ESP_LOGI(TAG,"Wi-Fi power save: set=%s get=%s mode=%d",esp_err_to_name(err),esp_err_to_name(checked),(int)mode);
    return err;
}

static esp_err_t radio_restart(void)
{
    publish_connected(false,NULL);
    link_facts_t facts=link_facts();
    esp_err_t err=ESP_OK;
    if (facts.radio_started) {
        err=esp_wifi_stop();
        if (err!=ESP_OK || !wait_radio_event(false,facts.stops)) return err==ESP_OK ? ESP_ERR_TIMEOUT : err;
    }
    /* Default STA_STOP handler has now brought netif down and cleaned DHCP.
     * Start while down leaves it INIT, including after a failed DHCP renewal. */
    err=esp_netif_dhcpc_start(s_sta);
    if (err!=ESP_OK && err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return err;
    return radio_start();
}

static const char *disconnect_reason(unsigned reason)
{
    if (reason==WIFI_REASON_AUTH_FAIL || reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || reason==WIFI_REASON_HANDSHAKE_TIMEOUT)
        return "认证失败，请检查 Wi-Fi 密码";
    if (reason==WIFI_REASON_NO_AP_FOUND) return "未找到已保存的 Wi-Fi";
    if (reason==WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY || reason==WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD)
        return "Wi-Fi 加密方式不兼容";
    return "连接已断开，稍后重试";
}

static void recovery_failed(network_retry_t *retry, const char *detail, esp_err_t error)
{
    retry->phase=NET_RETRY_FAILED;
    publish_connected(false,NULL);
    device_status_set(SERVICE_NETWORK,SERVICE_ERROR,detail);
    ESP_LOGE(TAG,"Recovery stopped: %s attempts=%u dhcp_restarts=%u radio_restarts=%u",
        esp_err_to_name(error),retry->attempts,retry->dhcp_renewals,retry->radio_restarts);
}

static void network_task(void *arg)
{
    (void)arg;
    s_sta=esp_netif_create_default_wifi_sta();
    esp_err_t err=s_sta ? ESP_OK : ESP_ERR_NO_MEM;
    if (err==ESP_OK) err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,network_event,NULL);
    if (err==ESP_OK) err=esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,network_event,NULL);
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if (err==ESP_OK) err=esp_wifi_init(&init);
    if (err==ESP_OK) err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_STA);
    if (err==ESP_OK) err=radio_start();
    if (err!=ESP_OK) {
        ESP_LOGE(TAG,"Radio setup failed: %s",esp_err_to_name(err));
        device_status_set(SERVICE_NETWORK,SERVICE_ERROR,"无线模块未就绪");
        scan_state(NETWORK_SCAN_ERROR,"无线模块未就绪，请稍后重试");
        vTaskDelete(NULL); return;
    }
    atomic_store(&s_radio_ready,true);
    esp_sntp_config_t time_config=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,ESP_SNTP_SERVER_LIST("pool.ntp.org","time.cloudflare.com"));
    time_config.start=false; time_config.server_from_dhcp=false;
    esp_netif_sntp_init(&time_config);
    bool clock_started=false,scanning=false;
    int64_t scan_deadline=0;
    uint32_t link_revision=link_facts().link_revision;
    network_retry_t retry;
    network_retry_reset(&retry,now_ms());
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now=now_ms();
        if (!scanning && atomic_exchange(&s_scan_requested,false)) {
            if (retry.phase==NET_RETRY_ASSOCIATING || retry.phase==NET_RETRY_DHCP) {
                err=disconnect_station();
                network_retry_disconnected(&retry,now_ms());
                if (err!=ESP_OK) {
                    recovery_failed(&retry,"连接未能停止，请重新连接",err);
                    scan_state(NETWORK_SCAN_ERROR,"扫描失败，请重新连接网络");
                    continue;
                }
            }
            atomic_store(&s_scan_done,0);
            err=esp_wifi_scan_start(NULL,false);
            if (err==ESP_OK) { scanning=true; scan_deadline=now_ms()+10000; }
            else { scan_state(NETWORK_SCAN_ERROR,"扫描失败，请稍后重新扫描"); ESP_LOGW(TAG,"Scan start: %s",esp_err_to_name(err)); }
        }
        if (scanning) {
            int done=atomic_exchange(&s_scan_done,0);
            if (done || now>=scan_deadline) {
                if (done>0) collect_scan();
                else { esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); scan_state(NETWORK_SCAN_ERROR,"扫描超时或中断，请重新扫描"); }
                scanning=false;
            }
        }
        if (!scanning && atomic_exchange(&s_reconfigure,false)) {
            device_status_set(SERVICE_NETWORK,SERVICE_BUSY,"正在重新连接 Wi-Fi");
            network_retry_reset(&retry,now_ms());
            err=radio_restart();
            link_revision=link_facts().link_revision;
            if (err!=ESP_OK) { recovery_failed(&retry,"无线恢复失败，请重新连接",err); continue; }
            /* A user request starts a new budget after the stop/start barrier. */
            network_retry_reset(&retry,now_ms());
        }
        link_facts_t facts=link_facts();
        bool address=has_address(&facts);
        if (retry.phase==NET_RETRY_FAILED) continue;
        if ((retry.phase==NET_RETRY_ASSOCIATING || retry.phase==NET_RETRY_DHCP || retry.phase==NET_RETRY_ONLINE) && address) {
            network_retry_online(&retry);
            publish_connected(true,&facts);
            link_revision=facts.link_revision;
            if (!clock_started) { esp_netif_sntp_start(); clock_started=true; }
            continue;
        }
        if (retry.phase==NET_RETRY_ONLINE) {
            publish_connected(false,NULL);
            network_retry_disconnected(&retry,now_ms());
            if (scanning) {
                esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); scanning=false;
                scan_state(NETWORK_SCAN_ERROR,"连接中断，请重新扫描");
            }
            if (facts.associated) {
                err=disconnect_station();
                if (err!=ESP_OK) { recovery_failed(&retry,"无法重新获取 IP，请重新连接",err); continue; }
            }
            device_status_set(SERVICE_NETWORK,SERVICE_WAITING,disconnect_reason(facts.reason));
            facts=link_facts();
        }
        if (facts.link_revision!=link_revision) {
            link_revision=facts.link_revision;
            if (facts.associated) {
                network_retry_associated(&retry,now_ms());
                if (retry.phase==NET_RETRY_DHCP) device_status_set(SERVICE_NETWORK,SERVICE_BUSY,"已关联 Wi-Fi，正在获取 IP");
            } else if (retry.phase==NET_RETRY_ASSOCIATING || retry.phase==NET_RETRY_DHCP) {
                network_retry_disconnected(&retry,now_ms());
                device_status_set(SERVICE_NETWORK,SERVICE_WAITING,disconnect_reason(facts.reason));
            }
        }
        /* Scanning pauses connection work, not link status or the total budget. */
        if (scanning) {
            if (now_ms()<retry.expires_ms) continue;
            esp_wifi_scan_stop(); esp_wifi_clear_ap_list(); scanning=false;
            scan_state(NETWORK_SCAN_ERROR,"连接超时，请重新扫描");
        }
        network_retry_t before=retry;
        network_retry_action_t action=network_retry_step(&retry,now_ms());
        /* Recheck live state before a timeout action, instead of letting an
         * event callback announce success while the task is about to disconnect. */
        facts=link_facts();
        if (action!=NET_RETRY_NONE && (before.phase==NET_RETRY_ASSOCIATING || before.phase==NET_RETRY_DHCP) && has_address(&facts)) {
            retry=before; network_retry_online(&retry); publish_connected(true,&facts); continue;
        }
        if (action==NET_RETRY_CONNECT) {
            device_settings_t settings;
            device_settings_get(&settings);
            if (!*settings.ssid) {
                memset(&settings,0,sizeof(settings)); retry.phase=NET_RETRY_FAILED;
                device_status_set(SERVICE_NETWORK,SERVICE_OFF,"请选择附近的 Wi-Fi"); continue;
            }
            wifi_config_t config={0};
            memcpy(config.sta.ssid,settings.ssid,strlen(settings.ssid));
            memcpy(config.sta.password,settings.password,strlen(settings.password));
            config.sta.pmf_cfg.capable=true;
            config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
            config.sta.threshold.authmode=*settings.password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
            device_status_set(SERVICE_NETWORK,SERVICE_BUSY,"正在连接 Wi-Fi");
            ESP_LOGI(TAG,"Connection attempt %u/%u",retry.attempts,NET_RETRY_MAX_ATTEMPTS);
            err=esp_wifi_set_config(WIFI_IF_STA,&config);
            memset(&settings,0,sizeof(settings)); memset(&config,0,sizeof(config));
            if (err==ESP_OK) err=esp_wifi_connect();
            if (err!=ESP_OK) {
                ESP_LOGW(TAG,"Connect request failed: %s",esp_err_to_name(err));
                network_retry_disconnected(&retry,now_ms());
                device_status_set(SERVICE_NETWORK,SERVICE_WAITING,"连接未成功，稍后重试");
            }
        } else if (action==NET_RETRY_RENEW_DHCP) {
            device_status_set(SERVICE_NETWORK,SERVICE_BUSY,"正在重新申请 IP 地址");
            esp_err_t stopped=esp_netif_dhcpc_stop(s_sta);
            esp_err_t started=esp_netif_dhcpc_start(s_sta);
            ESP_LOGW(TAG,"DHCP recovery: stop=%s start=%s",esp_err_to_name(stopped),esp_err_to_name(started));
            if ((stopped!=ESP_OK && stopped!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) || started!=ESP_OK)
                recovery_failed(&retry,"IP 申请失败，请重新连接",started!=ESP_OK ? started : stopped);
        } else if (action==NET_RETRY_RESTART_RADIO) {
            device_status_set(SERVICE_NETWORK,SERVICE_BUSY,"正在恢复无线连接");
            ESP_LOGW(TAG,"DHCP recovery exhausted; restarting Wi-Fi once");
            err=radio_restart();
            link_revision=link_facts().link_revision;
            if (err!=ESP_OK) recovery_failed(&retry,"无线恢复失败，请重新连接",err);
        } else if (action==NET_RETRY_DISCONNECT) {
            device_status_set(SERVICE_NETWORK,SERVICE_WAITING,"连接超时，稍后重试");
            err=disconnect_station();
            if (err!=ESP_OK) recovery_failed(&retry,"连接未能停止，请重新连接",err);
        } else if (action==NET_RETRY_STOP) {
            disconnect_station();
            recovery_failed(&retry,before.phase==NET_RETRY_DHCP ? "无法获取 IP，请重新连接网络" : "连接失败，请选择网络后重试",ESP_ERR_TIMEOUT);
        }
    }
}

esp_err_t device_network_start(void)
{
    esp_err_t err=esp_netif_init();
    if (err!=ESP_OK) return err;
    return xTaskCreate(network_task,"hotspot",6144,NULL,3,NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
