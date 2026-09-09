#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t cloud_service_start(void);
void cloud_request_upload(void);
esp_err_t cloud_request_ota(bool install, bool bench);
bool cloud_ota_busy(void);
