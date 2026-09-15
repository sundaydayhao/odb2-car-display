#pragma once
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if defined(CONFIG_DEVICE_CAMERA_ENABLED) && CONFIG_DEVICE_CAMERA_ENABLED
#define DEVICE_CAMERA_AVAILABLE 1
#else
#define DEVICE_CAMERA_AVAILABLE 0
#endif
