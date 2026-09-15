#include "record_storage.h"

#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t *s_card;
static sd_pwr_ctrl_handle_t s_power;

#define CARD_PROBE_TIMEOUT_MS 150

esp_err_t record_storage_mount(void)
{
    if (s_card) return ESP_OK;

    /* BSP 5.2.2 allocates a new LDO handle on every mount attempt and leaves
     * it allocated when FAT/card initialization fails. Own one here so an
     * empty socket can be retried without leaking or reacquiring LDO4. */
    if (!s_power) {
        const sd_pwr_ctrl_ldo_config_t power_config = {.ldo_chan_id = 4};
        sd_pwr_ctrl_handle_t power = NULL;
        esp_err_t result = sd_pwr_ctrl_new_on_chip_ldo(&power_config, &power);
        if (result != ESP_OK) return result;
        s_power = power;
    }

    sdmmc_host_t host;
    sdmmc_slot_config_t slot;
    bsp_sdcard_get_sdmmc_host(SDMMC_HOST_SLOT_0, &host);
    bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
    host.pwr_ctrl_handle = s_power;
    host.command_timeout_ms = CARD_PROBE_TIMEOUT_MS;
    /* ALLOC_ALIGNED_BUF belongs to the SDIO byte-transfer path. IDF 5.5.5
     * allocates that buffer during card_init but FAT mount failure/unmount
     * only frees card. Memory-card block IO has its own temporary DMA buffer
     * handling, so keep the BSP default (no persistent SDIO buffer) explicit. */
    host.flags &= ~SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;
    host.dma_aligned_buffer = NULL;
    /* Keep mount-failure and unmount cleanup scoped to SD slot 0. C6 uses
     * slot 1 on the same controller; global sdmmc_host_deinit is unsuitable. */
    host.flags |= SDMMC_HOST_FLAG_DEINIT_ARG;
    host.deinit_p = sdmmc_host_deinit_slot;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 64 * 1024,
        .disk_status_check_enable = false, /* Recorder schedules explicit probes. */
    };
    sdmmc_card_t *card = NULL;
    esp_err_t result = esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot,
                                             &mount_config, &card);
    /* IDF may assign out_card before a later allocation fails and frees it.
     * Publish the handle only after the complete mount succeeds. */
    if (result == ESP_OK) {
        /* Do not shorten normal data-write/programming timeouts. */
        card->host.command_timeout_ms = 0;
        s_card = card;
    }
    return result;
}

esp_err_t record_storage_probe(void)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    sdmmc_card_t probe = *s_card;
    probe.host.command_timeout_ms = CARD_PROBE_TIMEOUT_MS;
    return sdmmc_get_status(&probe);
}

esp_err_t record_storage_unmount(void)
{
    if (!s_card) return ESP_OK;
    sdmmc_card_t *card = s_card;
    s_card = NULL;
    /* Unmount can free card before reporting a later VFS cleanup failure.
     * Never retain or retry that pointer. Keep the IO LDO for future mounts. */
    return esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, card);
}
