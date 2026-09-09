#pragma once
#include "esp_http_client.h"

void device_http_config(esp_http_client_config_t *config, const char *url);
