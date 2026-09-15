#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char version[32], url[256], sha256[65];
    uint32_t size;
} ota_manifest_t;
bool ota_version_newer(const char *candidate, const char *current);
bool ota_manifest_parse(const char *json, size_t length, const char *board,
                        const char *current, uint32_t capacity, ota_manifest_t *out);
