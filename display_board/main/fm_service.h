#pragma once
#include "esp_err.h"
#include "fm_model.h"
esp_err_t fm_service_start(void);
bool fm_service_request(const fm_request_t *request);
void fm_service_status(fm_status_t *status);
