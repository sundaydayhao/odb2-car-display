#pragma once

#include "esp_err.h"

/* The recorder task owns this backend. Hold its filesystem lock and exclude
 * upload/file users before calling these functions. Enable every sdmmc_guard
 * linker wrapper; the guard also supports pre-app_main Hosted initialization.
 * The board has no CD input; probe reports card responsiveness, not physical
 * socket presence. The SD IO LDO is retained for the backend's lifetime. */

/* Mount the FAT card without formatting. Already mounted returns ESP_OK. */
esp_err_t record_storage_mount(void);

/* Issue read-only CMD13; returns ESP_ERR_INVALID_STATE when not mounted. */
esp_err_t record_storage_probe(void);

/* Close all file handles first. Already unmounted returns ESP_OK. */
esp_err_t record_storage_unmount(void);
