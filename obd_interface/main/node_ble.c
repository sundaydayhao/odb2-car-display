#include "node_ble.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
/* ESP-IDF 5.5 NimBLE examples declare this initializer explicitly. */
void ble_store_config_init(void);

typedef struct { uint32_t generation; uint16_t len; char text[240]; } outgoing_t;
static QueueHandle_t tx_queue;
static node_ble_rx_fn on_rx;
static atomic_uint generation, dropped;
static atomic_uint connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_bool subscribed, stopping;
static uint8_t addr_type;
static uint16_t notify_handle;
static const ble_uuid16_t service_uuid = BLE_UUID16_INIT(0xFFF0);
static const ble_uuid16_t rx_uuid = BLE_UUID16_INIT(0xFFF2);
static const ble_uuid16_t tx_uuid = BLE_UUID16_INIT(0xFFF1);

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t n = OS_MBUF_PKTLEN(ctxt->om); uint8_t bytes[256];
        if (n > sizeof(bytes)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (!atomic_load(&subscribed)) return BLE_ATT_ERR_UNLIKELY;
        if (ble_hs_mbuf_to_flat(ctxt->om, bytes, sizeof(bytes), &n)) return BLE_ATT_ERR_UNLIKELY;
        on_rx(bytes, n, atomic_load(&generation)); return 0;
    }
    const char *name = "OBDNode CAN/BLE revA";
    return os_mbuf_append(ctxt->om, name, strlen(name)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_uuid.u,
     .characteristics = (struct ble_gatt_chr_def[]) {
        {.uuid = &tx_uuid.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &notify_handle},
        {.uuid = &rx_uuid.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP}, {0}}}, {0}
};
static void advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!event->connect.status) {
            atomic_store(&connection, event->connect.conn_handle);
            atomic_fetch_add(&generation, 1); atomic_store(&subscribed, false);
            struct ble_gap_upd_params p = {.itvl_min = 12, .itvl_max = 24, .latency = 0, .supervision_timeout = 400};
            ble_gap_update_params(event->connect.conn_handle, &p);
        } else advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE); atomic_store(&subscribed, false);
        atomic_fetch_add(&generation, 1); on_rx(NULL, 0, atomic_load(&generation)); advertise(); break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == notify_handle) atomic_store(&subscribed, event->subscribe.cur_notify);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    return 0;
}
static void advertise(void) {
    if (atomic_load(&stopping)) return;
    struct ble_hs_adv_fields f = {0};
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids16 = (ble_uuid16_t *)&service_uuid; f.num_uuids16 = 1; f.uuids16_is_complete = 1;
    f.name = (uint8_t *)ble_svc_gap_device_name(); f.name_len = strlen((char *)f.name); f.name_is_complete = 1;
    if (ble_gap_adv_set_fields(&f)) { ESP_LOGE("node_ble", "advertisement fields rejected"); return; }
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = 800, .itvl_max = 1000};
    int rc = ble_gap_adv_start(addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc && rc != BLE_HS_EALREADY) ESP_LOGE("node_ble", "advertise rc=%d", rc);
}
static void sync_cb(void) {
    if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0, &addr_type)) advertise();
}
static void reset_cb(int reason) {
    ESP_LOGW("node_ble", "host reset=%d", reason);
    atomic_store(&connection, BLE_HS_CONN_HANDLE_NONE); atomic_store(&subscribed, false);
    atomic_fetch_add(&generation, 1);
    on_rx(NULL, 0, atomic_load(&generation));
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
static void sender(void *arg) {
    (void)arg; outgoing_t out;
    for (;;) {
        if (!xQueueReceive(tx_queue, &out, portMAX_DELAY)) continue;
        size_t pos = 0;
        while (pos < out.len && out.generation == atomic_load(&generation) && atomic_load(&subscribed)) {
            uint16_t conn = atomic_load(&connection);
            if (conn == BLE_HS_CONN_HANDLE_NONE) break;
            uint16_t mtu = ble_att_mtu(conn); size_t count = out.len - pos;
            if (mtu < 23) mtu = 23;
            if (count > (size_t)(mtu - 3)) count = mtu - 3;
            int rc = BLE_HS_ENOMEM;
            for (unsigned retry = 0; retry < 40 && out.generation == atomic_load(&generation); retry++) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(out.text + pos, count);
                rc = om ? ble_gatts_notify_custom(conn, notify_handle, om) : BLE_HS_ENOMEM;
                if (!rc) break;
                if (rc != BLE_HS_ENOMEM && rc != BLE_HS_EAGAIN) break;
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            if (rc) { atomic_fetch_add(&dropped, 1); ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM); break; }
            pos += count;
        }
    }
}
esp_err_t node_ble_init(node_ble_rx_fn rx) {
    on_rx = rx; tx_queue = xQueueCreate(64, sizeof(outgoing_t));
    if (!tx_queue) return ESP_ERR_NO_MEM;
    esp_err_t err = nimble_port_init(); if (err != ESP_OK) return err;
    ble_hs_cfg.sync_cb = sync_cb; ble_hs_cfg.reset_cb = reset_cb;
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set("OBDNode-C3");
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) return ESP_FAIL;
    ble_store_config_init();
    if (xTaskCreate(sender, "ble_output", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    nimble_port_freertos_init(host_task); return ESP_OK;
}
bool node_ble_send(const char *s, size_t n) {
    if (!atomic_load(&subscribed) || n > sizeof(((outgoing_t *)0)->text)) return false;
    outgoing_t out = {.generation = atomic_load(&generation), .len = n}; memcpy(out.text, s, n);
    if (xQueueSend(tx_queue, &out, 0)) return true;
    atomic_fetch_add(&dropped, 1); return false;
}
bool node_ble_connected(void) { return atomic_load(&connection) != BLE_HS_CONN_HANDLE_NONE; }
uint32_t node_ble_generation(void) { return atomic_load(&generation); }
uint32_t node_ble_dropped(void) { return atomic_load(&dropped); }
void node_ble_stop(void) { atomic_store(&stopping, true); ble_gap_adv_stop(); }
void node_ble_disconnect(void) {
    uint16_t conn = atomic_load(&connection);
    if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}
