#pragma once
#include "media_model.h"
#include "esp_err.h"

esp_err_t media_service_start(void);
/* Nonblocking: true means copied into the bounded command queue. */
bool media_service_request(const media_request_t *request);
void media_service_status(media_status_t *out);
bool media_service_online_saved(char *url,size_t url_size,char *title,size_t title_size);
