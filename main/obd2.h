#pragma once

#include "esp_err.h"
#include "obd2_data.h"

esp_err_t obd2_init(void);
esp_err_t obd2_read_vehicle_data(obd2_vehicle_data_t *out);
