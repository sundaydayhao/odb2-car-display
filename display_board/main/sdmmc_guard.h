#pragma once

#include "esp_err.h"

/* Explicit initialization is optional; wrappers also initialize atomically
 * for Hosted constructors before app_main. They serialize host configuration
 * with SD/SDIO transactions.
 * The guard lives for the application's lifetime; there is no deinit. */
esp_err_t sdmmc_guard_init(void);
