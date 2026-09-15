#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MEDIA_TITLE_SIZE = 128 };
typedef enum { MEDIA_IDLE, MEDIA_CONNECTING, MEDIA_BUFFERING, MEDIA_PLAYING,
               MEDIA_PAUSED, MEDIA_FINISHED, MEDIA_ERROR } media_state_t;
typedef struct {
    uint32_t revision, played_ms, decoded_frames, received_bytes, buffered_bytes;
    media_state_t state;
    uint8_t volume;
    bool network, selected;
    uint16_t local_index;
    uint32_t storage_generation;
    char title[MEDIA_TITLE_SIZE], detail[128];
} media_status_t;
typedef enum { MEDIA_TOGGLE, MEDIA_STOP, MEDIA_VOLUME,
               MEDIA_SELECT_LOCAL, MEDIA_PREVIOUS, MEDIA_NEXT, MEDIA_ACTION_COUNT } media_action_t;
typedef struct {
    media_action_t action;
    unsigned value;
    uint32_t revision;
} media_request_t;
typedef bool (*media_action_cb_t)(const media_request_t *request);

/* MPEG Layer III frame length; 0 means incomplete header, -1 invalid. */
int media_mp3_frame_size(const uint8_t *data, size_t length);
/* Full ID3v2 tag size including header/footer; 0 incomplete header, -1 invalid. */
int media_id3_size(const uint8_t *data, size_t length);
