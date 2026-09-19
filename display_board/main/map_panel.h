#pragma once
#include "dashboard.h"
#include "gps_model.h"
void map_panel_create(lv_obj_t *parent,const dashboard_palette_t *palette);
void map_panel_detach(void);
void map_panel_update(const gps_fix_t *fix,uint32_t now_ms);
typedef struct map_panel_stats {
    uint32_t jobs,frames,cache_hits,cancelled,slices,max_slice_ms,last_render_ms;
    uint32_t ui_stack_min_bytes;
    bool busy,visible;
} map_panel_stats_t;
/* LVGL-thread/lock-only diagnostics; neither function performs rendering. */
bool map_panel_busy(void);
void map_panel_stats(map_panel_stats_t *out);
