#include "ota_policy.h"
#include <ctype.h>
#include <string.h>
#include "cJSON.h"

static bool version_parts(const char *text, unsigned parts[3])
{
    if (!text) return false;
    for (int i = 0; i < 3; ++i) {
        if (!isdigit((unsigned char)*text)) return false;
        parts[i] = 0;
        do {
            if (parts[i] > 9999) return false;
            parts[i] = parts[i] * 10 + (unsigned)(*text++ - '0');
        } while (isdigit((unsigned char)*text));
        if (i < 2 && *text++ != '.') return false;
    }
    return *text == 0;
}

bool ota_version_newer(const char *candidate, const char *current)
{
    unsigned a[3], b[3];
    if (!version_parts(candidate, a) || !version_parts(current, b)) return false;
    for (int i = 0; i < 3; ++i) if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

static const char *get_string(const cJSON *json, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

bool ota_manifest_parse(const char *text, size_t length, const char *board,
                        const char *current, uint32_t capacity, ota_manifest_t *out)
{
    if (!text || !length || length > 2048 || !out) return false;
    const char *end;
    cJSON *json = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!json) return false;
    bool ok = false;
    for (; end < text + length; ++end) if (!isspace((unsigned char)*end)) goto done;
    if (!cJSON_IsObject(json)) goto done;
    /* Duplicate keys make signatures and policy ambiguous across implementations. */
    for (cJSON *a = json->child; a; a = a->next) {
        for (cJSON *b = a->next; b; b = b->next) if (!strcmp(a->string, b->string)) goto done;
    }
    const char *target = get_string(json, "target"), *hardware = get_string(json, "board");
    const char *project = get_string(json, "project"), *idf = get_string(json, "idf");
    const char *version = get_string(json, "version"), *url = get_string(json, "url"), *sha = get_string(json, "sha256");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(json, "size");
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(json, "schema");
    if (!target || strcmp(target, "esp32p4") || !hardware || strcmp(hardware, board) ||
        !project || strcmp(project, "odb2_dash") || !idf || strcmp(idf, "5.5") ||
        !version || strlen(version) >= sizeof(out->version) || !ota_version_newer(version, current) ||
        !url || strlen(url) >= sizeof(out->url) || strncmp(url, "https://", 8) || !url[8] || url[8] == '/' ||
        !sha || strlen(sha) != 64 || !cJSON_IsNumber(size) || size->valuedouble < 1024 ||
        size->valuedouble > capacity || size->valuedouble != (double)(uint32_t)size->valuedouble ||
        !cJSON_IsNumber(schema) || schema->valuedouble != 1) goto done;
    for (unsigned i = 0; i < 64; ++i) if (!isdigit((unsigned char)sha[i]) && !(sha[i] >= 'a' && sha[i] <= 'f')) goto done;
    for (const char *p = url + 8; *p; ++p) if ((unsigned char)*p <= 32 || *p == '@' || *p == '#' || *p == '\\') goto done;
    memset(out, 0, sizeof(*out));
    strcpy(out->version, version);
    strcpy(out->url, url);
    strcpy(out->sha256, sha);
    out->size = (uint32_t)size->valuedouble;
    ok = true;
done:
    cJSON_Delete(json);
    return ok;
}
