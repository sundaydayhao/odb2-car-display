#pragma once
#include "node_core.h"
#include "esp_err.h"
esp_err_t node_capture_init(void);
bool node_capture_enable(bool);
void node_capture_frame(const node_frame_t *, uint64_t time_us, bool tx);
void node_capture_status(char *, size_t);
bool node_capture_format(void);
bool node_capture_export(bool (*write)(void *, const char *, size_t), void *ctx);
void node_capture_tick(uint64_t now_us);
