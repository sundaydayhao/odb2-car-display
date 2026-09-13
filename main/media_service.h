#pragma once
#include "media_model.h"
#include "esp_err.h"

esp_err_t media_service_start(void);
/* Nonblocking: true means copied into the bounded command queue. */
bool media_service_request(const media_request_t *request);
void media_service_status(media_status_t *out);
void media_service_custom_urls(char music[MEDIA_URL_SIZE], char radio[MEDIA_URL_SIZE]);
