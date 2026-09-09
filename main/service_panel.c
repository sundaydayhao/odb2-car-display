#include "service_panel.h"
#include <string.h>
#include <stdio.h>
#include "fonts/dashboard_fonts.h"

static panel_action_cb_t s_action;
static lv_obj_t *s_screen, *s_toolbar, *s_overlay, *s_camera, *s_canvas, *s_symbols[6], *s_more[6], *s_upload;
static lv_obj_t *s_tooltip, *s_tooltip_text;
static lv_obj_t *s_placeholder;
static uint16_t *s_pixels;
static char s_full_detail[6][96];
static unsigned s_states[6];
static const char *s_titles[] = {"网络", "小智语音", "车内视野", "行车记录", "同步", "系统更新"};
static const char *s_icons[] = {LV_SYMBOL_WIFI, LV_SYMBOL_AUDIO, LV_SYMBOL_VIDEO,
                              LV_SYMBOL_SD_CARD, LV_SYMBOL_UPLOAD, LV_SYMBOL_DOWNLOAD};
static const uint32_t s_colors[2][5] = {
    {0x7F918F, 0xF7C776, 0x70D9FF, 0x61EDBF, 0xFF7F92},
    {0x5E666B, 0x8F5700, 0x0A679B, 0x007E6D, 0xC62727},
};

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, w);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(obj, value);
    return obj;
}

static void close_event(lv_event_t *event)
{
    lv_obj_t *overlay = lv_event_get_user_data(event);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    if (overlay == s_camera && s_action) s_action(PANEL_CAMERA_OFF);
}

static lv_obj_t *modal(lv_obj_t *overlay, const char *title, int width, int height)
{
    lv_obj_t *sheet = box(overlay, 0, 0, width, height, 0x202826);
    lv_obj_center(sheet);
    lv_obj_set_style_radius(sheet, 8, 0);
    text(sheet, title, 24, 20, width - 100, &dashboard_text_22, 0xEFF6F3);
    lv_obj_t *close = box(sheet, width - 64, 8, 48, 48, 0x202826);
    text(close, LV_SYMBOL_CLOSE, 16, 17, 28, &lv_font_montserrat_14, 0xA8B7B2);
    lv_obj_add_event_cb(close, close_event, LV_EVENT_CLICKED, overlay);
    return sheet;
}

static void command(lv_event_t *event)
{
    panel_action_t action = (panel_action_t)(uintptr_t)lv_event_get_user_data(event);
    if (s_action) s_action(action);
}

static void cmd_button(lv_obj_t *parent, const char *title, int x, int y, int w, panel_action_t action)
{
    lv_obj_t *obj = box(parent, x, y, w, 48, 0x35423D);
    lv_obj_set_style_radius(obj, 4, 0);
    text(obj, title, 16, 14, w - 24, &dashboard_text_18, 0xB8F0D8);
    lv_obj_add_event_cb(obj, command, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
}

static void confirm_update(lv_event_t *event)
{
    (void)event;
    lv_obj_clean(s_overlay);
    memset(s_more, 0, sizeof(s_more)); s_upload = NULL;
    lv_obj_t *sheet = modal(s_overlay, "安装更新", 600, 288);
    text(sheet, "请先驻车并保持供电。升级期间设备会重启。", 24, 86, 552, &dashboard_text_22, 0xEFF6F3);
    text(sheet, "车速未知或车辆移动时禁止安装。", 24, 150, 552, &dashboard_text_18, 0xF7C776);
    cmd_button(sheet, "确认安装", 24, 216, 180, PANEL_OTA_INSTALL);
}

static void upload_changed(lv_event_t *event)
{
    if (s_action) s_action(lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED) ?
                          PANEL_UPLOAD_ON : PANEL_UPLOAD_OFF);
}

static void open_services(void)
{
    lv_obj_add_flag(s_tooltip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(s_overlay);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sheet = modal(s_overlay, "设备中心", 700, 452);
    for (int i = 0; i < 6; ++i) {
        text(sheet, s_titles[i], 24, 80 + i * 40, 120, &dashboard_text_18, 0xA8B7B2);
        s_more[i] = text(sheet, s_full_detail[i], 158, 80 + i * 40, 514, &dashboard_text_18, 0xEFF6F3);
    }
    text(sheet, "上传车辆记录", 24, 326, 180, &dashboard_text_18, 0xEFF6F3);
    s_upload = lv_switch_create(sheet);
    lv_obj_set_pos(s_upload, 210, 314); lv_obj_set_size(s_upload, 64, 38);
    lv_obj_add_event_cb(s_upload, upload_changed, LV_EVENT_VALUE_CHANGED, NULL);
    cmd_button(sheet, "同步记录", 24, 380, 152, PANEL_UPLOAD_NOW);
    cmd_button(sheet, "检查更新", 192, 380, 152, PANEL_OTA_CHECK);
    lv_obj_t *install = box(sheet, 360, 380, 152, 48, 0x35423D);
    lv_obj_set_style_radius(install, 4, 0);
    text(install, "安装更新", 16, 14, 128, &dashboard_text_18, 0xB8F0D8);
    lv_obj_add_event_cb(install, confirm_update, LV_EVENT_CLICKED, NULL);
}

static void tile_event(lv_event_t *event)
{
    unsigned id = (uintptr_t)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_HOVER_OVER) {
        lv_label_set_text(s_tooltip_text, s_titles[id]);
        lv_obj_center(s_tooltip_text);
        lv_obj_align_to(s_tooltip, lv_event_get_target_obj(event), LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
        lv_obj_remove_flag(s_tooltip, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_HOVER_LEAVE || code == LV_EVENT_PRESSED) {
        lv_obj_add_flag(s_tooltip, LV_OBJ_FLAG_HIDDEN);
    }
    if (id == 1) {
        if (code == LV_EVENT_PRESSED && s_action) s_action(PANEL_VOICE_DOWN);
        if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_action) s_action(PANEL_VOICE_UP);
    } else if (code == LV_EVENT_CLICKED) {
        if (id == 2) { service_panel_camera_visible(true); if (s_action) s_action(PANEL_CAMERA_ON); }
        else if (id == 3) { if (s_action) s_action(PANEL_SEAL); open_services(); }
        else open_services();
    }
}

static void toolbar_style(lv_event_t *event)
{
    (void)event;
    bool daylight = lv_color_luminance(lv_obj_get_style_bg_color(s_toolbar, 0)) > 128;
    for (int i = 0; i < 6; ++i) {
        if (!s_symbols[i]) continue;
        lv_obj_set_style_text_color(s_symbols[i], lv_color_hex(s_colors[daylight][s_states[i]]), 0);
        lv_obj_set_style_bg_color(lv_obj_get_parent(s_symbols[i]),
                                 lv_color_hex(daylight ? 0xDCE3E1 : 0x26322E), LV_STATE_PRESSED);
    }
}

void service_panel_create(lv_obj_t *screen, lv_obj_t *toolbar, panel_action_cb_t action)
{
    s_screen = screen; s_toolbar = toolbar; s_action = action;
    int w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
    int toolbar_width = lv_obj_get_width(toolbar), toolbar_height = lv_obj_get_height(toolbar);
    for (int i = 0; i < 6; ++i) {
        int left = i * toolbar_width / 6, width = (i + 1) * toolbar_width / 6 - left;
        lv_obj_t *tile = box(toolbar, left, 0, width, toolbar_height, 0x26322E);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_radius(tile, 4, 0);
        s_symbols[i] = text(tile, s_icons[i], 0, 0, 32, &lv_font_montserrat_24, s_colors[0][0]);
        lv_obj_set_style_text_align(s_symbols[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(s_symbols[i]);
        lv_obj_add_event_cb(tile, tile_event, LV_EVENT_ALL, (void *)(uintptr_t)i);
    }
    lv_obj_add_event_cb(toolbar, toolbar_style, LV_EVENT_STYLE_CHANGED, NULL);
    toolbar_style(NULL);
    /* Keep hover hints under dashboard modals and outside the compact hit areas. */
    s_tooltip = box(lv_obj_get_parent(toolbar), 0, 0, 128, 36, 0x202826);
    lv_obj_remove_flag(s_tooltip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(s_tooltip, 4, 0);
    s_tooltip_text = text(s_tooltip, "", 0, 0, 112, &dashboard_text_18, 0xEFF6F3);
    lv_obj_set_style_text_align(s_tooltip_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_tooltip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(s_tooltip, lv_obj_get_index(toolbar) + 1);
    s_overlay = box(screen, 0, 0, w, h, 0x000000);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_camera = box(screen, 0, 0, w, h, 0x000000);
    lv_obj_set_style_bg_opa(s_camera, LV_OPA_70, 0);
    lv_obj_add_flag(s_camera, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sheet = modal(s_camera, "车内视野", 368, 350);
    s_pixels = lv_malloc_zeroed(320 * 240 * 2);
    if (s_pixels) {
        s_canvas = lv_canvas_create(sheet);
        lv_obj_set_pos(s_canvas, 24, 78);
        lv_canvas_set_buffer(s_canvas, s_pixels, 320, 240, LV_COLOR_FORMAT_RGB565);
    }
    s_placeholder = text(sheet, "等待画面", 24, 184, 320, &dashboard_text_22, 0xA8B7B2);
    lv_obj_set_style_text_align(s_placeholder, LV_TEXT_ALIGN_CENTER, 0);
}

void service_panel_status(unsigned id, unsigned state, const char *detail)
{
    if (id >= 6 || !s_screen) return;
    snprintf(s_full_detail[id], sizeof(s_full_detail[id]), "%s", detail);
    s_states[id] = state < 5 ? state : 4;
    toolbar_style(NULL);
    if (s_more[id]) lv_label_set_text(s_more[id], detail);
    if (id == 2) lv_label_set_text(s_placeholder, detail);
}

void service_panel_upload_enabled(bool enabled)
{
    if (!s_upload) return;
    if (enabled) lv_obj_add_state(s_upload, LV_STATE_CHECKED); else lv_obj_remove_state(s_upload, LV_STATE_CHECKED);
}

void service_panel_camera_visible(bool visible)
{
    if (visible) lv_obj_remove_flag(s_camera, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_camera, LV_OBJ_FLAG_HIDDEN);
}

void service_panel_camera_frame(const uint16_t *pixels)
{
    if (!s_canvas) return;
    memcpy(s_pixels, pixels, 320 * 240 * 2);
    lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_canvas);
}

void service_panel_camera_clear(void)
{
    if (s_canvas) { memset(s_pixels, 0, 320 * 240 * 2); lv_obj_invalidate(s_canvas); }
    lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
}
