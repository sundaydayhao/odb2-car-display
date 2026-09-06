#pragma once

#include "elm327_protocol.h"
#include "obd2_data.h"

#define OBD_PARAMETER_COUNT 10
typedef struct { uint8_t pid, bytes; uint32_t stale_ms; size_t valid_offset; } obd_parameter_t;
typedef struct { obd2_vehicle_data_t data; int64_t updated[OBD_PARAMETER_COUNT]; } obd_cache_t;
extern const obd_parameter_t obd_parameters[OBD_PARAMETER_COUNT];
bool obd_cache_apply(obd_cache_t *cache, unsigned index, const elm_pid_reply_t *reply, int64_t now);
void obd_cache_expire(obd_cache_t *cache, int64_t now);
void obd_cache_invalidate(obd_cache_t *cache, unsigned index);
