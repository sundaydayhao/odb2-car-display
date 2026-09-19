#pragma once

#include "elm327_protocol.h"
#include "obd2_data.h"

#define OBD_PARAMETER_COUNT 12
typedef struct { uint8_t pid, bytes; uint32_t stale_ms; size_t valid_offset; uint32_t poll_ms; } obd_parameter_t;
typedef struct {
    int64_t last_ms;
    bool initialized, speed, fuel, idle;
    uint16_t last_speed, last_rate;
    uint64_t speed_area, speed_ms, fuel_area, fuel_ms, idle_ms;
    uint64_t paired_speed_area, paired_fuel_area;
} obd_trip_t;
typedef struct { obd2_vehicle_data_t data; int64_t updated[OBD_PARAMETER_COUNT]; obd_trip_t trip; } obd_cache_t;
typedef struct { int64_t due[OBD_PARAMETER_COUNT]; uint8_t failures[OBD_PARAMETER_COUNT]; } obd_poll_t;
extern const obd_parameter_t obd_parameters[OBD_PARAMETER_COUNT];
/* Earliest due supported PID, -1 when none due. Never queues ECU writes. */
int obd_poll_next(const obd_poll_t *poll, const bool supported[OBD_PARAMETER_COUNT], int64_t now);
void obd_poll_result(obd_poll_t *poll, unsigned index, bool valid, int64_t now);
bool obd_cache_apply(obd_cache_t *cache, unsigned index, const elm_pid_reply_t *reply, int64_t now);
void obd_cache_expire(obd_cache_t *cache, int64_t now);
void obd_cache_invalidate(obd_cache_t *cache, unsigned index);
