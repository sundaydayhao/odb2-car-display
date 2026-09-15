#include "sdmmc_guard.h"

#include <stdatomic.h>
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static _Atomic(SemaphoreHandle_t) s_host_mutex;

esp_err_t sdmmc_guard_init(void)
{
    /* Hosted can initialize from a constructor before app_main. Publish
     * exactly one mutex even if two clients make their first call together. */
    if (atomic_load(&s_host_mutex)) return ESP_OK;
    SemaphoreHandle_t candidate = xSemaphoreCreateRecursiveMutex();
    if (!candidate) return ESP_ERR_NO_MEM;
    SemaphoreHandle_t expected = NULL;
    if (!atomic_compare_exchange_strong(&s_host_mutex, &expected, candidate))
        vSemaphoreDelete(candidate);
    return ESP_OK;
}

static bool guard_take(void)
{
    return sdmmc_guard_init() == ESP_OK &&
           xSemaphoreTakeRecursive(atomic_load(&s_host_mutex), portMAX_DELAY) == pdTRUE;
}

/* --wrap redirects both external calls and SDMMC_HOST_DEFAULT's function
 * pointers. Calls within sdmmc_host.c may bind directly to their original
 * symbol; their outer public call already owns this same recursive mutex.
 * Release after each operation, not after a complete multi-command mount,
 * so Hosted can transfer between SD initialization/probe commands. */
#define GUARDED_ERROR_FUNCTION(name, parameters, arguments) \
    esp_err_t __real_##name parameters; \
    esp_err_t __wrap_##name parameters \
    { \
        if (!guard_take()) return ESP_ERR_INVALID_STATE; \
        esp_err_t result = __real_##name arguments; \
        xSemaphoreGiveRecursive(s_host_mutex); \
        return result; \
    }

GUARDED_ERROR_FUNCTION(sdmmc_host_init, (void), ())
/* IDF 5.5.5 applies card clock, input phase and the frequency readback in
 * this short host-configuration step. Keep them together: an intervening
 * other-slot transaction would make the phase/readback use its shared
 * clock context. Both current non-UHS clients use this path. The remaining
 * card initialization commands can still interleave with Hosted traffic. */
GUARDED_ERROR_FUNCTION(sdmmc_init_host_frequency,
                       (sdmmc_card_t *card), (card))

esp_err_t __real_sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *config);
esp_err_t __wrap_sdmmc_host_init_slot(int slot, const sdmmc_slot_config_t *config)
{
    if (!guard_take()) return ESP_ERR_INVALID_STATE;
    /* The other slot may have finished its last unmount after this client's
     * host_init but before init_slot. Recheck/reinitialize atomically with
     * slot creation; host_init is explicitly idempotent in IDF 5.5.5. */
    esp_err_t result = __real_sdmmc_host_init();
    if (result == ESP_OK) result = __real_sdmmc_host_init_slot(slot, config);
    xSemaphoreGiveRecursive(s_host_mutex);
    return result;
}

GUARDED_ERROR_FUNCTION(sdmmc_host_deinit_slot, (int slot), (slot))
GUARDED_ERROR_FUNCTION(sdmmc_host_deinit, (void), ())
GUARDED_ERROR_FUNCTION(sdmmc_host_do_transaction,
                       (int slot, sdmmc_command_t *command), (slot, command))
GUARDED_ERROR_FUNCTION(sdmmc_host_set_card_clk,
                       (int slot, uint32_t frequency_khz), (slot, frequency_khz))
GUARDED_ERROR_FUNCTION(sdmmc_host_set_bus_width,
                       (int slot, size_t width), (slot, width))
GUARDED_ERROR_FUNCTION(sdmmc_host_set_bus_ddr_mode,
                       (int slot, bool enabled), (slot, enabled))
GUARDED_ERROR_FUNCTION(sdmmc_host_set_input_delay,
                       (int slot, sdmmc_delay_phase_t phase), (slot, phase))
GUARDED_ERROR_FUNCTION(sdmmc_host_set_cclk_always_on,
                       (int slot, bool enabled), (slot, enabled))
GUARDED_ERROR_FUNCTION(sdmmc_host_io_int_enable, (int slot), (slot))
GUARDED_ERROR_FUNCTION(sdmmc_host_get_real_freq,
                       (int slot, int *frequency_khz), (slot, frequency_khz))
GUARDED_ERROR_FUNCTION(sdmmc_host_is_slot_set_to_uhs1,
                       (int slot, bool *is_uhs1), (slot, is_uhs1))
GUARDED_ERROR_FUNCTION(sdmmc_host_get_state,
                       (sdmmc_host_state_t *state), (state))

size_t __real_sdmmc_host_get_slot_width(int slot);
size_t __wrap_sdmmc_host_get_slot_width(int slot)
{
    if (!guard_take()) return 0;
    size_t width = __real_sdmmc_host_get_slot_width(slot);
    xSemaphoreGiveRecursive(s_host_mutex);
    return width;
}

/* Do not lock sdmmc_host_io_int_wait: it sleeps for an external interrupt
 * while another Hosted task may need to send the transaction that causes it.
 * It uses the live slot-1 interrupt/GPIO; slot-0 teardown retains the shared
 * host while slot 1 is initialized. Host shutdown must first stop its users.
 * check_buffer_alignment is also deliberately unwrapped: it only inspects
 * the address/cache alignment and does not access mutable host state. */
