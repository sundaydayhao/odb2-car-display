#include "voice_codec_memory.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_uintptr_t s_owner;
static atomic_uintptr_t s_media_owner;
static size_t s_media_bytes, s_media_allocations;

bool media_codec_memory_begin(void)
{
    uintptr_t task=(uintptr_t)xTaskGetCurrentTaskHandle(), expected=0;
    if (!task || !atomic_compare_exchange_strong(&s_media_owner,&expected,task)) return false;
    s_media_bytes=s_media_allocations=0; return true;
}
void media_codec_memory_end(void)
{
    uintptr_t task=(uintptr_t)xTaskGetCurrentTaskHandle();
    if (!task || atomic_load(&s_media_owner)!=task) return;
    size_t bytes=s_media_bytes, count=s_media_allocations;
    atomic_store(&s_media_owner,0);
    if (count) ESP_LOGI("media","MP3 state in PSRAM: allocations=%u bytes=%u",(unsigned)count,(unsigned)bytes);
}
static bool media_scope(void)
{
    uintptr_t owner=atomic_load(&s_media_owner);
    return owner && owner==(uintptr_t)xTaskGetCurrentTaskHandle();
}
/* Only the owning task accesses these counters while the scope is open. */
static size_t s_allocations, s_internal_bytes, s_fallbacks;

bool voice_codec_memory_begin(void)
{
    uintptr_t task = (uintptr_t)xTaskGetCurrentTaskHandle();
    uintptr_t expected = 0;
    if (!task || !atomic_compare_exchange_strong(&s_owner, &expected, task)) {
        ESP_LOGW("voice", "Opus allocation scope already in use or no current task");
        return false;
    }
    s_allocations = s_internal_bytes = s_fallbacks = 0;
    return true;
}

void voice_codec_memory_end(void)
{
    uintptr_t task = (uintptr_t)xTaskGetCurrentTaskHandle();
    if (!task || atomic_load(&s_owner) != task) {
        ESP_LOGW("voice", "Opus allocation scope end ignored for non-owner");
        return;
    }
    size_t allocations = s_allocations, internal_bytes = s_internal_bytes, fallbacks = s_fallbacks;
    atomic_store(&s_owner, 0);
    ESP_LOGI("voice", "Opus allocation scope: allocations=%u internal_bytes=%u fallbacks=%u",
             (unsigned)allocations, (unsigned)internal_bytes, (unsigned)fallbacks);
}

/* esp_audio_codec 2.3.0's P4 audio_codec_mem.c.obj defines this exact ABI as
 * weak and otherwise forwards to malloc(size). Its module tag is AUD_Codec
 * for both encoders and decoders, so task + explicit scope selects this one
 * Opus open. Keep heap allocations compatible with the library's normal free.
 * Global heap thresholds and allocations from other tasks remain unchanged. */
void *media_lib_module_malloc(const char *module, size_t size);
void *media_lib_module_malloc(const char *module, size_t size)
{
    (void)module;
    if (media_scope()) {
        void *memory=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (memory) { ++s_media_allocations; s_media_bytes+=size; }
        return memory;
    }
    uintptr_t owner = atomic_load(&s_owner);
    if (!owner || owner != (uintptr_t)xTaskGetCurrentTaskHandle()) return malloc(size);

    void *memory = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    bool fallback = memory == NULL;
    if (fallback) {
        /* Preserve the original allocation policy if internal RAM is tight.
         * At the current 16 KB threshold, Opus state prefers PSRAM here. */
        memory = malloc(size);
        ++s_fallbacks;
        ESP_LOGW("voice", "Opus internal allocation unavailable: bytes=%u; default heap fallback",
                 (unsigned)size);
    }
    ++s_allocations;
    bool internal = memory && esp_ptr_internal(memory);
    if (internal) s_internal_bytes += size;
    ESP_LOGI("voice", "Opus state allocation: bytes=%u internal=%d fallback=%d success=%d",
             (unsigned)size, internal, fallback, memory != NULL);
    return memory;
}

/* Same verified weak ABI as the codec archive: module, element count, size.
 * MP3 opens through calloc; the explicit owner scope cannot catch Opus/AEC. */
void *media_lib_module_calloc(const char *module, size_t count, size_t size);
void *media_lib_module_calloc(const char *module, size_t count, size_t size)
{
    (void)module;
    if (!media_scope()) return calloc(count,size);
    if (size && count>SIZE_MAX/size) return NULL;
    void *memory=heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (memory) { ++s_media_allocations; s_media_bytes+=count*size; }
    return memory;
}

/* esp_audio_effects' weak hooks use these ABIs. Its stock aligned hook also
 * ignores the hint and calls posix_memalign; preserve that outside media's
 * scope. These are CPU workspaces, never peripheral DMA buffers. */
void *media_lib_malloc(size_t size) { return media_lib_module_malloc(NULL,size); }
void *media_lib_calloc(size_t count,size_t size) { return media_lib_module_calloc(NULL,count,size); }
void *media_lib_caps_malloc_align(size_t alignment,size_t size,unsigned hint)
{
    (void)hint;
    if (!media_scope()) {
        void *memory=NULL;
        return posix_memalign(&memory,alignment,size)==0?memory:NULL;
    }
    void *memory=heap_caps_aligned_alloc(alignment,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (memory) { ++s_media_allocations; s_media_bytes+=size; }
    return memory;
}
