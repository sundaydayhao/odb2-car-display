#pragma once
#include "dashboard.h"
#include "media_model.h"

void media_panel_handlers(media_action_cb_t action, void (*settings)(void));
void media_panel_create(lv_obj_t *parent, const dashboard_palette_t *palette);
void media_panel_detach(void);
void media_panel_update(const media_status_t *status);
