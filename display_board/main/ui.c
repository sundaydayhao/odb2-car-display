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
#include "map_panel.h"
#include "gps_service.h"
#include "fm_service.h"
#include "alarm_service.h"
#include "feature_config.h"
#include "ui_update.h"

static bool s_ready;
static atomic_uint s_heartbeat;
static atomic_int s_save_result;
static atomic_int s_fm_save_result;
static QueueHandle_t s_actions;
/* Only service_tick owns these snapshots. Keep the large status/library
 * aggregates off the 7 KiB LVGL stack, including nested page updates. */
typedef struct {
    device_status_t status;
    device_settings_t settings;
    ble_devices_t bluetooth;
    wifi_networks_t networks;
    media_status_t media;
    fm_status_t radio;
    alarm_status_t alarms;
    av_status_t av;
} ui_service_snapshot_t;
static ui_service_snapshot_t *s_snapshot;
static TaskHandle_t s_lvgl_task;

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
static bool fm_presets_save(const fm_presets_t *presets)
{
    panel_request_t request={.action=PANEL_FM_PRESETS_SAVE,.data.fm_presets=*presets};
    return panel_action(&request);
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
        else if (action == PANEL_VOICE_ON) voice_set_enabled(true);
        else if (action == PANEL_VOICE_OFF) voice_set_enabled(false);
        else if (action == PANEL_VOICE_START) voice_begin_conversation();
        else if (action == PANEL_VOICE_END) voice_end_conversation();
        else if (action == PANEL_VOLUME) {
            media_request_t volume={.action=MEDIA_VOLUME,.value=request.data.volume};
            if (!media_service_request(&volume)) atomic_store(&s_save_result,-1);
        }
        else if(action==PANEL_ALARM_ADD){
            int id=alarm_service_set(request.data.alarm.hour,request.data.alarm.minute,request.data.alarm.daily,0,"提醒");
            atomic_store(&s_save_result,id>0?1:-1);
        }
        else if(action==PANEL_ALARM_DELETE)atomic_store(&s_save_result,alarm_service_delete(request.data.alarm.id)?1:-1);
        else if(action==PANEL_ALARM_DISMISS)alarm_service_dismiss();
        else if (action == PANEL_WIFI_SAVE) {
            device_settings_t settings;
            device_settings_get(&settings);
            memcpy(settings.ssid, request.data.wifi.ssid, sizeof(settings.ssid));
            memcpy(settings.password, request.data.wifi.password, sizeof(settings.password));
            esp_err_t err = device_settings_save(&settings);
            memset(&settings, 0, sizeof(settings));
            if (err == ESP_OK) device_network_reconfigure();
            atomic_store(&s_save_result, err == ESP_OK ? 1 : -1);
        } else if (action == PANEL_FM_PRESETS_SAVE) {
            nvs_handle_t nvs;
            esp_err_t err=nvs_open("display",NVS_READWRITE,&nvs);
            if (err==ESP_OK) {
                err=nvs_set_blob(nvs,"fm_slots_v1",&request.data.fm_presets,sizeof(request.data.fm_presets));
                if (err==ESP_OK) err=nvs_commit(nvs);
                nvs_close(nvs);
            }
            atomic_store(&s_fm_save_result,err==ESP_OK?1:-1);
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
    s_lvgl_task=xTaskGetCurrentTaskHandle();
    ui_service_snapshot_t *v=s_snapshot;
    atomic_store(&s_heartbeat, (uint32_t)(esp_timer_get_time() / 1000));
    /* Flash settings commits must not make the LVGL task wait on their lock. */
    if (!device_ui_snapshot(&v->status, &v->settings)) return;
    for (int i = 0; i < SERVICE_COUNT; ++i) service_panel_status(i, v->status.state[i], v->status.detail[i]);
    service_panel_voice(voice_is_enabled(), voice_is_active());
    gps_fix_t gps;
    gps_service_get(&gps);
    uint32_t gps_now=(uint32_t)(esp_timer_get_time()/1000);
    bool positioned=gps_fix_current(&gps,gps_now);
    service_panel_gps(gps.enabled,gps.error?SERVICE_ERROR:positioned?SERVICE_READY:SERVICE_WAITING,
        gps.error?"定位串口异常":positioned?"卫星定位正常":"等待卫星定位");
    map_panel_update(&gps,gps_now);
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
    elm_ble_devices_get(&v->bluetooth);
    service_panel_bluetooth_devices(&v->bluetooth);
    service_panel_upload_enabled(v->settings.upload_enabled);
    service_panel_network_config(v->settings.ssid, v->settings.password);
    memset(&v->settings, 0, sizeof(v->settings));
    device_network_scan_get(&v->networks);
    service_panel_network_scan(&v->networks);
    media_service_status(&v->media);
    media_panel_update(&v->media);
    service_panel_media(&v->media);
    fm_service_status(&v->radio);
    media_panel_fm_update(&v->radio);
    service_panel_fm(&v->radio);
    if (alarm_service_try_status(&v->alarms)) service_panel_alarms(&v->alarms);
    memset(&v->av,0,sizeof(v->av));
    v->av.camera_enabled=camera_is_enabled();
    v->av.voice_enabled=voice_is_enabled();v->av.voice_active=voice_is_active();
    v->av.camera_state=v->status.state[SERVICE_CAMERA];v->av.voice_state=v->status.state[SERVICE_VOICE];
    snprintf(v->av.camera_detail, sizeof(v->av.camera_detail), "%s", v->status.detail[SERVICE_CAMERA]);
    snprintf(v->av.voice_detail, sizeof(v->av.voice_detail), "%s", v->status.detail[SERVICE_VOICE]);
    media_library_status(&v->av.library);
    if(camera_photo_state()==1)snprintf(v->av.library.photo_detail,sizeof(v->av.library.photo_detail),"正在等待拍照");
    else if(camera_photo_state()==2)snprintf(v->av.library.photo_detail,sizeof(v->av.library.photo_detail),"正在保存照片");
    else if(camera_photo_state()==4)snprintf(v->av.library.photo_detail,sizeof(v->av.library.photo_detail),"拍照失败，请检查摄像头和 SD 卡");
    media_panel_av_update(&v->av);
    int result = atomic_exchange(&s_save_result, 0);
    if (result) service_panel_save_result(result > 0);
    result=atomic_exchange(&s_fm_save_result,0);
    if (result) media_panel_fm_save_result(result>0);
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
        .buffer_size = BSP_LCD_H_RES * UI_DRAW_LINES,
        .double_buffer = true,
        .hw_cfg = {.hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {.phy_clk_src = 0, .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS}},
        .flags = {.buff_dma = false, .buff_spiram = true, .sw_rotate = true},
    };
    /* Stay below microphone capture priority (6), above storage/network work.
     * Leave affinity flexible because speech detection already uses core 1. */
    display_config.lvgl_port_cfg.task_priority = 5;
    display_config.lvgl_port_cfg.timer_period_ms = 2;
    lv_display_t *display = bsp_display_start_with_config(&display_config);
    if (!display) return false;
    if (!bsp_display_lock(0)) return false;
    ui_display_timing(display, bsp_display_get_input_dev());
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    lv_obj_set_size(screen, width, height);
    lv_obj_t *dashboard = lv_obj_create(screen);
    dashboard_create_on(dashboard, width, height);
    media_panel_handlers(media_service_request);
    media_panel_av_handlers(center_action, media_library_files);
    media_panel_fm_handler(fm_presets_save);
    media_panel_fm_control(fm_service_request);
    service_panel_create(screen, dashboard_service_toolbar(), panel_action);
    panel_preferences_t preferences = PANEL_PREFERENCES_DEFAULT;
    bool restored = false;
    nvs_handle_t nvs;
    if (nvs_open("display", NVS_READONLY, &nvs) == ESP_OK) {
        panel_preferences_t saved;
        size_t size = sizeof(saved);
        if (nvs_get_blob(nvs, "prefs_v1", &saved, &size) == ESP_OK && size == sizeof(saved) &&
            service_panel_preferences_valid(&saved)) { preferences = saved; restored = true; }
        fm_presets_t presets;
        size=sizeof(presets);
        if (nvs_get_blob(nvs,"fm_slots_v1",&presets,&size)==ESP_OK && size==sizeof(presets))
            media_panel_fm_restore(&presets);
        nvs_close(nvs);
    }
    service_panel_preferences(&preferences);
    ESP_LOGI("ui", "Appearance: %s theme=%u daylight=%u brightness=%u", restored ? "saved" : "default",
             preferences.theme, preferences.daylight, preferences.brightness);
    s_snapshot=lv_malloc(sizeof(*s_snapshot));
    if(!s_snapshot){bsp_display_unlock();return false;}
    memset(s_snapshot,0,sizeof(*s_snapshot));
    lv_timer_create(service_tick, UI_STATUS_PERIOD_MS, NULL);
    ESP_LOGI("ui", "Touch=%dms refresh=%dms status=%dms draw=2x%d lines in PSRAM",
             UI_TOUCH_PERIOD_MS, UI_REFRESH_PERIOD_MS, UI_STATUS_PERIOD_MS, UI_DRAW_LINES);
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
    dashboard_select_page(DASHBOARD_PAGE_MEDIA);
    bsp_display_unlock();
    return true;
}
bool ui_open_vehicle_page(unsigned page)
{
    if(page>=5 || !s_ready || !bsp_display_lock(1000))return false;
    service_panel_close();dashboard_select_page(page);bsp_display_unlock();return true;
}
bool ui_open_map(void)
{
    if(!s_ready || !bsp_display_lock(1000))return false;
    service_panel_close();dashboard_select_page(DASHBOARD_PAGE_MAP_TRACK);
    bsp_display_unlock();return true;
}
bool ui_map_status(map_panel_stats_t *out)
{
    if(!out || !s_ready || !bsp_display_lock(100))return false;
    map_panel_stats(out);
    if(s_lvgl_task)out->ui_stack_min_bytes=uxTaskGetStackHighWaterMark(s_lvgl_task);
    bsp_display_unlock();return true;
}
bool ui_open_alarms(void)
{
    if(!s_ready || !bsp_display_lock(1000))return false;
    service_panel_open_alarms();bsp_display_unlock();return true;
}

bool ui_open_av(av_tab_t tab)
{
    if (tab > AV_FM || (!DEVICE_CAMERA_AVAILABLE && tab == AV_CAMERA) || !s_ready || !bsp_display_lock(1000)) return false;
    if (tab == AV_VOICE) {
        service_panel_open_voice();
        bsp_display_unlock();
        return true;
    }
    service_panel_close();dashboard_select_page(DASHBOARD_PAGE_MEDIA);
    media_panel_select(tab);
    if(tab==AV_CAMERA)media_panel_show_camera();
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
            [DASH_BOOST] = data->boost_kpa, [DASH_INSTANT_FUEL] = data->instant_fuel_tenths,
            [DASH_AVERAGE_FUEL] = data->average_fuel_tenths, [DASH_TRIP_DISTANCE] = data->trip_distance_tenths,
            [DASH_TRIP_MINUTES] = data->trip_minutes, [DASH_TRIP_SPEED] = data->trip_speed_tenths,
            [DASH_TRIP_FUEL] = data->trip_fuel_hundredths, [DASH_TRIP_IDLE] = data->trip_idle_seconds,
        },
        .valid = {
            [DASH_SPEED] = data->speed_valid, [DASH_RPM] = data->rpm_valid,
            [DASH_FUEL] = data->fuel_valid, [DASH_COOLANT] = data->coolant_temp_valid,
            [DASH_VOLTAGE] = data->control_voltage_valid, [DASH_LOAD] = data->engine_load_valid,
            [DASH_THROTTLE] = data->throttle_valid, [DASH_INTAKE_TEMP] = data->intake_temp_valid,
            [DASH_OIL_TEMP] = data->oil_temp_valid, [DASH_MANIFOLD] = data->manifold_pressure_valid,
            [DASH_BOOST] = data->boost_valid, [DASH_INSTANT_FUEL] = data->instant_fuel_valid,
            [DASH_AVERAGE_FUEL] = data->average_fuel_valid, [DASH_TRIP_DISTANCE] = data->trip_valid,
            [DASH_TRIP_MINUTES] = data->trip_valid, [DASH_TRIP_SPEED] = data->trip_valid,
            [DASH_TRIP_FUEL] = data->trip_fuel_valid, [DASH_TRIP_IDLE] = data->trip_idle_valid,
        },
        .link_state = data->link_state == OBD_LINK_SCANNING ? DASH_LINK_SCANNING :
                      data->link_state == OBD_LINK_CONNECTING ? DASH_LINK_CONNECTING :
                      data->link_state == OBD_LINK_INITIALIZING ? DASH_LINK_INITIALIZING : DASH_LINK_WAITING,
    };
    if (bsp_display_lock(30)) { dashboard_update(&view); bsp_display_unlock(); }
}
