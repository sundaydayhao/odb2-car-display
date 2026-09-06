#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

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
typedef struct {
    int32_t value[DASH_METRIC_COUNT];
    bool valid[DASH_METRIC_COUNT];
} dashboard_data_t;

void dashboard_create(lv_display_t *display);
void dashboard_update(const dashboard_data_t *data);
void dashboard_select_page(uint8_t page);
uint8_t dashboard_active_page(void);
void dashboard_select_theme(uint8_t theme);
uint8_t dashboard_active_theme(void);
void dashboard_set_demo(bool enabled);
bool dashboard_demo_enabled(void);
void dashboard_set_daylight(bool enabled);
