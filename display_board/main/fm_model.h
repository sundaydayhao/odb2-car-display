#pragma once
#include "media_model.h"

typedef enum { FM_ABSENT, FM_READY, FM_TUNING, FM_SEEKING, FM_ERROR } fm_state_t;
typedef enum { FM_TUNE, FM_LISTEN, FM_STOP, FM_SEEK, FM_RETRY, FM_ACTION_COUNT } fm_action_t;
typedef struct { fm_action_t action; uint16_t frequency; uint32_t id; } fm_request_t;
typedef bool (*fm_action_cb_t)(const fm_request_t *request);
typedef struct {
    fm_state_t state;
    uint16_t frequency;
    uint8_t rssi,volume;
    bool available,listening,stereo,station;
    uint32_t revision,completed_id;
    char detail[128];
} fm_status_t;
