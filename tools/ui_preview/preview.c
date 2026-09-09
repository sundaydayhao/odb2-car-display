#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard.h"
#include "service_panel.h"
#include "dashboard_text.h"
#include "fonts/dashboard_fonts.h"
#include "src/libs/lodepng/lodepng.h"
#include "src/misc/lv_text_private.h"

static int s_errors;
static int s_labels;
static lv_point_t s_touch_point;
static lv_indev_state_t s_touch_state;
enum { SCREEN_WIDTH = 1024, SCREEN_HEIGHT = 600, DASH_HEIGHT = SCREEN_HEIGHT };
static unsigned s_actions[10];
static void panel_action(panel_action_t action) { ++s_actions[action]; }

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
    if (bounds.x1 < 0 || bounds.y1 < 0 || bounds.x2 >= SCREEN_WIDTH || bounds.y2 >= SCREEN_HEIGHT) {
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
        lv_point_t text_size;
        lv_text_get_size(&text_size, text, font, 0, 0, lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
        int width = lv_text_get_width(text, strlen(text), font, 0);
        if ((lv_label_get_long_mode(obj) == LV_LABEL_LONG_CLIP && width > lv_obj_get_width(obj)) ||
            text_size.y > lv_obj_get_height(obj)) {
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
    lv_obj_t *toolbar = dashboard_service_toolbar();
    assert(lv_obj_get_child_count(toolbar) == 6);
    for (int i = 0; i < 6; ++i) {
        lv_obj_t *tile = lv_obj_get_child(toolbar, i);
        lv_area_t bounds;
        lv_obj_get_coords(tile, &bounds);
        assert(bounds.y1 >= 0 && bounds.y2 < 75);
        assert(lv_obj_get_width(tile) >= 44 && lv_obj_get_height(tile) >= 44);
        assert(lv_obj_get_child_count(tile) == 1);
        lv_obj_t *symbol = lv_obj_get_child(tile, 0);
        assert(lv_obj_check_type(symbol, &lv_label_class));
        const char *value = lv_label_get_text(symbol);
        uint32_t offset = 0;
        assert(lv_text_encoded_next(value, &offset) != 0 && value[offset] == '\0');
    }
    inspect(screen);
    lv_draw_buf_t *buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB888);
    assert(buf != NULL);
    unsigned char *rgb = malloc(SCREEN_WIDTH * SCREEN_HEIGHT * 3);
    assert(rgb != NULL);
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            const uint8_t *pixel = buf->data + y * buf->header.stride + x * 3;
            size_t pos = (y * SCREEN_WIDTH + x) * 3;
            rgb[pos] = pixel[2];
            rgb[pos + 1] = pixel[1];
            rgb[pos + 2] = pixel[0];
        }
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s-%d.png", dir, state, page);
    unsigned char *png = NULL;
    size_t png_size = 0;
    unsigned result = lodepng_encode24(&png, &png_size, rgb, SCREEN_WIDTH, SCREEN_HEIGHT);
    assert(result == 0);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(png, 1, png_size, file) == png_size);
    fclose(file);
    lv_free(png);
    free(rgb);
    lv_draw_buf_destroy(buf);
}

static void tap_raw(lv_indev_t *pointer, int x, int y)
{
    lv_obj_update_layout(lv_screen_active());
    s_touch_point = (lv_point_t){x, y};
    s_touch_state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(pointer);
    lv_tick_inc(50);
    s_touch_state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(pointer);
}

static void tap_at(lv_indev_t *pointer, int x, int y)
{
    tap_raw(pointer, x * SCREEN_WIDTH / 800, y * DASH_HEIGHT / 480);
}

static void tap_service(lv_indev_t *pointer, unsigned id)
{
    lv_obj_update_layout(lv_screen_active());
    lv_area_t bounds;
    lv_obj_get_coords(lv_obj_get_child(dashboard_service_toolbar(), id), &bounds);
    tap_raw(pointer, (bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2);
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
    lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    static uint8_t buffer[SCREEN_WIDTH * 48 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(pointer, display);
    lv_indev_set_read_cb(pointer, read_pointer);
    lv_obj_t *root = lv_screen_active();
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root, SCREEN_WIDTH, SCREEN_HEIGHT);
    dashboard_create_on(lv_obj_create(root), SCREEN_WIDTH, DASH_HEIGHT);
    service_panel_create(root, dashboard_service_toolbar(), panel_action);
    const char *details[] = {"热点已连接", "语音待命", "摄像头已关闭", "正在记录车况", "上传已关闭", "尚未配置升级密钥"};
    for (int i = 0; i < 6; ++i) service_panel_status(i, i == 0 || i == 3 ? 3 : 0, details[i]);
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
    tap_service(pointer,1);
    assert(s_actions[PANEL_VOICE_DOWN] == 0 && s_actions[PANEL_VOICE_UP] == 0);
    tap_at(pointer,548,207);
    assert(dashboard_demo_enabled());
    assert(count_text(screen,TXT_DEMO) == 1);
    dashboard_update(&stopped);
    assert(count_text(screen,"86") == 1);
    for (int day = 0; day < 2; ++day) {
        dashboard_set_daylight(day);
        lv_obj_t *network_symbol = lv_obj_get_child(lv_obj_get_child(dashboard_service_toolbar(), 0), 0);
        assert(lv_color_eq(lv_obj_get_style_text_color(network_symbol, 0), lv_color_hex(day ? 0x007E6D : 0x61EDBF)));
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
    lv_obj_t *voice_icon = lv_obj_get_child(dashboard_service_toolbar(), 1);
    lv_obj_send_event(voice_icon, LV_EVENT_HOVER_OVER, NULL);
    assert(count_text(root,"小智语音") == 1);
    snapshot(display,argv[1],"service-hover",0);
    lv_obj_send_event(voice_icon, LV_EVENT_HOVER_LEAVE, NULL);
    assert(count_text(root,"小智语音") == 0);
    tap_service(pointer,1);
    assert(s_actions[PANEL_VOICE_DOWN] == 1 && s_actions[PANEL_VOICE_UP] == 1);
    lv_obj_send_event(voice_icon, LV_EVENT_PRESSED, NULL);
    lv_obj_send_event(voice_icon, LV_EVENT_PRESS_LOST, NULL);
    assert(s_actions[PANEL_VOICE_DOWN] == 2 && s_actions[PANEL_VOICE_UP] == 2);
    tap_service(pointer,2);
    assert(s_actions[PANEL_CAMERA_ON] == 1);
    snapshot(display,argv[1],"camera-waiting",0);
    static uint16_t camera[320*240];
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x)
        camera[y*320+x] = x < 106 ? 0xf800 : x < 213 ? 0x07e0 : 0x001f;
    service_panel_camera_frame(camera);
    snapshot(display,argv[1],"camera-test-pattern",0);
    assert(count_text(root,"等待画面") == 0);
    tap_raw(pointer,660,160);
    assert(s_actions[PANEL_CAMERA_OFF] == 1);
    tap_service(pointer,0);
    snapshot(display,argv[1],"device-center",0);
    tap_service(pointer,1);
    assert(s_actions[PANEL_VOICE_DOWN] == 2 && s_actions[PANEL_VOICE_UP] == 2);
    tap_raw(pointer,600,476);
    assert(s_actions[PANEL_OTA_INSTALL] == 0);
    snapshot(display,argv[1],"update-confirm",0);
    tap_raw(pointer,320,392);
    assert(s_actions[PANEL_OTA_INSTALL] == 1);
    tap_raw(pointer,760,180);
    tap_service(pointer,3);
    assert(s_actions[PANEL_SEAL] == 1);
    tap_raw(pointer,818,106);
    tap_service(pointer,4);
    tap_raw(pointer,400,407);
    assert(s_actions[PANEL_UPLOAD_ON] == 1);
    tap_raw(pointer,400,407);
    assert(s_actions[PANEL_UPLOAD_OFF] == 1);
    tap_raw(pointer,240,478);
    assert(s_actions[PANEL_UPLOAD_NOW] == 1);
    tap_raw(pointer,818,106);
    tap_service(pointer,5);
    tap_raw(pointer,410,478);
    assert(s_actions[PANEL_OTA_CHECK] == 1);
    printf("Inspected %d rendered labels; %d errors. Dashboard and P4 service controls passed.\n", s_labels, s_errors);
    return s_errors ? 1 : 0;
}
