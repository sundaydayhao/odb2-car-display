#include "ui.h"
#include <stdatomic.h>
#include <string.h>
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "dashboard.h"
#include "service_panel.h"
#include "device_services.h"
#include "cloud_service.h"
#include "recorder.h"
#include "voice_service.h"
#include "camera_service.h"
#include "elm327_ble.h"
#include "media_panel.h"
#include "media_service.h"
#include "media_library.h"
#include "feature_config.h"

static bool s_ready;
static atomic_uint s_heartbeat;
static atomic_int s_save_result;
static QueueHandle_t s_actions;

static bool center_action(av_action_t action, unsigned value, uint32_t revision)
{
    if (!DEVICE_CAMERA_AVAILABLE && action>=AV_CAMERA_ON && action<=AV_VIEW_PHOTO) return false;
    switch (action) {
    case AV_REFRESH_FILES: media_library_refresh(); return true;
    case AV_CAMERA_ON: camera_set_enabled(true); return true;
    case AV_CAMERA_OFF: camera_set_enabled(false); return true;
    case AV_TAKE_PHOTO: return camera_take_photo();
    case AV_VIEW_PHOTO:
        camera_set_enabled(false);
        return media_library_view_photo(value, revision);
    case AV_VOICE_ON: voice_set_enabled(true); return true;
    case AV_VOICE_OFF: voice_set_enabled(false); return true;
    case AV_VOICE_START: return voice_begin_conversation();
    case AV_VOICE_END: voice_end_conversation(); return true;
    default: return false;
    }
}

static bool panel_action(const panel_request_t *request)
{
    panel_action_t action = request->action;
    if (!DEVICE_CAMERA_AVAILABLE && (action==PANEL_CAMERA_ON || action==PANEL_CAMERA_OFF)) return false;
    if (action == PANEL_CAMERA_OFF) { camera_set_enabled(false); return true; }
    return s_actions && xQueueSend(s_actions, request, 0) == pdTRUE;
}

static void action_task(void *arg)
{
    (void)arg;
    panel_request_t request;
    while (xQueueReceive(s_actions, &request, portMAX_DELAY) == pdTRUE) {
        panel_action_t action = request.action;
        if (action == PANEL_CAMERA_ON) camera_set_enabled(true);
        else if (action == PANEL_CAMERA_OFF) camera_set_enabled(false);
        else if (action == PANEL_SEAL) recorder_seal();
        else if (action == PANEL_UPLOAD_ON || action == PANEL_UPLOAD_OFF) {
            device_settings_t settings;
            device_settings_get(&settings);
            settings.upload_enabled = action == PANEL_UPLOAD_ON;
            if (device_settings_save(&settings) != ESP_OK)
                device_status_set(SERVICE_UPLOAD, SERVICE_ERROR, "设置保存失败");
        } else if (action == PANEL_UPLOAD_NOW) { recorder_seal(); cloud_request_upload(); }
        else if (action == PANEL_OTA_CHECK) cloud_request_ota(false, false);
        else if (action == PANEL_OTA_INSTALL) cloud_request_ota(true, false);
        else if (action == PANEL_WIFI_SCAN) device_network_scan_request();
        else if (action == PANEL_BLUETOOTH_ON || action == PANEL_BLUETOOTH_OFF || action == PANEL_BLUETOOTH_RECONNECT ||
                 action == PANEL_BLUETOOTH_SCAN || action == PANEL_BLUETOOTH_CONNECT ||
                 action == PANEL_BLUETOOTH_DISCONNECT || action == PANEL_BLUETOOTH_FORGET) {
            esp_err_t err;
            if (action==PANEL_BLUETOOTH_SCAN) err=elm_ble_scan();
            else if (action==PANEL_BLUETOOTH_CONNECT) err=elm_ble_connect_device(&request.data.bluetooth);
            else if (action==PANEL_BLUETOOTH_DISCONNECT) err=elm_ble_disconnect();
            else if (action==PANEL_BLUETOOTH_FORGET) err=elm_ble_forget();
            else if (action==PANEL_BLUETOOTH_RECONNECT) err=elm_ble_reconnect();
            else err=elm_ble_set_enabled(action==PANEL_BLUETOOTH_ON);
            atomic_store(&s_save_result,err==ESP_OK ? 1 : -1);
        }
        else if (action == PANEL_WIFI_SAVE) {
            device_settings_t settings;
            device_settings_get(&settings);
            memcpy(settings.ssid, request.data.wifi.ssid, sizeof(settings.ssid));
            memcpy(settings.password, request.data.wifi.password, sizeof(settings.password));
            esp_err_t err = device_settings_save(&settings);
            memset(&settings, 0, sizeof(settings));
            if (err == ESP_OK) device_network_reconfigure();
            atomic_store(&s_save_result, err == ESP_OK ? 1 : -1);
        } else if (action == PANEL_PREFERENCES_SAVE) {
            nvs_handle_t nvs;
            esp_err_t err = nvs_open("display", NVS_READWRITE, &nvs);
            if (err == ESP_OK) {
                err = nvs_set_blob(nvs, "prefs_v1", &request.data.preferences, sizeof(request.data.preferences));
                if (err == ESP_OK) err = nvs_commit(nvs);
                nvs_close(nvs);
            }
            if (bsp_display_brightness_set(request.data.preferences.brightness) != ESP_OK) err = ESP_FAIL;
            atomic_store(&s_save_result, err == ESP_OK ? 1 : -1);
        }
        memset(&request, 0, sizeof(request));
    }
}

static void service_tick(lv_timer_t *timer)
{
    (void)timer;
    device_status_t status;
    device_status_get(&status);
    for (int i = 0; i < SERVICE_COUNT; ++i) service_panel_status(i, status.state[i], status.detail[i]);
    elm_ble_status_t bt=elm_ble_status();
    unsigned bt_state=SERVICE_BUSY;
    const char *bt_detail="正在查找适配器";
    if (!bt.available) { bt_state=SERVICE_WAITING; bt_detail="蓝牙尚未就绪"; }
    else if (bt.state==ELM_BLE_OFF) { bt_state=SERVICE_OFF; bt_detail="蓝牙已关闭"; }
    else if (bt.state==ELM_BLE_IDLE) { bt_state=SERVICE_WAITING; bt_detail="等待选择设备连接"; }
    else if (bt.state==ELM_BLE_STOPPING) bt_detail="正在断开适配器";
    else if (bt.state==ELM_BLE_CONNECTING) bt_detail="正在连接适配器";
    else if (bt.state==ELM_BLE_DISCOVERING) bt_detail="正在准备车辆连接";
    else if (bt.state==ELM_BLE_READY) { bt_state=SERVICE_READY; bt_detail="已连接适配器"; }
    service_panel_bluetooth(bt.available,bt.enabled,bt_state,bt_detail,bt.address);
    ble_devices_t bluetooth;
    elm_ble_devices_get(&bluetooth);
    service_panel_bluetooth_devices(&bluetooth);
    device_settings_t settings;
    device_settings_get(&settings);
    service_panel_upload_enabled(settings.upload_enabled);
    service_panel_network_config(settings.ssid, settings.password);
    memset(&settings, 0, sizeof(settings));
    wifi_networks_t networks;
    device_network_scan_get(&networks);
    service_panel_network_scan(&networks);
    media_status_t media;
    media_service_status(&media);
    media_panel_update(&media);
    av_status_t av = {
        .camera_enabled = camera_is_enabled(),
        .voice_enabled = voice_is_enabled(), .voice_active = voice_is_active(),
        .camera_state = status.state[SERVICE_CAMERA], .voice_state = status.state[SERVICE_VOICE],
    };
    snprintf(av.camera_detail, sizeof(av.camera_detail), "%s", status.detail[SERVICE_CAMERA]);
    snprintf(av.voice_detail, sizeof(av.voice_detail), "%s", status.detail[SERVICE_VOICE]);
    media_library_status(&av.library);
    media_panel_av_update(&av);
    int result = atomic_exchange(&s_save_result, 0);
    if (result) service_panel_save_result(result > 0);
    atomic_store(&s_heartbeat, (uint32_t)(esp_timer_get_time() / 1000));
}

bool ui_init(void)
{
    s_actions = xQueueCreate(16, sizeof(panel_request_t));
    if (!s_actions) return false;
    /* Settings writes disable the flash cache; a P4 SPM stack is unsuitable. */
    if (xTaskCreateWithCaps(action_task, "ui_actions", 6144, NULL, 3, NULL,
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT) != pdPASS) return false;
    /* DSI/PPA can read PSRAM on P4. BSP's default draw + rotation buffers
     * otherwise occupy about 200 KiB of the RAM needed by I2S and TLS DMA. */
    bsp_display_cfg_t display_config = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {.hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {.phy_clk_src = 0, .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS}},
        .flags = {.buff_dma = false, .buff_spiram = true, .sw_rotate = true},
    };
    lv_display_t *display = bsp_display_start_with_config(&display_config);
    if (!display) return false;
    if (!bsp_display_lock(0)) return false;
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    lv_obj_set_size(screen, width, height);
    lv_obj_t *dashboard = lv_obj_create(screen);
    dashboard_create_on(dashboard, width, height);
    media_panel_handlers(media_service_request);
    media_panel_av_handlers(center_action, media_library_files);
    service_panel_create(screen, dashboard_service_toolbar(), panel_action);
    panel_preferences_t preferences = PANEL_PREFERENCES_DEFAULT;
    bool restored = false;
    nvs_handle_t nvs;
    if (nvs_open("display", NVS_READONLY, &nvs) == ESP_OK) {
        panel_preferences_t saved;
        size_t size = sizeof(saved);
        if (nvs_get_blob(nvs, "prefs_v1", &saved, &size) == ESP_OK && size == sizeof(saved) &&
            service_panel_preferences_valid(&saved)) { preferences = saved; restored = true; }
        nvs_close(nvs);
    }
    service_panel_preferences(&preferences);
    ESP_LOGI("ui", "Appearance: %s theme=%u daylight=%u brightness=%u", restored ? "saved" : "default",
             preferences.theme, preferences.daylight, preferences.brightness);
    lv_timer_create(service_tick, 250, NULL);
    bsp_display_unlock();
    if (bsp_display_brightness_set(preferences.brightness) != ESP_OK) return false;
    s_ready = true;
    return true;
}

bool ui_healthy(void)
{
    uint32_t stamp = atomic_load(&s_heartbeat);
    return s_ready && stamp && (uint32_t)(esp_timer_get_time() / 1000) - stamp < 2000;
}
bool ui_open_media(void)
{
    if (!s_ready || !bsp_display_lock(1000)) return false;
    dashboard_select_page(DASHBOARD_PAGE_COUNT - 1);
    bsp_display_unlock();
    return true;
}

bool ui_open_av(av_tab_t tab)
{
    if (tab>AV_FM || (!DEVICE_CAMERA_AVAILABLE && tab==AV_CAMERA) || !s_ready || !bsp_display_lock(1000)) return false;
    dashboard_select_page(DASHBOARD_PAGE_COUNT - 1);
    media_panel_select(tab);
    bsp_display_unlock(); return true;
}

void ui_camera_frame(const uint16_t *pixels)
{
    if (s_ready && bsp_display_lock(30)) {
        service_panel_camera_frame(pixels);
        media_panel_frame(pixels, false);
        bsp_display_unlock();
    }
}

void ui_camera_clear(void)
{
    if (s_ready && bsp_display_lock(100)) {
        service_panel_camera_clear();
        media_panel_camera_clear();
        bsp_display_unlock();
    }
}

void ui_media_photo_frame(const uint16_t *pixels)
{
    if (s_ready && bsp_display_lock(200)) {
        media_panel_frame(pixels, true);
        bsp_display_unlock();
    }
}

void ui_render_vehicle_data(const obd2_vehicle_data_t *data)
{
    if (!data || !s_ready) return;
    const dashboard_data_t view = {
        .value = {
            [DASH_SPEED] = data->speed_kph, [DASH_RPM] = data->rpm,
            [DASH_FUEL] = data->fuel_percent, [DASH_COOLANT] = data->coolant_temp_c,
            [DASH_VOLTAGE] = data->control_voltage_mv, [DASH_LOAD] = data->engine_load_percent,
            [DASH_THROTTLE] = data->throttle_percent, [DASH_INTAKE_TEMP] = data->intake_temp_c,
            [DASH_OIL_TEMP] = data->oil_temp_c, [DASH_MANIFOLD] = data->manifold_pressure_kpa,
        },
        .valid = {
            [DASH_SPEED] = data->speed_valid, [DASH_RPM] = data->rpm_valid,
            [DASH_FUEL] = data->fuel_valid, [DASH_COOLANT] = data->coolant_temp_valid,
            [DASH_VOLTAGE] = data->control_voltage_valid, [DASH_LOAD] = data->engine_load_valid,
            [DASH_THROTTLE] = data->throttle_valid, [DASH_INTAKE_TEMP] = data->intake_temp_valid,
            [DASH_OIL_TEMP] = data->oil_temp_valid, [DASH_MANIFOLD] = data->manifold_pressure_valid,
        },
        .link_state = data->link_state == OBD_LINK_SCANNING ? DASH_LINK_SCANNING :
                      data->link_state == OBD_LINK_CONNECTING ? DASH_LINK_CONNECTING :
                      data->link_state == OBD_LINK_INITIALIZING ? DASH_LINK_INITIALIZING : DASH_LINK_WAITING,
    };
    if (bsp_display_lock(30)) { dashboard_update(&view); bsp_display_unlock(); }
}
