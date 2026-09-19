#pragma once
#include "alarm_model.h"
#include "esp_err.h"
esp_err_t alarm_service_start(void);
void alarm_service_status(alarm_status_t *out);
/* UI polling must not wait while a changed alarm is committed to flash. */
bool alarm_service_try_status(alarm_status_t *out);
int alarm_service_set(unsigned hour,unsigned minute,bool daily,unsigned delay,const char *label);
bool alarm_service_delete(uint32_t id);
void alarm_service_dismiss(void);
/* Runs on the media task, which already owns a flash-safe stack. */
void alarm_service_tick(void);
void alarm_service_audio(void);
