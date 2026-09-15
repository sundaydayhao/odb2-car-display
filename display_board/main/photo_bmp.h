#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Uncompressed 24-bit BMP, bounded to the camera's supported 2048px geometry.
 * Caller supplies at least 6144 bytes of row workspace. No heap ownership. */
bool photo_bmp_write(FILE *file, const uint16_t *rgb565, unsigned width, unsigned height,
                     unsigned stride, uint8_t *row, size_t capacity);
bool photo_bmp_preview(FILE *file, uint16_t *pixels, unsigned width, unsigned height,
                       uint8_t *row, size_t capacity);
