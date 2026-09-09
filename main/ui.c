#include "ui.h"
#include <stdatomic.h>
#include "bsp/esp-bsp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "dashboard.h"
#include "service_panel.h"
#include "device_services.h"
#include "cloud_service.h"
#include "recorder.h"
#include "voice_service.h"
#include "camera_service.h"

static bool s_ready;
static atomic_uint s_heartbeat;
static QueueHandle_t s_actions;

static void panel_action(panel_action_t action)
{
    if (action == PANEL_VOICE_DOWN || action == PANEL_VOICE_UP) {
        voice_set_pressed(action == PANEL_VOICE_DOWN);
        return;
    }
    if (action == PANEL_CAMERA_OFF) camera_set_enabled(false);
    if (s_actions) xQueueSend(s_actions, &action, 0);
}

static void action_task(void *arg)
{
    (void)arg;
    panel_action_t action;
    while (xQueueReceive(s_actions, &action, portMAX_DELAY) == pdTRUE) {
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
    }
}

static void service_tick(lv_timer_t *timer)
{
    (void)timer;
    device_status_t status;
    device_status_get(&status);
    for (int i = 0; i < SERVICE_COUNT; ++i) service_panel_status(i, status.state[i], status.detail[i]);
    device_settings_t settings;
    device_settings_get(&settings);
    service_panel_upload_enabled(settings.upload_enabled);
    if (camera_is_enabled() && status.state[SERVICE_CAMERA] == SERVICE_READY) service_panel_camera_visible(true);
    atomic_store(&s_heartbeat, (uint32_t)(esp_timer_get_time() / 1000));
}

bool ui_init(void)
{
    s_actions = xQueueCreate(16, sizeof(panel_action_t));
    if (!s_actions) return false;
    if (xTaskCreate(action_task, "ui_actions", 6144, NULL, 3, NULL) != pdPASS) return false;
    lv_display_t *display = bsp_display_start();
    if (!display) return false;
    if (!bsp_display_lock(0)) return false;
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    int width = lv_display_get_horizontal_resolution(display), height = lv_display_get_vertical_resolution(display);
    lv_obj_set_size(screen, width, height);
    lv_obj_t *dashboard = lv_obj_create(screen);
    dashboard_create_on(dashboard, width, height);
    service_panel_create(screen, dashboard_service_toolbar(), panel_action);
    lv_timer_create(service_tick, 250, NULL);
    bsp_display_unlock();
    if (bsp_display_backlight_on() != ESP_OK) return false;
    s_ready = true;
    return true;
}

bool ui_healthy(void)
{
    uint32_t stamp = atomic_load(&s_heartbeat);
    return s_ready && stamp && (uint32_t)(esp_timer_get_time() / 1000) - stamp < 2000;
}

void ui_camera_frame(const uint16_t *pixels)
{
    if (s_ready && bsp_display_lock(30)) {
        service_panel_camera_frame(pixels);
        bsp_display_unlock();
    }
}

void ui_camera_clear(void)
{
    if (s_ready && bsp_display_lock(100)) {
        service_panel_camera_clear();
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
