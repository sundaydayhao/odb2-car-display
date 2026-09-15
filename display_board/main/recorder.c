#include "recorder.h"
#include "record_storage.h"
#include "sdmmc_guard.h"

#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "device_services.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define RECORD_DIR BSP_SD_MOUNT_POINT "/vehicle"
#define RECORD_METRICS 10
typedef struct {
    int64_t us;
    uint32_t sequence, generation, transaction, notification;
    uint16_t offset, total, length;
    int kind, result;
    union { uint8_t bytes[64]; obd2_vehicle_data_t metrics; } payload;
} record_t;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_files;
static atomic_uint s_sequence, s_dropped;
static atomic_bool s_seal, s_storage;
static atomic_bool s_external_error;
static atomic_uint s_storage_generation;
static unsigned s_file_counter;
static unsigned s_storage_users; /* Protected by s_files; prevents unmount during upload. */
static const char *TAG = "recorder";

uint32_t recorder_dropped(void) { return atomic_load(&s_dropped); }
void recorder_seal(void) { atomic_store(&s_seal, true); }
bool recorder_storage_ready(void) { return atomic_load(&s_storage); }
uint32_t recorder_storage_generation(void) { return atomic_load(&s_storage_generation); }
void recorder_storage_report_error(void) { atomic_store(&s_external_error, true); }

bool recorder_storage_acquire(void)
{
    if (!s_files) return false;
    xSemaphoreTake(s_files, portMAX_DELAY);
    bool ready = atomic_load(&s_storage);
    if (ready) ++s_storage_users;
    xSemaphoreGive(s_files);
    return ready;
}

void recorder_storage_release(void)
{
    xSemaphoreTake(s_files, portMAX_DELAY);
    configASSERT(s_storage_users);
    --s_storage_users;
    xSemaphoreGive(s_files);
}

static void submit(record_t *record)
{
    record->us = esp_timer_get_time();
    record->sequence = atomic_fetch_add(&s_sequence, 1);
    if (!s_queue || xQueueSend(s_queue, record, 0) != pdTRUE) atomic_fetch_add(&s_dropped, 1);
}

void recorder_wire(record_kind_t kind, uint32_t generation, uint32_t transaction, uint32_t notification,
                   uint16_t offset, uint16_t total, const void *data, size_t length, int result)
{
    if (length > 64 || (length && !data)) { atomic_fetch_add(&s_dropped, 1); return; }
    record_t record = {.kind = kind, .generation = generation, .transaction = transaction,
        .notification = notification, .offset = offset, .total = total, .length = length, .result = result};
    if (length) memcpy(record.payload.bytes, data, length);
    submit(&record);
}

void recorder_metrics(const obd2_vehicle_data_t *data)
{
    record_t record = {.kind = RECORD_METRICS, .payload.metrics = *data};
    submit(&record);
}

static bool own_file(const char *name, const char *extension)
{
    size_t n = strlen(name), suffix = strlen(extension);
    if (n != 29 + suffix || strncmp(name, "veh-", 4) || name[20] != '-' || strcmp(name + n - suffix, extension)) return false;
    for (size_t i = 4; i < n - suffix; ++i) {
        if (i != 20 && !(name[i] >= '0' && name[i] <= '9') && !(name[i] >= 'a' && name[i] <= 'f')) return false;
    }
    return true;
}

static unsigned file_count(void)
{
    unsigned count = 0;
    DIR *dir = opendir(RECORD_DIR);
    if (!dir) return CONFIG_DEVICE_RECORD_MAX_FILES;
    struct dirent *entry;
    while ((entry = readdir(dir))) if (own_file(entry->d_name, ".jsonl") || own_file(entry->d_name, ".part") || own_file(entry->d_name, ".broken")) ++count;
    closedir(dir);
    return count;
}

bool recorder_next_file(char *path, size_t size)
{
    if (!atomic_load(&s_storage) || !path || size < 160) return false;
    char first[100] = {0};
    xSemaphoreTake(s_files, portMAX_DELAY);
    DIR *dir = atomic_load(&s_storage) ? opendir(RECORD_DIR) : NULL;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (own_file(entry->d_name, ".jsonl") && (!*first || strcmp(entry->d_name, first) < 0)) strlcpy(first, entry->d_name, sizeof(first));
        }
        closedir(dir);
    }
    xSemaphoreGive(s_files);
    if (!*first) return false;
    snprintf(path, size, "%s/%s", RECORD_DIR, first);
    return true;
}

esp_err_t recorder_ack_file(const char *path)
{
    if (strncmp(path, RECORD_DIR "/", strlen(RECORD_DIR) + 1) || !own_file(path + strlen(RECORD_DIR) + 1, ".jsonl")) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_files, portMAX_DELAY);
    int result = atomic_load(&s_storage) ? unlink(path) : -1;
    xSemaphoreGive(s_files);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

static bool write_json(FILE *file, cJSON *json, int members)
{
    if (!json) return false;
    if (cJSON_GetArraySize(json) != members) { cJSON_Delete(json); return false; }
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!text) return false;
    bool ok = fprintf(file, "%s\n", text) > 0;
    cJSON_free(text);
    return ok;
}

static cJSON *record_json(const record_t *record)
{
    static const char *names[] = {"tx", "rx", "result", "link", "parser_gap"};
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;
    cJSON_AddStringToObject(json, "kind", record->kind == RECORD_METRICS ? "metrics" : names[record->kind]);
    cJSON_AddNumberToObject(json, "us", (double)record->us);
    cJSON_AddNumberToObject(json, "seq", record->sequence);
    if (record->kind == RECORD_METRICS) {
        const obd2_vehicle_data_t *d = &record->payload.metrics;
        const char *keys[] = {"speed_kph", "rpm", "fuel_percent", "coolant_c", "voltage_mv", "load_percent", "throttle_percent", "intake_c", "oil_c", "manifold_kpa"};
        int values[] = {d->speed_kph,d->rpm,d->fuel_percent,d->coolant_temp_c,d->control_voltage_mv,d->engine_load_percent,d->throttle_percent,d->intake_temp_c,d->oil_temp_c,d->manifold_pressure_kpa};
        bool valid[] = {d->speed_valid,d->rpm_valid,d->fuel_valid,d->coolant_temp_valid,d->control_voltage_valid,d->engine_load_valid,d->throttle_valid,d->intake_temp_valid,d->oil_temp_valid,d->manifold_pressure_valid};
        for (unsigned i = 0; i < 10; ++i) {
            if (valid[i]) cJSON_AddNumberToObject(json, keys[i], values[i]);
            else cJSON_AddNullToObject(json, keys[i]);
        }
        cJSON_AddNumberToObject(json, "link", d->link_state);
    } else {
        cJSON_AddNumberToObject(json, "generation", record->generation);
        cJSON_AddNumberToObject(json, "transaction", record->transaction);
        cJSON_AddNumberToObject(json, "notification", record->notification);
        cJSON_AddNumberToObject(json, "offset", record->offset);
        cJSON_AddNumberToObject(json, "total", record->total);
        cJSON_AddNumberToObject(json, "result", record->result);
        char hex[129];
        static const char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < record->length; ++i) {
            hex[i * 2] = digits[record->payload.bytes[i] >> 4];
            hex[i * 2 + 1] = digits[record->payload.bytes[i] & 15];
        }
        hex[record->length * 2] = 0;
        cJSON_AddStringToObject(json, "hex", hex);
    }
    return json;
}

static bool finish_file(FILE **file, const char *path, bool recovered)
{
    cJSON *footer = cJSON_CreateObject();
    cJSON_AddStringToObject(footer, "kind", "end");
    cJSON_AddBoolToObject(footer, "recovered", recovered);
    if (recovered) cJSON_AddNullToObject(footer, "dropped_boot_total");
    else cJSON_AddNumberToObject(footer, "dropped_boot_total", recorder_dropped());
    bool ok = write_json(*file, footer, 3);
    if (fflush(*file) || fsync(fileno(*file))) ok = false;
    if (fclose(*file)) ok = false;
    *file = NULL;
    if (!ok) return false;
    char sealed[160];
    snprintf(sealed, sizeof(sealed), "%.*s.jsonl", (int)(strlen(path) - 5), path);
    return rename(path, sealed) == 0;
}

static void recover_files(void)
{
    DIR *dir = opendir(RECORD_DIR);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!own_file(entry->d_name, ".part")) continue;
        char path[160];
        snprintf(path, sizeof(path), "%s/%.99s", RECORD_DIR, entry->d_name);
        FILE *file = fopen(path, "r+");
        if (!file) continue;
        long last_line = 0;
        char line[2048];
        while (fgets(line, sizeof(line), file) && ftell(file) <= 2 * 1024 * 1024) {
            if (!strchr(line, '\n')) break;
            cJSON *json = cJSON_Parse(line);
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(json, "kind");
            bool valid = cJSON_IsString(kind) && (last_line || !strcmp(kind->valuestring, "header"));
            bool end = cJSON_IsString(kind) && !strcmp(kind->valuestring, "end");
            cJSON_Delete(json);
            if (!valid || end) break;
            last_line = ftell(file);
        }
        if (!last_line) {
            fclose(file);
            char broken[160];
            snprintf(broken, sizeof(broken), "%.*s.broken", (int)(strlen(path) - 5), path);
            rename(path, broken);
        } else if (ferror(file) || ftruncate(fileno(file), last_line) || fseek(file, last_line, SEEK_SET)) fclose(file);
        else finish_file(&file, path, true);
    }
    closedir(dir);
}

static void record_task(void *arg)
{
    (void)arg;
    bool mounted = false;
    int64_t mount_at = 0, probe_at = 0;
    device_status_set(SERVICE_RECORD, SERVICE_OFF, "未检测到存储卡");
    FILE *file = NULL;
    char path[160];
    int64_t opened = 0, flushed = 0;
    uint32_t reported_drops = 0;
    for (;;) {
        record_t record;
        bool received = xQueueReceive(s_queue, &record, pdMS_TO_TICKS(200)) == pdTRUE;
        xSemaphoreTake(s_files, portMAX_DELAY);
        int64_t now = esp_timer_get_time();
        if (atomic_exchange(&s_external_error,false)) {
            atomic_store(&s_storage,false);
            if (file) { fclose(file); file=NULL; }
            mount_at=now+3000000;
            device_status_set(SERVICE_RECORD,SERVICE_ERROR,"存储卡读写失败");
        }
        if (!atomic_load(&s_storage)) {
            /* Stop new users immediately, but keep VFS alive until old users close. */
            if (mounted && !s_storage_users) {
                esp_err_t err = record_storage_unmount();
                ESP_LOGI(TAG, "SD unmounted: %s", esp_err_to_name(err));
                mounted = false;
            }
            if (!mounted && now >= mount_at) {
                esp_err_t err = record_storage_mount();
                mounted = err == ESP_OK;
                if (mounted && mkdir(RECORD_DIR, 0770) && errno != EEXIST) err = ESP_FAIL;
                if (err == ESP_OK) {
                    recover_files();
                    atomic_fetch_add(&s_storage_generation,1);
                    atomic_store(&s_storage, true);
                    probe_at = esp_timer_get_time() + 1000000;
                    device_status_set(SERVICE_RECORD, SERVICE_READY, "存储卡已就绪");
                    ESP_LOGI(TAG, "SD available; recording resumed");
                } else {
                    bool absent = err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND;
                    device_status_set(SERVICE_RECORD, absent ? SERVICE_OFF : SERVICE_ERROR,
                                      absent ? "未检测到存储卡" : "存储卡不可用");
                }
                mount_at = esp_timer_get_time() + 3000000;
            }
            if (!atomic_load(&s_storage)) {
                if (received) atomic_fetch_add(&s_dropped, 1);
                xSemaphoreGive(s_files);
                continue;
            }
        }
        /* No CD pin: a real card status command also catches removal while idle/full. */
        if (now >= probe_at && !s_storage_users) {
            esp_err_t err = record_storage_probe();
            probe_at = esp_timer_get_time() + 1000000;
            if (err != ESP_OK) {
                atomic_store(&s_storage, false);
                if (file) { fclose(file); file = NULL; }
                mount_at = esp_timer_get_time() + 3000000;
                bool absent = err == ESP_ERR_TIMEOUT || err == ESP_ERR_NOT_FOUND;
                device_status_set(SERVICE_RECORD, absent ? SERVICE_OFF : SERVICE_ERROR,
                                  absent ? "未检测到存储卡" : "存储卡不可用");
                ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(err));
                if (received) atomic_fetch_add(&s_dropped, 1);
                xSemaphoreGive(s_files);
                continue;
            }
        }
        if (!file && received) {
            if (file_count() >= CONFIG_DEVICE_RECORD_MAX_FILES) {
                atomic_fetch_add(&s_dropped, 1);
                device_status_set(SERVICE_RECORD, SERVICE_ERROR, "记录已满，请导出");
                xSemaphoreGive(s_files);
                continue;
            }
            snprintf(path, sizeof(path), RECORD_DIR "/veh-%s-%08x.part", device_boot_id(), s_file_counter++);
            file = fopen(path, "wx");
            if (file) {
                cJSON *header = cJSON_CreateObject();
                cJSON_AddStringToObject(header, "kind", "header");
                cJSON_AddNumberToObject(header, "schema", 1);
                cJSON_AddStringToObject(header, "device", device_id());
                cJSON_AddStringToObject(header, "boot", device_boot_id());
                cJSON_AddStringToObject(header, "board", DEVICE_BOARD_ID);
                cJSON_AddStringToObject(header, "firmware", esp_app_get_description()->version);
                cJSON_AddBoolToObject(header, "utc_valid", device_clock_ready());
                cJSON_AddNumberToObject(header, "utc", (double)time(NULL));
                cJSON_AddNumberToObject(header, "us", (double)esp_timer_get_time());
                if (!write_json(file, header, 9)) { fclose(file); file = NULL; }
                opened = flushed = esp_timer_get_time();
            }
        }
        bool ok = true;
        if (file) {
            uint32_t drops = recorder_dropped();
            if (drops != reported_drops) {
                cJSON *gap = cJSON_CreateObject();
                cJSON_AddStringToObject(gap, "kind", "gap");
                cJSON_AddNumberToObject(gap, "dropped", drops - reported_drops);
                ok = write_json(file, gap, 2);
                reported_drops = drops;
            }
            if (received && ok) ok = write_json(file, record_json(&record), record.kind == RECORD_METRICS ? 14 : 10);
            if (esp_timer_get_time() - flushed >= 2000000) {
                if (fflush(file) || fsync(fileno(file))) ok = false;
                flushed = esp_timer_get_time();
            }
            if (ok && (ftell(file) >= CONFIG_DEVICE_RECORD_FILE_KB * 1024 || esp_timer_get_time() - opened > 60000000 || atomic_exchange(&s_seal, false))) ok = finish_file(&file, path, false);
            if (!ok && file) { fclose(file); file = NULL; }
        } else if (received) ok = false;
        if (!ok) {
            atomic_fetch_add(&s_dropped, 1);
            atomic_store(&s_storage, false);
            mount_at = esp_timer_get_time() + 3000000;
            device_status_set(SERVICE_RECORD, SERVICE_ERROR, "记录写入失败");
            ESP_LOGW(TAG, "SD write failed; waiting for storage recovery");
        } else if (file || received) {
            device_status_set(SERVICE_RECORD, SERVICE_READY, "正在记录车况");
        }
        xSemaphoreGive(s_files);
    }
}

esp_err_t recorder_start(void)
{
    esp_err_t err = sdmmc_guard_init();
    if (err != ESP_OK) return err;
    s_queue = xQueueCreate(256, sizeof(record_t));
    s_files = xSemaphoreCreateMutex();
    if (!s_queue || !s_files) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Bounded recorder enabled; SD is never formatted automatically");
    return xTaskCreate(record_task, "record", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
