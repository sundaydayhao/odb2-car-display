#include "dashboard.h"

#include <stdio.h>
#include <string.h>

#include "dashboard_text.h"
#include "fonts/dashboard_fonts.h"
#include "media_panel.h"

#define PAGE_COUNT DASHBOARD_PAGE_COUNT
#define BINDING_COUNT 24
#define RPM_LIMIT 8000
#define RPM_WARNING 6000

static const dashboard_palette_t s_palettes[2][DASHBOARD_THEME_COUNT] = {
    {
        {0x101214,0xF3F7F3,0xA0ACA9,0x303A37,0x26322E,0x1C2321,0x46F3C5,0xDDF87A,0xFF86BB,0x83E69A,0xFFD079,0xFF7474,0x101B18,0x172A24},
        {0x101214,0xF3F7F3,0xA0ACA9,0x42332D,0x352922,0x1C2321,0xFF9B70,0xFFAB75,0x7BDAFF,0x83E69A,0xFFD079,0xFF7474,0x101B18,0x2B201B},
        {0x101214,0xF3F7F3,0xA0ACA9,0x393F28,0x303621,0x1C2321,0xE7FB69,0xE7FB69,0x59E1CE,0x83E69A,0xFF86BB,0xFF7474,0x101B18,0x252919},
    },
    {
        /* Clear white, sky blue and warm sand: complete light appearances. */
        {0xF5F7FA,0x202C3B,0x596879,0xCCD5E0,0xE0E6ED,0xFFFFFF,0x1765B0,0x174D86,0x7150A0,0x24734D,0x945400,0xBB3037,0xFFFFFF,0xE4EDF8},
        {0xE2F2FB,0x163C53,0x446679,0xB3D3E4,0xCAE2F0,0xF6FCFF,0x006C95,0x005C86,0x724CAF,0x1E724E,0x925300,0xB82E40,0xFFFFFF,0xCBE9F6},
        {0xF5EBDD,0x463B30,0x6C5D4D,0xD7C6AF,0xE7D8C3,0xFFFAF1,0x8F4D20,0x86451F,0x3F685E,0x326B40,0x875200,0xB52B32,0xFFFFFF,0xEEDBC0},
    },
};

typedef struct {
    lv_obj_t *label, *meter;
    dashboard_metric_t metric;
    const lv_font_t *font;
    uint32_t color;
    bool cost;
} binding_t;

static lv_obj_t *s_screen, *s_content, *s_nav, *s_overlay, *s_title, *s_index;
static lv_obj_t *s_toolbar;
static lv_obj_t *s_status, *s_settings_icon, *s_top[2], *s_rule;
static void (*s_settings_handler)(void);
static lv_obj_t *s_tabs[PAGE_COUNT], *s_tab_labels[PAGE_COUNT], *s_tab_icons[PAGE_COUNT];
static lv_obj_t *s_revs[32], *s_wheels[4], *s_gear_bg, *s_g_point;
static binding_t s_bindings[BINDING_COUNT];
static size_t s_binding_count;
static dashboard_data_t s_live;
static uint32_t s_last_update;
static bool s_has_live, s_demo, s_daylight = true, s_turbo = true;
static uint8_t s_page, s_theme, s_period, s_pressure;
static int s_fuel_price = 815;
static const dashboard_palette_t *s_p = &s_palettes[1][0];
static const char *s_names[] = {TXT_DRIVE, TXT_HEALTH, TXT_TIRES, TXT_TRIP, TXT_POWER, "多媒体"};
static const char *s_titles[] = {TXT_DRIVE_TITLE, TXT_HEALTH_TITLE, TXT_TIRES_TITLE, TXT_TRIP_TITLE, TXT_POWER_TITLE, "多媒体"};
static const char *s_units[] = {"bar", "kPa", "psi"};
static int s_width = 800, s_height = 480;
static int sx(int x) { return x * s_width / 800; }
static int sy(int y) { return y * s_height / 480; }

static void rebuild(void);
static void refresh(void);
static void action(lv_event_t *event);

enum { ACTION_SETTINGS = 11, ACTION_ALERTS, ACTION_CLOSE, ACTION_ENGINE, ACTION_SERVICE, ACTION_RECORDS };

static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, sx(x), sy(y));
    lv_obj_set_size(obj, sx(w), sy(h));
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = surface(parent, x, y, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                       const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, sx(x), sy(y));
    lv_obj_set_size(obj, sx(width), font->line_height);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, int x, int y, int w, int h, int id, uint32_t color)
{
    lv_obj_t *obj = rect(parent, x, y, w, h, color, 3);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(obj, action, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
    return obj;
}

static void text_button(lv_obj_t *parent, const char *text, int x, int y, int w, int id, bool selected)
{
    lv_obj_t *obj = button(parent, x, y, w, 44, id, selected ? s_p->accent : s_p->track);
    label(obj, text, 4, 12, w - 8, &dashboard_text_18, selected ? s_p->on_accent : s_p->ink, LV_TEXT_ALIGN_CENTER);
}

static binding_t *value(dashboard_metric_t metric, int x, int y, int w, const lv_font_t *font, uint32_t color)
{
    LV_ASSERT(s_binding_count < BINDING_COUNT);
    binding_t *b = &s_bindings[s_binding_count++];
    *b = (binding_t){.label = label(s_content, "--", x, y, w, font, color, LV_TEXT_ALIGN_LEFT),
                     .metric = metric, .font = font, .color = color};
    return b;
}

static void meter(binding_t *b, int x, int y, int w, int max, uint32_t color)
{
    lv_obj_t *obj = lv_bar_create(s_content);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, sx(x), sy(y));
    lv_obj_set_size(obj, sx(w), sy(6));
    lv_obj_set_style_bg_color(obj, lv_color_hex(s_p->track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(obj, 0, max);
    b->meter = obj;
}

static void small_metric(dashboard_metric_t id, const char *title, const char *unit, int x, int y, uint32_t color)
{
    rect(s_content, x, y, 3, 61, color, 0);
    label(s_content, title, x + 12, y, 164, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(id, x + 12, y + 29, 90, &lv_font_montserrat_32, s_p->ink);
    label(s_content, unit, x + 108, y + 38, 80, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
}

static void create_drive(void)
{
    label(s_content, TXT_ENGINE " / rpm", 28, 12, 132, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < 32; ++i) s_revs[i] = rect(s_content, 166 + i * 15, 17, 11, 13, s_p->track, 0);
    value(DASH_RPM, 682, 9, 90, &lv_font_montserrat_24, s_p->accent);
    label(s_content, TXT_SPEED, 28, 61, 220, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_SPEED, 28, 105, 304, &dashboard_digits_144, s_p->hero);
    s_gear_bg = rect(s_content, 344, 126, 50, 44, s_p->accent, 3);
    value(DASH_GEAR, 348, 136, 43, &lv_font_montserrat_24, s_p->on_accent);
    label(s_content, "km/h", 30, 224, 160, &dashboard_text_22, s_p->muted, LV_TEXT_ALIGN_LEFT);

    rect(s_content, 418, 56, 354, 209, s_p->tint, 0);
    rect(s_content, 418, 56, 2, 209, s_p->accent, 0);
    label(s_content, TXT_RANGE, 446, 70, 260, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_RANGE, 446, 100, 126, &lv_font_montserrat_48, s_p->accent);
    label(s_content, "km", 580, 123, 38, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    binding_t *fuel = value(DASH_FUEL, 680, 116, 49, &lv_font_montserrat_24, s_p->green);
    label(s_content, "%", 738, 120, 24, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    meter(fuel, 446, 160, 300, 100, s_p->green);
    label(s_content, TXT_INSTANT, 446, 182, 294, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_INSTANT_FUEL, 446, 213, 142, &lv_font_montserrat_48, s_p->secondary);
    label(s_content, "L/100km", 604, 237, 135, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    rect(s_content, 28, 278, 744, 1, s_p->line, 0);
    small_metric(DASH_COOLANT, TXT_TEMPERATURE, "°C", 28, 294, s_p->accent);
    small_metric(DASH_OIL_TEMP, TXT_OIL_TEMP, "°C", 218, 294, s_p->amber);
    small_metric(DASH_VOLTAGE, TXT_VOLTAGE, "V", 408, 294, s_p->secondary);
    small_metric(DASH_AVERAGE_FUEL, TXT_ECONOMY, "L/100km", 598, 294, s_p->green);
}

static void service_button(const char *title, const char *subtitle, int x, int id)
{
    lv_obj_t *obj = button(s_content, x, 288, 352, 66, id, s_p->bg);
    label(obj, title, 0, 1, 300, &dashboard_text_18, s_p->ink, LV_TEXT_ALIGN_LEFT);
    label(obj, subtitle, 0, 34, 310, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(obj, LV_SYMBOL_RIGHT, 326, 23, 20, &lv_font_montserrat_14, s_p->accent, LV_TEXT_ALIGN_CENTER);
}

static void create_health(void)
{
    const dashboard_metric_t metrics[] = {DASH_COOLANT,DASH_OIL_TEMP,DASH_TRANS_TEMP,DASH_INTAKE_TEMP,DASH_OIL_PRESSURE,DASH_VOLTAGE};
    const char *titles[] = {TXT_COOLANT,TXT_OIL_TEMP,TXT_TRANS_TEMP,TXT_INTAKE_TEMP,TXT_OIL_PRESSURE,TXT_VOLTAGE};
    const char *units[] = {"°C","°C","°C","°C","bar","V"};
    for (int i = 0; i < 6; ++i) {
        int x = 28 + (i % 3) * 254, y = 18 + (i / 3) * 129;
        uint32_t color = i == 1 || i == 2 ? s_p->amber : i >= 4 ? s_p->secondary : s_p->accent;
        label(s_content, titles[i], x, y, 224, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        value(metrics[i], x, y + 37, 166, &lv_font_montserrat_48, color);
        label(s_content, units[i], x + 174, y + 61, 48, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        rect(s_content, x, y + 106, 36, 2, color, 0);
    }
    rect(s_content, 28, 137, 744, 1, s_p->line, 0);
    rect(s_content, 28, 279, 744, 1, s_p->line, 0);
    service_button(TXT_ENGINE_STATUS, s_demo ? TXT_NO_FAULT : TXT_NOT_READ, 28, ACTION_ENGINE);
    service_button(TXT_MAINTENANCE, s_demo ? TXT_SAMPLE_PLAN : TXT_NO_PLAN, 418, ACTION_SERVICE);
}

static void create_tires(void)
{
    const char *names[] = {TXT_FL,TXT_FR,TXT_RL,TXT_RR};
    for (int i = 0; i < 4; ++i) {
        int x = i % 2 ? 604 : 28, y = i / 2 ? 169 : 46;
        rect(s_content, x, y, 3, 110, s_p->accent, 0);
        label(s_content, names[i], x + 12, y, 150, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        value(DASH_TIRE_FL + i, x + 12, y + 30, 108, &lv_font_montserrat_48, s_p->accent);
        label(s_content, s_units[s_pressure], x + 124, y + 54, 42, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        value(DASH_TIRE_TEMP_FL + i, x + 12, y + 92, 62, &lv_font_montserrat_14, s_p->muted);
        label(s_content, "°C", x + 78, y + 88, 30, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    }
    rect(s_content, 315, 91, 170, 1, s_p->line, 0);
    rect(s_content, 315, 231, 170, 1, s_p->line, 0);
    rect(s_content, 350, 30, 100, 246, s_p->accent, 29);
    rect(s_content, 395, 30, 10, 246, s_p->tint, 0);
    rect(s_content, 358, 87, 84, 130, s_p->bg, 20);
    rect(s_content, 365, 111, 70, 75, s_p->panel, 6);
    rect(s_content, 396, 111, 8, 75, s_p->accent, 0);
    rect(s_content, 363, 254, 74, 6, s_p->secondary, 0);
    for (int i = 0; i < 4; ++i) s_wheels[i] = rect(s_content, i % 2 ? 452 : 336, i / 2 ? 213 : 75, 12, 35, s_p->muted, 4);
    rect(s_content, 28, 294, 744, 1, s_p->line, 0);
    label(s_content, s_demo ? TXT_DEMO : TXT_NO_SENSOR, 28, 320, 240, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, s_demo ? TXT_TIRE_REFERENCE " 2.4 / 2.3 bar" : TXT_NO_REFERENCE, 288, 320, 324, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *obj = button(s_content, 650, 307, 122, 48, ACTION_SETTINGS, s_p->bg);
    label(obj, TXT_TIRE_SETTINGS, 0, 13, 122, &dashboard_text_18, s_p->accent, LV_TEXT_ALIGN_RIGHT);
}

static void line(lv_obj_t *parent, const lv_point_precise_t *points, int count, uint32_t color, int width)
{
    static lv_point_precise_t scaled[8];
    LV_ASSERT(count <= 8);
    for (int i = 0; i < count; ++i) scaled[i] = (lv_point_precise_t){sx(points[i].x), sy(points[i].y)};
    lv_obj_t *obj = lv_line_create(parent);
    lv_line_set_points(obj, scaled, count);
    lv_obj_set_style_line_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(obj, width, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static void create_trip(void)
{
    const char *periods[] = {TXT_NOW,TXT_TODAY,TXT_TANK};
    for (int i = 0; i < 3; ++i) text_button(s_content, periods[i], 28 + i * 99, 9, 95, 40 + i, s_period == i);
    label(s_content, s_demo ? TXT_DEMO : TXT_LIVE_ONLY, 418, 20, 354, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_RIGHT);
    label(s_content, TXT_DISTANCE, 28, 72, 240, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_TRIP_DISTANCE, 28, 111, 255, &dashboard_digits_72, s_p->hero);
    label(s_content, "km", 292, 139, 46, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, TXT_DURATION, 28, 178, 84, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_TRIP_MINUTES, 125, 178, 95, &lv_font_montserrat_24, s_p->ink);
    label(s_content, TXT_MINUTE, 238, 178, 60, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, TXT_FUEL_TREND, 418, 72, 240, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, "L/100km", 674, 72, 98, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_RIGHT);
    rect(s_content, 454, 113, 318, 1, s_p->line, 0);
    rect(s_content, 454, 146, 318, 1, s_p->line, 0);
    rect(s_content, 454, 179, 318, 1, s_p->line, 0);
    if (s_demo) {
        static const lv_point_precise_t trend[] = {{454,123},{499,135},{544,144},{589,157},{634,151},{679,164},{724,159},{770,167}};
        label(s_content, "9", 419, 104, 24, &lv_font_montserrat_14, s_p->muted, LV_TEXT_ALIGN_LEFT);
        label(s_content, "7", 419, 171, 24, &lv_font_montserrat_14, s_p->muted, LV_TEXT_ALIGN_LEFT);
        line(s_content, trend, 8, s_p->accent, 3);
    } else label(s_content, TXT_NO_HISTORY, 478, 137, 260, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_CENTER);
    label(s_content, TXT_START, 454, 186, 65, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, TXT_CURRENT, 692, 186, 80, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_RIGHT);
    const char *titles[] = {TXT_ECONOMY,TXT_FUEL_USED,TXT_COST,TXT_AVERAGE_SPEED,TXT_IDLE,TXT_FUEL_PRICE};
    const char *units[] = {"L/100km","L",TXT_YUAN,"km/h",TXT_MIN_SEC,TXT_YUAN_L};
    dashboard_metric_t ids[] = {DASH_AVERAGE_FUEL,DASH_TRIP_FUEL,DASH_TRIP_FUEL,DASH_TRIP_SPEED,DASH_TRIP_IDLE,DASH_TRIP_FUEL};
    for (int i = 0; i < 6; ++i) {
        int x = 28 + i % 3 * 254, y = 220 + i / 3 * 72;
        rect(s_content, x, y, 234, 1, s_p->line, 0);
        label(s_content, titles[i], x, y + 8, 230, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        if (i == 5) {
            char price[12];
            snprintf(price, sizeof(price), "%d.%02d", s_fuel_price / 100, s_fuel_price % 100);
            label(s_content, price, x, y + 34, 142, &lv_font_montserrat_32, s_p->ink, LV_TEXT_ALIGN_LEFT);
        } else {
            binding_t *b = value(ids[i], x, y + 34, 142, &lv_font_montserrat_32, i == 1 ? s_p->accent : i == 2 ? s_p->secondary : s_p->ink);
            b->cost = i == 2;
        }
        label(s_content, units[i], x + 150, y + 43, 85, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    }
}

static void create_performance(void)
{
    label(s_content, s_turbo ? TXT_BOOST : TXT_MANIFOLD, 28, 18, 350, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(s_turbo ? DASH_BOOST : DASH_MANIFOLD, 28, 61, 260, &dashboard_digits_72, s_p->hero);
    label(s_content, s_turbo ? "bar" : "kPa", 300, 90, 74, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, TXT_THROTTLE, 28, 148, 230, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    binding_t *throttle = value(DASH_THROTTLE, 308, 149, 47, &lv_font_montserrat_14, s_p->accent);
    label(s_content, "%", 360, 147, 24, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    meter(throttle, 28, 178, 354, 100, s_p->accent);
    label(s_content, TXT_LOAD, 28, 204, 230, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    binding_t *load = value(DASH_LOAD, 308, 205, 47, &lv_font_montserrat_14, s_p->secondary);
    label(s_content, "%", 360, 203, 24, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    meter(load, 28, 234, 354, 100, s_p->secondary);
    lv_obj_t *ring = surface(s_content, 436, 36, 120, 120);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 2, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(s_p->secondary), 0);
    lv_obj_t *inner = surface(s_content, 467, 67, 58, 58);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(s_p->line), 0);
    rect(s_content, 436, 96, 120, 1, s_p->line, 0);
    rect(s_content, 496, 36, 1, 120, s_p->line, 0);
    label(s_content, "0.5 g", 461, 12, 75, &lv_font_montserrat_14, s_p->muted, LV_TEXT_ALIGN_CENTER);
    s_g_point = rect(s_content, 496, 96, 9, 9, s_p->accent, LV_RADIUS_CIRCLE);
    label(s_content, TXT_LONGITUDINAL, 596, 24, 174, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_LONG_G, 596, 55, 111, &lv_font_montserrat_32, s_p->accent);
    label(s_content, "g", 733, 65, 24, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    label(s_content, TXT_LATERAL, 596, 107, 174, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_LATERAL_G, 596, 138, 111, &lv_font_montserrat_32, s_p->accent);
    label(s_content, "g", 733, 148, 24, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    rect(s_content, 418, 188, 354, 1, s_p->line, 0);
    label(s_content, TXT_RPM, 418, 213, 168, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    value(DASH_RPM, 592, 207, 126, &lv_font_montserrat_32, s_p->secondary);
    label(s_content, "rpm", 728, 220, 44, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_RIGHT);
    rect(s_content, 28, 257, 744, 1, s_p->line, 0);
    const char *records[] = {"0-100 km/h","80-120 km/h","100-0 km/h"};
    const char *values[] = {"8.4","5.8","39.2"};
    for (int i = 0; i < 3; ++i) {
        int x = 28 + i * 254;
        rect(s_content, x, 276, 3, 50, i == 0 ? s_p->accent : i == 1 ? s_p->secondary : s_p->hero, 0);
        label(s_content, records[i], x + 12, 273, 220, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
        label(s_content, s_demo ? values[i] : "--", x + 12, 302, 144, &lv_font_montserrat_32, s_p->ink, LV_TEXT_ALIGN_LEFT);
        label(s_content, i == 2 ? "m" : "s", x + 169, 312, 50, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    }
    label(s_content, s_demo ? TXT_SAMPLE_RECORD : TXT_NO_RECORD, 28, 343, 448, &dashboard_text_18, s_p->muted, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *obj = button(s_content, 650, 335, 122, 30, ACTION_RECORDS, s_p->bg);
    label(obj, TXT_RECORDS, 0, 5, 122, &dashboard_text_18, s_p->accent, LV_TEXT_ALIGN_RIGHT);
}

static void sample_data(dashboard_data_t *data)
{
    *data = (dashboard_data_t){.value = {
        [DASH_SPEED]=86,[DASH_RPM]=2450,[DASH_FUEL]=68,[DASH_COOLANT]=89,[DASH_VOLTAGE]=14200,
        [DASH_LOAD]=52,[DASH_THROTTLE]=38,[DASH_RANGE]=386,[DASH_INSTANT_FUEL]=68,[DASH_AVERAGE_FUEL]=74,
        [DASH_OIL_TEMP]=98,[DASH_TRANS_TEMP]=82,[DASH_INTAKE_TEMP]=32,[DASH_OIL_PRESSURE]=360,
        [DASH_BOOST]=62,[DASH_MANIFOLD]=84,[DASH_GEAR]=6,[DASH_TIRE_FL]=240,[DASH_TIRE_FR]=240,
        [DASH_TIRE_RL]=230,[DASH_TIRE_RR]=230,[DASH_TIRE_TEMP_FL]=31,[DASH_TIRE_TEMP_FR]=30,
        [DASH_TIRE_TEMP_RL]=32,[DASH_TIRE_TEMP_RR]=32,[DASH_TRIP_DISTANCE]=376,[DASH_TRIP_MINUTES]=44,
        [DASH_TRIP_SPEED]=513,[DASH_TRIP_FUEL]=278,[DASH_TRIP_IDLE]=380,[DASH_LONG_G]=20,[DASH_LATERAL_G]=12,
    }};
    for (int i = 0; i < DASH_METRIC_COUNT; ++i) data->valid[i] = true;
    if (s_period) {
        data->value[DASH_TRIP_DISTANCE] = s_period == 1 ? 824 : 3260;
        data->value[DASH_TRIP_MINUTES] = s_period == 1 ? 106 : 418;
        data->value[DASH_TRIP_SPEED] = s_period == 1 ? 466 : 468;
        data->value[DASH_TRIP_FUEL] = s_period == 1 ? 626 : 2445;
        data->value[DASH_TRIP_IDLE] = s_period == 1 ? 850 : 2910;
    }
}

static bool warning(dashboard_metric_t metric, int32_t number)
{
    return (metric == DASH_COOLANT && number >= 105) || (metric == DASH_FUEL && number <= 15) ||
           (metric == DASH_RPM && number >= RPM_WARNING);
}

static const char *status_text(const dashboard_data_t *data)
{
    if (s_demo) return TXT_DEMO;
    if (data->valid[DASH_COOLANT] && warning(DASH_COOLANT,data->value[DASH_COOLANT])) return TXT_COOLANT_HIGH;
    if (data->valid[DASH_FUEL] && warning(DASH_FUEL,data->value[DASH_FUEL])) return TXT_FUEL_LOW;
    if (data->valid[DASH_RPM] && warning(DASH_RPM,data->value[DASH_RPM])) return TXT_RPM_HIGH;
    /* Connection progress belongs to Bluetooth settings; retain demo and vehicle warnings. */
    return "";
}

static void fixed(char *text, size_t size, int64_t number, int scale, int decimals)
{
    int64_t magnitude = number < 0 ? -number : number;
    snprintf(text, size, "%s%ld.%0*ld", number < 0 ? "-" : "", (long)(magnitude / scale), decimals, (long)(magnitude % scale));
}

static void format_value(binding_t *b, int32_t n, char *text, size_t size)
{
    dashboard_metric_t id = b->metric;
    if (b->cost) fixed(text,size,((int64_t)n*s_fuel_price+50)/100,100,2);
    else if (id == DASH_VOLTAGE) fixed(text,size,n/100,10,1);
    else if (id == DASH_GEAR) snprintf(text,size,"D%ld",(long)n);
    else if (id == DASH_BOOST || id == DASH_LONG_G || id == DASH_LATERAL_G || id == DASH_TRIP_FUEL) fixed(text,size,n,100,2);
    else if (id == DASH_OIL_PRESSURE) fixed(text,size,n/10,10,1);
    else if (id >= DASH_TIRE_FL && id <= DASH_TIRE_RR && s_pressure != 1) {
        fixed(text,size,s_pressure == 2 ? ((int64_t)n*14504+5000)/10000 : n/10,10,1);
    } else if (id == DASH_INSTANT_FUEL || id == DASH_AVERAGE_FUEL || id == DASH_TRIP_DISTANCE || id == DASH_TRIP_SPEED) fixed(text,size,n,10,1);
    else if (id == DASH_TRIP_IDLE) snprintf(text,size,"%02ld:%02ld",(long)(n/60),(long)(n%60));
    else snprintf(text,size,"%ld",(long)n);
}

static void fit_value(binding_t *b, const char *text)
{
    const lv_font_t *font = b->font;
    const lv_font_t *fallbacks[] = {&lv_font_montserrat_32,&lv_font_montserrat_24,&lv_font_montserrat_14};
    lv_point_t size;
    lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    for (int i = 0; size.x > lv_obj_get_width(b->label) && i < 3; ++i) {
        if (fallbacks[i]->line_height >= font->line_height) continue;
        font = fallbacks[i];
        lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    }
    lv_obj_set_style_text_font(b->label,font,0);
}

static void refresh(void)
{
    if (!s_screen) return;
    dashboard_data_t data = s_live;
    if (s_demo) sample_data(&data);
    else if (s_page == 3 && s_period) {
        for (int i = DASH_TRIP_DISTANCE; i <= DASH_TRIP_IDLE; ++i) data.valid[i] = false;
        data.valid[DASH_AVERAGE_FUEL] = false;
    }
    const char *status = status_text(&data);
    bool alert = !s_demo && (!strcmp(status,TXT_COOLANT_HIGH) || !strcmp(status,TXT_FUEL_LOW) || !strcmp(status,TXT_RPM_HIGH));
    lv_label_set_text(s_status,status);
    lv_obj_set_style_text_color(s_status,lv_color_hex(s_demo ? s_p->secondary : alert ? s_p->red : s_p->muted),0);
    for (size_t i = 0; i < s_binding_count; ++i) {
        binding_t *b = &s_bindings[i];
        bool valid = data.valid[b->metric];
        int32_t n = data.value[b->metric];
        char text[24];
        if (valid) format_value(b,n,text,sizeof(text)); else strcpy(text,"--");
        fit_value(b,text);
        if (strcmp(lv_label_get_text(b->label),text)) lv_label_set_text(b->label,text);
        lv_obj_set_style_text_color(b->label,lv_color_hex(!valid ? s_p->muted : warning(b->metric,n) ? s_p->red : b->color),0);
        if (b->meter) lv_bar_set_value(b->meter,valid ? n : 0,LV_ANIM_OFF);
    }
    int rpm = data.valid[DASH_RPM] ? data.value[DASH_RPM] : 0;
    for (int i = 0; i < 32 && s_revs[i]; ++i) {
        uint32_t color = i < 24 ? s_p->accent : i < 28 ? s_p->secondary : s_p->red;
        lv_color_t fill = lv_color_hex(s_p->track);
        if (rpm > i * RPM_LIMIT / 32) fill = lv_color_hex(color);
        else if (i >= 24) fill = lv_color_mix(lv_color_hex(color),fill,55);
        lv_obj_set_style_bg_color(s_revs[i],fill,0);
    }
    if (s_gear_bg) lv_obj_set_style_bg_color(s_gear_bg,lv_color_hex(data.valid[DASH_GEAR] ? s_p->accent : s_p->track),0);
    for (int i = 0; i < 4 && s_wheels[i]; ++i) lv_obj_set_style_bg_color(s_wheels[i],lv_color_hex(data.valid[DASH_TIRE_FL+i] ? s_p->green : s_p->muted),0);
    if (s_g_point) {
        if (data.valid[DASH_LONG_G] && data.valid[DASH_LATERAL_G]) {
            int x = data.value[DASH_LATERAL_G], y = data.value[DASH_LONG_G];
            x = LV_CLAMP(-50,x,50); y = LV_CLAMP(-50,y,50);
            lv_obj_set_pos(s_g_point,sx(492+x),sy(92-y));
            lv_obj_remove_flag(s_g_point,LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(s_g_point,LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuild(void)
{
    if (!s_screen) return;
    s_p = &s_palettes[s_daylight][s_theme];
    media_panel_detach();
    lv_obj_clean(s_content);
    s_binding_count = 0;
    memset(s_revs,0,sizeof(s_revs));
    memset(s_wheels,0,sizeof(s_wheels));
    s_gear_bg = s_g_point = NULL;
    lv_obj_set_style_bg_color(s_screen,lv_color_hex(s_p->bg),0);
    lv_obj_set_style_bg_color(s_toolbar,lv_color_hex(s_p->bg),0);
    lv_obj_send_event(s_toolbar,LV_EVENT_STYLE_CHANGED,NULL);
    lv_obj_set_style_bg_color(s_nav,lv_color_hex(s_p->panel),0);
    lv_obj_set_style_bg_color(s_top[0],lv_color_hex(s_p->accent),0);
    lv_obj_set_style_bg_color(s_top[1],lv_color_hex(s_p->secondary),0);
    lv_obj_set_style_bg_color(s_rule,lv_color_hex(s_p->line),0);
    lv_label_set_text(s_title,s_titles[s_page]);
    char index[4]; snprintf(index,sizeof(index),"%02u",s_page+1);
    lv_label_set_text(s_index,index);
    lv_obj_set_style_text_color(s_title,lv_color_hex(s_p->ink),0);
    lv_obj_set_style_text_color(s_index,lv_color_hex(s_p->accent),0);
    lv_obj_set_style_text_color(s_settings_icon,lv_color_hex(s_p->muted),0);
    for (int i = 0; i < PAGE_COUNT; ++i) {
        lv_obj_set_style_bg_color(s_tabs[i],lv_color_hex(s_page == i ? s_p->accent : s_p->panel),0);
        lv_obj_set_style_text_color(s_tab_labels[i],lv_color_hex(s_page == i ? s_p->on_accent : s_p->muted),0);
        lv_obj_set_style_text_color(s_tab_icons[i],lv_color_hex(s_page == i ? s_p->on_accent : s_p->muted),0);
    }
    switch (s_page) {
        case 0: create_drive(); break;
        case 1: create_health(); break;
        case 2: create_tires(); break;
        case 3: create_trip(); break;
        case 4: create_performance(); break;
        case 5: media_panel_create(s_content, s_p); break;
    }
    lv_obj_update_layout(s_content);
    refresh();
}

void dashboard_select_page(uint8_t page)
{
    if (page >= PAGE_COUNT || !s_screen) return;
    s_page = page;
    rebuild();
}

uint8_t dashboard_active_page(void) { return s_page; }
uint8_t dashboard_active_theme(void) { return s_theme; }
const dashboard_palette_t *dashboard_palette(uint8_t theme, bool daylight)
{
    return &s_palettes[daylight][theme < DASHBOARD_THEME_COUNT ? theme : 0];
}
bool dashboard_demo_enabled(void) { return s_demo; }
bool dashboard_daylight_enabled(void) { return s_daylight; }
bool dashboard_turbo_enabled(void) { return s_turbo; }
uint8_t dashboard_pressure_unit(void) { return s_pressure; }
void dashboard_set_turbo(bool enabled) { s_turbo = enabled; rebuild(); }
void dashboard_set_pressure_unit(uint8_t unit) { if (unit < 3) { s_pressure = unit; rebuild(); } }
void dashboard_set_settings_handler(void (*handler)(void)) { s_settings_handler = handler; }

void dashboard_select_theme(uint8_t theme)
{
    if (theme >= DASHBOARD_THEME_COUNT) return;
    s_theme = theme;
    rebuild();
}

void dashboard_set_demo(bool enabled)
{
    s_demo = enabled;
    if (!enabled && lv_tick_elaps(s_last_update) > 5000) memset(&s_live,0,sizeof(s_live));
    rebuild();
}

void dashboard_set_daylight(bool enabled)
{
    s_daylight = enabled;
    rebuild();
}

static lv_obj_t *dialog(const char *title)
{
    lv_obj_clean(s_overlay);
    lv_obj_remove_flag(s_overlay,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sheet = rect(s_overlay,190,54,420,372,s_p->panel,8);
    label(sheet,title,24,19,302,&dashboard_text_22,s_p->ink,LV_TEXT_ALIGN_LEFT);
    lv_obj_t *close = button(sheet,362,10,44,44,ACTION_CLOSE,s_p->panel);
    label(close,LV_SYMBOL_CLOSE,4,14,36,&lv_font_montserrat_14,s_p->muted,LV_TEXT_ALIGN_CENTER);
    rect(sheet,24,61,372,1,s_p->line,0);
    return sheet;
}

static void action(lv_event_t *event)
{
    int id = (int)(uintptr_t)lv_event_get_user_data(event);
    if (id < PAGE_COUNT) { dashboard_select_page(id); return; }
    if (id >= 40 && id < 43) { s_period = id-40; rebuild(); return; }
    if (id == ACTION_CLOSE) { lv_obj_add_flag(s_overlay,LV_OBJ_FLAG_HIDDEN); return; }
    if (id == ACTION_SETTINGS) { if (s_settings_handler) s_settings_handler(); return; }
    const char *title = id == ACTION_ENGINE ? TXT_ENGINE_STATUS : id == ACTION_SERVICE ? TXT_MAINTENANCE : id == ACTION_RECORDS ? TXT_RECORDS : TXT_ALERTS;
    const char *detail = id == ACTION_ENGINE ? (s_demo ? TXT_NO_FAULT : TXT_NOT_READ) :
                         id == ACTION_SERVICE ? (s_demo ? TXT_SAMPLE_PLAN : TXT_NO_PLAN) :
                         id == ACTION_RECORDS ? (s_demo ? TXT_SAMPLE_RECORD : TXT_NO_RECORD) : status_text(&s_live);
    lv_obj_t *sheet = dialog(title);
    label(sheet,detail,24,105,372,&dashboard_text_22,s_p->ink,LV_TEXT_ALIGN_LEFT);
    label(sheet,s_demo ? TXT_DEMO_ONLY : TXT_LIVE_ONLY,24,293,372,&dashboard_text_18,s_p->muted,LV_TEXT_ALIGN_LEFT);
}

void dashboard_update(const dashboard_data_t *data)
{
    if (!data || !s_screen) return;
    s_live = *data;
    s_last_update = lv_tick_get();
    s_has_live = true;
    refresh();
}

static void expire_data(lv_timer_t *timer)
{
    (void)timer;
    if (s_has_live && lv_tick_elaps(s_last_update) > 5000) {
        memset(&s_live,0,sizeof(s_live));
        s_has_live = false;
        refresh();
    }
}

void dashboard_create(lv_display_t *display)
{
    dashboard_create_on(lv_display_get_screen_active(display), lv_display_get_horizontal_resolution(display),
                        lv_display_get_vertical_resolution(display));
}

void dashboard_create_on(lv_obj_t *parent, int width, int height)
{
    s_width = width; s_height = height;
    s_screen = parent;
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, width, height);
    lv_obj_remove_flag(s_screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_screen,LV_OPA_COVER,0);
    s_top[0] = rect(s_screen,0,0,640,3,s_p->accent,0);
    s_top[1] = rect(s_screen,640,0,160,3,s_p->secondary,0);
    s_index = label(s_screen,"01",28,21,28,&lv_font_montserrat_14,s_p->accent,LV_TEXT_ALIGN_LEFT);
    s_title = label(s_screen,TXT_DRIVE_TITLE,65,15,170,&dashboard_text_22,s_p->ink,LV_TEXT_ALIGN_LEFT);
    s_toolbar = surface(s_screen,448,6,282,44);
    s_rule = rect(s_screen,28,53,744,1,s_p->line,0);
    lv_obj_t *status_button = surface(s_screen,240,6,200,44);
    lv_obj_remove_flag(status_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(status_button,LV_OPA_TRANSP,0);
    s_status = label(status_button,"",0,12,200,&dashboard_text_18,s_p->muted,LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *settings = button(s_screen,738,6,44,44,ACTION_SETTINGS,s_p->bg);
    lv_obj_set_style_bg_opa(settings,LV_OPA_TRANSP,0);
    s_settings_icon = label(settings,LV_SYMBOL_SETTINGS,0,11,44,&lv_font_montserrat_24,s_p->muted,LV_TEXT_ALIGN_CENTER);
    s_content = surface(s_screen,0,54,800,366);
    s_nav = rect(s_screen,0,420,800,60,s_p->panel,0);
    const char *icons[] = {LV_SYMBOL_HOME,LV_SYMBOL_BARS,LV_SYMBOL_REFRESH,LV_SYMBOL_GPS,LV_SYMBOL_CHARGE,LV_SYMBOL_AUDIO};
    for (int i = 0; i < PAGE_COUNT; ++i) {
        s_tabs[i] = button(s_nav,22+i*126,6,122,48,i,s_p->panel);
        s_tab_icons[i] = label(s_tabs[i],icons[i],14,16,20,&lv_font_montserrat_14,s_p->muted,LV_TEXT_ALIGN_CENTER);
        s_tab_labels[i] = label(s_tabs[i],s_names[i],43,13,72,&dashboard_text_18,s_p->muted,LV_TEXT_ALIGN_LEFT);
    }
    s_overlay = rect(s_screen,0,0,800,480,0x000000,0);
    lv_obj_set_style_bg_opa(s_overlay,LV_OPA_50,0);
    lv_obj_add_flag(s_overlay,LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    rebuild();
    lv_timer_create(expire_data,1000,NULL);
}

lv_obj_t *dashboard_service_toolbar(void) { return s_toolbar; }
