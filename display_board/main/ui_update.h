#pragma once
#include "lvgl.h"
#include <string.h>

/* LVGL 9.2 invalidates labels/styles even when their value is unchanged.
 * Keep regular service polls from competing with touch-driven rendering. */
static inline void ui_label_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static inline void ui_bg_color(lv_obj_t *obj, lv_color_t color)
{
    if (!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), color)) lv_obj_set_style_bg_color(obj, color, 0);
}
static inline void ui_text_color(lv_obj_t *obj, lv_color_t color)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, 0), color)) lv_obj_set_style_text_color(obj, color, 0);
}
static inline void ui_text_font(lv_obj_t *obj, const lv_font_t *font)
{
    if (lv_obj_get_style_text_font(obj, 0) != font) lv_obj_set_style_text_font(obj, font, 0);
}

enum { UI_TOUCH_PERIOD_MS = 8, UI_REFRESH_PERIOD_MS = 16, UI_STATUS_PERIOD_MS = 100,
       UI_DRAW_LINES = 100 };

/* Called with the LVGL lock held. The stock P4 panel has no touch IRQ,
 * so its input read timer must remain enabled and independent of rendering. */
static inline void ui_display_timing(lv_display_t *display, lv_indev_t *input)
{
    lv_timer_set_period(lv_display_get_refr_timer(display), UI_REFRESH_PERIOD_MS);
    if (input && lv_indev_get_read_timer(input))
        lv_timer_set_period(lv_indev_get_read_timer(input), UI_TOUCH_PERIOD_MS);
}
