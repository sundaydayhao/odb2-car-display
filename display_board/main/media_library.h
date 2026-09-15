#pragma once
#include "av_model.h"
#include "esp_err.h"

esp_err_t media_library_start(void);
void media_library_refresh(void);
void media_library_status(av_library_status_t *out);
size_t media_library_files(av_file_kind_t kind, unsigned first, av_file_t *files,
                           size_t capacity, uint32_t revision);
bool media_library_music(unsigned index, uint32_t revision, char *path, size_t path_size,
                          char *title, size_t title_size, uint32_t *generation);
/* Short mount leases per read; a paused player never prevents card detection. */
int media_library_read(const char *path, uint32_t generation, uint32_t offset,
                        void *buffer, size_t capacity);
bool media_library_view_photo(unsigned index, uint32_t revision);
bool media_library_save_photo(const uint16_t *pixels, unsigned width, unsigned height, unsigned stride);
