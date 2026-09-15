#include "lvgl.h"
#include "esp_heap_caps.h"
#include <string.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

/* Screen objects, labels and draw buffers share the board's 32 MB PSRAM.
 * Task stacks and peripheral DMA buffers are allocated by their own drivers.
 * Do not fall back to internal RAM: filling it can break flash/OTA and radio. */
static const uint32_t gui_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, gui_caps); }
void *lv_realloc_core(void *ptr, size_t size) { return heap_caps_realloc(ptr, size, gui_caps); }
void lv_free_core(void *ptr) { heap_caps_free(ptr); }

void lv_mem_monitor_core(lv_mem_monitor_t *monitor)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, gui_caps);
    memset(monitor, 0, sizeof(*monitor));
    /* These figures describe the shared PSRAM heap, including non-GUI users. */
    monitor->total_size = info.total_free_bytes + info.total_allocated_bytes;
    monitor->free_size = info.total_free_bytes;
    monitor->free_biggest_size = info.largest_free_block;
    monitor->free_cnt = info.free_blocks;
    monitor->used_cnt = info.allocated_blocks;
    monitor->max_used = monitor->total_size - info.minimum_free_bytes;
    if (monitor->total_size)
        monitor->used_pct = (uint8_t)(100 * info.total_allocated_bytes / monitor->total_size);
    if (monitor->free_size)
        monitor->frag_pct = (uint8_t)(100 - 100 * monitor->free_biggest_size / monitor->free_size);
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(gui_caps, true) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif
