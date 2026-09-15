#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "obd2_data.h"

typedef enum { RECORD_TX, RECORD_RX, RECORD_RESULT, RECORD_LINK, RECORD_PARSE_GAP } record_kind_t;
esp_err_t recorder_start(void);
void recorder_wire(record_kind_t kind, uint32_t generation, uint32_t transaction, uint32_t notification,
                   uint16_t offset, uint16_t total, const void *data, size_t length, int result);
void recorder_metrics(const obd2_vehicle_data_t *data);
void recorder_seal(void);
/* Hold a mount reference from before file discovery until all upload files close. */
bool recorder_storage_acquire(void);
void recorder_storage_release(void);
bool recorder_storage_ready(void);
uint32_t recorder_storage_generation(void);
void recorder_storage_report_error(void);
bool recorder_next_file(char *path, size_t size);
esp_err_t recorder_ack_file(const char *path);
uint32_t recorder_dropped(void);
