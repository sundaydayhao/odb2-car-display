#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { OBD_LINK_WAITING, OBD_LINK_SCANNING, OBD_LINK_CONNECTING, OBD_LINK_INITIALIZING, OBD_LINK_ACTIVE } obd2_link_state_t;
typedef struct {
    bool speed_valid, fuel_valid, rpm_valid, coolant_temp_valid, engine_load_valid, throttle_valid, control_voltage_valid;
    bool intake_temp_valid, oil_temp_valid, manifold_pressure_valid;
    uint16_t speed_kph, rpm, control_voltage_mv, manifold_pressure_kpa;
    uint8_t fuel_percent, engine_load_percent, throttle_percent;
    int16_t coolant_temp_c, intake_temp_c, oil_temp_c;
    int64_t last_update_ms;
    obd2_link_state_t link_state;
} obd2_vehicle_data_t;
