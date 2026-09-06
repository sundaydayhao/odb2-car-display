#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard.h"
#include "dashboard_text.h"
#include "fonts/dashboard_fonts.h"
#include "src/libs/lodepng/lodepng.h"
#include "src/misc/lv_text_private.h"

static int s_errors;
static int s_labels;
static lv_point_t s_touch_point;
static lv_indev_state_t s_touch_state;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    data->point = s_touch_point;
    data->state = s_touch_state;
}

static void check_glyphs(const char *text, const lv_font_t *font)
{
    uint32_t offset = 0;
    while (text[offset]) {
        uint32_t codepoint = lv_text_encoded_next(text, &offset);
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder) {
            fprintf(stderr, "Missing glyph U+%04lX in %s\n", (unsigned long)codepoint, text);
            ++s_errors;
        }
    }
}

static void inspect(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    if (bounds.x1 < 0 || bounds.y1 < 0 || bounds.x2 >= 800 || bounds.y2 >= 480) {
        fprintf(stderr, "Object outside screen: %ld,%ld..%ld,%ld\n",
                (long)bounds.x1, (long)bounds.y1, (long)bounds.x2, (long)bounds.y2);
        ++s_errors;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        ++s_labels;
        const char *text = lv_label_get_text(obj);
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        check_glyphs(text, font);
        if (strstr(text, "OBD") || strstr(text, "CAN") || strstr(text, "ESP32")) {
            fprintf(stderr, "Technical text on screen: %s\n", text);
            ++s_errors;
        }
        int width = lv_text_get_width(text, strlen(text), font, 0);
        if (width > lv_obj_get_width(obj)) {
            fprintf(stderr, "Clipped text: %s requires %d, has %ld\n", text, width, (long)lv_obj_get_width(obj));
            ++s_errors;
        }
        lv_obj_t *parent = lv_obj_get_parent(obj);
        lv_area_t parent_bounds;
        lv_obj_get_coords(parent, &parent_bounds);
        if (bounds.x1 < parent_bounds.x1 || bounds.y1 < parent_bounds.y1 ||
            bounds.x2 > parent_bounds.x2 || bounds.y2 > parent_bounds.y2) {
            fprintf(stderr, "Text outside parent: %s\n", text);
            ++s_errors;
        }
        for (uint32_t i = lv_obj_get_index(obj) + 1; i < lv_obj_get_child_count(parent); ++i) {
            lv_obj_t *other = lv_obj_get_child(parent, i);
            if (!lv_obj_check_type(other, &lv_label_class) || lv_obj_has_flag(other, LV_OBJ_FLAG_HIDDEN)) continue;
            lv_area_t other_bounds;
            lv_obj_get_coords(other, &other_bounds);
            if (bounds.x1 <= other_bounds.x2 && bounds.x2 >= other_bounds.x1 &&
                bounds.y1 <= other_bounds.y2 && bounds.y2 >= other_bounds.y1) {
                fprintf(stderr, "Overlapping labels: %s / %s\n", text, lv_label_get_text(other));
                ++s_errors;
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) inspect(lv_obj_get_child(obj, i));
}

static unsigned count_text(lv_obj_t *obj, const char *text)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return 0;
    unsigned count = lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) count += count_text(lv_obj_get_child(obj, i), text);
    return count;
}

static void snapshot(lv_display_t *display, const char *dir, const char *state, int page)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_update_layout(screen);
    inspect(screen);
    lv_draw_buf_t *buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
    assert(buf != NULL);
    unsigned char *rgb = malloc(800 * 480 * 3);
    assert(rgb != NULL);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 800; ++x) {
            const uint8_t *pixel = buf->data + y * buf->header.stride + x * 3;
            size_t pos = (y * 800 + x) * 3;
            rgb[pos] = pixel[2];
            rgb[pos + 1] = pixel[1];
            rgb[pos + 2] = pixel[0];
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s-%d.png", dir, state, page);
    unsigned char *png = NULL;
    size_t png_size = 0;
    unsigned result = lodepng_encode24(&png, &png_size, rgb, 800, 480);
    assert(result == 0);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(png, 1, png_size, file) == png_size);
    fclose(file);
    lv_free(png);
    free(rgb);
    lv_draw_buf_destroy(buf);
}

static void tap_at(lv_indev_t *pointer, int x, int y)
{
    lv_obj_update_layout(lv_screen_active());
    s_touch_point = (lv_point_t){x, y};
    s_touch_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(pointer);
    lv_tick_inc(50);
    s_touch_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(pointer);
}

static void inspect_all(lv_display_t *display, lv_indev_t *pointer, const char *dir, const char *state)
{
    for (int page = 0; page < 5; ++page) {
        lv_obj_update_layout(lv_display_get_screen_active(display));
        tap_at(pointer, 96 + page * 152, 450);
        assert(dashboard_active_page() == page);
        snapshot(display, dir, state, page);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *display = lv_display_create(800, 480);
    static uint8_t buffer[800 * 48 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    dashboard_create(display);
    dashboard_data_t link_state={.link_state=DASH_LINK_SCANNING};
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_SCANNING)==1);
    snapshot(display,argv[1],"ble-scanning",0);
    link_state.link_state=DASH_LINK_CONNECTING;
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_CONNECTING)==1);
    snapshot(display,argv[1],"ble-connecting",0);
    link_state.link_state=DASH_LINK_INITIALIZING;
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_INITIALIZING)==1);
    snapshot(display,argv[1],"ble-initializing",0);
    link_state.link_state=DASH_LINK_WAITING;
    dashboard_update(&link_state);
    inspect_all(display, pointer, argv[1], "waiting");

    dashboard_data_t sample = {
        .value = {86, 2450, 68, 89, 14200, 36, 24},
        .valid = {true, true, true, true, true, true, true},
    };
    dashboard_update(&sample);
    inspect_all(display, pointer, argv[1], "live");
    sample.value[DASH_SPEED] = 255;
    sample.value[DASH_RPM] = 16383;
    sample.value[DASH_FUEL] = 100;
    sample.value[DASH_COOLANT] = 215;
    sample.value[DASH_VOLTAGE] = 65535;
    sample.value[DASH_LOAD] = 100;
    sample.value[DASH_THROTTLE] = 100;
    dashboard_update(&sample);
    inspect_all(display, pointer, argv[1], "limits");
    sample.value[DASH_COOLANT] = -40;
    sample.value[DASH_FUEL] = 5;
    dashboard_update(&sample);
    inspect_all(display, pointer, argv[1], "low-fuel");
    sample.value[DASH_FUEL] = 68;
    dashboard_update(&sample);
    inspect_all(display, pointer, argv[1], "high-rpm");

    uint8_t selected = dashboard_active_page();
    lv_tick_inc(61000);
    lv_timer_handler();
    assert(dashboard_active_page() == selected);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    assert(count_text(screen, TXT_WAITING) == 1);
    assert(count_text(screen, "--") >= 6);
    inspect_all(display, pointer, argv[1], "expired");
    dashboard_data_t stopped = {.value = {0}, .valid = {[DASH_SPEED] = true}};
    dashboard_update(&stopped);
    dashboard_select_page(0);
    assert(count_text(screen, "0") == 1);
    assert(count_text(screen, "--") >= 6);
    assert(count_text(screen, TXT_LIVE) == 1);
    /* Bench demonstration is explicitly entered by touch and survives live updates. */
    tap_at(pointer,760,28);
    snapshot(display,argv[1],"settings",0);
    tap_at(pointer,548,207);
    assert(dashboard_demo_enabled());
    assert(count_text(screen,TXT_DEMO) == 1);
    dashboard_update(&stopped);
    assert(count_text(screen,"86") == 1);
    for (int day = 0; day < 2; ++day) {
        dashboard_set_daylight(day);
        for (int theme = 0; theme < 3; ++theme) {
            tap_at(pointer,710,28);
            char name[32];
            snprintf(name,sizeof(name),"theme-picker-%d-%d",day,theme);
            snapshot(display,argv[1],name,0);
            tap_at(pointer,450,165+theme*90);
            assert(dashboard_active_theme() == theme);
            snprintf(name,sizeof(name),"demo-%d-%d",day,theme);
            inspect_all(display,pointer,argv[1],name);
        }
    }
    dashboard_select_page(3);
    tap_at(pointer,177,85);
    assert(count_text(screen,"82.4") == 1);
    snapshot(display,argv[1],"today",3);
    dashboard_select_page(2);
    tap_at(pointer,760,28);
    tap_at(pointer,460,326);
    assert(count_text(screen,"240") == 2);
    snapshot(display,argv[1],"kpa",2);
    tap_at(pointer,760,28);
    tap_at(pointer,535,326);
    assert(count_text(screen,"34.8") == 2);
    snapshot(display,argv[1],"psi",2);
    dashboard_select_page(4);
    tap_at(pointer,760,28);
    tap_at(pointer,548,267);
    assert(count_text(screen,TXT_MANIFOLD) == 1);
    snapshot(display,argv[1],"naturally-aspirated",4);
    lv_tick_inc(61000);
    lv_timer_handler();
    assert(dashboard_active_page() == 4);
    assert(dashboard_demo_enabled());
    tap_at(pointer,760,28);
    tap_at(pointer,548,207);
    assert(!dashboard_demo_enabled());
    assert(count_text(screen,TXT_WAITING) == 1);
    /* Repeated rebuilds exercise object/binding lifetimes and stable touch targets. */
    for (int i = 0; i < 150; ++i) {
        dashboard_select_theme(i%3);
        tap_at(pointer,96+(i%5)*152,450);
        assert(dashboard_active_page() == i%5);
    }
    printf("Inspected %d rendered labels; %d errors. Five-page touch navigation, themes, demo isolation, stale data and units passed.\n", s_labels, s_errors);
    return s_errors ? 1 : 0;
}
