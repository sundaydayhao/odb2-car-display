#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

enum { DASHBOARD_THEME_COUNT = 3, DASHBOARD_PAGE_COUNT = 6 };
typedef struct {
    uint32_t bg, ink, muted, line, track, panel, accent, hero, secondary, green, amber, red, on_accent, tint;
} dashboard_palette_t;
/* Shared by the instruments, settings and appearance preview cards. */
const dashboard_palette_t *dashboard_palette(uint8_t theme, bool daylight);

typedef enum {
    DASH_SPEED,
    DASH_RPM,
    DASH_FUEL,
    DASH_COOLANT,
    DASH_VOLTAGE,
    DASH_LOAD,
    DASH_THROTTLE,
    DASH_RANGE,
    DASH_INSTANT_FUEL,
    DASH_AVERAGE_FUEL,
    DASH_OIL_TEMP,
    DASH_TRANS_TEMP,
    DASH_INTAKE_TEMP,
    DASH_OIL_PRESSURE,
    DASH_BOOST,
    DASH_MANIFOLD,
    DASH_GEAR,
    DASH_TIRE_FL,
    DASH_TIRE_FR,
    DASH_TIRE_RL,
    DASH_TIRE_RR,
    DASH_TIRE_TEMP_FL,
    DASH_TIRE_TEMP_FR,
    DASH_TIRE_TEMP_RL,
    DASH_TIRE_TEMP_RR,
    DASH_TRIP_DISTANCE,
    DASH_TRIP_MINUTES,
    DASH_TRIP_SPEED,
    DASH_TRIP_FUEL,
    DASH_TRIP_IDLE,
    DASH_LONG_G,
    DASH_LATERAL_G,
    DASH_METRIC_COUNT,
} dashboard_metric_t;

/* Voltage: mV. Fuel economy, distance, trip speed: tenths. Pressures: kPa.
 * Trip fuel: hundredths of a litre. Acceleration: hundredths of g. Idle: seconds.
 * All other metrics use displayed units. Unsupported inputs remain invalid. */
typedef enum { DASH_LINK_WAITING, DASH_LINK_SCANNING, DASH_LINK_CONNECTING, DASH_LINK_INITIALIZING } dashboard_link_t;
typedef struct {
    int32_t value[DASH_METRIC_COUNT];
    bool valid[DASH_METRIC_COUNT];
    dashboard_link_t link_state;
} dashboard_data_t;

void dashboard_create(lv_display_t *display);
void dashboard_create_on(lv_obj_t *parent, int width, int height);
lv_obj_t *dashboard_service_toolbar(void);
void dashboard_update(const dashboard_data_t *data);
void dashboard_select_page(uint8_t page);
uint8_t dashboard_active_page(void);
void dashboard_select_theme(uint8_t theme);
uint8_t dashboard_active_theme(void);
void dashboard_set_demo(bool enabled);
bool dashboard_demo_enabled(void);
void dashboard_set_daylight(bool enabled);
bool dashboard_daylight_enabled(void);
bool dashboard_turbo_enabled(void);
uint8_t dashboard_pressure_unit(void);
void dashboard_set_turbo(bool enabled);
void dashboard_set_pressure_unit(uint8_t unit);
void dashboard_set_settings_handler(void (*handler)(void));
