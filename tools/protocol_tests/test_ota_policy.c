#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "ota_policy.h"

#define BOARD "esp32-p4-function-ev-board-v1.4"
static const char *base = "{\"schema\":1,\"target\":\"esp32p4\",\"board\":\"" BOARD "\","
    "\"project\":\"odb2_dash\",\"idf\":\"5.5\",\"version\":\"0.2.1\",\"size\":2048,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"url\":\"https://localhost/firmware.bin\"}";

static bool accepts(const char *data, size_t size)
{
    ota_manifest_t out;
    return ota_manifest_parse(data, size, BOARD, "0.2.0", 0x600000, &out);
}

int main(void)
{
    assert(ota_version_newer("0.2.1", "0.2.0"));
    assert(ota_version_newer("1.0.0", "0.99.99"));
    const char *bad_versions[] = {"0.2.0", "0.1.99", "v0.2.1", "1.0", "1.0.0-beta", "-1.0.0", "999999.0.0", "1.0.1x"};
    for (unsigned i = 0; i < sizeof(bad_versions)/sizeof(*bad_versions); ++i)
        assert(!ota_version_newer(bad_versions[i], "0.2.0"));
    assert(accepts(base, strlen(base)));
    assert(!accepts(base, strlen(base) - 1));
    char duplicate[2048];
    snprintf(duplicate, sizeof(duplicate), "%.*s,\"version\":\"0.2.2\"}", (int)strlen(base)-1, base);
    assert(!accepts(duplicate, strlen(duplicate)));
    snprintf(duplicate, sizeof(duplicate), "%s x", base);
    assert(!accepts(duplicate, strlen(duplicate)));
    const char *fields[] = {"schema", "target", "board", "project", "idf", "version", "size", "sha256", "url"};
    for (unsigned i = 0; i < sizeof(fields)/sizeof(*fields); ++i) {
        cJSON *json = cJSON_Parse(base);
        cJSON_DeleteItemFromObjectCaseSensitive(json, fields[i]);
        char *data = cJSON_PrintUnformatted(json);
        assert(!accepts(data, strlen(data)));
        cJSON_free(data); cJSON_Delete(json);
    }
    const char *urls[] = {"http://localhost/f.bin", "https://", "https:///file", "https://user@host/a", "https://host/a#x", "https://host/a b"};
    for (unsigned i = 0; i < sizeof(urls)/sizeof(*urls); ++i) {
        cJSON *json = cJSON_Parse(base);
        cJSON_ReplaceItemInObjectCaseSensitive(json, "url", cJSON_CreateString(urls[i]));
        char *data = cJSON_PrintUnformatted(json);
        assert(!accepts(data, strlen(data)));
        cJSON_free(data); cJSON_Delete(json);
    }
    const double sizes[] = {-1, 0, 1023, 2048.5, 0x600001, 1e99};
    for (unsigned i = 0; i < sizeof(sizes)/sizeof(*sizes); ++i) {
        cJSON *json = cJSON_Parse(base);
        cJSON_ReplaceItemInObjectCaseSensitive(json, "size", cJSON_CreateNumber(sizes[i]));
        char *data = cJSON_PrintUnformatted(json);
        assert(!accepts(data, strlen(data)));
        cJSON_free(data); cJSON_Delete(json);
    }
    ota_manifest_t out;
    assert(!ota_manifest_parse(base, strlen(base), "different-board", "0.2.0", 0x600000, &out));
    assert(!ota_manifest_parse(base, strlen(base), BOARD, "0.2.2", 0x600000, &out));
    puts("OTA manifest policy tests passed");
    return 0;
}
