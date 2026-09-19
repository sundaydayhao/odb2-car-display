#pragma once
#include "esp_mcp_engine.h"
#include <stdbool.h>
esp_err_t voice_tools_register(esp_mcp_t *engine);
bool voice_tools_pending(void);
void voice_tools_cancel(void);
void voice_tools_run_pending(void);
