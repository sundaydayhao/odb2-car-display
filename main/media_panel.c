#include "media_panel.h"
#include "fonts/dashboard_fonts.h"
#include <stdio.h>
#include <string.h>

static media_action_cb_t s_action;
static void (*s_settings)(void);
static media_status_t s_status = {.volume = 35};
static media_kind_t s_kind;
static lv_obj_t *s_parent, *s_title, *s_detail, *s_time, *s_play, *s_volume, *s_volume_text, *s_network;
static lv_obj_t *s_rows[3], *s_marks[3], *s_modes[2], *s_mode_labels[2];
static dashboard_palette_t s_palette;
static int s_width, s_height;
static int px(int n) { return n * s_width / 1024; }
static int py(int n) { return n * s_height / 456; }

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, px(x), py(y)); lv_obj_set_size(obj, px(w), py(h));
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}
static lv_obj_t *label(lv_obj_t *parent, const char *value, int x, int y, int w, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, px(x), py(y)); lv_obj_set_width(obj, px(w));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, value);
    return obj;
}
static bool emit(media_action_t action, unsigned value)
{
    media_request_t request = {.action = action, .value = value};
    bool accepted = s_action && s_action(&request);
    if (!accepted && s_detail) lv_label_set_text(s_detail, "操作未提交，请稍后重试");
    return accepted;
}
static void rebuild(void);
static void choose_kind(lv_event_t *event)
{
    s_kind = (media_kind_t)(uintptr_t)lv_event_get_user_data(event);
    rebuild();
}
static void choose_source(lv_event_t *event)
{
    unsigned source = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if ((source == MEDIA_CUSTOM_MUSIC && !s_status.custom_music) ||
        (source == MEDIA_CUSTOM_RADIO && !s_status.custom_radio)) {
        if (s_settings) s_settings();
    } else emit(MEDIA_SELECT, source);
}
static void control(lv_event_t *event)
{
    unsigned action = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (action == 0) emit(MEDIA_SELECT, media_adjacent_source(s_status.source, -1, s_status.source >= 3 ? s_status.custom_radio : s_status.custom_music));
    if (action == 1) emit(MEDIA_TOGGLE, 0);
    if (action == 2) emit(MEDIA_SELECT, media_adjacent_source(s_status.source, 1, s_status.source >= 3 ? s_status.custom_radio : s_status.custom_music));
    if (action == 3) emit(MEDIA_STOP, 0);
}
static void volume_changed(lv_event_t *event)
{
    unsigned value = lv_slider_get_value(lv_event_get_target_obj(event));
    char text[20]; snprintf(text, sizeof(text), "%u%%", value);
    lv_label_set_text(s_volume_text, text);
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) emit(MEDIA_VOLUME, value);
}
static void refresh(void)
{
    if (!s_parent) return;
    const dashboard_palette_t *p = &s_palette;
    lv_label_set_text(s_title, *s_status.title ? s_status.title : media_sources[s_status.source].title);
    lv_label_set_text(s_detail, *s_status.detail ? s_status.detail : "选择音乐或电台开始播放");
    lv_obj_set_style_text_color(s_detail, lv_color_hex(s_status.state == MEDIA_ERROR ? p->red : p->muted), 0);
    lv_label_set_text(s_network, s_status.network ? "Wi-Fi 已连接" : "等待 Wi-Fi 连接");
    bool active = s_status.state == MEDIA_PLAYING || s_status.state == MEDIA_BUFFERING || s_status.state == MEDIA_CONNECTING;
    lv_label_set_text(s_play, active ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    char time[64]; unsigned seconds = s_status.played_ms / 1000;
    snprintf(time, sizeof(time), "已播放 %02u:%02u%s", seconds / 60, seconds % 60, s_status.source >= 3 ? " · 直播" : "");
    lv_label_set_text(s_time, time);
    if (!lv_obj_has_state(s_volume, LV_STATE_PRESSED)) {
        lv_slider_set_value(s_volume, s_status.volume, LV_ANIM_OFF);
        char volume[16]; snprintf(volume, sizeof(volume), "%u%%", s_status.volume);
        lv_label_set_text(s_volume_text, volume);
    }
    for (unsigned i = 0; i < 3; ++i) {
        bool selected = s_kind * 3 + i == s_status.source;
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(selected ? p->accent : p->line), 0);
        lv_obj_set_style_border_width(s_rows[i], selected ? 2 : 0, 0);
        lv_label_set_text(s_marks[i], selected && active ? LV_SYMBOL_AUDIO : LV_SYMBOL_PLAY);
    }
}
static void rebuild(void)
{
    if (!s_parent) return;
    lv_obj_clean(s_parent);
    const dashboard_palette_t *p = &s_palette;
    const char *modes[] = {"在线音乐", "网络电台"};
    for (unsigned i = 0; i < 2; ++i) {
        s_modes[i] = box(s_parent, 28 + i * 156, 12, 148, 46, s_kind == i ? p->accent : p->panel, 12);
        lv_obj_add_flag(s_modes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_modes[i], choose_kind, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_mode_labels[i] = label(s_modes[i], modes[i], 16, 11, 116, &dashboard_text_18, s_kind == i ? p->on_accent : p->ink);
        lv_obj_set_style_text_align(s_mode_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    s_network = label(s_parent, "", 778, 25, 218, &dashboard_text_18, p->muted);
    lv_obj_set_style_text_align(s_network, LV_TEXT_ALIGN_RIGHT, 0);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned index = s_kind * 3 + i;
        const media_source_t *source = &media_sources[index];
        lv_obj_t *row = s_rows[i] = box(s_parent, 28, 82 + i * 102, 330, 90, p->panel, 14);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, choose_source, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        label(row, source->title, 18, 16, 256, &dashboard_text_22, p->ink);
        label(row, source->subtitle, 18, 53, 278, &dashboard_text_18, p->muted);
        s_marks[i] = label(row, LV_SYMBOL_PLAY, 294, 19, 22, &lv_font_montserrat_18, p->accent);
    }
    label(s_parent, s_kind == MEDIA_RADIO ? "通过热点收听电台直播" : "播放地址在设置中管理", 28, 406, 330, &dashboard_text_18, p->muted);
    lv_obj_t *player = box(s_parent, 382, 82, 614, 342, p->panel, 18);
    lv_obj_t *cover = box(player, 22, 24, 152, 152, p->tint, 18);
    lv_obj_t *disc = box(cover, 14, 14, 124, 124, p->accent, LV_RADIUS_CIRCLE);
    lv_obj_t *ring = box(disc, 30, 30, 64, 64, p->panel, LV_RADIUS_CIRCLE);
    box(ring, 22, 22, 20, 20, p->accent, LV_RADIUS_CIRCLE);
    label(player, "播放器", 198, 28, 360, &dashboard_text_18, p->muted);
    s_title = label(player, "", 198, 67, 390, &dashboard_text_22, p->ink);
    s_detail = label(player, "", 198, 110, 390, &dashboard_text_18, p->muted);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_detail, py(54));
    s_time = label(player, "", 24, 199, 256, &dashboard_text_18, p->muted);
    const char *icons[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT, LV_SYMBOL_STOP};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *button = box(player, 282 + i * 78, 185, 64, 58, i == 1 ? p->accent : p->tint, 16);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(button, control, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *symbol = label(button, icons[i], 0, 16, 64, &lv_font_montserrat_24, i == 1 ? p->on_accent : p->accent);
        lv_obj_set_style_text_align(symbol, LV_TEXT_ALIGN_CENTER, 0);
        if (i == 1) s_play = symbol;
    }
    label(player, LV_SYMBOL_VOLUME_MAX, 24, 281, 28, &lv_font_montserrat_24, p->muted);
    s_volume = lv_slider_create(player);
    lv_obj_set_pos(s_volume, px(76), py(288)); lv_obj_set_size(s_volume, px(422), py(8));
    lv_slider_set_range(s_volume, 0, 100);
    lv_obj_set_style_bg_color(s_volume, lv_color_hex(p->track), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume, lv_color_hex(p->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume, lv_color_hex(p->accent), LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_volume, volume_changed, LV_EVENT_RELEASED, NULL);
    s_volume_text = label(player, "", 530, 279, 64, &dashboard_text_18, p->ink);
    refresh();
}
void media_panel_handlers(media_action_cb_t action, void (*settings)(void)) { s_action = action; s_settings = settings; }
void media_panel_create(lv_obj_t *parent, const dashboard_palette_t *palette)
{
    s_parent = parent; s_palette = *palette;
    s_width = lv_obj_get_width(parent); s_height = lv_obj_get_height(parent);
    rebuild();
}
void media_panel_detach(void) { s_parent = NULL; }
void media_panel_update(const media_status_t *status)
{
    if (!status || status->source >= MEDIA_SOURCE_COUNT) return;
    s_status = *status;
    refresh();
}
