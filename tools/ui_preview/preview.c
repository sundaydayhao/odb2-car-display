#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "dashboard.h"
#include "feature_config.h"
#include "service_panel.h"
#include "media_panel.h"
#include "dashboard_text.h"
#include "fonts/dashboard_fonts.h"
#include "src/libs/lodepng/lodepng.h"
#include "src/misc/lv_text_private.h"

static int s_errors;
static int s_labels;
static lv_point_t s_touch_point;
static lv_indev_state_t s_touch_state;
enum { SCREEN_WIDTH = 1024, SCREEN_HEIGHT = 600, DASH_HEIGHT = SCREEN_HEIGHT };
static unsigned s_actions[PANEL_ACTION_COUNT];
static panel_request_t s_last_request;
static bool s_accept = true;
static media_request_t s_last_media;
static unsigned s_media_actions[MEDIA_ACTION_COUNT];
static av_status_t s_av;
static unsigned s_av_actions[AV_VOICE_END + 1], s_av_value;
static bool av_action(av_action_t action, unsigned value, uint32_t revision) {
    if (!s_accept) return false;
    assert(revision == s_av.library.revision);
    ++s_av_actions[action]; s_av_value=value; return true;
}
static size_t av_files(av_file_kind_t kind,unsigned first,av_file_t *files,size_t capacity,uint32_t revision) {
    if (revision!=s_av.library.revision || !s_av.library.storage) return 0;
    unsigned count=kind==AV_FILES_MUSIC?s_av.library.music_count:s_av.library.photo_count;
    size_t n=0;
    while (n<capacity && first+n<count) {
        snprintf(files[n].name,sizeof(files[n].name),kind==AV_FILES_MUSIC?"Music-%02u.mp3":"img-%02u.bmp",(unsigned)(first+n)); ++n;
    }
    return n;
}
static bool media_action(const media_request_t *request) {
    if (!s_accept) return false;
    s_last_media = *request; ++s_media_actions[request->action]; return true;
}
static bool panel_action(const panel_request_t *request) {
    if (!s_accept) return false;
    assert(request->action < PANEL_ACTION_COUNT);
    ++s_actions[request->action]; s_last_request = *request;
    return true;
}

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
        if (codepoint=='\n' || codepoint=='\r') continue;
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
    bool scroll_child = false;
    for (lv_obj_t *parent = lv_obj_get_parent(obj); parent; parent = lv_obj_get_parent(parent))
        if (lv_obj_has_flag(parent, LV_OBJ_FLAG_SCROLLABLE)) scroll_child = true;
    if (bounds.x1 < 0 || bounds.x2 >= SCREEN_WIDTH ||
        (!scroll_child && (bounds.y1 < 0 || bounds.y2 >= SCREEN_HEIGHT))) {
        fprintf(stderr, "Object outside screen: %ld,%ld..%ld,%ld\n",
                (long)bounds.x1, (long)bounds.y1, (long)bounds.x2, (long)bounds.y2);
        ++s_errors;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        ++s_labels;
        const char *text = lv_label_get_text(obj);
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        check_glyphs(text, font);
        if (font != &dashboard_wifi_18 && (strstr(text, "OBD") || strstr(text, "CAN") || strstr(text, "ESP32"))) {
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
    /* Let state transitions finish before inspecting their visible appearance. */
    lv_tick_inc(200);
    lv_timer_handler();
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_update_layout(screen);
    lv_obj_t *toolbar = dashboard_service_toolbar();
    assert(lv_obj_get_child_count(toolbar) == 8);
    if (!DEVICE_CAMERA_AVAILABLE) assert(lv_obj_has_flag(lv_obj_get_child(toolbar,2),LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(toolbar, LV_OBJ_FLAG_CLICKABLE));
    int visible=0, left=SCREEN_WIDTH, right=0;
    for (int i = 0; i < 8; ++i) {
        lv_obj_t *symbol = lv_obj_get_child(toolbar, i);
        assert(lv_obj_check_type(symbol, i == 2 ? &lv_obj_class : &lv_label_class));
        assert(!lv_obj_has_flag(symbol, LV_OBJ_FLAG_CLICKABLE));
        if (!lv_obj_has_flag(symbol,LV_OBJ_FLAG_HIDDEN)) {
            lv_area_t bounds; lv_obj_get_coords(symbol,&bounds);
            if (bounds.x1<left) left=bounds.x1;
            if (bounds.x2>right) right=bounds.x2;
            ++visible;
        }
    }
    lv_area_t toolbar_bounds; lv_obj_get_coords(toolbar,&toolbar_bounds);
    assert(right==toolbar_bounds.x2);
    assert(right-left+1==visible*(SCREEN_WIDTH*32/800));
    assert(left>SCREEN_WIDTH/2);
    lv_obj_t *settings = lv_obj_get_child(screen, 1);
    if (!lv_obj_has_flag(settings, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_t *sidebar = lv_obj_get_child(settings, 0);
        lv_obj_t *body = lv_obj_get_child(settings, 2);
        lv_area_t side_bounds, body_bounds;
        lv_obj_get_coords(sidebar, &side_bounds); lv_obj_get_coords(body, &body_bounds);
        assert(lv_obj_get_width(sidebar) == 272);
        assert(lv_obj_get_width(body) <= 680);
        assert(body_bounds.x1 >= side_bounds.x2 + 24);
        assert(body_bounds.x2 < SCREEN_WIDTH);
        assert(lv_obj_get_child_count(sidebar) == 8+DEVICE_CAMERA_AVAILABLE);
        if (!DEVICE_CAMERA_AVAILABLE) assert(!count_text(sidebar,"摄像头"));
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

static lv_obj_t *find_label(lv_obj_t *parent,const char *value)
{
    if (lv_obj_has_flag(parent,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(parent,&lv_label_class) && !strcmp(lv_label_get_text(parent),value)) return parent;
    for (uint32_t i=0;i<lv_obj_get_child_count(parent);++i) {
        lv_obj_t *found=find_label(lv_obj_get_child(parent,i),value);
        if (found) return found;
    }
    return NULL;
}
static void tap_category(lv_indev_t *pointer,const char *value)
{
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t *label=find_label(lv_screen_active(),value);
    if (!label) fprintf(stderr,"Tap target missing: %s\n",value);
    assert(label);
    lv_area_t bounds; lv_obj_get_coords(label,&bounds);
    tap_raw(pointer,(bounds.x1+bounds.x2)/2,(bounds.y1+bounds.y2)/2);
}

static lv_obj_t *find_class(lv_obj_t *parent, const lv_obj_class_t *type)
{
    if (lv_obj_check_type(parent, type)) return parent;
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t *found = find_class(lv_obj_get_child(parent, i), type);
        if (found) return found;
    }
    return NULL;
}

static void tap_toggle(lv_indev_t *pointer, const char *title)
{
    lv_obj_t *label = find_label(lv_screen_active(), title); assert(label);
    lv_obj_t *control = find_class(lv_obj_get_parent(label), &lv_switch_class); assert(control);
    lv_obj_update_layout(control);
    lv_area_t bounds; lv_obj_get_coords(control, &bounds);
    tap_raw(pointer, (bounds.x1+bounds.x2)/2, (bounds.y1+bounds.y2)/2);
}

static lv_obj_t *network_list(void)
{
    lv_obj_t *label = find_label(lv_screen_active(), "附近的 Wi-Fi"); assert(label);
    lv_obj_t *body = lv_obj_get_parent(label);
    for (unsigned i = 0; i < lv_obj_get_child_count(body); ++i) {
        lv_obj_t *child = lv_obj_get_child(body, i);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_SCROLLABLE)) return child;
    }
    assert(false); return NULL;
}
static void tap_network(lv_indev_t *pointer, const char *ssid)
{
    lv_obj_t *list = network_list();
    lv_obj_update_layout(list);
    lv_obj_t *label = find_label(list, ssid);
    if (!label) {
        fprintf(stderr, "Missing scanned SSID %s in list with %u children\n", ssid, (unsigned)lv_obj_get_child_count(list));
        for (unsigned i=0;i<lv_obj_get_child_count(list);++i) {
            lv_obj_t *row=lv_obj_get_child(list,i);
            lv_obj_t *name=lv_obj_get_child(row,1);
            if (name && lv_obj_check_type(name,&lv_label_class)) fprintf(stderr,"row %u: [%s]\n",i,lv_label_get_text(name));
        }
    }
    assert(label);
    lv_obj_scroll_to_view(lv_obj_get_parent(label), LV_ANIM_OFF);
    lv_obj_update_layout(list);
    lv_area_t bounds; lv_obj_get_coords(label, &bounds);
    tap_raw(pointer, (bounds.x1+bounds.x2)/2, (bounds.y1+bounds.y2)/2);
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
    const int nav_start = DASHBOARD_PAGE_COUNT > 6 ? 7 : 22;
    const int nav_step = DASHBOARD_PAGE_COUNT > 6 ? 113 : 126;
    const int nav_width = DASHBOARD_PAGE_COUNT > 6 ? 110 : 122;
    for (int page = 0; page < DASHBOARD_PAGE_COUNT; ++page) {
        lv_obj_update_layout(lv_display_get_screen_active(display));
        tap_at(pointer, nav_start + page * nav_step + nav_width / 2, 450);
        assert(dashboard_active_page() == page);
        snapshot(display, dir, state, page);
    }
}

static double luminance(uint32_t color)
{
    double channels[3];
    for (int i = 0; i < 3; ++i) {
        double value = ((color >> (16 - 8 * i)) & 255) / 255.0;
        channels[i] = value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

static void check_light_contrast(void)
{
    for (unsigned i = 0; i < DASHBOARD_THEME_COUNT; ++i) {
        const dashboard_palette_t *p = dashboard_palette(i, true);
        const uint32_t foregrounds[] = {p->ink, p->muted, p->accent, p->hero, p->secondary, p->green, p->amber, p->red};
        const uint32_t backgrounds[] = {p->bg, p->panel, p->tint};
        for (unsigned f = 0; f < sizeof(foregrounds)/sizeof(*foregrounds); ++f) {
            for (unsigned b = 0; b < sizeof(backgrounds)/sizeof(*backgrounds); ++b) {
                double contrast = (luminance(backgrounds[b]) + 0.05) / (luminance(foregrounds[f]) + 0.05);
                if (contrast < 4.5) { fprintf(stderr,"Light theme %u contrast %.2f below 4.5 (text %06x / background %06x)\n",i,contrast,foregrounds[f],backgrounds[b]); ++s_errors; }
            }
        }
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    check_light_contrast();
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
    media_panel_handlers(media_action);
    media_panel_av_handlers(av_action,av_files);
    panel_preferences_t defaults = PANEL_PREFERENCES_DEFAULT;
    assert(sizeof(defaults) == 6 && service_panel_preferences_valid(&defaults));
    service_panel_preferences(&defaults);
    assert(dashboard_daylight_enabled() && dashboard_active_theme() == 0);
    const char *details[] = {"热点已连接", "语音待命", "摄像头已关闭", "正在记录车况", "上传已关闭", "尚未配置升级密钥"};
    for (int i = 0; i < 6; ++i) service_panel_status(i, i == 0 || i == 3 ? 3 : 0, details[i]);
    service_panel_bluetooth(true,true,2,"正在查找适配器","");
    service_panel_gps(false,0,"GPS 未接入");
    lv_obj_t *gps_symbol = lv_obj_get_child(dashboard_service_toolbar(), 7);
    assert(gps_symbol && !lv_obj_has_flag(gps_symbol, LV_OBJ_FLAG_HIDDEN));
    lv_color_t gps_off = lv_obj_get_style_text_color(gps_symbol, 0);
    service_panel_gps(true,3,"GPS 已定位");
    assert(!lv_color_eq(gps_off, lv_obj_get_style_text_color(gps_symbol, 0)));
    service_panel_gps(false,0,"GPS 未接入");
    dashboard_data_t link_state={.link_state=DASH_LINK_SCANNING};
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_SCANNING)==0);
    assert(count_text(lv_screen_active(),LV_SYMBOL_BLUETOOTH)==1);
    snapshot(display,argv[1],"ble-scanning",0);
    link_state.link_state=DASH_LINK_CONNECTING;
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_CONNECTING)==0);
    snapshot(display,argv[1],"ble-connecting",0);
    link_state.link_state=DASH_LINK_INITIALIZING;
    dashboard_update(&link_state);
    assert(count_text(lv_screen_active(),TXT_INITIALIZING)==0);
    snapshot(display,argv[1],"ble-initializing",0);
    link_state.link_state=DASH_LINK_WAITING;
    dashboard_update(&link_state);
    inspect_all(display, pointer, argv[1], "waiting");
    assert(find_label(root, TXT_MAP_TRACK));
    assert(find_label(root, TXT_MAP_TRACK_WAIT));
    assert(find_label(root, TXT_MAP_TRACK_DATA_WAIT));
    assert(!find_label(root, TXT_DHX30));
    snapshot(display, argv[1], "map-track-placeholder", 0);

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

    dashboard_select_page(4);
    uint8_t selected = dashboard_active_page();
    lv_tick_inc(61000);
    lv_timer_handler();
    assert(dashboard_active_page() == selected);
    lv_obj_t *screen = lv_display_get_screen_active(display);
    assert(count_text(screen, TXT_WAITING) == 0);
    assert(count_text(screen, "--") >= 6);
    inspect_all(display, pointer, argv[1], "expired");
    dashboard_data_t stopped = {.value = {0}, .valid = {[DASH_SPEED] = true}};
    dashboard_update(&stopped);
    dashboard_select_page(0);
    assert(find_label(screen, TXT_DHX30));
    assert(find_label(screen, TXT_DHX30_TEMPERATURE));
    assert(find_label(screen, TXT_DHX30_HUMIDITY));
    assert(count_text(screen, "0") == 1);
    assert(count_text(screen, "--") >= 6);
    assert(count_text(screen, TXT_LIVE) == 0);
    /* All settings are reached through the single gear. Status taps do nothing. */
    for (unsigned i = 0; i < 8; ++i) {
        if (!lv_obj_has_flag(lv_obj_get_child(dashboard_service_toolbar(),i),LV_OBJ_FLAG_HIDDEN)) tap_service(pointer,i);
    }
    for (unsigned i = 0; i < PANEL_ACTION_COUNT; ++i) assert(s_actions[i] == 0);
    assert(count_text(screen, LV_SYMBOL_SETTINGS) == 1);
    assert(count_text(screen, LV_SYMBOL_TINT) == 0);
    tap_at(pointer,760,28);
    snapshot(display,argv[1],"settings-home",0);
    tap_category(pointer,"蓝牙设置");
    service_panel_bluetooth(true,true,1,"等待选择设备连接","");
    ble_devices_t bluetooth={.revision=1};
    service_panel_bluetooth_devices(&bluetooth);
    snapshot(display,argv[1],"settings-bluetooth-idle",0);
    tap_toggle(pointer,"蓝牙车辆连接"); assert(s_actions[PANEL_BLUETOOTH_OFF]==1);
    service_panel_bluetooth(true,false,0,"蓝牙已关闭","");
    snapshot(display,argv[1],"settings-bluetooth-off",0);
    tap_category(pointer,"扫描设备"); assert(s_actions[PANEL_BLUETOOTH_SCAN]==0);
    s_accept=false; tap_toggle(pointer,"蓝牙车辆连接"); assert(s_actions[PANEL_BLUETOOTH_ON]==0);
    s_accept=true; tap_toggle(pointer,"蓝牙车辆连接"); assert(s_actions[PANEL_BLUETOOTH_ON]==1);
    service_panel_bluetooth(true,true,1,"等待选择设备连接","");
    tap_category(pointer,"扫描设备"); assert(s_actions[PANEL_BLUETOOTH_SCAN]==1);
    bluetooth.scanning=true; bluetooth.revision=2; bluetooth.count=2;
    bluetooth.items[0]=(ble_device_t){.type=0,.address={1,2,3,4,5,6},.name="OBD BLE",.rssi=-40};
    bluetooth.items[1]=(ble_device_t){.type=1,.address={7,8,9,10,11,12},.name="OBD BLE",.rssi=-55};
    service_panel_bluetooth_devices(&bluetooth);
    service_panel_bluetooth(true,true,2,"正在查找适配器","");
    snapshot(display,argv[1],"settings-bluetooth-scanning",0);
    tap_category(pointer,"扫描设备"); assert(s_actions[PANEL_BLUETOOTH_SCAN]==1);
    assert(s_actions[PANEL_BLUETOOTH_CONNECT]==0); /* Merely discovering devices never connects. */
    tap_category(pointer,"0C:0B:0A:09:08:07");
    assert(s_actions[PANEL_BLUETOOTH_CONNECT]==1);
    assert(ble_device_same(&s_last_request.data.bluetooth,&bluetooth.items[1]));
    bluetooth.scanning=false; bluetooth.has_saved=true; bluetooth.saved=bluetooth.items[1]; ++bluetooth.revision;
    service_panel_bluetooth_devices(&bluetooth);
    service_panel_bluetooth(true,true,1,"等待选择设备连接","");
    snapshot(display,argv[1],"settings-bluetooth-saved",0);
    tap_category(pointer,"连接"); assert(s_actions[PANEL_BLUETOOTH_RECONNECT]==1);
    service_panel_bluetooth(true,true,3,"已连接适配器","AA:BB:CC:DD:EE:FF");
    snapshot(display,argv[1],"settings-bluetooth-connected",0);
    tap_category(pointer,"断开连接"); assert(s_actions[PANEL_BLUETOOTH_DISCONNECT]==1);
    tap_category(pointer,"忘记"); assert(s_actions[PANEL_BLUETOOTH_FORGET]==1);
    service_panel_bluetooth(false,true,1,"蓝牙尚未就绪","");
    tap_toggle(pointer,"蓝牙车辆连接"); assert(s_actions[PANEL_BLUETOOTH_OFF]==1);
    tap_category(pointer,"0C:0B:0A:09:08:07"); assert(s_actions[PANEL_BLUETOOTH_CONNECT]==1);
    service_panel_bluetooth(true,true,2,"正在查找适配器","");

    tap_category(pointer,"显示与外观"); /* Display category. */
    snapshot(display,argv[1],"settings-display",0);
    tap_category(pointer,"界面主题");
    snapshot(display,argv[1],"settings-themes",0);
    s_accept=false; tap_category(pointer,TXT_LAVA);
    assert(dashboard_daylight_enabled() && dashboard_active_theme()==0);
    assert(count_text(screen,LV_SYMBOL_OK)==1); /* Rejection retains selected appearance. */
    s_accept=true;
    tap_category(pointer,LV_SYMBOL_LEFT); /* Theme page goes back to display, not the home menu. */
    assert(find_label(screen,"屏幕亮度"));
    tap_toggle(pointer,"演示模式（重启关闭）");
    assert(dashboard_demo_enabled());
    dashboard_update(&stopped);
    assert(count_text(screen,"86") == 1);
    for (int day = 0; day < 2; ++day) {
        for (int theme = 0; theme < DASHBOARD_THEME_COUNT; ++theme) {
            tap_category(pointer,"界面主题");
            const char *names[2][3]={{TXT_NEON,TXT_LAVA,TXT_ACID},{"清爽白","晴空蓝","暖沙米"}};
            tap_category(pointer,names[day][theme]);
            assert(dashboard_active_theme() == theme);
            assert(dashboard_daylight_enabled() == (bool)day);
            assert(s_last_request.action == PANEL_PREFERENCES_SAVE);
            assert(s_last_request.data.preferences.theme == theme);
            assert(s_last_request.data.preferences.daylight == day);
            assert(s_last_request.data.preferences.brightness == defaults.brightness);
            assert(count_text(screen,LV_SYMBOL_OK)==1);
            panel_preferences_t saved = s_last_request.data.preferences;
            assert(service_panel_preferences_valid(&saved));
            /* Exercise the same v1 restore entry point used by ui.c after NVS load. */
            service_panel_preferences(&defaults);
            service_panel_preferences(&saved);
            assert(dashboard_active_theme()==theme && dashboard_daylight_enabled()==(bool)day);
            const dashboard_palette_t *palette=dashboard_palette(theme,day);
            lv_obj_t *overlay=lv_obj_get_child(screen,1);
            assert(lv_color_eq(lv_obj_get_style_bg_color(overlay,0),lv_color_hex(palette->bg)));
            assert(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_child(overlay,2),0),lv_color_hex(palette->bg)));
            assert(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_child(overlay,0),0),lv_color_hex(palette->panel)));
            char settings_name[40]; snprintf(settings_name,sizeof(settings_name),"themes-%d-%d",day,theme);
            snapshot(display,argv[1],settings_name,0);
            tap_category(pointer,LV_SYMBOL_CLOSE);
            char name[32]; snprintf(name,sizeof(name),"demo-%d-%d",day,theme);
            inspect_all(display,pointer,argv[1],name);
            tap_at(pointer,760,28); tap_category(pointer,"显示与外观");
        }
    }
    tap_category(pointer,LV_SYMBOL_CLOSE);
    dashboard_select_page(3); tap_at(pointer,177,85);
    assert(count_text(screen,"82.4") == 1); snapshot(display,argv[1],"today",3);
    dashboard_select_page(2); tap_at(pointer,760,28); tap_category(pointer,"显示与外观");
    tap_category(pointer,"kPa"); assert(dashboard_pressure_unit()==1);
    tap_category(pointer,LV_SYMBOL_CLOSE); assert(count_text(screen,"240") == 2); snapshot(display,argv[1],"kpa",2);
    tap_at(pointer,760,28); tap_category(pointer,"显示与外观"); tap_category(pointer,"psi");
    tap_category(pointer,LV_SYMBOL_CLOSE); assert(count_text(screen,"34.8") == 2); snapshot(display,argv[1],"psi",2);
    dashboard_select_page(4); tap_at(pointer,760,28); tap_category(pointer,"显示与外观");
    tap_toggle(pointer,"涡轮车型显示"); assert(!dashboard_turbo_enabled());
    tap_toggle(pointer,"演示模式（重启关闭）"); assert(!dashboard_demo_enabled());
    tap_category(pointer,LV_SYMBOL_CLOSE);
    snapshot(display,argv[1],"naturally-aspirated",4);
    /* Scan results are fixtures; the real scanner and connection run on the board. */
    service_panel_network_config("PhoneHotspot","testpass123");
    service_panel_status(0,1,"未找到已保存的热点");
    tap_at(pointer,760,28); tap_category(pointer,"网络与连接");
    assert(s_actions[PANEL_WIFI_SCAN] == 1);
    snapshot(display,argv[1],"network-scanning",0);
    wifi_networks_t networks = {.revision=1,.state=NETWORK_SCAN_READY};
    uint8_t name[33]="PhoneHotspot";
    wifi_networks_add(&networks,name,-35,NETWORK_WPA2);
    snprintf((char *)name,sizeof(name),"我的手机热点"); wifi_networks_add(&networks,name,-48,NETWORK_WPA3);
    snprintf((char *)name,sizeof(name),"Guest WiFi"); wifi_networks_add(&networks,name,-60,NETWORK_OPEN);
    snprintf((char *)name,sizeof(name),"Office Secure"); wifi_networks_add(&networks,name,-68,NETWORK_UNSUPPORTED);
    for (int i=0;i<12;++i) { snprintf((char *)name,sizeof(name),"Nearby-%02d",i); wifi_networks_add(&networks,name,-70-i,NETWORK_WPA2); }
    snprintf((char *)name,sizeof(name),"PhoneHotspot");
    wifi_networks_add(&networks,name,-83,NETWORK_OPEN); /* Same name, different security is not the connected AP. */
    service_panel_network_scan(&networks);
    assert(!find_class(lv_obj_get_child(screen,1),&lv_textarea_class));
    assert(count_text(screen,"热点名称")==0);
    snapshot(display,argv[1],"settings-network",0);
    lv_obj_scroll_to_y(network_list(),10000,LV_ANIM_OFF);
    snapshot(display,argv[1],"network-list-bottom",0);
    lv_obj_scroll_to_y(network_list(),0,LV_ANIM_OFF);
    s_accept=false; tap_category(pointer,"重新扫描"); assert(s_actions[PANEL_WIFI_SCAN]==1);
    s_accept=true; tap_category(pointer,"重新扫描"); assert(s_actions[PANEL_WIFI_SCAN]==2);
    networks.revision++; service_panel_network_scan(&networks);
    tap_network(pointer,"Office Secure"); assert(!find_class(lv_obj_get_child(screen,1),&lv_textarea_class));
    tap_network(pointer,"PhoneHotspot");
    lv_obj_t *settings_overlay=lv_obj_get_child(screen,1);
    lv_obj_t *password=find_class(settings_overlay,&lv_textarea_class);
    lv_obj_t *keyboard=find_class(settings_overlay,&lv_keyboard_class);
    assert(password && keyboard && lv_textarea_get_password_mode(password));
    assert(!strcmp(lv_textarea_get_text(password),"testpass123"));
    snapshot(display,argv[1],"network-password",0);
    lv_area_t field_bounds; lv_obj_get_coords(password,&field_bounds);
    tap_raw(pointer,(field_bounds.x1+field_bounds.x2)/2,(field_bounds.y1+field_bounds.y2)/2);
    snapshot(display,argv[1],"network-keyboard",0);
    lv_textarea_set_text(password,"short");
    networks.revision++; service_panel_network_scan(&networks);
    assert(!strcmp(lv_textarea_get_text(password),"short")); /* Scan update preserves input. */
    lv_obj_send_event(keyboard,LV_EVENT_READY,NULL);
    tap_category(pointer,"连接"); assert(s_actions[PANEL_WIFI_SAVE]==0);
    snapshot(display,argv[1],"network-validation",0);
    lv_textarea_set_text(password,"testpass123");
    s_accept=false; tap_category(pointer,"连接"); assert(s_actions[PANEL_WIFI_SAVE]==0);
    s_accept=true; tap_category(pointer,"连接"); assert(s_actions[PANEL_WIFI_SAVE]==1);
    assert(!strcmp(s_last_request.data.wifi.ssid,"PhoneHotspot"));
    assert(!strcmp(s_last_request.data.wifi.password,"testpass123"));
    service_panel_save_result(true);
    networks.revision++; networks.connected=true;
    networks.connected_security=NETWORK_WPA2;
    service_panel_status(0,3,"已连接 Wi-Fi");
    snprintf(networks.connected_ssid,sizeof(networks.connected_ssid),"PhoneHotspot");
    service_panel_network_scan(&networks);
    assert(count_text(screen,"已连接")==1);
    snapshot(display,argv[1],"network-connected",0);
    tap_category(pointer,"忘记"); assert(s_actions[PANEL_WIFI_SAVE]==2);
    assert(!*s_last_request.data.wifi.ssid && !*s_last_request.data.wifi.password);
    service_panel_network_config("","");
    service_panel_status(0,0,"请选择附近的 Wi-Fi");
    networks.revision++; networks.connected=false; networks.connected_ssid[0]=0;
    service_panel_network_scan(&networks);
    tap_network(pointer,"Guest WiFi");
    assert(!find_class(settings_overlay,&lv_textarea_class));
    snapshot(display,argv[1],"network-open",0);
    tap_category(pointer,"连接"); assert(s_actions[PANEL_WIFI_SAVE]==3);
    assert(!strcmp(s_last_request.data.wifi.ssid,"Guest WiFi") && !*s_last_request.data.wifi.password);
    tap_network(pointer,"我的手机热点");
    password=find_class(settings_overlay,&lv_textarea_class);
    assert(password && !*lv_textarea_get_text(password));
    lv_obj_get_coords(password,&field_bounds);
    tap_raw(pointer,(field_bounds.x1+field_bounds.x2)/2,(field_bounds.y1+field_bounds.y2)/2);
    tap_category(pointer,"蓝牙设置");
    assert(!find_class(settings_overlay,&lv_keyboard_class));
    tap_category(pointer,"网络与连接");
    wifi_networks_t empty={.revision=networks.revision+1,.state=NETWORK_SCAN_READY};
    snprintf(empty.message,sizeof(empty.message),"未发现热点，请开启手机的 2.4 GHz 热点");
    service_panel_network_scan(&empty);
    snapshot(display,argv[1],"network-empty",0);
    empty.revision++; empty.state=NETWORK_SCAN_ERROR;
    snprintf(empty.message,sizeof(empty.message),"扫描失败，请稍后重新扫描");
    service_panel_network_scan(&empty);
    snapshot(display,argv[1],"network-error",0);
    service_panel_status(0,3,"已连接 Wi-Fi");
    service_panel_status(1,1,"等待唤醒");
    service_panel_voice(true,false);
    tap_category(pointer,"语音助手");
    snapshot(display,argv[1],"settings-voice",0);
    assert(find_label(screen,"唤醒词：你好小智"));
    assert(!find_label(screen,"按住说话"));
    assert(find_class(screen,&lv_image_class));
    static uint16_t camera[AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT];
    for (int y=0;y<AV_PREVIEW_HEIGHT;++y) for (int x=0;x<AV_PREVIEW_WIDTH;++x) camera[y*AV_PREVIEW_WIDTH+x]=x<160?0xf800:x<320?0x07e0:0x001f;
    if (DEVICE_CAMERA_AVAILABLE) {
    tap_category(pointer,"摄像头");
    snapshot(display,argv[1],"settings-camera",0);
    tap_category(pointer,"打开本地预览"); assert(s_actions[PANEL_CAMERA_ON]==1);
    snapshot(display,argv[1],"camera-waiting",0);
    service_panel_camera_frame(camera); snapshot(display,argv[1],"camera-test-pattern",0);
    tap_raw(pointer,738,144); assert(s_actions[PANEL_CAMERA_OFF]==1);
    } else {
        assert(!find_label(screen,"摄像头"));
        service_panel_camera_visible(true); service_panel_camera_frame(camera); service_panel_camera_clear();
        assert(!find_label(screen,"本地预览"));
    }
     tap_category(pointer,"记录与同步");
    snapshot(display,argv[1],"settings-record",0);
    tap_toggle(pointer,"允许上传车辆记录"); assert(s_actions[PANEL_UPLOAD_ON]==1);
    tap_toggle(pointer,"允许上传车辆记录"); assert(s_actions[PANEL_UPLOAD_OFF]==1);
    tap_category(pointer,"保存当前记录"); assert(s_actions[PANEL_SEAL]==1);
    tap_category(pointer,"立即同步"); assert(s_actions[PANEL_UPLOAD_NOW]==1);
     tap_category(pointer,"系统更新");
    snapshot(display,argv[1],"settings-update",0);
    tap_category(pointer,"检查更新"); assert(s_actions[PANEL_OTA_CHECK]==1);
    tap_category(pointer,"安装更新"); assert(s_actions[PANEL_OTA_INSTALL]==0);
    snapshot(display,argv[1],"update-confirm",0);
    tap_category(pointer,"确认安装"); assert(s_actions[PANEL_OTA_INSTALL]==1);
    tap_category(pointer,LV_SYMBOL_CLOSE);
    for (int i=0;i<100;++i) {
        static const char *categories[]={"网络与连接","蓝牙设置","显示与外观","语音助手","摄像头","记录与同步","系统更新"};
        tap_at(pointer,760,28); tap_category(pointer,!DEVICE_CAMERA_AVAILABLE && i%7==4?"记录与同步":categories[i%7]);
        service_panel_status(i%6,0,"等待车辆");
        tap_category(pointer,LV_SYMBOL_CLOSE);
        dashboard_select_theme(i%DASHBOARD_THEME_COUNT); dashboard_select_page(i%DASHBOARD_PAGE_COUNT);
    }
    service_panel_bluetooth(true,true,2,"正在查找适配器","");
    dashboard_set_daylight(false); dashboard_select_theme(0); dashboard_select_page(0);
    snapshot(display,argv[1],"final-dashboard",0);
    for (unsigned i=0;i<6;++i) service_panel_status(i,2,"正在运行");
    snapshot(display,argv[1],"all-status-icons",0);
    dashboard_set_daylight(true);
    snapshot(display,argv[1],"all-status-icons-day",0);
    for (unsigned day = 0; day < 2; ++day) {
        dashboard_set_daylight(day);
        lv_obj_t *sd = lv_obj_get_child(dashboard_service_toolbar(), 3);
        service_panel_status(3,0,"未检测到存储卡");
        lv_color_t absent = lv_obj_get_style_text_color(sd,0);
        assert(!lv_obj_has_flag(sd,LV_OBJ_FLAG_HIDDEN));
        snapshot(display,argv[1],day ? "sd-absent-light" : "sd-absent-dark",0);
        service_panel_status(3,3,"存储卡已就绪");
        lv_color_t ready = lv_obj_get_style_text_color(sd,0);
        assert(!lv_color_eq(absent,ready));
        snapshot(display,argv[1],day ? "sd-ready-light" : "sd-ready-dark",0);
        service_panel_status(3,4,"存储卡不可用");
        lv_color_t error = lv_obj_get_style_text_color(sd,0);
        assert(!lv_color_eq(error,ready) && !lv_color_eq(error,absent));
        snapshot(display,argv[1],day ? "sd-error-light" : "sd-error-dark",0);
        service_panel_status(3,0,"未检测到存储卡");
        assert(lv_color_eq(absent,lv_obj_get_style_text_color(sd,0)));
        assert(!lv_obj_has_flag(sd,LV_OBJ_FLAG_HIDDEN));
        service_panel_status(4,0,"上传已关闭");
        tap_at(pointer,760,28);
        tap_category(pointer,"记录与同步");
        snapshot(display,argv[1],day ? "sd-settings-absent-light" : "sd-settings-absent-dark",0);
        service_panel_status(3,3,"存储卡已就绪");
        snapshot(display,argv[1],day ? "sd-settings-ready-light" : "sd-settings-ready-dark",0);
        tap_category(pointer,LV_SYMBOL_CLOSE);
    }
    for (unsigned day=0;day<2;++day) {
        dashboard_set_daylight(day);
        for (unsigned theme=0;theme<DASHBOARD_THEME_COUNT;++theme) {
            dashboard_select_theme(theme); dashboard_select_page(DASHBOARD_PAGE_MEDIA); media_panel_select(AV_MUSIC);
            s_av=(av_status_t){.voice_enabled=true,.library={.revision=100+day*3+theme,.storage=true,.music_count=12}};
            media_panel_av_update(&s_av);
            media_status_t media={.network=false,.state=MEDIA_PLAYING,.selected=true,.volume=35,.played_ms=71000};
            strcpy(media.title,"Music-00.mp3"); strcpy(media.detail,"正在播放音乐"); media_panel_update(&media);
            assert(!find_label(root,"在线音乐") && !find_label(root,"网络电台"));
            assert(!find_label(root,"SD 卡已就绪") && !find_label(root,"未检测到 SD 卡"));
            assert(!find_label(root,"Wi-Fi 已连接") && !find_label(root,"Wi-Fi 未连接"));
            tap_category(pointer,LV_SYMBOL_PREV); assert(s_last_media.action==MEDIA_PREVIOUS);
            tap_category(pointer,LV_SYMBOL_NEXT); assert(s_last_media.action==MEDIA_NEXT);
            tap_category(pointer,LV_SYMBOL_STOP); assert(s_last_media.action==MEDIA_STOP);
            tap_category(pointer,LV_SYMBOL_PAUSE); assert(s_last_media.action==MEDIA_TOGGLE);
            snapshot(display,argv[1],day?"media-sd-playing-light":"media-sd-playing-dark",theme);
            media.state=MEDIA_PAUSED; strcpy(media.detail,"已暂停"); media_panel_update(&media);
            snapshot(display,argv[1],day?"media-sd-paused-light":"media-sd-paused-dark",theme);
            media.state=MEDIA_ERROR; strcpy(media.detail,"音乐读取失败，请重新选择"); media_panel_update(&media);
            snapshot(display,argv[1],day?"media-sd-error-light":"media-sd-error-dark",theme);
            s_accept=false; tap_category(pointer,LV_SYMBOL_PLAY);
            assert(count_text(root,"操作未提交，请稍后重试")==1); s_accept=true;
            unsigned before[MEDIA_ACTION_COUNT]; memcpy(before,s_media_actions,sizeof(before));
            tap_category(pointer,"FM 电台");
            media_panel_update(&media); ++s_av.library.revision; media_panel_av_update(&s_av);
            assert(!find_label(root,"播放器") && !find_class(root,&lv_slider_class));
            assert(!find_label(root,"拍照保存") && !find_label(root,"开始对话"));
            assert(!memcmp(before,s_media_actions,sizeof(before)));
            snapshot(display,argv[1],day?"av-fm-light":"av-fm-dark",theme);
            service_panel_open();
            assert(!find_label(root,"音乐播放地址") && !find_label(root,"电台播放地址") && !find_label(root,"多媒体"));
            snapshot(display,argv[1],day?"settings-sd-only-light":"settings-sd-only-dark",theme);
            tap_category(pointer,LV_SYMBOL_CLOSE);
        }
    }
    /* Actual LVGL input dispatch across the new center, including disabled controls and hotplug. */
    for (unsigned day=0;day<2;++day) {
        dashboard_set_daylight(day); dashboard_select_page(DASHBOARD_PAGE_MEDIA); media_panel_select(AV_MUSIC);
        media_status_t media={.network=true,.volume=35}; media_panel_update(&media);
        s_av=(av_status_t){.voice_enabled=true,.library={.revision=1,.storage=true,.music_count=12,.photo_count=7}};
        strcpy(s_av.voice_detail,"请说“你好小智”唤醒"); media_panel_av_update(&s_av);
        tap_category(pointer,"Music-00.mp3");
        assert(s_last_media.action==MEDIA_SELECT_LOCAL && s_last_media.value==0 && s_last_media.revision==1);
        tap_category(pointer,"下一页"); tap_category(pointer,"Music-05.mp3");
        assert(s_last_media.action==MEDIA_SELECT_LOCAL && s_last_media.value==5);
        snapshot(display,argv[1],day?"av-sd-light":"av-sd-dark",0);
        s_av.library.storage=false; s_av.library.music_count=0; ++s_av.library.revision; media_panel_av_update(&s_av);
        snapshot(display,argv[1],day?"av-sd-removed-light":"av-sd-removed-dark",0);
        assert(!find_label(root,"Music-05.mp3"));
        s_av.library.storage=true; s_av.library.music_count=12; ++s_av.library.revision; media_panel_av_update(&s_av);
        if (DEVICE_CAMERA_AVAILABLE) {
        tap_category(pointer,"摄像头");
        /* Previous iteration may have left the album selected. */
        tap_category(pointer,"实时预览");
        unsigned photos=s_av_actions[AV_TAKE_PHOTO];
        tap_category(pointer,"拍照保存"); assert(s_av_actions[AV_TAKE_PHOTO]==photos);
        tap_category(pointer,"开启摄像头"); assert(s_av_actions[AV_CAMERA_ON]>0);
        s_av.camera_enabled=true; s_av.camera_state=3; strcpy(s_av.camera_detail,"本地预览中");
        media_panel_av_update(&s_av); media_panel_frame(camera,false);
        tap_category(pointer,"拍照保存"); assert(s_av_actions[AV_TAKE_PHOTO]==photos+1);
        snapshot(display,argv[1],day?"av-camera-light":"av-camera-dark",0);
        tap_category(pointer,"相册");
        if (!find_label(root,"img-00.bmp")) tap_category(pointer,"上一页");
        tap_category(pointer,"img-00.bmp"); assert(s_av_actions[AV_VIEW_PHOTO]>0 && s_av_value==0);
        media_panel_frame(camera,true);
        snapshot(display,argv[1],day?"av-album-light":"av-album-dark",0);
        tap_category(pointer,"下一页"); tap_category(pointer,"img-04.bmp"); assert(s_av_value==4);
        } else {
            assert(!find_label(root,"摄像头"));
            lv_obj_update_layout(root);
            assert(!find_label(root,"SD 卡音乐"));
            media_panel_select(AV_CAMERA);
            assert(!find_label(root,"SD 卡音乐") && !find_label(root,"拍照保存"));
            snapshot(display,argv[1],day?"av-camera-disabled-light":"av-camera-disabled-dark",0);
        }
        tap_at(pointer,760,28);
        service_panel_status(0,3,"已连接 Wi-Fi");
        service_panel_status(1,1,"等待唤醒");
        service_panel_voice(true,false);
        tap_category(pointer,"语音助手");
        unsigned starts=s_actions[PANEL_VOICE_START],ends=s_actions[PANEL_VOICE_END];
        tap_category(pointer,"结束对话"); assert(s_actions[PANEL_VOICE_END]==ends);
        tap_category(pointer,"开始对话"); assert(s_actions[PANEL_VOICE_START]==starts+1);
        service_panel_voice(true,true);
        tap_category(pointer,"开始对话"); assert(s_actions[PANEL_VOICE_START]==starts+1);
        tap_category(pointer,"结束对话"); assert(s_actions[PANEL_VOICE_END]==ends+1);
        snapshot(display,argv[1],day?"settings-voice-light":"settings-voice-dark",0);
        tap_category(pointer,"关闭助手"); assert(s_actions[PANEL_VOICE_OFF]>0);
        service_panel_voice(false,false);
        tap_category(pointer,"开始对话"); assert(s_actions[PANEL_VOICE_START]==starts+1);
        snapshot(display,argv[1],day?"settings-voice-off-light":"settings-voice-off-dark",0);
        tap_category(pointer,"开启助手"); assert(s_actions[PANEL_VOICE_ON]>0);
        tap_category(pointer,LV_SYMBOL_CLOSE);
    }
    printf("Inspected %d rendered labels; %d errors. Dashboard and P4 service controls passed.\n", s_labels, s_errors);
    return s_errors ? 1 : 0;
}
