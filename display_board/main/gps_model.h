#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { GPS_FIX_TTL_MS=10000, GPS_NMEA_CAPACITY=128 };
typedef struct {
    double latitude, longitude;
    float speed_kph;
    uint32_t received_ms, sequence;
    bool enabled, valid, speed_valid, error;
} gps_fix_t;
typedef struct { char line[GPS_NMEA_CAPACITY]; size_t length; bool collecting; } gps_nmea_t;
/* A complete checksummed RMC publishes a new fix, including an explicit V/no-fix.
 * Other sentences, malformed input, and overlong lines never publish coordinates. */
bool gps_nmea_feed(gps_nmea_t *parser, char byte, uint32_t now_ms, gps_fix_t *fix);
bool gps_fix_current(const gps_fix_t *fix,uint32_t now_ms);
