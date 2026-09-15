#include "elm327_ble.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "recorder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "store/config/ble_store_config.h"
#include "sdkconfig.h"

#ifndef CONFIG_OBD2_ELM327_BLE_REQUIRE_PAIRING
#define CONFIG_OBD2_ELM327_BLE_REQUIRE_PAIRING 0
#endif

/* ESP-IDF's NimBLE examples expose this initializer outside the public header. */
void ble_store_config_init(void);

#define RX_CHUNK 64
#define MAX_SERVICES 8
#define MAX_CHARS 24
static const char *TAG = "elm_ble";

typedef struct { ble_uuid_any_t service, tx, rx; } profile_t;
typedef struct { uint16_t start, end; unsigned profile; } service_t;
typedef struct { uint32_t generation; uint16_t length; uint8_t data[RX_CHUNK]; } rx_chunk_t;
typedef struct {
    elm_ble_state_t state;
    uint32_t generation;
    uint16_t conn, tx, rx;
    uint8_t tx_properties;
    bool overflow;
    int write_status;
    int64_t deadline;
    char address[18];
} link_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static link_t s_link = {.state=ELM_BLE_IDLE,.conn=BLE_HS_CONN_HANDLE_NONE,.generation=1};
static QueueHandle_t s_rx;
static SemaphoreHandle_t s_command_lock, s_write_done, s_settings_lock;
static struct ble_npl_callout s_watchdog;
static struct ble_npl_event s_reset_event;
static profile_t s_profiles[4];
static unsigned s_profile_count;
static service_t s_services[MAX_SERVICES];
static unsigned s_service_count, s_service_index;
static struct ble_gatt_chr s_chars[MAX_CHARS];
static unsigned s_char_count;
static uint16_t s_cccd;
static uint8_t s_subscription;
static bool s_synced, s_pairing;
static ble_addr_t s_peer;
static atomic_uint s_transaction, s_transaction_counter, s_notification;
static atomic_bool s_enabled = true, s_available, s_control_pending, s_scan_requested;
typedef enum { CONTROL_NONE, CONTROL_CONNECT, CONTROL_DISCONNECT, CONTROL_FORGET } control_t;
static control_t s_control, s_after_stop; /* s_control under s_lock; s_after_stop in host context. */
static ble_device_t s_requested_device, s_target, s_connected_device;
static ble_devices_t s_devices;
static ble_reconnect_policy_t s_reconnect;
/* Byte-only, versioned NVS record; no RSSI, pointers, or compiler padding. */
typedef struct { uint8_t version, type, address[6]; char name[BLE_DEVICE_NAME_SIZE]; } saved_peer_t;

static int gap_event(struct ble_gap_event *event, void *arg);
static int services_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg);
static int chars_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg);
static void scan(void);
static void connect_device(const ble_device_t *device);

static int64_t now_ms(void) { return esp_timer_get_time()/1000; }
static link_t snapshot(void)
{
    portENTER_CRITICAL(&s_lock);
    link_t copy=s_link;
    portEXIT_CRITICAL(&s_lock);
    return copy;
}

void elm_ble_devices_get(ble_devices_t *devices)
{
    if (!devices) return;
    portENTER_CRITICAL(&s_lock);
    *devices=s_devices;
    portEXIT_CRITICAL(&s_lock);
}

static void message(const char *value)
{
    portENTER_CRITICAL(&s_lock);
    snprintf(s_devices.message,sizeof(s_devices.message),"%s",value);
    ++s_devices.revision;
    portEXIT_CRITICAL(&s_lock);
}

static void request_control(control_t control, const ble_device_t *device)
{
    portENTER_CRITICAL(&s_lock);
    s_control=control;
    if (device) s_requested_device=*device;
    atomic_store(&s_control_pending,true);
    portEXIT_CRITICAL(&s_lock);
    xSemaphoreGive(s_write_done);
}

elm_ble_status_t elm_ble_status(void)
{
    link_t link=snapshot();
    elm_ble_status_t result = {.state=link.state, .generation=link.generation,
        .available=atomic_load(&s_available), .enabled=atomic_load(&s_enabled)};
    if (!result.enabled && link.state!=ELM_BLE_OFF) result.state=ELM_BLE_STOPPING;
    memcpy(result.address,link.address,sizeof(result.address));
    return result;
}

static bool current(uint16_t conn, void *arg)
{
    link_t link=snapshot();
    return atomic_load(&s_enabled) && !atomic_load(&s_control_pending) &&
           conn==link.conn && link.generation==(uint32_t)(uintptr_t)arg;
}

static void stage(elm_ble_state_t state, uint32_t timeout)
{
    portENTER_CRITICAL(&s_lock);
    s_link.state=state;
    s_link.deadline=timeout ? now_ms()+timeout : 0;
    portEXIT_CRITICAL(&s_lock);
}

static void clear_link(void)
{
    recorder_wire(RECORD_LINK, snapshot().generation, atomic_load(&s_transaction), 0, 0, 0, NULL, 0, 0);
    portENTER_CRITICAL(&s_lock);
    uint32_t generation=s_link.generation+1;
    s_link=(link_t){.conn=BLE_HS_CONN_HANDLE_NONE,.generation=generation,
        .state=atomic_load(&s_enabled) ? ELM_BLE_IDLE : ELM_BLE_OFF};
    portEXIT_CRITICAL(&s_lock);
    xSemaphoreGive(s_write_done);
}

static void drop(void)
{
    link_t link=snapshot();
    message("连接失败，请选择设备重试");
    stage(ELM_BLE_DISCOVERING,3000);
    if (link.conn!=BLE_HS_CONN_HANDLE_NONE) {
        int rc=ble_gap_terminate(link.conn,BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc!=BLE_HS_EALREADY) clear_link();
    } else { ble_gap_conn_cancel(); clear_link(); }
}

static void reset_event(struct ble_npl_event *event) {
    (void)event;
    if (atomic_load(&s_enabled) && !atomic_load(&s_control_pending)) drop();
}

void elm_ble_reset_link(void)
{
    if (!atomic_load(&s_available) || !atomic_load(&s_enabled) || atomic_load(&s_control_pending)) return;
    stage(ELM_BLE_DISCOVERING,3000);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&s_reset_event);
}

esp_err_t elm_ble_set_enabled(bool enabled)
{
    if (!atomic_load(&s_available)) return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("bluetooth",NVS_READWRITE,&nvs);
    if (err==ESP_OK) {
        err=nvs_set_u8(nvs,"enabled",enabled ? 1 : 0);
        if (err==ESP_OK) err=nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err!=ESP_OK) return err;
    if (atomic_exchange(&s_enabled,enabled)!=enabled) {
        ble_device_t saved;
        portENTER_CRITICAL(&s_lock);
        bool has_saved=s_devices.has_saved;
        saved=s_devices.saved;
        portEXIT_CRITICAL(&s_lock);
        request_control(enabled && has_saved ? CONTROL_CONNECT : CONTROL_DISCONNECT,&saved);
    }
    return ESP_OK;
}

esp_err_t elm_ble_reconnect(void)
{
    if (!atomic_load(&s_available) || !atomic_load(&s_enabled)) return ESP_ERR_INVALID_STATE;
    ble_device_t saved;
    portENTER_CRITICAL(&s_lock);
    bool has_saved=s_devices.has_saved;
    saved=s_devices.saved;
    portEXIT_CRITICAL(&s_lock);
    if (!has_saved) return ESP_ERR_NOT_FOUND;
    request_control(CONTROL_CONNECT,&saved);
    return ESP_OK;
}

esp_err_t elm_ble_scan(void)
{
    if (!atomic_load(&s_available) || !atomic_load(&s_enabled)) return ESP_ERR_INVALID_STATE;
    link_t link=snapshot();
    if (link.state!=ELM_BLE_IDLE && link.state!=ELM_BLE_READY) return ESP_ERR_INVALID_STATE;
    atomic_store(&s_scan_requested,true);
    return ESP_OK;
}

esp_err_t elm_ble_connect_device(const ble_device_t *device)
{
    if (!device || !atomic_load(&s_available) || !atomic_load(&s_enabled)) return ESP_ERR_INVALID_STATE;
    ble_device_t chosen={0};
    bool found=false;
    portENTER_CRITICAL(&s_lock);
    if (s_devices.has_saved && ble_device_same(device,&s_devices.saved)) {
        chosen=s_devices.saved; found=true;
    }
    for (unsigned i=0; !found && i<s_devices.count; ++i) {
        if (ble_device_same(device,&s_devices.items[i])) { chosen=s_devices.items[i]; found=true; }
    }
    portEXIT_CRITICAL(&s_lock);
    if (!found) return ESP_ERR_NOT_FOUND;
    request_control(CONTROL_CONNECT,&chosen);
    return ESP_OK;
}

esp_err_t elm_ble_disconnect(void)
{
    if (!atomic_load(&s_available)) return ESP_ERR_INVALID_STATE;
    request_control(CONTROL_DISCONNECT,NULL);
    return ESP_OK;
}

esp_err_t elm_ble_forget(void)
{
    if (!atomic_load(&s_available)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_settings_lock,portMAX_DELAY);
    nvs_handle_t nvs;
    esp_err_t err=nvs_open("bluetooth",NVS_READWRITE,&nvs);
    if (err==ESP_OK) {
        err=nvs_erase_key(nvs,"peer_v1");
        if (err==ESP_ERR_NVS_NOT_FOUND) err=ESP_OK;
        if (err==ESP_OK) err=nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err==ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        ble_device_t old=s_devices.saved;
        s_devices.has_saved=false;
        memset(&s_devices.saved,0,sizeof(s_devices.saved));
        s_reconnect.allowed=false;
        ++s_devices.revision;
        portEXIT_CRITICAL(&s_lock);
        request_control(CONTROL_FORGET,&old);
        message("已忘记设备，点击扫描设备重新选择");
    }
    xSemaphoreGive(s_settings_lock);
    return err;
}

esp_err_t elm_ble_confirm_adapter(uint32_t generation)
{
    xSemaphoreTake(s_settings_lock,portMAX_DELAY);
    portENTER_CRITICAL(&s_lock);
    bool valid=s_link.generation==generation && s_link.state==ELM_BLE_READY &&
        atomic_load(&s_enabled) && !atomic_load(&s_control_pending);
    ble_device_t peer=s_connected_device;
    bool unchanged=s_devices.has_saved && ble_device_same(&peer,&s_devices.saved) &&
        !strcmp(peer.name,s_devices.saved.name);
    portEXIT_CRITICAL(&s_lock);
    esp_err_t err=valid ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (valid && !unchanged) {
        saved_peer_t record={.version=1,.type=peer.type};
        memcpy(record.address,peer.address,6);
        snprintf(record.name,sizeof(record.name),"%s",peer.name);
        nvs_handle_t nvs;
        err=nvs_open("bluetooth",NVS_READWRITE,&nvs);
        if (err==ESP_OK) {
            err=nvs_set_blob(nvs,"peer_v1",&record,sizeof(record));
            if (err==ESP_OK) err=nvs_commit(nvs);
            nvs_close(nvs);
        }
    }
    if (err==ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_devices.saved=peer;
        s_devices.has_saved=true;
        /* A failed reconnect cannot rearm this: only a validated ELM session can. */
        s_reconnect.allowed=s_link.generation==generation && s_link.state==ELM_BLE_READY &&
            !atomic_load(&s_control_pending);
        ++s_devices.revision;
        portEXIT_CRITICAL(&s_lock);
        message("已连接并保存，下次自动重连一次");
        ESP_LOGI(TAG,"Validated adapter remembered; one reconnect permitted");
    }
    xSemaphoreGive(s_settings_lock);
    return err;
}

/* NimBLE host context only. Cancel scans/connections, then let disconnect invalidate the generation. */
static void apply_control(void)
{
    link_t link=snapshot();
    atomic_store(&s_scan_requested,false);
    portENTER_CRITICAL(&s_lock);
    s_reconnect.allowed=false;
    portEXIT_CRITICAL(&s_lock);
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    if (ble_gap_conn_active()) ble_gap_conn_cancel();
    if (link.conn!=BLE_HS_CONN_HANDLE_NONE) {
        stage(ELM_BLE_STOPPING,3000);
        int rc=ble_gap_terminate(link.conn,BLE_ERR_REM_USER_CONN_TERM);
        if (rc && rc!=BLE_HS_EALREADY) clear_link();
    } else clear_link();
}

static bool secure(uint16_t conn, uint16_t status)
{
    if (s_pairing || (status!=BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) &&
                     status!=BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC))) return false;
    s_pairing=true;
    stage(ELM_BLE_DISCOVERING,15000);
    ESP_LOGI(TAG,"Adapter requires an encrypted link; initiating pairing");
    return ble_gap_security_initiate(conn)==0;
}

static int subscribe_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    if (!current(conn,arg)) return 0;
    if (error->status) {
        if (!secure(conn,error->status)) { ESP_LOGW(TAG,"CCCD subscription failed: %u",error->status); drop(); }
        return 0;
    }
    stage(ELM_BLE_READY,0);
    link_t link=snapshot();
    ESP_LOGI(TAG,"UART ready: generation=%lu TX=%u RX=%u CCCD=%u",(unsigned long)link.generation,link.tx,link.rx,s_cccd);
    return 0;
}

static int descriptors_cb(uint16_t conn, const struct ble_gatt_error *error, uint16_t chr_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    if (!current(conn,arg)) return 0;
    if (!error->status) {
        if (chr_handle==snapshot().rx && ble_uuid_u16(&dsc->uuid.u)==0x2902) s_cccd=dsc->handle;
        return 0;
    }
    if (error->status!=BLE_HS_EDONE || !s_cccd) { ESP_LOGW(TAG,"No usable notification descriptor"); drop(); return 0; }
    const uint8_t value[2]={s_subscription,0};
    if (ble_gattc_write_flat(conn,s_cccd,value,sizeof(value),subscribe_cb,arg)) drop();
    return 0;
}

static void discover_chars(uint16_t conn, void *arg)
{
    if (s_service_index>=s_service_count) { ESP_LOGW(TAG,"No supported UART characteristic pair"); drop(); return; }
    s_char_count=0;
    service_t *s=&s_services[s_service_index];
    if (ble_gattc_disc_all_chrs(conn,s->start,s->end,chars_cb,arg)) drop();
}

static int chars_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    if (!current(conn,arg)) return 0;
    if (!error->status) {
        if (s_char_count==MAX_CHARS) { drop(); return BLE_HS_ENOMEM; }
        s_chars[s_char_count++]=*chr;
        return 0;
    }
    if (error->status!=BLE_HS_EDONE) { drop(); return 0; }
    service_t *s=&s_services[s_service_index];
    profile_t *p=&s_profiles[s->profile];
    const struct ble_gatt_chr *tx=NULL,*rx=NULL;
    uint16_t end=s->end;
    for (unsigned i=0;i<s_char_count;++i) {
        const struct ble_gatt_chr *c=&s_chars[i];
        if (!ble_uuid_cmp(&c->uuid.u,&p->tx.u) && (c->properties&(BLE_GATT_CHR_PROP_WRITE|BLE_GATT_CHR_PROP_WRITE_NO_RSP))) tx=c;
        if (!ble_uuid_cmp(&c->uuid.u,&p->rx.u) && (c->properties&(BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_INDICATE))) rx=c;
    }
    if (!tx || !rx) { ++s_service_index; discover_chars(conn,arg); return 0; }
    for (unsigned i=0;i<s_char_count;++i) if (s_chars[i].def_handle>rx->val_handle && s_chars[i].def_handle-1<end) end=s_chars[i].def_handle-1;
    portENTER_CRITICAL(&s_lock);
    s_link.tx=tx->val_handle; s_link.rx=rx->val_handle; s_link.tx_properties=tx->properties;
    portEXIT_CRITICAL(&s_lock);
    s_cccd=0;
    s_subscription=(rx->properties&BLE_GATT_CHR_PROP_NOTIFY) ? 1 : 2;
    if (end<=rx->val_handle || ble_gattc_disc_all_dscs(conn,rx->val_handle,end,descriptors_cb,arg)) drop();
    return 0;
}

static int services_cb(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *service, void *arg)
{
    if (!current(conn,arg)) return 0;
    if (!error->status) {
        for (unsigned i=0;i<s_profile_count;++i) {
            if (ble_uuid_cmp(&service->uuid.u,&s_profiles[i].service.u)) continue;
            if (s_service_count==MAX_SERVICES) { drop(); return BLE_HS_ENOMEM; }
            s_services[s_service_count++]=(service_t){service->start_handle,service->end_handle,i};
            break;
        }
    } else if (error->status==BLE_HS_EDONE) { s_service_index=0; discover_chars(conn,arg); }
    else drop();
    return 0;
}

static void discover(uint16_t conn)
{
    s_service_count=0;
    stage(ELM_BLE_DISCOVERING,20000);
    void *arg=(void *)(uintptr_t)snapshot().generation;
    if (ble_gattc_disc_all_svcs(conn,services_cb,arg)) drop();
}

static int mtu_cb(uint16_t conn, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    if (!current(conn,arg)) return 0;
    ESP_LOGI(TAG,"MTU exchange: status=%u mtu=%u",error->status,mtu);
    discover(conn);
    return 0;
}

static void found_device(const struct ble_gap_disc_desc *disc)
{
    if (disc->event_type!=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND && disc->event_type!=BLE_HCI_ADV_RPT_EVTYPE_DIR_IND &&
        disc->event_type!=BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) return;
    ble_device_t device={.type=disc->addr.type,.rssi=disc->rssi};
    memcpy(device.address,disc->addr.val,6);
    struct ble_hs_adv_fields fields={0};
    if (!ble_hs_adv_parse_fields(&fields,disc->data,disc->length_data) && fields.name_len) {
        size_t n=fields.name_len<sizeof(device.name)-1 ? fields.name_len : sizeof(device.name)-1;
        /* Do not cut a UTF-8 sequence at the end of the bounded name. */
        if (n<fields.name_len) while (n && (fields.name[n]&0xc0)==0x80) --n;
        memcpy(device.name,fields.name,n);
        for (size_t i=0;i<n;++i) if ((uint8_t)device.name[i]<32 || device.name[i]==127) device.name[i]=' ';
    }
    portENTER_CRITICAL(&s_lock);
    /* A scan response only updates a device already known to be connectable. */
    bool connectable=disc->event_type!=BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    for (unsigned i=0; !connectable && i<s_devices.count; ++i)
        connectable=ble_device_same(&device,&s_devices.items[i]);
    if (connectable && s_devices.scanning) ble_devices_add(&s_devices,&device);
    portEXIT_CRITICAL(&s_lock);
}

static void scan(void)
{
    if (!s_synced || !atomic_load(&s_enabled) || atomic_load(&s_control_pending) ||
        ble_gap_conn_active() || ble_gap_disc_active()) return;
    link_t link=snapshot();
    if (link.state!=ELM_BLE_IDLE && link.state!=ELM_BLE_READY) return;
    uint8_t own_type;
    if (ble_hs_id_infer_auto(0,&own_type)) { message("扫描失败，请重试"); return; }
    portENTER_CRITICAL(&s_lock);
    s_devices.count=0;
    s_devices.scanning=true;
    ++s_devices.revision;
    portEXIT_CRITICAL(&s_lock);
    struct ble_gap_disc_params params={.itvl=160,.window=96,.passive=0,.filter_duplicates=0};
    int rc=ble_gap_disc(own_type,15000,&params,gap_event,NULL);
    if (!rc) {
        if (link.state==ELM_BLE_IDLE) stage(ELM_BLE_SCANNING,0);
        message("正在扫描，点击设备名称连接");
        ESP_LOGI(TAG,"Scanning BLE devices for 15 seconds; selection required");
    } else {
        portENTER_CRITICAL(&s_lock);
        s_devices.scanning=false;
        portEXIT_CRITICAL(&s_lock);
        message("扫描失败，请重试");
        ESP_LOGW(TAG,"Scan start failed: %d; waiting for manual request",rc);
    }
}

static void connect_device(const ble_device_t *device)
{
    uint8_t own_type;
    if (ble_hs_id_infer_auto(0,&own_type)) { message("连接失败，请重试"); return; }
    s_peer.type=device->type;
    memcpy(s_peer.val,device->address,6);
    char address[18];
    ble_device_address(device,address);
    portENTER_CRITICAL(&s_lock);
    s_connected_device=*device;
    memcpy(s_link.address,address,sizeof(address));
    s_reconnect.allowed=false;
    portEXIT_CRITICAL(&s_lock);
    stage(ELM_BLE_CONNECTING,12000);
    message("正在连接所选设备");
    ESP_LOGI(TAG,"Connecting selected device %s (one attempt)",address);
    if (ble_gap_connect(own_type,&s_peer,10000,NULL,gap_event,NULL)) drop();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    link_t link=snapshot();
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (atomic_load(&s_enabled) && !atomic_load(&s_control_pending)) found_device(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        portENTER_CRITICAL(&s_lock);
        s_devices.scanning=false;
        portEXIT_CRITICAL(&s_lock);
        if (link.state==ELM_BLE_SCANNING) stage(ELM_BLE_IDLE,0);
        message("扫描结束，请选择设备连接");
        ESP_LOGI(TAG,"Scan finished; waiting for device selection");
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) {
            if (!atomic_load(&s_enabled)) clear_link();
            else { ESP_LOGW(TAG,"Connection failed: %d",event->connect.status); drop(); }
            break;
        }
        portENTER_CRITICAL(&s_lock);
        ++s_link.generation;
        s_link.conn=event->connect.conn_handle;
        s_link.overflow=false;
        portEXIT_CRITICAL(&s_lock);
        if (!atomic_load(&s_enabled) || atomic_load(&s_control_pending)) { apply_control(); break; }
        struct ble_gap_conn_desc descriptor;
        if (!ble_gap_conn_find(event->connect.conn_handle,&descriptor)) {
            portENTER_CRITICAL(&s_lock);
            s_connected_device.type=descriptor.peer_id_addr.type;
            memcpy(s_connected_device.address,descriptor.peer_id_addr.val,6);
            portEXIT_CRITICAL(&s_lock);
        }
        s_pairing=false;
        stage(ELM_BLE_DISCOVERING,15000);
        if (CONFIG_OBD2_ELM327_BLE_REQUIRE_PAIRING || CONFIG_OBD2_ELM327_BLE_PASSKEY>=0) {
            s_pairing=true;
            if (ble_gap_security_initiate(event->connect.conn_handle)) drop();
        } else if (ble_gattc_exchange_mtu(event->connect.conn_handle,mtu_cb,(void *)(uintptr_t)snapshot().generation)) {
            discover(event->connect.conn_handle);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle!=link.conn) break;
        ESP_LOGW(TAG,"Disconnected: reason=%d",event->disconnect.reason);
        portENTER_CRITICAL(&s_lock);
        bool retry=ble_reconnect_take(&s_reconnect,atomic_load(&s_enabled),s_devices.has_saved,
            atomic_load(&s_control_pending));
        if (retry) s_target=s_devices.saved;
        portEXIT_CRITICAL(&s_lock);
        clear_link();
        if (retry) {
            s_after_stop=CONTROL_CONNECT;
            ESP_LOGI(TAG,"Link lost; attempting remembered device once");
            message("连接断开，正在尝试重连一次");
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (!atomic_load(&s_enabled) || atomic_load(&s_control_pending) ||
            event->notify_rx.conn_handle!=link.conn || event->notify_rx.attr_handle!=link.rx) break;
        uint16_t length=OS_MBUF_PKTLEN(event->notify_rx.om);
        uint32_t notification = atomic_fetch_add(&s_notification, 1) + 1;
        uint32_t transaction = atomic_load(&s_transaction);
        for (uint16_t offset=0;offset<length;) {
            rx_chunk_t chunk={.generation=link.generation,.length=length-offset};
            if (chunk.length>RX_CHUNK) chunk.length=RX_CHUNK;
            if (os_mbuf_copydata(event->notify_rx.om,offset,chunk.length,chunk.data)) {
                recorder_wire(RECORD_PARSE_GAP, link.generation, transaction, notification, offset, length, NULL, 0, ESP_FAIL);
                portENTER_CRITICAL(&s_lock); s_link.overflow=true; portEXIT_CRITICAL(&s_lock);
                break;
            }
            recorder_wire(RECORD_RX, link.generation, transaction, notification, offset, length, chunk.data, chunk.length, 0);
            if (xQueueSend(s_rx,&chunk,0)!=pdTRUE) {
                recorder_wire(RECORD_PARSE_GAP, link.generation, transaction, notification, offset, length, NULL, 0, ESP_ERR_NO_MEM);
                portENTER_CRITICAL(&s_lock); s_link.overflow=true; portEXIT_CRITICAL(&s_lock);
            }
            offset+=chunk.length;
        }
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.conn_handle==link.conn) {
            if (event->enc_change.status) drop();
            else if (s_pairing && link.state==ELM_BLE_DISCOVERING) discover(link.conn);
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io io={.action=event->passkey.params.action};
        if (io.action==BLE_SM_IOACT_INPUT && CONFIG_OBD2_ELM327_BLE_PASSKEY>=0) {
            io.passkey=CONFIG_OBD2_ELM327_BLE_PASSKEY;
            if (ble_sm_inject_io(event->passkey.conn_handle,&io)) drop();
        } else { ESP_LOGW(TAG,"Pairing requires unsupported user interaction; configure BLE passkey if required"); drop(); }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGW(TAG,"Peer requested new pairing; refusing to silently replace the stored bond");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}

static void watchdog(struct ble_npl_event *event)
{
    (void)event;
    link_t link=snapshot();
    if (s_synced) {
        if (atomic_load(&s_control_pending)) {
            portENTER_CRITICAL(&s_lock);
            s_after_stop=s_control;
            s_target=s_requested_device;
            s_control=CONTROL_NONE;
            atomic_store(&s_control_pending,false);
            portEXIT_CRITICAL(&s_lock);
            apply_control();
            if (s_after_stop==CONTROL_DISCONNECT) message("已断开，选择设备可重新连接");
        } else if (s_after_stop!=CONTROL_NONE && link.conn==BLE_HS_CONN_HANDLE_NONE &&
                   !ble_gap_conn_active() && !ble_gap_disc_active()) {
            control_t action=s_after_stop;
            s_after_stop=CONTROL_NONE;
            if (action==CONTROL_CONNECT && atomic_load(&s_enabled)) connect_device(&s_target);
            else if (action==CONTROL_FORGET) {
                ble_addr_t peer={.type=s_target.type};
                memcpy(peer.val,s_target.address,6);
                int rc=ble_store_util_delete_peer(&peer);
                if (rc) ESP_LOGW(TAG,"Remove saved bond returned %d",rc);
            }
        } else if (!atomic_load(&s_enabled)) {
            if (link.state!=ELM_BLE_OFF && (!link.deadline || now_ms()>link.deadline)) apply_control();
        } else if (link.deadline && now_ms()>link.deadline) {
            if (link.state==ELM_BLE_STOPPING) apply_control();
            else { ESP_LOGW(TAG,"Connection/discovery deadline expired"); drop(); }
        } else if (atomic_exchange(&s_scan_requested,false)) {
            scan();
        }
    }
    ble_npl_callout_reset(&s_watchdog,ble_npl_time_ms_to_ticks32(1000));
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0)) { ESP_LOGE(TAG,"No local BLE identity"); return; }
    s_synced=true;
    s_after_stop=CONTROL_NONE;
    stage(atomic_load(&s_enabled) ? ELM_BLE_IDLE : ELM_BLE_OFF,0);
    ESP_LOGI(TAG,"Bluetooth ready; scanning requires a manual request");
    portENTER_CRITICAL(&s_lock);
    bool reconnect=ble_reconnect_startup(&s_reconnect,atomic_load(&s_enabled),s_devices.has_saved) &&
        !atomic_load(&s_control_pending);
    if (reconnect) s_target=s_devices.saved;
    portEXIT_CRITICAL(&s_lock);
    if (reconnect) {
        s_after_stop=CONTROL_CONNECT;
        ESP_LOGI(TAG,"Startup: attempting remembered device once");
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG,"Host reset: %d",reason);
    s_synced=false;
    portENTER_CRITICAL(&s_lock);
    s_reconnect.allowed=false;
    s_devices.scanning=false;
    ++s_devices.revision;
    portEXIT_CRITICAL(&s_lock);
    clear_link();
}

static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }

static int write_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    if (!current(conn,arg)) return 0;
    bool pairing=error->status && secure(conn,error->status);
    portENTER_CRITICAL(&s_lock);
    s_link.write_status=pairing ? BLE_HS_EAGAIN : error->status;
    portEXIT_CRITICAL(&s_lock);
    xSemaphoreGive(s_write_done);
    return 0;
}

esp_err_t elm_ble_command(const char *command, elm_response_t *response, uint32_t timeout_ms)
{
    if (!command || !response || strlen(command)>18 || strchr(command,'\r') || strchr(command,'\n')) return ESP_ERR_INVALID_ARG;
    elm_response_reset(response);
    if (!s_command_lock || xSemaphoreTake(s_command_lock,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_INVALID_STATE;
    esp_err_t result=ESP_ERR_INVALID_STATE;
    link_t link=snapshot();
    if (!atomic_load(&s_enabled) || atomic_load(&s_control_pending) || link.state!=ELM_BLE_READY) goto done;
    rx_chunk_t chunk;
    while (xQueueReceive(s_rx,&chunk,0)==pdTRUE) {}
    portENTER_CRITICAL(&s_lock); s_link.overflow=false; portEXIT_CRITICAL(&s_lock);
    char wire[20];
    int length=snprintf(wire,sizeof(wire),"%s\r",command);
    atomic_store(&s_transaction, atomic_fetch_add(&s_transaction_counter, 1) + 1);
    recorder_wire(RECORD_TX, link.generation, atomic_load(&s_transaction), 0, 0, length, wire, length, 0);
    int64_t deadline=now_ms()+timeout_ms;
    int rc;
    if (link.tx_properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) rc=ble_gattc_write_no_rsp_flat(link.conn,link.tx,wire,length);
    else {
        while (xSemaphoreTake(s_write_done,0)==pdTRUE) {}
        rc=ble_gattc_write_flat(link.conn,link.tx,wire,length,write_cb,(void *)(uintptr_t)link.generation);
        if (!rc) {
            if (xSemaphoreTake(s_write_done,pdMS_TO_TICKS(timeout_ms))!=pdTRUE) { result=ESP_ERR_TIMEOUT; goto broken; }
            link_t updated=snapshot();
            if (!atomic_load(&s_enabled) || atomic_load(&s_control_pending) || updated.generation!=link.generation) goto done;
            if (updated.write_status==BLE_HS_EAGAIN) goto done;
            rc=updated.write_status;
        }
    }
    if (rc) { ESP_LOGW(TAG,"UART write failed: %d",rc); result=ESP_FAIL; goto broken; }
    while (now_ms()<deadline) {
        link_t updated=snapshot();
        if (!atomic_load(&s_enabled) || atomic_load(&s_control_pending) || updated.generation!=link.generation || updated.state!=ELM_BLE_READY) goto done;
        if (updated.overflow) { result=ESP_ERR_INVALID_SIZE; goto broken; }
        if (xQueueReceive(s_rx,&chunk,pdMS_TO_TICKS(20))!=pdTRUE || chunk.generation!=link.generation) continue;
        elm_response_feed(response,chunk.data,chunk.length);
        if (response->invalid) { result=ESP_ERR_INVALID_RESPONSE; goto broken; }
        if (response->complete) { result=ESP_OK; goto done; }
    }
    result=ESP_ERR_TIMEOUT;
broken:
    /* A late prompt must never be consumed by the next command. */
    elm_ble_reset_link();
done:
    recorder_wire(RECORD_RESULT, link.generation, atomic_load(&s_transaction), 0, 0, 0, NULL, 0, result);
    atomic_store(&s_transaction, 0);
    xSemaphoreGive(s_command_lock);
    return result;
}

esp_err_t elm_ble_init(void)
{
    if (s_command_lock) return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    uint8_t enabled=1;
    esp_err_t settings_err=nvs_open("bluetooth",NVS_READONLY,&nvs);
    if (settings_err==ESP_OK) {
        settings_err=nvs_get_u8(nvs,"enabled",&enabled);
        saved_peer_t saved;
        size_t length=sizeof(saved);
        if (nvs_get_blob(nvs,"peer_v1",&saved,&length)==ESP_OK && length==sizeof(saved) &&
            saved.version==1 && saved.type<=3 && memchr(saved.name,0,sizeof(saved.name))) {
            s_devices.has_saved=true;
            s_devices.saved.type=saved.type;
            memcpy(s_devices.saved.address,saved.address,6);
            memcpy(s_devices.saved.name,saved.name,sizeof(saved.name));
        }
        nvs_close(nvs);
    }
    if (settings_err!=ESP_OK && settings_err!=ESP_ERR_NVS_NOT_FOUND) return settings_err;
    atomic_store(&s_enabled,enabled!=0);
    message("点击扫描设备，再选择一个连接");
    static const char *uuids[][3]={
        {"FFF0","FFF2","FFF1"}, {"FFE0","FFE1","FFE1"},
        {"6E400001-B5A3-F393-E0A9-E50E24DCCA9E","6E400002-B5A3-F393-E0A9-E50E24DCCA9E","6E400003-B5A3-F393-E0A9-E50E24DCCA9E"},
    };
    const char *custom[]={CONFIG_OBD2_ELM327_SERVICE_UUID,CONFIG_OBD2_ELM327_WRITE_UUID,CONFIG_OBD2_ELM327_NOTIFY_UUID};
    bool has_custom=*custom[0] || *custom[1] || *custom[2];
    if (has_custom && (!*custom[0] || !*custom[1] || !*custom[2])) return ESP_ERR_INVALID_ARG;
    s_profile_count=has_custom ? 1 : 3;
    for (unsigned i=0;i<s_profile_count;++i) {
        const char *const *source=has_custom ? custom : uuids[i];
        if (ble_uuid_from_str(&s_profiles[i].service,source[0]) || ble_uuid_from_str(&s_profiles[i].tx,source[1]) || ble_uuid_from_str(&s_profiles[i].rx,source[2])) return ESP_ERR_INVALID_ARG;
    }
    s_rx=xQueueCreate(32,sizeof(rx_chunk_t));
    s_command_lock=xSemaphoreCreateMutex();
    s_write_done=xSemaphoreCreateBinary();
    s_settings_lock=xSemaphoreCreateMutex();
    if (!s_rx || !s_command_lock || !s_write_done || !s_settings_lock) goto fail;
    esp_err_t err=nimble_port_init();
    if (err!=ESP_OK) goto fail;
    ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.reset_cb=on_reset;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=CONFIG_OBD2_ELM327_BLE_PASSKEY>=0 ? BLE_HS_IO_KEYBOARD_ONLY : BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding=1;
    ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_npl_event_init(&s_reset_event,reset_event,NULL);
    ble_npl_callout_init(&s_watchdog,nimble_port_get_dflt_eventq(),watchdog,NULL);
    ble_npl_callout_reset(&s_watchdog,ble_npl_time_ms_to_ticks32(1000));
    atomic_store(&s_available,true);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
fail:
    if (s_rx) vQueueDelete(s_rx);
    if (s_command_lock) vSemaphoreDelete(s_command_lock);
    if (s_write_done) vSemaphoreDelete(s_write_done);
    if (s_settings_lock) vSemaphoreDelete(s_settings_lock);
    s_rx=NULL; s_command_lock=NULL; s_write_done=NULL; s_settings_lock=NULL;
    return ESP_FAIL;
}
