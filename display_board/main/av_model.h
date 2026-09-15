#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { AV_PREVIEW_WIDTH = 480, AV_PREVIEW_HEIGHT = 270,
       AV_FILE_NAME_SIZE = 128, AV_FILE_PATH_SIZE = 192, AV_FILE_LIMIT = 128 };
typedef enum { AV_MUSIC, AV_CAMERA, AV_VOICE, AV_FM } av_tab_t;
typedef enum { AV_FILES_MUSIC, AV_FILES_PHOTOS } av_file_kind_t;
typedef struct { char name[AV_FILE_NAME_SIZE]; } av_file_t;
typedef struct {
    uint32_t revision;
    uint16_t music_count, photo_count;
    bool storage, scanning, truncated;
    char detail[96], photo_detail[96];
} av_library_status_t;
typedef struct {
    bool camera_enabled, voice_enabled, voice_active;
    unsigned camera_state, voice_state;
    char camera_detail[96], voice_detail[96];
    av_library_status_t library;
} av_status_t;
typedef enum {
    AV_REFRESH_FILES, AV_CAMERA_ON, AV_CAMERA_OFF, AV_TAKE_PHOTO,
    AV_VIEW_PHOTO, AV_VOICE_ON, AV_VOICE_OFF, AV_VOICE_START, AV_VOICE_END
} av_action_t;
typedef bool (*av_action_cb_t)(av_action_t action, unsigned value, uint32_t revision);
typedef size_t (*av_files_cb_t)(av_file_kind_t kind, unsigned first, av_file_t *files,
                              size_t capacity, uint32_t revision);
