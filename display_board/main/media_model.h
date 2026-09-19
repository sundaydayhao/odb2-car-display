#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MEDIA_TITLE_SIZE = 128, MEDIA_URL_SIZE=512 };
typedef enum { MEDIA_IDLE, MEDIA_CONNECTING, MEDIA_BUFFERING, MEDIA_PLAYING,
               MEDIA_PAUSED, MEDIA_FINISHED, MEDIA_ERROR } media_state_t;
typedef struct {
    uint32_t revision, played_ms, decoded_frames, received_bytes, buffered_bytes;
    media_state_t state;
    uint8_t volume;
    bool network, selected, online;
    uint16_t local_index;
    uint32_t storage_generation;
    char title[MEDIA_TITLE_SIZE], detail[128];
} media_status_t;
typedef enum { MEDIA_TOGGLE, MEDIA_STOP, MEDIA_VOLUME,
               MEDIA_SELECT_LOCAL, MEDIA_PREVIOUS, MEDIA_NEXT, MEDIA_SELECT_ONLINE,
               MEDIA_VOLUME_DELTA, MEDIA_PAUSE, MEDIA_RESUME, MEDIA_ACTION_COUNT } media_action_t;
typedef struct {
    media_action_t action;
    unsigned value;
    uint32_t revision;
    int delta;
    char url[MEDIA_URL_SIZE],title[MEDIA_TITLE_SIZE];
} media_request_t;
typedef bool (*media_action_cb_t)(const media_request_t *request);

/* UI-selected FM frequencies, in tenths of MHz; zero denotes an empty slot.
 * These are user bookmarks, not stations verified by a receiver. */
enum { FM_MIN_FREQUENCY = 875, FM_MAX_FREQUENCY = 1080, FM_PRESET_COUNT = 6 };
typedef struct { uint16_t frequency[FM_PRESET_COUNT]; } fm_presets_t;
/* Copy before returning. Completion is reported on the LVGL thread. */
typedef bool (*fm_presets_save_cb_t)(const fm_presets_t *presets);

/* MPEG Layer III frame length; 0 means incomplete header, -1 invalid. */
int media_mp3_frame_size(const uint8_t *data, size_t length);
/* Full ID3v2 tag size including header/footer; 0 incomplete header, -1 invalid. */
int media_id3_size(const uint8_t *data, size_t length);
bool media_url_valid(const char *url);
