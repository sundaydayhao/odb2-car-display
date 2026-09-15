#include "node_capture.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct { node_frame_t frame; uint64_t us; uint32_t sequence; uint8_t kind; } log_item_t;
static QueueHandle_t queue;
static SemaphoreHandle_t ack;
static atomic_bool enabled, failed;
static atomic_uint dropped, sequence, written;
static bool mounted, command_timed_out;
static uint64_t stop_at;
#define CAPTURE_LIMIT (1024u * 1024u)
static void writer(void *arg) {
    (void)arg; FILE *file = NULL; log_item_t item; unsigned bytes = 0;
    for (;;) {
        if (!xQueueReceive(queue, &item, portMAX_DELAY)) continue;
        if (item.kind == 2) {
            if (file) fclose(file);
            file = fopen("/capture/latest.jsonl", "w"); bytes = 0;
            atomic_store(&failed, !file); atomic_store(&written, 0);
            if (file) {
                int n = fprintf(file, "{\"type\":\"start\",\"format\":1,\"clock\":\"software_receive_boot_us\",\"bus_loss_unknown\":true}\n");
                if (n < 0) atomic_store(&failed, true); else bytes = n;
            }
            xSemaphoreGive(ack); continue;
        }
        if (item.kind == 3) {
            if (file) {
                fprintf(file, "{\"type\":\"end\",\"records\":%u,\"queue_dropped\":%u,\"write_failed\":%s}\n", atomic_load(&written), atomic_load(&dropped), atomic_load(&failed) ? "true" : "false");
                if (fclose(file)) atomic_store(&failed, true);
                file = NULL;
            }
            xSemaphoreGive(ack); continue;
        }
        if (!file || atomic_load(&failed)) continue;
        char hex[17]; unsigned count = item.frame.rtr ? 0 : item.frame.dlc;
        for (unsigned i = 0; i < count; i++) snprintf(hex + i * 2, 3, "%02X", item.frame.data[i]);
        hex[count * 2] = 0;
        char line[220];
        int n = snprintf(line, sizeof(line), "{\"type\":\"frame\",\"seq\":%lu,\"t_us\":%llu,\"dir\":\"%s\",\"id\":%lu,\"ext\":%s,\"rtr\":%s,\"dlc\":%u,\"data\":\"%s\"}\n",
            (unsigned long)item.sequence, (unsigned long long)item.us, item.kind ? "tx_request" : "rx",
            (unsigned long)item.frame.id, item.frame.extended ? "true" : "false", item.frame.rtr ? "true" : "false", item.frame.dlc, hex);
        if (n < 0 || bytes + n > CAPTURE_LIMIT || fwrite(line, 1, n, file) != (size_t)n) {
            atomic_store(&failed, true); atomic_store(&enabled, false); continue;
        }
        bytes += n; atomic_fetch_add(&written, 1);
        if (!(atomic_load(&written) % 32) && fflush(file)) { atomic_store(&failed, true); atomic_store(&enabled, false); }
    }
}
esp_err_t node_capture_init(void) {
    esp_vfs_spiffs_conf_t cfg = {.base_path = "/capture", .partition_label = "capture", .max_files = 2, .format_if_mount_failed = false};
    esp_err_t err = esp_vfs_spiffs_register(&cfg);
    /* Never erase a capture automatically after a mount failure. First-use format is explicit. */
    mounted = err == ESP_OK;
    queue = xQueueCreate(256, sizeof(log_item_t)); ack = xSemaphoreCreateBinary();
    if (!queue || !ack) return ESP_ERR_NO_MEM;
    if (xTaskCreate(writer, "capture_writer", 4096, NULL, 2, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
bool node_capture_enable(bool on) {
    if (command_timed_out) return false; /* Reboot required; never consume a stale command ACK. */
    if (on && !mounted) return false;
    atomic_store(&enabled, false);
    if (on) { atomic_store(&dropped, 0); atomic_store(&sequence, 0); }
    log_item_t cmd = {.kind = on ? 2 : 3};
    xSemaphoreTake(ack, 0);
    if (!xQueueSend(queue, &cmd, pdMS_TO_TICKS(1500))) return false;
    if (!xSemaphoreTake(ack, pdMS_TO_TICKS(3000))) { command_timed_out = true; return false; }
    bool ok = !atomic_load(&failed);
    if (on && ok) { stop_at = esp_timer_get_time() + 60000000ULL; atomic_store(&enabled, true); }
    return ok;
}
void node_capture_frame(const node_frame_t *f, uint64_t us, bool tx) {
    if (!atomic_load(&enabled) || f->dlc > 8) return;
    log_item_t item = {.frame = *f, .us = us, .kind = tx ? 1 : 0, .sequence = atomic_fetch_add(&sequence, 1)};
    if (!xQueueSend(queue, &item, 0)) atomic_fetch_add(&dropped, 1);
}
void node_capture_status(char *s, size_t n) {
    snprintf(s, n, "LOG=%u MOUNT=%u RECORDS=%u LOG_DROP=%u LOG_FAIL=%u", atomic_load(&enabled), mounted,
             atomic_load(&written), atomic_load(&dropped), atomic_load(&failed));
}
bool node_capture_format(void) {
    node_capture_enable(false);
    if (command_timed_out) return false;
    if (esp_spiffs_format("capture") != ESP_OK) return false;
    if (!mounted) {
        esp_vfs_spiffs_conf_t cfg = {.base_path = "/capture", .partition_label = "capture", .max_files = 2};
        if (esp_vfs_spiffs_register(&cfg) != ESP_OK) return false;
    }
    mounted = true; atomic_store(&failed, false); return true;
}
bool node_capture_export(bool (*write)(void *, const char *, size_t), void *ctx) {
    node_capture_enable(false);
    if (command_timed_out || !mounted) return false;
    FILE *file = fopen("/capture/latest.jsonl", "r"); if (!file) return false;
    char block[128]; size_t n; bool ok = true;
    while ((n = fread(block, 1, sizeof(block), file))) {
        if (!write(ctx, block, n)) { ok = false; break; }
    }
    if (ferror(file)) ok = false;
    fclose(file); return ok;
}
void node_capture_tick(uint64_t now_us) {
    if (atomic_load(&enabled) && now_us >= stop_at) node_capture_enable(false);
}
