#include "cloud_service.h"
#include "esp_system.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "cJSON.h"
#include "device_services.h"
#include "device_http.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "ota_policy.h"
#include "recorder.h"
#include "voice_service.h"

typedef struct { bool install, bench; int64_t expires; } ota_request_t;
static QueueHandle_t s_ota_requests;
static atomic_bool s_upload_now, s_ota_busy;
static const char *TAG = "cloud";

bool cloud_ota_busy(void) { return atomic_load(&s_ota_busy); }
void cloud_request_upload(void) { recorder_seal(); atomic_store(&s_upload_now, true); }

static void digest_hex(const uint8_t digest[32], char hex[65])
{
    for (unsigned i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
}

static void authorize(esp_http_client_handle_t client, const char *token)
{
    if (!*token) return;
    char header[140];
    snprintf(header, sizeof(header), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", header);
}

static int read_body(esp_http_client_handle_t client, char *buffer, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        int count = esp_http_client_read(client, buffer + used, capacity - used - 1);
        if (count < 0) return -1;
        if (!count) break;
        used += count;
    }
    if (!esp_http_client_is_complete_data_received(client)) return -1;
    buffer[used] = 0;
    return (int)used;
}

static esp_err_t upload_file(const device_settings_t *settings, const char *path)
{
    struct stat statbuf;
    if (stat(path, &statbuf) || statbuf.st_size <= 0 || statbuf.st_size > 2 * 1024 * 1024) return ESP_ERR_INVALID_SIZE;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_FAIL;
    esp_err_t result = ESP_FAIL;
    char buffer[2048], hash[65];
    uint8_t digest[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    size_t count;
    while (recorder_storage_ready() && (count = fread(buffer, 1, sizeof(buffer), file)))
        mbedtls_sha256_update(&sha, (const uint8_t *)buffer, count);
    bool failed = ferror(file) || !recorder_storage_ready();
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    digest_hex(digest, hash);
    if (failed || fseek(file, 0, SEEK_SET)) { fclose(file); return ESP_FAIL; }
    esp_http_client_config_t config;
    device_http_config(&config, settings->upload_url);
    config.method = HTTP_METHOD_POST;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { fclose(file); return ESP_ERR_NO_MEM; }
    const char *report = strrchr(path, '/') + 1;
    authorize(client, settings->token);
    esp_http_client_set_header(client, "Content-Type", "application/x-ndjson");
    esp_http_client_set_header(client, "X-Device-Id", device_id());
    esp_http_client_set_header(client, "X-Report-Id", report);
    esp_http_client_set_header(client, "X-Content-SHA256", hash);
    if (esp_http_client_open(client, statbuf.st_size) != ESP_OK) goto cleanup;
    while (recorder_storage_ready() && (count = fread(buffer, 1, sizeof(buffer), file))) {
        device_settings_t current;
        device_settings_get(&current);
        if (!current.upload_enabled || cloud_ota_busy() || voice_is_active() || !device_network_ready()) goto cleanup;
        size_t sent = 0;
        while (sent < count) {
            int written = esp_http_client_write(client, buffer + sent, count - sent);
            if (written <= 0) goto cleanup;
            sent += written;
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    if (ferror(file) || !recorder_storage_ready() || esp_http_client_fetch_headers(client) < 0) goto cleanup;
    int status = esp_http_client_get_status_code(client);
    if (status == 401 || status == 403) { result = ESP_ERR_INVALID_STATE; goto cleanup; }
    if (status < 200 || status >= 300 || read_body(client, buffer, sizeof(buffer)) <= 0) goto cleanup;
    cJSON *ack = cJSON_Parse(buffer);
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(ack, "ok");
    const cJSON *digest_ack = cJSON_GetObjectItemCaseSensitive(ack, "sha256");
    const cJSON *report_ack = cJSON_GetObjectItemCaseSensitive(ack, "report");
    const cJSON *size_ack = cJSON_GetObjectItemCaseSensitive(ack, "size");
    if (cJSON_IsTrue(ok) && cJSON_IsString(digest_ack) && !strcmp(digest_ack->valuestring, hash) &&
        cJSON_IsString(report_ack) && !strcmp(report_ack->valuestring, report) &&
        cJSON_IsNumber(size_ack) && size_ack->valuedouble == (double)statbuf.st_size) result = ESP_OK;
    cJSON_Delete(ack);
cleanup:
    esp_http_client_cleanup(client);
    fclose(file);
    if (result == ESP_OK) result = recorder_ack_file(path);
    return result;
}

static void upload_task(void *arg)
{
    (void)arg;
    unsigned backoff = 2;
    for (;;) {
        device_settings_t settings;
        device_settings_get(&settings);
        if (!settings.upload_enabled || !*settings.upload_url) {
            device_status_set(SERVICE_UPLOAD, SERVICE_OFF, "上传已关闭");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!device_network_ready() || !device_clock_ready() || cloud_ota_busy() || voice_is_active()) {
            device_status_set(SERVICE_UPLOAD, SERVICE_WAITING, "等待联网校时");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        char path[160];
        if (!recorder_storage_acquire()) {
            device_status_set(SERVICE_UPLOAD, SERVICE_WAITING, "等待存储卡");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!recorder_next_file(path, sizeof(path))) {
            recorder_storage_release();
            device_status_set(SERVICE_UPLOAD, SERVICE_WAITING, "暂无待传文件");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        device_status_set(SERVICE_UPLOAD, SERVICE_BUSY, "正在上传记录");
        esp_err_t err = upload_file(&settings, path);
        recorder_storage_release();
        if (err == ESP_OK) backoff = 2;
        else {
            device_status_set(SERVICE_UPLOAD, SERVICE_ERROR, err == ESP_ERR_INVALID_STATE ? "上传凭据无效" : "上传失败，稍后重试");
            if (err == ESP_ERR_INVALID_STATE) {
                device_settings_t current;
                device_settings_get(&current);
                if (!strcmp(current.token, settings.token) && !strcmp(current.upload_url, settings.upload_url)) {
                    current.upload_enabled = false;
                    device_settings_save(&current);
                }
            }
            if (backoff < 120) backoff *= 2;
        }
        for (unsigned i = 0; i < backoff; ++i) {
            if (atomic_exchange(&s_upload_now, false)) break;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

static bool verified_manifest(const char *body, ota_manifest_t *manifest, uint32_t capacity)
{
#ifdef DEVICE_CUSTOM_TRUST
    extern const char key_start[] asm("_binary_ota_public_pem_start");
    cJSON *envelope = cJSON_Parse(body);
    const cJSON *payload = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
    const cJSON *signature = cJSON_GetObjectItemCaseSensitive(envelope, "signature");
    bool ok = false;
    unsigned char decoded[2049], sig[512], hash[32];
    size_t decoded_len = 0, sig_len = 0;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    if (!cJSON_IsString(payload) || !cJSON_IsString(signature) ||
        mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len, (const unsigned char *)payload->valuestring, strlen(payload->valuestring)) ||
        mbedtls_base64_decode(sig, sizeof(sig), &sig_len, (const unsigned char *)signature->valuestring, strlen(signature->valuestring))) goto done;
    if (mbedtls_pk_parse_public_key(&key, (const unsigned char *)key_start, strlen(key_start) + 1) ||
        !mbedtls_pk_can_do(&key, MBEDTLS_PK_RSA) || mbedtls_sha256(decoded, decoded_len, hash, 0) ||
        mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_len)) goto done;
    ok = ota_manifest_parse((const char *)decoded, decoded_len, DEVICE_BOARD_ID, esp_app_get_description()->version, capacity, manifest);
done:
    mbedtls_pk_free(&key);
    cJSON_Delete(envelope);
    return ok;
#else
    (void)body; (void)manifest; (void)capacity;
    return false;
#endif
}

static bool same_origin(const char *a, const char *b)
{
    const char *a_end = strchr(a + 8, '/'), *b_end = strchr(b + 8, '/');
    size_t a_size = a_end ? (size_t)(a_end - a) : strlen(a);
    size_t b_size = b_end ? (size_t)(b_end - b) : strlen(b);
    return a_size == b_size && !strncasecmp(a, b, a_size);
}

static esp_err_t download_ota(const device_settings_t *settings, const ota_manifest_t *manifest,
                              const esp_partition_t *partition, bool bench)
{
    esp_http_client_config_t config;
    device_http_config(&config, manifest->url);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    if (same_origin(settings->ota_url, manifest->url)) authorize(client, settings->token);
    esp_ota_handle_t ota = 0;
    esp_err_t result = ESP_FAIL;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) != manifest->size ||
        esp_http_client_get_status_code(client) != 200) goto done;
    if (esp_ota_begin(partition, manifest->size, &ota) != ESP_OK) goto done;
    mbedtls_sha256_starts(&sha, 0);
    char buffer[4096];
    uint32_t received = 0;
    while (received < manifest->size) {
        if (!device_maintenance_allowed(bench) || !device_network_ready()) goto done;
        unsigned wanted = manifest->size - received;
        if (wanted > sizeof(buffer)) wanted = sizeof(buffer);
        int count = esp_http_client_read(client, buffer, wanted);
        if (count <= 0 || esp_ota_write(ota, buffer, count) != ESP_OK ||
            mbedtls_sha256_update(&sha, (const uint8_t *)buffer, count)) goto done;
        received += count;
        char progress[96];
        snprintf(progress, sizeof(progress), "正在升级 %lu%%", (unsigned long)(received * 100 / manifest->size));
        device_status_set(SERVICE_OTA, SERVICE_BUSY, progress);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    uint8_t digest[32]; char hex[65];
    mbedtls_sha256_finish(&sha, digest);
    digest_hex(digest, hex);
    if (strcmp(hex, manifest->sha256) || !esp_http_client_is_complete_data_received(client)) goto done;
    result = esp_ota_end(ota);
    ota = 0;
    if (result != ESP_OK) goto done;
    esp_app_desc_t description;
    result = ESP_ERR_INVALID_RESPONSE;
    if (esp_ota_get_partition_description(partition, &description) != ESP_OK ||
        strncmp(description.project_name, DEVICE_PROJECT_ID, sizeof(description.project_name)) ||
        strncmp(description.version, manifest->version, sizeof(description.version)) ||
        (strncmp(description.idf_ver, "v5.5", 4) && strncmp(description.idf_ver, "5.5", 3))) goto done;
    if (!device_maintenance_allowed(bench)) goto done;
    result = esp_ota_set_boot_partition(partition);
done:
    if (ota) esp_ota_abort(ota);
    mbedtls_sha256_free(&sha);
    esp_http_client_cleanup(client);
    return result;
}

static void ota_task(void *arg)
{
    (void)arg;
    ota_request_t request;
    for (;;) {
        xQueueReceive(s_ota_requests, &request, portMAX_DELAY);
        device_settings_t settings;
        device_settings_get(&settings);
        esp_err_t result = ESP_FAIL;
        esp_http_client_handle_t client = NULL;
        const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
        if (!partition || !*settings.ota_url || !device_network_ready() || !device_clock_ready()) goto failed;
        if (request.install && (esp_timer_get_time() > request.expires || !device_maintenance_allowed(request.bench))) goto failed;
        device_status_set(SERVICE_OTA, SERVICE_BUSY, "正在检查更新");
        esp_http_client_config_t config;
        device_http_config(&config, settings.ota_url);
        client = esp_http_client_init(&config);
        if (!client) goto failed;
        authorize(client, settings.token);
        char body[4096];
        if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0 ||
            esp_http_client_get_status_code(client) != 200 || read_body(client, body, sizeof(body)) <= 0) goto failed;
        ota_manifest_t manifest;
        if (!verified_manifest(body, &manifest, partition->size)) goto failed;
        esp_http_client_cleanup(client); client = NULL;
        if (!request.install) {
            char detail[96]; snprintf(detail, sizeof(detail), "可用更新 %s", manifest.version);
            device_status_set(SERVICE_OTA, SERVICE_READY, detail);
            atomic_store(&s_ota_busy, false);
            continue;
        }
        if (esp_timer_get_time() > request.expires || !device_maintenance_allowed(request.bench)) goto failed;
        recorder_seal();
        result = download_ota(&settings, &manifest, partition, request.bench);
        if (result == ESP_OK) {
            device_status_set(SERVICE_OTA, SERVICE_READY, "升级完成，即将重启");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
failed:
        if (client) esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "Update refused or failed; active slot is unchanged: %s", esp_err_to_name(result));
        device_status_set(SERVICE_OTA, SERVICE_ERROR, "更新未执行，请检查配置");
        atomic_store(&s_ota_busy, false);
    }
}

esp_err_t cloud_request_ota(bool install, bool bench)
{
#ifndef DEVICE_CUSTOM_TRUST
    (void)install; (void)bench;
    device_status_set(SERVICE_OTA, SERVICE_OFF, "尚未配置升级密钥");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_ota_requests || (install && !device_maintenance_allowed(bench))) {
        device_status_set(SERVICE_OTA, SERVICE_WAITING, "驻车状态未确认");
        return ESP_ERR_INVALID_STATE;
    }
    if (atomic_exchange(&s_ota_busy, true)) return ESP_ERR_INVALID_STATE;
    ota_request_t request = {.install = install, .bench = bench, .expires = esp_timer_get_time() + 60000000};
    if (xQueueSend(s_ota_requests, &request, 0) == pdTRUE) return ESP_OK;
    atomic_store(&s_ota_busy, false);
    return ESP_ERR_NO_MEM;
#endif
}

esp_err_t cloud_service_start(void)
{
#ifdef DEVICE_CUSTOM_TRUST
    device_status_set(SERVICE_OTA, SERVICE_OFF, "更新待命");
#else
    device_status_set(SERVICE_OTA, SERVICE_OFF, "尚未配置升级密钥");
#endif
    s_ota_requests = xQueueCreate(1, sizeof(ota_request_t));
    if (!s_ota_requests) return ESP_ERR_NO_MEM;
    if (xTaskCreate(upload_task, "upload", 10240, NULL, 1, NULL) != pdPASS ||
        xTaskCreate(ota_task, "ota", 16384, NULL, 1, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
