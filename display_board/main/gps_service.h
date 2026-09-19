#pragma once
#include "gps_model.h"
#include "esp_err.h"
esp_err_t gps_service_start(void);
void gps_service_get(gps_fix_t *fix);
