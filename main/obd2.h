#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool speed_valid;
    bool fuel_valid;
    bool rpm_valid;
    uint16_t speed_kph;
    uint8_t fuel_percent;
    uint16_t rpm;
    int64_t last_update_ms;
} obd2_vehicle_data_t;

esp_err_t obd2_init(void);
esp_err_t obd2_read_vehicle_data(obd2_vehicle_data_t *out);
