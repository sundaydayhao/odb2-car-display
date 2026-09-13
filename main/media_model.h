#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { MEDIA_URL_SIZE = 384, MEDIA_TITLE_SIZE = 64, MEDIA_SOURCE_COUNT = 6,
       MEDIA_CUSTOM_MUSIC = 2, MEDIA_CUSTOM_RADIO = 5 };
typedef enum { MEDIA_MUSIC, MEDIA_RADIO } media_kind_t;
typedef enum { MEDIA_IDLE, MEDIA_CONNECTING, MEDIA_BUFFERING, MEDIA_PLAYING,
               MEDIA_PAUSED, MEDIA_FINISHED, MEDIA_ERROR } media_state_t;
typedef struct {
    media_kind_t kind;
    const char *title, *subtitle, *url;
} media_source_t;
extern const media_source_t media_sources[MEDIA_SOURCE_COUNT];
typedef struct {
    uint32_t revision, played_ms, decoded_frames, received_bytes, buffered_bytes;
    media_state_t state;
    uint8_t source, volume;
    bool network, custom_music, custom_radio;
    char title[MEDIA_TITLE_SIZE], detail[128];
} media_status_t;
typedef enum { MEDIA_SELECT, MEDIA_TOGGLE, MEDIA_STOP, MEDIA_VOLUME, MEDIA_CUSTOM_SAVE } media_action_t;
typedef struct {
    media_action_t action;
    unsigned value;
    char url[MEDIA_URL_SIZE];
} media_request_t;
typedef bool (*media_action_cb_t)(const media_request_t *request);

bool media_url_valid(const char *url);
unsigned media_adjacent_source(unsigned current, int direction, bool custom_available);
/* Incremental Icecast metadata removal. Filters in place across arbitrary chunks. */
typedef struct { uint32_t interval, audio_left, metadata_left; bool length_pending; } media_icy_t;
void media_icy_init(media_icy_t *icy, uint32_t interval);
size_t media_icy_filter(media_icy_t *icy, uint8_t *data, size_t length);
/* MPEG Layer III frame length; 0 means incomplete header, -1 invalid. */
int media_mp3_frame_size(const uint8_t *data, size_t length);
/* Full ID3v2 tag size including header/footer; 0 incomplete header, -1 invalid. */
int media_id3_size(const uint8_t *data, size_t length);
