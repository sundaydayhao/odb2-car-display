#include "service_panel.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "dashboard.h"
#include "voice_strings.h"
#include "av_model.h"
#include "feature_config.h"
#include "dashboard_text.h"
#include "fonts/dashboard_fonts.h"

enum { PAGE_HOME, PAGE_NETWORK, PAGE_DISPLAY, PAGE_VOICE, PAGE_CAMERA, PAGE_RECORD, PAGE_UPDATE, PAGE_CONFIRM, PAGE_BLUETOOTH, PAGE_THEMES, PAGE_WIFI_CONNECT };
static panel_action_cb_t s_action;
static lv_obj_t *s_screen, *s_overlay, *s_sidebar, *s_body, *s_camera, *s_canvas, *s_placeholder;
static lv_obj_t *s_symbols[7], *s_more[7], *s_upload, *s_notice, *s_password, *s_keyboard, *s_brightness_text;
static lv_obj_t *s_network_list, *s_network_name, *s_network_forget, *s_scan_button;
static wifi_networks_t s_networks;
static wifi_network_t s_selected_network;
static unsigned s_current_page;
static lv_obj_t *s_bt_switch, *s_bt_peer, *s_bt_reconnect, *s_bt_name, *s_bt_forget, *s_bt_scan, *s_bt_list;
static ble_devices_t s_bt_devices;
static bool s_bt_available, s_bt_enabled = true;
static char s_bt_address[18];
static uint16_t *s_pixels;
static char s_full_detail[7][96], s_saved_ssid[33], s_saved_password[65];
static unsigned s_states[7];
static bool s_upload_enabled;
static panel_preferences_t s_preferences = PANEL_PREFERENCES_DEFAULT;
static int s_width, s_height, s_body_width;
static unsigned s_category = PAGE_DISPLAY;
static uint32_t s_bg, s_card, s_ink, s_muted, s_accent;
static const char *s_titles[] = {"设置", "网络与连接", "显示与外观", "语音助手", "摄像头", "记录与同步", "系统更新", "安装更新", "蓝牙设置", "界面主题", "连接 Wi-Fi"};
/* Keep the v1 theme/daylight NVS mapping, including existing dark selections. */
static const char *s_theme_names[2][DASHBOARD_THEME_COUNT] = {
    {TXT_NEON, TXT_LAVA, TXT_ACID}, {"清爽白", "晴空蓝", "暖沙米"},
};
static const char *s_theme_notes[2][DASHBOARD_THEME_COUNT] = {
    {"墨黑 · 薄荷绿", "墨黑 · 珊瑚橙", "墨黑 · 荧光黄"},
    {"亮白 · 简洁清晰", "浅蓝 · 清凉通透", "暖米 · 柔和温润"},
};
static const char *s_icons[] = {LV_SYMBOL_WIFI, LV_SYMBOL_AUDIO, NULL, LV_SYMBOL_SD_CARD, LV_SYMBOL_UPLOAD, LV_SYMBOL_DOWNLOAD, LV_SYMBOL_BLUETOOTH};
static const uint32_t s_colors[2][5] = {
    {0x7F918F, 0xF7C776, 0x70D9FF, 0x61EDBF, 0xFF7F92},
    {0x5E666B, 0x8F5700, 0x0A679B, 0x007E6D, 0xC62727},
};
static void show_page(unsigned page);
static void request_scan(lv_event_t *event);
static void render_networks(void);
static void render_bluetooth(void);

static bool emit(panel_action_t action)
{
    panel_request_t request = {.action = action};
    return s_action && s_action(&request);
}
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
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(obj, value);
    return obj;
}
static void camera_icon_draw(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_area_t bounds;
    lv_obj_get_coords(obj, &bounds);
    int x = bounds.x1 + (lv_area_get_width(&bounds) - 24) / 2;
    int y = bounds.y1;
    lv_layer_t *layer = lv_event_get_layer(event);
    /* Read the shared icon ink on each draw so status and theme changes apply. */
    lv_color_t ink = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = ink;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 1;
    lv_area_t area = {x + 8, y + 4, x + 15, y + 7};
    lv_draw_rect(layer, &dsc, &area);

    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = ink;
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_width = 2;
    dsc.radius = 3;
    area = (lv_area_t){x + 2, y + 7, x + 21, y + 20};
    lv_draw_rect(layer, &dsc, &area);
    dsc.radius = LV_RADIUS_CIRCLE;
    area = (lv_area_t){x + 8, y + 10, x + 15, y + 17};
    lv_draw_rect(layer, &dsc, &area);

    dsc.border_width = 0;
    dsc.bg_opa = LV_OPA_COVER;
    area = (lv_area_t){x + 18, y + 10, x + 19, y + 11};
    lv_draw_rect(layer, &dsc, &area);
}
static lv_obj_t *camera_icon(lv_obj_t *parent, int x, int y, int width, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 24);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, camera_icon_draw, LV_EVENT_DRAW_MAIN, NULL);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *title, int x, int y, int w, lv_event_cb_t cb, uintptr_t id)
{
    lv_obj_t *obj = box(parent, x, y, w, 52, s_card);
    lv_obj_set_style_radius(obj, 10, 0);
    text(obj, title, 16, 14, w - 32, &dashboard_text_18, s_accent);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)id);
    return obj;
}
static void navigation(lv_event_t *event)
{
    uintptr_t target = (uintptr_t)lv_event_get_user_data(event);
    if (target == 100) {
        if (s_keyboard) lv_keyboard_set_textarea(s_keyboard, NULL);
        show_page(PAGE_HOME); /* Delete input widgets, including their password copy. */
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    } else {
        show_page((unsigned)target);
        if (target == PAGE_NETWORK) request_scan(NULL);
    }
}
static void icon_button(lv_obj_t *parent, const char *icon, int x, lv_event_cb_t cb, uintptr_t id)
{
    lv_obj_t *obj = box(parent, x, 20, 52, 52, s_bg);
    lv_obj_t *label = text(obj, icon, 0, 15, 52, &lv_font_montserrat_24, s_muted);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)id);
}
static void command(lv_event_t *event)
{
    panel_action_t action = (panel_action_t)(uintptr_t)lv_event_get_user_data(event);
    if (emit(action)) {
        if (action == PANEL_CAMERA_ON) service_panel_camera_visible(true);
        if (action == PANEL_CAMERA_OFF) service_panel_camera_visible(false);
    } else if (s_notice) lv_label_set_text(s_notice, "操作未提交，请稍后重试");
}
static void detail(unsigned service, int y)
{
    s_more[service] = text(s_body, s_full_detail[service], 20, y, s_body_width - 40, &dashboard_text_22, s_ink);
}
static void upload_changed(lv_event_t *event)
{
    bool enabled = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    if (!emit(enabled ? PANEL_UPLOAD_ON : PANEL_UPLOAD_OFF)) service_panel_upload_enabled(s_upload_enabled);
}
static void bluetooth_changed(lv_event_t *event)
{
    bool enabled=lv_obj_has_state(lv_event_get_target_obj(event),LV_STATE_CHECKED);
    if (emit(enabled ? PANEL_BLUETOOTH_ON : PANEL_BLUETOOTH_OFF)) {
        if (s_notice) lv_label_set_text(s_notice,"蓝牙设置已提交");
    } else {
        service_panel_bluetooth(s_bt_available,s_bt_enabled,s_states[6],NULL,NULL);
        if (s_notice) lv_label_set_text(s_notice,"操作未提交，请稍后重试");
    }
}
static void bluetooth_scan(lv_event_t *event)
{
    (void)event;
    if (!s_bt_available || !s_bt_enabled || s_bt_devices.scanning) return;
    if (emit(PANEL_BLUETOOTH_SCAN)) {
        if (s_notice) lv_label_set_text(s_notice,"扫描已提交");
        if (s_bt_scan) lv_obj_add_state(s_bt_scan,LV_STATE_DISABLED);
    } else if (s_notice) lv_label_set_text(s_notice,"扫描未提交，请稍后重试");
}
static void bluetooth_select(lv_event_t *event)
{
    unsigned index=(uintptr_t)lv_event_get_user_data(event);
    if (!s_bt_available || !s_bt_enabled || index>=s_bt_devices.count) return;
    panel_request_t request={.action=PANEL_BLUETOOTH_CONNECT,
        .data.bluetooth=s_bt_devices.items[index]};
    bool accepted=s_action && s_action(&request);
    if (s_notice) lv_label_set_text(s_notice,accepted ? "正在连接所选设备" : "连接未提交，请稍后重试");
}
static void bluetooth_saved(lv_event_t *event)
{
    (void)event;
    panel_action_t action=*s_bt_address ? PANEL_BLUETOOTH_DISCONNECT : PANEL_BLUETOOTH_RECONNECT;
    if (!emit(action) && s_notice) lv_label_set_text(s_notice,"操作未提交，请稍后重试");
}
static void keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY || lv_event_get_code(event) == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}
static void focus_input(lv_event_t *event)
{
    lv_keyboard_set_textarea(s_keyboard, lv_event_get_target_obj(event));
    lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard);
}
static void show_password(lv_event_t *event)
{
    lv_textarea_set_password_mode(s_password, !lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED));
}
static lv_obj_t *input(const char *value, int y, unsigned max_chars, bool password)
{
    lv_obj_t *obj = lv_textarea_create(s_body);
    lv_obj_set_pos(obj, 160, y); lv_obj_set_size(obj, s_body_width - 180, 52);
    lv_obj_set_style_text_font(obj, &dashboard_text_18, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(s_ink), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(s_card), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(s_muted), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_textarea_set_one_line(obj, true);
    lv_textarea_set_max_length(obj, max_chars);
    lv_textarea_set_password_mode(obj, password);
    if (password) lv_textarea_set_password_show_time(obj, 0);
    lv_textarea_set_text(obj, value);
    lv_obj_add_event_cb(obj, focus_input, LV_EVENT_CLICKED, NULL);
    return obj;
}
static void save_wifi(lv_event_t *event)
{
    bool forget = (uintptr_t)lv_event_get_user_data(event) != 0;
    panel_request_t request = {.action = PANEL_WIFI_SAVE};
    if (!forget) {
        const char *ssid = s_selected_network.ssid;
        const char *password = s_selected_network.security == NETWORK_OPEN ? "" : lv_textarea_get_text(s_password);
        size_t ns = strlen(ssid), np = strlen(password);
        bool hex_key = np == 64;
        for (size_t i = 0; hex_key && i < np; ++i) hex_key = isxdigit((unsigned char)password[i]) != 0;
        if (!ns || ns > 32 || s_selected_network.security == NETWORK_UNSUPPORTED ||
            (s_selected_network.security != NETWORK_OPEN && (np < 8 || np > 64 || (np == 64 && !hex_key)))) {
            lv_label_set_text(s_notice, "密码为 8 至 63 位，或 64 位十六进制密钥");
            return;
        }
        memcpy(request.data.wifi.ssid, ssid, ns + 1);
        memcpy(request.data.wifi.password, password, np + 1);
    }
    bool accepted = s_action && s_action(&request);
    memset(&request, 0, sizeof(request));
    if (accepted) {
        show_page(PAGE_NETWORK);
    }
    lv_label_set_text(s_notice, accepted ? (forget ? "正在忘记此网络" : "正在保存并连接热点") : "操作未提交，请稍后重试");
}
bool service_panel_preferences_valid(const panel_preferences_t *p)
{
    return p && p->version == 1 && p->theme < DASHBOARD_THEME_COUNT && p->pressure < 3 && p->brightness >= 10 &&
           p->brightness <= 100 && p->daylight <= 1 && p->turbo <= 1;
}
void service_panel_preferences(const panel_preferences_t *p)
{
    if (!service_panel_preferences_valid(p)) return;
    s_preferences = *p;
    dashboard_select_theme(p->theme);
    dashboard_set_pressure_unit(p->pressure);
    dashboard_set_turbo(p->turbo);
    dashboard_set_daylight(p->daylight);
}
static void preference_event(lv_event_t *event)
{
    uintptr_t id = (uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *obj = lv_event_get_target_obj(event);
    if (id == 10 && lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text_fmt(s_brightness_text, "%ld%%", (long)lv_slider_get_value(obj));
        return;
    }
    if (id == 3) { dashboard_set_demo(lv_obj_has_state(obj, LV_STATE_CHECKED)); return; }
    panel_request_t request = {.action = PANEL_PREFERENCES_SAVE, .data.preferences = s_preferences};
    bool theme_choice = id >= 20 && id < 20 + 2 * DASHBOARD_THEME_COUNT;
    if (id == 2) request.data.preferences.turbo = lv_obj_has_state(obj, LV_STATE_CHECKED);
    else if (theme_choice) {
        unsigned choice = id - 20;
        request.data.preferences.theme = choice % DASHBOARD_THEME_COUNT;
        request.data.preferences.daylight = choice < DASHBOARD_THEME_COUNT;
    }
    else if (id >= 30 && id < 33) request.data.preferences.pressure = id - 30;
    else if (id == 10) request.data.preferences.brightness = lv_slider_get_value(obj);
    if (s_action && s_action(&request)) {
        service_panel_preferences(&request.data.preferences);
        show_page(theme_choice ? PAGE_THEMES : PAGE_DISPLAY);
        if (s_notice) lv_label_set_text(s_notice, "设置已提交，正在保存");
    } else {
        show_page(theme_choice ? PAGE_THEMES : PAGE_DISPLAY);
        if (s_notice) lv_label_set_text(s_notice, "操作未提交，请稍后重试");
    }
}
static lv_obj_t *toggle(const char *title, int y, bool checked, lv_event_cb_t cb, uintptr_t id)
{
    lv_obj_t *row = box(s_body, 20, y, s_body_width - 40, 64, s_card);
    lv_obj_set_style_radius(row, 12, 0);
    text(row, title, 18, 20, s_body_width - 176, &dashboard_text_18, s_ink);
    lv_obj_t *obj = lv_switch_create(row);
    lv_obj_set_pos(obj, s_body_width - 130, 12); lv_obj_set_size(obj, 72, 40);
    lv_obj_set_style_bg_color(obj, lv_color_hex(s_muted), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(s_accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (checked) lv_obj_add_state(obj, LV_STATE_CHECKED);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, (void *)id);
    return obj;
}
static void theme_card(unsigned theme, bool day, int x, int y, int width)
{
    const dashboard_palette_t *p = dashboard_palette(theme, day);
    bool selected = s_preferences.theme == theme && s_preferences.daylight == day;
    lv_obj_t *card = box(s_body, x, y, width, 176, p->bg);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? p->accent : p->line), 0);
    text(card, "88", 18, 12, 126, &dashboard_digits_72, p->hero);
    text(card, "km/h", 146, 76, 48, &lv_font_montserrat_14, p->muted);
    if (selected) text(card, LV_SYMBOL_OK, width - 40, 20, 24, &lv_font_montserrat_24, p->accent);
    text(card, s_theme_names[day][theme], 18, 104, width - 40, &dashboard_text_22, p->ink);
    text(card, s_theme_notes[day][theme], 18, 142, width - 40, &dashboard_text_18, p->muted);
    lv_obj_add_event_cb(card, preference_event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(20 + theme + (day ? 0 : DASHBOARD_THEME_COUNT)));
}
static void ssid_text(lv_obj_t *label, const char *ssid)
{
    char printable[33];
    snprintf(printable, sizeof(printable), "%s", ssid);
    for (unsigned i = 0; printable[i]; ++i)
        if ((unsigned char)printable[i] < 32 || printable[i] == 127) printable[i] = ' ';
    lv_label_set_text(label, printable);
}
static void select_network(lv_event_t *event)
{
    unsigned index = (uintptr_t)lv_event_get_user_data(event);
    if (index >= s_networks.count || s_networks.items[index].security == NETWORK_UNSUPPORTED) return;
    s_selected_network = s_networks.items[index]; /* Selection survives subsequent scan updates. */
    show_page(PAGE_WIFI_CONNECT);
}
static void request_scan(lv_event_t *event)
{
    (void)event;
    if (s_networks.state == NETWORK_SCAN_BUSY) return;
    if (emit(PANEL_WIFI_SCAN)) {
        s_networks.state = NETWORK_SCAN_BUSY;
        snprintf(s_networks.message, sizeof(s_networks.message), "正在查找附近的 Wi-Fi");
        render_networks();
    } else if (s_notice) lv_label_set_text(s_notice, "扫描未提交，请稍后重试");
}
static void render_networks(void)
{
    if (!s_network_list || s_current_page != PAGE_NETWORK) return;
    if (s_scan_button) {
        if (s_networks.state == NETWORK_SCAN_BUSY) lv_obj_add_state(s_scan_button, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_scan_button, LV_STATE_DISABLED);
    }
    if (s_network_name) ssid_text(s_network_name, s_networks.connected ? s_networks.connected_ssid : *s_saved_ssid ? s_saved_ssid : "未连接 Wi-Fi");
    if (s_network_forget) {
        if (*s_saved_ssid) lv_obj_remove_flag(s_network_forget, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_network_forget, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clean(s_network_list);
    lv_obj_scroll_to_y(s_network_list, 0, LV_ANIM_OFF);
    int width = s_body_width - 40;
    for (unsigned i = 0; i < s_networks.count; ++i) {
        const wifi_network_t *network = &s_networks.items[i];
        bool connected = s_networks.connected && network->security == s_networks.connected_security &&
                         !strcmp(network->ssid, s_networks.connected_ssid);
        lv_obj_t *row = box(s_network_list, 0, i * 72, width - 8, 64, s_card);
        lv_obj_set_style_radius(row, 12, 0);
        text(row, LV_SYMBOL_WIFI, 16, 18, 28, &lv_font_montserrat_24, connected ? s_accent : s_muted);
        lv_obj_t *name = text(row, "", 58, 0, width - 204, &dashboard_wifi_18, s_ink);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        ssid_text(name, network->ssid);
        const char *security_name = network->security == NETWORK_OPEN ? "开放网络" : network->security == NETWORK_WPA2 ? "WPA2" : network->security == NETWORK_WPA3 ? "WPA3" : "不支持此加密方式";
        const char *signal = network->rssi >= -55 ? "信号强" : network->rssi >= -70 ? "信号中等" : "信号较弱";
        lv_obj_t *meta = text(row, "", 58, 36, width - 204, &dashboard_text_18, s_muted);
        lv_label_set_text_fmt(meta, "%s · %s", security_name, signal);
        if (connected) text(row, "已连接", width - 104, 21, 80, &dashboard_text_18, s_accent);
        else text(row, LV_SYMBOL_RIGHT, width - 42, 25, 24, &lv_font_montserrat_14, s_muted);
        if (network->security == NETWORK_UNSUPPORTED) lv_obj_add_state(row, LV_STATE_DISABLED);
        lv_obj_add_event_cb(row, select_network, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    if (!s_networks.count) {
        const char *message = s_networks.state == NETWORK_SCAN_BUSY ? "正在扫描附近热点" : s_networks.state == NETWORK_SCAN_ERROR ? "扫描失败，请重新扫描" : "未发现附近热点";
        text(s_network_list, message, 20, 60, width - 40, &dashboard_text_22, s_muted);
        text(s_network_list, "请开启手机的 2.4 GHz 热点", 20, 112, width - 40, &dashboard_text_18, s_muted);
    }
    if (s_notice) lv_label_set_text(s_notice, *s_networks.message ? s_networks.message : "选择一个热点连接");
}
static void show_page(unsigned page)
{
    if (s_keyboard) lv_keyboard_set_textarea(s_keyboard, NULL);
    memset(s_more, 0, sizeof(s_more));
    s_upload = s_notice = s_password = s_keyboard = s_brightness_text = NULL;
    s_network_list = s_network_name = s_network_forget = s_scan_button = NULL;
    s_bt_switch = s_bt_peer = s_bt_reconnect = s_bt_name = s_bt_forget = s_bt_scan = s_bt_list = NULL;
    lv_obj_clean(s_overlay);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    if (page == PAGE_HOME) page = s_category;
    if (!DEVICE_CAMERA_AVAILABLE && page==PAGE_CAMERA) page=PAGE_DISPLAY;
    s_current_page = page;
    s_category = page == PAGE_THEMES ? PAGE_DISPLAY : page == PAGE_CONFIRM ? PAGE_UPDATE : page == PAGE_WIFI_CONNECT ? PAGE_NETWORK : page;
    bool day = dashboard_daylight_enabled();
    const dashboard_palette_t *p = dashboard_palette(dashboard_active_theme(), day);
    s_bg = p->bg; s_card = p->panel; s_ink = p->ink; s_muted = p->muted; s_accent = p->accent;
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(s_bg), 0);

    /* Tablet layout: stable category rail, bounded detail column and generous gutter. */
    s_sidebar = box(s_overlay, 0, 0, 272, s_height, s_card);
    text(s_sidebar, "设置", 36, 30, 196, &dashboard_text_22, s_ink);
    text(s_sidebar, "设备与偏好", 36, 72, 196, &dashboard_text_18, s_muted);
    const unsigned pages[] = {PAGE_NETWORK, PAGE_BLUETOOTH, PAGE_DISPLAY, PAGE_VOICE, PAGE_CAMERA, PAGE_RECORD, PAGE_UPDATE};
    const char *icons[] = {LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_SETTINGS, LV_SYMBOL_AUDIO, NULL, LV_SYMBOL_SD_CARD, LV_SYMBOL_DOWNLOAD};
    unsigned visible_row=0;
    for (unsigned i = 0; i < sizeof(pages)/sizeof(*pages); ++i) {
        if (!DEVICE_CAMERA_AVAILABLE && pages[i]==PAGE_CAMERA) continue;
        bool selected = pages[i] == s_category;
        lv_obj_t *row = box(s_sidebar, 20, 116 + visible_row++ * 60, 232, 52, selected ? p->tint : s_card);
        lv_obj_set_style_radius(row, 12, 0);
        if (selected) {
            lv_obj_t *mark = box(row, 0, 14, 3, 24, s_accent);
            lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
        }
        if (pages[i] == PAGE_CAMERA) camera_icon(row, 18, 14, 26, selected ? s_accent : s_muted);
        else text(row, icons[i], 18, 14, 26, &lv_font_montserrat_24, selected ? s_accent : s_muted);
        text(row, s_titles[pages[i]], 56, 14, 156, &dashboard_text_18, selected ? s_accent : s_ink);
        lv_obj_add_event_cb(row, navigation, LV_EVENT_CLICKED, (void *)(uintptr_t)pages[i]);
    }
    lv_obj_t *divider = box(s_overlay, 272, 24, 1, s_height - 48, p->line);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
    s_body_width = s_width - 344;
    if (s_body_width > 680) s_body_width = 680;
    s_body = box(s_overlay, 292 + (s_width - 292 - s_body_width) / 2, 0, s_body_width, s_height, s_bg);
    bool nested = page == PAGE_THEMES || page == PAGE_CONFIRM || page == PAGE_WIFI_CONNECT;
    if (nested) icon_button(s_body, LV_SYMBOL_LEFT, 8, navigation, s_category);
    text(s_body, s_titles[page], nested ? 74 : 20, 30, s_body_width - 150, &dashboard_text_22, s_ink);
    icon_button(s_body, LV_SYMBOL_CLOSE, s_body_width - 72, navigation, 100);
    int width = s_body_width - 40;

    if (page == PAGE_NETWORK) {
        lv_obj_t *current = box(s_body, 20, 100, width, 82, s_card);
        lv_obj_set_style_radius(current, 12, 0);
        s_network_name = text(current, "", 18, 10, width - 150, &dashboard_wifi_18, s_ink);
        s_more[0] = text(current, s_full_detail[0], 18, 46, width - 150, &dashboard_text_18, s_muted);
        s_network_forget = button(current, "忘记", width - 98, 15, 80, save_wifi, 1);
        text(s_body, "附近的 Wi-Fi", 20, 210, 350, &dashboard_text_22, s_ink);
        s_scan_button = button(s_body, "重新扫描", s_body_width - 152, 196, 132, request_scan, 0);
        s_network_list = box(s_body, 20, 258, width, 280, s_bg);
        lv_obj_add_flag(s_network_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_network_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_network_list, LV_SCROLLBAR_MODE_AUTO);
        s_notice = text(s_body, "", 20, 552, width, &dashboard_text_18, s_muted);
        render_networks();
    } else if (page == PAGE_WIFI_CONNECT) {
        lv_obj_t *name = text(s_body, "", 20, 102, width, &dashboard_wifi_18, s_ink);
        ssid_text(name, s_selected_network.ssid);
        bool secured = s_selected_network.security != NETWORK_OPEN;
        text(s_body, secured ? "输入热点密码后连接" : "开放网络，无需密码", 20, 154, width, &dashboard_text_18, s_muted);
        if (secured) {
            text(s_body, "热点密码", 20, 220, 120, &dashboard_text_18, s_ink);
            const char *password = !strcmp(s_selected_network.ssid, s_saved_ssid) ? s_saved_password : "";
            s_password = input(password, 204, 64, true);
            toggle("显示密码", 272, false, show_password, 0);
            s_keyboard = lv_keyboard_create(s_overlay);
            lv_obj_set_size(s_keyboard, s_width, 260);
            lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            lv_obj_add_event_cb(s_keyboard, keyboard_event, LV_EVENT_ALL, NULL);
            lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        button(s_body, "连接", 20, 358, 220, save_wifi, 0);
        if (!strcmp(s_selected_network.ssid, s_saved_ssid)) button(s_body, "忘记此网络", 256, 358, 184, save_wifi, 1);
        text(s_body, "连接信息自动保存，下次开机自动重连", 20, 446, width, &dashboard_text_18, s_muted);
        s_notice = text(s_body, "", 20, 502, width, &dashboard_text_18, s_accent);
    } else if (page == PAGE_BLUETOOTH) {
        s_bt_switch = toggle("蓝牙车辆连接", 104, s_bt_enabled, bluetooth_changed, 0);
        lv_obj_t *saved=box(s_body,20,176,width,100,s_card);
        lv_obj_set_style_radius(saved,12,0);
        s_bt_name=text(saved,"",18,0,width-246,&dashboard_wifi_18,s_ink);
        lv_label_set_long_mode(s_bt_name,LV_LABEL_LONG_DOT);
        s_bt_peer=text(saved,"",18,40,width-246,&dashboard_text_18,s_muted);
        s_more[6]=text(saved,s_full_detail[6],18,70,width-36,&dashboard_text_18,s_muted);
        s_bt_reconnect=button(saved,"连接",width-214,10,112,bluetooth_saved,0);
        s_bt_forget=button(saved,"忘记",width-90,10,80,command,PANEL_BLUETOOTH_FORGET);
        text(s_body,"附近的蓝牙设备",20,310,350,&dashboard_text_22,s_ink);
        s_bt_scan=button(s_body,"扫描设备",s_body_width-152,296,132,bluetooth_scan,0);
        s_bt_list=box(s_body,20,358,width,178,s_bg);
        lv_obj_add_flag(s_bt_list,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(s_bt_list,LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_bt_list,LV_SCROLLBAR_MODE_AUTO);
        s_notice=text(s_body,"",20,552,width,&dashboard_text_18,s_muted);
        service_panel_bluetooth(s_bt_available, s_bt_enabled, s_states[6], NULL, NULL);
        render_bluetooth();
    } else if (page == PAGE_DISPLAY) {
        lv_obj_t *row = box(s_body, 20, 100, width, 94, s_card);
        lv_obj_set_style_radius(row, 12, 0);
        text(row, "界面主题", 18, 14, 240, &dashboard_text_22, s_ink);
        text(row, s_theme_names[day][dashboard_active_theme()], 18, 54, 240, &dashboard_text_18, s_accent);
        const uint32_t swatches[] = {p->bg, p->accent, p->hero};
        for (unsigned i = 0; i < 3; ++i) {
            lv_obj_t *swatch = box(row, width - 204 + i * 44, 32, 32, 32, swatches[i]);
            lv_obj_set_style_radius(swatch, 16, 0);
            lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        }
        text(row, LV_SYMBOL_RIGHT, width - 40, 38, 24, &lv_font_montserrat_14, s_muted);
        lv_obj_add_event_cb(row, navigation, LV_EVENT_CLICKED, (void *)PAGE_THEMES);
        row = box(s_body, 20, 212, width, 90, s_card);
        lv_obj_set_style_radius(row, 12, 0);
        text(row, "屏幕亮度", 18, 14, 240, &dashboard_text_18, s_ink);
        s_brightness_text = text(row, "", width - 98, 14, 80, &dashboard_text_18, s_accent);
        lv_obj_set_style_text_align(s_brightness_text, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_text_fmt(s_brightness_text, "%u%%", s_preferences.brightness);
        lv_obj_t *slider = lv_slider_create(row);
        lv_obj_set_pos(slider, 28, 60); lv_obj_set_size(slider, width - 56, 14);
        lv_slider_set_range(slider, 10, 100); lv_slider_set_value(slider, s_preferences.brightness, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(s_accent), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(s_accent), LV_PART_KNOB);
        lv_obj_add_event_cb(slider, preference_event, LV_EVENT_VALUE_CHANGED, (void *)10);
        lv_obj_add_event_cb(slider, preference_event, LV_EVENT_RELEASED, (void *)10);
        toggle("涡轮车型显示", 320, dashboard_turbo_enabled(), preference_event, 2);
        row = box(s_body, 20, 396, width, 64, s_card);
        lv_obj_set_style_radius(row, 12, 0);
        text(row, "胎压单位", 18, 20, 180, &dashboard_text_18, s_ink);
        const char *units[] = {"bar", "kPa", "psi"};
        for (int i = 0; i < 3; ++i) {
            lv_obj_t *choice = button(row, units[i], width - 348 + i * 112, 6, 104, preference_event, 30 + i);
            lv_obj_set_style_border_width(choice, s_preferences.pressure == i ? 2 : 1, 0);
            lv_obj_set_style_border_color(choice, lv_color_hex(s_preferences.pressure == i ? s_accent : p->line), 0);
        }
        toggle("演示模式（重启关闭）", 474, dashboard_demo_enabled(), preference_event, 3);
        s_notice = text(s_body, "外观设置自动保存；演示数据不代表真实车况", 20, 550, width, &dashboard_text_18, s_muted);
    } else if (page == PAGE_THEMES) {
        text(s_body, "浅色外观", 20, 90, width, &dashboard_text_18, s_muted);
        text(s_body, "深色外观", 20, 312, width, &dashboard_text_18, s_muted);
        int card_width = (width - 24) / DASHBOARD_THEME_COUNT;
        for (unsigned i = 0; i < DASHBOARD_THEME_COUNT; ++i) {
            theme_card(i, true, 20 + i * (card_width + 12), 128, card_width);
            theme_card(i, false, 20 + i * (card_width + 12), 350, card_width);
        }
        s_notice = text(s_body, "点击卡片立即应用并保存；数字仅为配色预览", 20, 550, width, &dashboard_text_18, s_muted);
    } else if (page == PAGE_VOICE) {
        detail(1, 102);
        text(s_body, "唤醒词：" VOICE_WAKE_WORD, 20, 190, width, &dashboard_text_22, s_ink);
        text(s_body, "唤醒后直接说话，说完自动回复", 20, 242, width, &dashboard_text_22, s_ink);
        text(s_body, "在影音中心可手动开启、关闭和结束对话", 20, 304, width, &dashboard_text_18, s_muted);
        text(s_body, "待机仅在本机识别唤醒词，唤醒后才上传语音", 20, 352, width, &dashboard_text_18, s_muted);
        text(s_body, "回复后可继续说话，语音对话需要联网", 20, 400, width, &dashboard_text_18, s_muted);
    } else if (page == PAGE_CAMERA) {
        detail(2, 102);
        text(s_body, "影音中心支持预览、拍照和查看相册", 20, 190, width, &dashboard_text_22, s_ink);
        button(s_body, "打开本地预览", 20, 280, 240, command, PANEL_CAMERA_ON);
        button(s_body, "关闭摄像头", 276, 280, 220, command, PANEL_CAMERA_OFF);
    } else if (page == PAGE_RECORD) {
        detail(3, 102); detail(4, 174);
        text(s_body, "保存车辆通信记录，用于复现连接与数据问题", 20, 246, width, &dashboard_text_18, s_muted);
        s_upload = toggle("允许上传车辆记录", 310, s_upload_enabled, upload_changed, 0);
        button(s_body, "保存当前记录", 20, 402, 220, command, PANEL_SEAL);
        button(s_body, "立即同步", 256, 402, 200, command, PANEL_UPLOAD_NOW);
        text(s_body, "手机采集前请在蓝牙设置中关闭车辆连接", 20, 496, width, &dashboard_text_18, s_muted);
    } else if (page == PAGE_UPDATE) {
        detail(5, 102);
        text(s_body, "检查更新不会自动安装", 20, 190, width, &dashboard_text_22, s_ink);
        button(s_body, "检查更新", 20, 284, 220, command, PANEL_OTA_CHECK);
        button(s_body, "安装更新", 256, 284, 220, navigation, PAGE_CONFIRM);
    } else if (page == PAGE_CONFIRM) {
        text(s_body, "请先驻车并保持供电。升级期间设备会重启。", 20, 126, width, &dashboard_text_22, s_ink);
        text(s_body, "车速未知或车辆移动时禁止安装。", 20, 208, width, &dashboard_text_18, s_muted);
        button(s_body, "确认安装", 20, 306, 220, command, PANEL_OTA_INSTALL);
    }
    if (!s_notice) s_notice = text(s_body, "", 20, s_height - 30, width, &dashboard_text_18, s_accent);
}
void service_panel_open(void)
{
    if (s_overlay) {
        show_page(PAGE_HOME);
        if (s_category == PAGE_NETWORK) request_scan(NULL);
    }
}
static void toolbar_style(lv_event_t *event)
{
    (void)event;
    bool day = dashboard_daylight_enabled();
    const unsigned order[]={1,2,4,5,3,0,6};
    unsigned count=0;
    for (int i = 0; i < 7; ++i) {
        if (!s_symbols[i]) continue;
        lv_obj_set_style_text_color(s_symbols[i], lv_color_hex(s_colors[day][s_states[i]]), 0);
        bool visible = i == 0 || i == 3 || i == 6 || s_states[i] == 2 || s_states[i] == 4 ||
                       (i == 2 && s_states[i] == 3) || (i == 5 && s_states[i] == 3);
        if (i==2 && !DEVICE_CAMERA_AVAILABLE) visible=false;
        if (visible) { lv_obj_remove_flag(s_symbols[i], LV_OBJ_FLAG_HIDDEN); ++count; }
        else lv_obj_add_flag(s_symbols[i], LV_OBJ_FLAG_HIDDEN);
    }
    /* Pack visible symbols against the gear; hidden services leave no gaps. */
    if (!s_symbols[0]) return;
    lv_obj_t *toolbar=lv_obj_get_parent(s_symbols[0]);
    int slot=s_width*32/800, x=lv_obj_get_width(toolbar)-(int)count*slot;
    for (unsigned i=0;i<7;++i) {
        lv_obj_t *symbol=s_symbols[order[i]];
        if (lv_obj_has_flag(symbol,LV_OBJ_FLAG_HIDDEN)) continue;
        lv_obj_set_x(symbol,x); lv_obj_set_width(symbol,slot); x+=slot;
    }
}
static void camera_close(lv_event_t *event)
{
    (void)event;
    service_panel_camera_visible(false);
    emit(PANEL_CAMERA_OFF);
}
void service_panel_create(lv_obj_t *screen, lv_obj_t *toolbar, panel_action_cb_t action)
{
    s_screen = screen; s_action = action;
    s_width = lv_obj_get_width(screen); s_height = lv_obj_get_height(screen);
    int th = lv_obj_get_height(toolbar);
    lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 7; ++i) {
        s_symbols[i] = i == 2 ? camera_icon(toolbar, 0, (th - 24) / 2, s_width*32/800, s_colors[0][0]) :
            text(toolbar, s_icons[i], 0, (th - 24) / 2, s_width*32/800, &lv_font_montserrat_24, s_colors[0][0]);
        lv_obj_set_style_text_align(s_symbols[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_add_event_cb(toolbar, toolbar_style, LV_EVENT_STYLE_CHANGED, NULL);
    toolbar_style(NULL);
    s_overlay = box(screen, 0, 0, s_width, s_height, 0x101714);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    dashboard_set_settings_handler(service_panel_open);
    if (!DEVICE_CAMERA_AVAILABLE) return;
    s_camera = box(screen, 0, 0, s_width, s_height, 0x000000);
    lv_obj_set_style_bg_opa(s_camera, LV_OPA_70, 0);
    lv_obj_add_flag(s_camera, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sheet = box(s_camera, 0, 0, AV_PREVIEW_WIDTH+48, AV_PREVIEW_HEIGHT+110, 0x202826);
    lv_obj_center(sheet);
    text(sheet, "本地预览", 24, 24, 240, &dashboard_text_22, 0xEFF6F3);
    lv_obj_t *close = box(sheet, AV_PREVIEW_WIDTH-16, 8, 52, 52, 0x202826);
    text(close, LV_SYMBOL_CLOSE, 16, 17, 28, &lv_font_montserrat_14, 0xA8B7B2);
    lv_obj_add_event_cb(close, camera_close, LV_EVENT_CLICKED, NULL);
    s_pixels = lv_malloc_zeroed(AV_PREVIEW_WIDTH * AV_PREVIEW_HEIGHT * 2);
    if (s_pixels) {
        s_canvas = lv_canvas_create(sheet);
        lv_obj_set_pos(s_canvas, 24, 78);
        lv_canvas_set_buffer(s_canvas, s_pixels, AV_PREVIEW_WIDTH, AV_PREVIEW_HEIGHT, LV_COLOR_FORMAT_RGB565);
    }
    s_placeholder = text(sheet, "等待画面", 24, 184, AV_PREVIEW_WIDTH, &dashboard_text_22, 0xA8B7B2);
    lv_obj_set_style_text_align(s_placeholder, LV_TEXT_ALIGN_CENTER, 0);
}
void service_panel_status(unsigned id, unsigned state, const char *detail_text)
{
    if (id >= 6 || !s_screen) return;
    unsigned next_state = state < 5 ? state : 4;
    bool state_changed = s_states[id] != next_state;
    const char *detail = detail_text ? detail_text : "";
    bool detail_changed = strcmp(s_full_detail[id], detail) != 0;
    if (detail_changed) snprintf(s_full_detail[id], sizeof(s_full_detail[id]), "%s", detail);
    s_states[id] = next_state;
    if (state_changed) toolbar_style(NULL);
    if (detail_changed && s_more[id]) lv_label_set_text(s_more[id], s_full_detail[id]);
    if (id == 2 && detail_changed && s_placeholder) lv_label_set_text(s_placeholder, s_full_detail[id]);
}
void service_panel_upload_enabled(bool enabled)
{
    s_upload_enabled = enabled;
    if (!s_upload) return;
    if (enabled) lv_obj_add_state(s_upload, LV_STATE_CHECKED); else lv_obj_remove_state(s_upload, LV_STATE_CHECKED);
}
void service_panel_network_config(const char *ssid, const char *password)
{
    bool changed = strcmp(s_saved_ssid, ssid) != 0;
    snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", ssid);
    snprintf(s_saved_password, sizeof(s_saved_password), "%s", password);
    if (changed) render_networks();
}
void service_panel_network_scan(const wifi_networks_t *networks)
{
    if (!networks || networks->revision == s_networks.revision) return;
    s_networks = *networks;
    if (s_networks.count > WIFI_NETWORK_LIMIT) s_networks.count = WIFI_NETWORK_LIMIT;
    render_networks(); /* Password input is a separate page and is never rebuilt here. */
}
void service_panel_bluetooth(bool available, bool enabled, unsigned state, const char *detail_text, const char *address)
{
    bool changed=available!=s_bt_available || enabled!=s_bt_enabled || state!=s_states[6] ||
        (address && strcmp(address,s_bt_address));
    bool detail_changed = detail_text && strcmp(detail_text, s_full_detail[6]);
    s_bt_available=available; s_bt_enabled=enabled;
    s_states[6]=state<5?state:4;
    if (detail_changed) snprintf(s_full_detail[6],sizeof(s_full_detail[6]),"%s",detail_text);
    if (address) snprintf(s_bt_address,sizeof(s_bt_address),"%s",address);
    if (detail_changed && s_more[6]) lv_label_set_text(s_more[6],s_full_detail[6]);
    if (s_bt_switch) {
        if (enabled) lv_obj_add_state(s_bt_switch,LV_STATE_CHECKED); else lv_obj_remove_state(s_bt_switch,LV_STATE_CHECKED);
        if (available) lv_obj_remove_state(s_bt_switch,LV_STATE_DISABLED); else lv_obj_add_state(s_bt_switch,LV_STATE_DISABLED);
    }
    if (changed) render_bluetooth();
    if (changed) toolbar_style(NULL);
}

void service_panel_bluetooth_devices(const ble_devices_t *devices)
{
    if (!devices || devices->revision==s_bt_devices.revision) return;
    s_bt_devices=*devices;
    if (s_bt_devices.count>BLE_DEVICE_LIMIT) s_bt_devices.count=BLE_DEVICE_LIMIT;
    render_bluetooth();
}

static void render_bluetooth(void)
{
    if (!s_bt_list || s_current_page!=PAGE_BLUETOOTH) return;
    bool enabled=s_bt_available && s_bt_enabled;
    bool connecting=s_states[6]==2 && *s_bt_address;
    bool current=*s_bt_address!=0;
    char saved_address[18];
    ble_device_address(&s_bt_devices.saved,saved_address);
    bool saved_current=current && !strcmp(saved_address,s_bt_address);
    const char *name=current && !saved_current ? "当前设备" :
        s_bt_devices.has_saved ? (*s_bt_devices.saved.name ? s_bt_devices.saved.name : "已保存设备") : "尚未保存设备";
    lv_label_set_text(s_bt_name,name);
    lv_label_set_text(s_bt_peer,current ? s_bt_address : s_bt_devices.has_saved ? saved_address : "成功连接后自动保存");
    if (current || s_bt_devices.has_saved) lv_obj_remove_flag(s_bt_reconnect,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bt_reconnect,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(s_bt_reconnect,0),current ? "断开连接" : "连接");
    if (enabled) lv_obj_remove_state(s_bt_reconnect,LV_STATE_DISABLED);
    else lv_obj_add_state(s_bt_reconnect,LV_STATE_DISABLED);
    if (s_bt_devices.has_saved) lv_obj_remove_flag(s_bt_forget,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bt_forget,LV_OBJ_FLAG_HIDDEN);
    if (enabled && !connecting && !s_bt_devices.scanning) lv_obj_remove_state(s_bt_scan,LV_STATE_DISABLED);
    else lv_obj_add_state(s_bt_scan,LV_STATE_DISABLED);
    int32_t scroll=lv_obj_get_scroll_y(s_bt_list);
    lv_obj_clean(s_bt_list);
    int width=s_body_width-48;
    for (unsigned i=0;i<s_bt_devices.count;++i) {
        const ble_device_t *device=&s_bt_devices.items[i];
        char address[18]; ble_device_address(device,address);
        bool selected=current && !strcmp(address,s_bt_address);
        lv_obj_t *row=box(s_bt_list,0,i*76,width,68,s_card);
        lv_obj_set_style_radius(row,12,0);
        text(row,LV_SYMBOL_BLUETOOTH,16,20,28,&lv_font_montserrat_24,selected ? s_accent : s_muted);
        lv_obj_t *title=text(row,*device->name ? device->name : "未命名设备",58,0,width-180,&dashboard_wifi_18,s_ink);
        lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
        text(row,address,58,40,width-180,&dashboard_text_18,s_muted);
        text(row,selected ? (connecting ? "连接中" : "已连接") : "连接",width-90,23,74,&dashboard_text_18,s_accent);
        if (!enabled || connecting || selected) lv_obj_add_state(row,LV_STATE_DISABLED);
        lv_obj_add_event_cb(row,bluetooth_select,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
    if (!s_bt_devices.count) {
        text(s_bt_list,!enabled ? "请先开启蓝牙" : s_bt_devices.scanning ? "正在查找附近设备" : "点击扫描设备，再选择一个连接",
            20,28,width-40,&dashboard_text_22,s_muted);
        text(s_bt_list,"已保存设备开机重连一次，失败后停止",20,76,width-40,&dashboard_text_18,s_muted);
    }
    lv_obj_scroll_to_y(s_bt_list,scroll,LV_ANIM_OFF);
    if (s_notice) lv_label_set_text(s_notice,*s_bt_devices.message ? s_bt_devices.message : "每次扫描十五秒，选择设备后才会连接");
}
void service_panel_save_result(bool success)
{
    if (s_notice) lv_label_set_text(s_notice, success ? "设置已保存" : "设置保存失败，请重试");
}
void service_panel_camera_visible(bool visible)
{
    if (!s_camera) return;
    if (visible) lv_obj_remove_flag(s_camera, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_camera, LV_OBJ_FLAG_HIDDEN);
}
void service_panel_camera_frame(const uint16_t *pixels)
{
    if (!s_canvas) return;
    memcpy(s_pixels, pixels, AV_PREVIEW_WIDTH * AV_PREVIEW_HEIGHT * 2);
    lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_canvas);
}
void service_panel_camera_clear(void)
{
    if (s_canvas) { memset(s_pixels, 0, AV_PREVIEW_WIDTH * AV_PREVIEW_HEIGHT * 2); lv_obj_invalidate(s_canvas); }
    if (s_placeholder) lv_obj_remove_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
}
