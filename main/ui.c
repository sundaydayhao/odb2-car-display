#include "ui.h"

#include <assert.h>
#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

static const char *TAG = "ui";

#define LCD_H_RES                    800
#define LCD_V_RES                    480
#define LCD_PIXEL_CLOCK_HZ           (16 * 1000 * 1000)
#define LCD_HSYNC_PULSE_WIDTH        4
#define LCD_HSYNC_BACK_PORCH         8
#define LCD_HSYNC_FRONT_PORCH        8
#define LCD_VSYNC_PULSE_WIDTH        4
#define LCD_VSYNC_BACK_PORCH         8
#define LCD_VSYNC_FRONT_PORCH        8
#define LCD_RGB_DATA_WIDTH           16
#define LCD_RGB_PIXEL_SIZE           2
#define LCD_NUM_FB                   1
#define LCD_DRAW_BUF_LINES           48
#define LCD_DRAW_BUF_SIZE            (LCD_H_RES * LCD_DRAW_BUF_LINES * LCD_RGB_PIXEL_SIZE)

#define LCD_PIN_NUM_HSYNC            46
#define LCD_PIN_NUM_VSYNC            3
#define LCD_PIN_NUM_DE               5
#define LCD_PIN_NUM_PCLK             7
#define LCD_PIN_NUM_DISP_EN          -1

#define BOARD_I2C_PORT               I2C_NUM_0
#define BOARD_I2C_SDA                8
#define BOARD_I2C_SCL                9
#define BOARD_I2C_SPEED_HZ           400000
#define BOARD_I2C_TIMEOUT_MS         100

#define CH422G_ADDR_OUT              0x24
#define CH422G_ADDR_CONFIG           0x48
#define CH422G_ADDR_IO               0x38
#define CH422G_OC_MODE               0x01
#define CH422G_IO_OE                 0x02
#define CH422G_BL_BIT                BIT2

#define LVGL_TICK_PERIOD_MS          2
#define LVGL_TASK_STACK_SIZE         (8 * 1024)
#define LVGL_TASK_PRIORITY           2
#define LVGL_TASK_MAX_DELAY_MS       500
#define LVGL_TASK_MIN_DELAY_MS       (1000 / CONFIG_FREERTOS_HZ)

#define SPEED_MAX_KPH                240
#define RPM_MAX                      8000
#define RPM_WARN                     6000
#define FUEL_LOW_PERCENT             15
#define UI_ARRAY_SIZE(a)             (sizeof(a) / sizeof((a)[0]))

static _lock_t s_lvgl_lock;
static lv_display_t *s_display;
static bool s_ui_ready;

static lv_obj_t *s_speed_arc;
static lv_obj_t *s_speed_value_label;
static lv_obj_t *s_speed_state_label;
static lv_obj_t *s_rpm_arc;
static lv_obj_t *s_rpm_value_label;
static lv_obj_t *s_rpm_state_label;
static lv_obj_t *s_fuel_bar;
static lv_obj_t *s_fuel_value_label;
static lv_obj_t *s_fuel_state_label;
static lv_obj_t *s_status_label;

static lv_style_t s_screen_style;
static lv_style_t s_panel_style;
static lv_style_t s_label_dim_style;
static lv_style_t s_label_value_style;
static lv_style_t s_speed_arc_main_style;
static lv_style_t s_speed_arc_indicator_style;
static lv_style_t s_rpm_arc_main_style;
static lv_style_t s_rpm_arc_indicator_style;
static lv_style_t s_fuel_bar_main_style;
static lv_style_t s_fuel_bar_indicator_style;

static bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel,
                                    const esp_lcd_rgb_panel_event_data_t *event_data,
                                    void *user_ctx)
{
    (void)panel;
    (void)event_data;
    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);
    return false;
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                              area->x1,
                                              area->y1,
                                              area->x2 + 1,
                                              area->y2 + 1,
                                              px_map));
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "LVGL task started");

    while (1) {
        _lock_acquire(&s_lvgl_lock);
        uint32_t delay_ms = lv_timer_handler();
        _lock_release(&s_lvgl_lock);

        delay_ms = MAX(delay_ms, LVGL_TASK_MIN_DELAY_MS);
        delay_ms = MIN(delay_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(delay_ms * 1000);
    }
}

static esp_err_t i2c_add_device(i2c_master_bus_handle_t bus,
                                uint16_t address,
                                i2c_master_dev_handle_t *out_dev)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = BOARD_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &dev_config, out_dev);
}

static esp_err_t i2c_write_u8(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, BOARD_I2C_TIMEOUT_MS);
}

static esp_err_t init_board_backlight(void)
{
    esp_err_t ret = ESP_OK;
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t ch422g_config = NULL;
    i2c_master_dev_handle_t ch422g_out = NULL;
    i2c_master_dev_handle_t ch422g_io = NULL;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "create I2C bus failed");
    ESP_GOTO_ON_ERROR(i2c_add_device(bus, CH422G_ADDR_CONFIG, &ch422g_config), cleanup, TAG, "add CH422G config device failed");
    ESP_GOTO_ON_ERROR(i2c_add_device(bus, CH422G_ADDR_OUT, &ch422g_out), cleanup, TAG, "add CH422G output device failed");
    ESP_GOTO_ON_ERROR(i2c_add_device(bus, CH422G_ADDR_IO, &ch422g_io), cleanup, TAG, "add CH422G IO device failed");

    ESP_GOTO_ON_ERROR(i2c_write_u8(ch422g_config, CH422G_IO_OE), cleanup, TAG, "enable CH422G output failed");
    ESP_GOTO_ON_ERROR(i2c_write_u8(ch422g_out, CH422G_OC_MODE), cleanup, TAG, "set CH422G open drain mode failed");
    ESP_GOTO_ON_ERROR(i2c_write_u8(ch422g_io, CH422G_BL_BIT), cleanup, TAG, "turn on LCD backlight failed");

cleanup:
    if (ch422g_io != NULL) {
        i2c_master_bus_rm_device(ch422g_io);
    }
    if (ch422g_out != NULL) {
        i2c_master_bus_rm_device(ch422g_out);
    }
    if (ch422g_config != NULL) {
        i2c_master_bus_rm_device(ch422g_config);
    }
    if (bus != NULL) {
        i2c_del_master_bus(bus);
    }

    return ret;
}

static void configure_arc(lv_obj_t *arc, int32_t min, int32_t max, int16_t rotation, int16_t bg_start, int16_t bg_end)
{
    lv_arc_set_range(arc, min, max);
    lv_arc_set_value(arc, min);
    lv_arc_set_rotation(arc, rotation);
    lv_arc_set_bg_angles(arc, bg_start, bg_end);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *create_label(lv_obj_t *parent,
                              const char *text,
                              const lv_font_t *font,
                              lv_color_t color,
                              lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static void init_styles(void)
{
    lv_style_init(&s_screen_style);
    lv_style_set_bg_color(&s_screen_style, lv_color_hex(0x070a0f));
    lv_style_set_bg_opa(&s_screen_style, LV_OPA_COVER);

    lv_style_init(&s_panel_style);
    lv_style_set_bg_color(&s_panel_style, lv_color_hex(0x111821));
    lv_style_set_bg_opa(&s_panel_style, LV_OPA_80);
    lv_style_set_border_color(&s_panel_style, lv_color_hex(0x233241));
    lv_style_set_border_width(&s_panel_style, 1);
    lv_style_set_radius(&s_panel_style, 8);
    lv_style_set_pad_all(&s_panel_style, 12);

    lv_style_init(&s_label_dim_style);
    lv_style_set_text_color(&s_label_dim_style, lv_color_hex(0x7d8ea3));
    lv_style_set_text_letter_space(&s_label_dim_style, 0);

    lv_style_init(&s_label_value_style);
    lv_style_set_text_color(&s_label_value_style, lv_color_hex(0xf3f7fb));
    lv_style_set_text_letter_space(&s_label_value_style, 0);

    lv_style_init(&s_speed_arc_main_style);
    lv_style_set_arc_color(&s_speed_arc_main_style, lv_color_hex(0x263442));
    lv_style_set_arc_width(&s_speed_arc_main_style, 18);
    lv_style_set_arc_rounded(&s_speed_arc_main_style, true);

    lv_style_init(&s_speed_arc_indicator_style);
    lv_style_set_arc_color(&s_speed_arc_indicator_style, lv_color_hex(0x16c7d6));
    lv_style_set_arc_width(&s_speed_arc_indicator_style, 18);
    lv_style_set_arc_rounded(&s_speed_arc_indicator_style, true);

    lv_style_init(&s_rpm_arc_main_style);
    lv_style_set_arc_color(&s_rpm_arc_main_style, lv_color_hex(0x2b3038));
    lv_style_set_arc_width(&s_rpm_arc_main_style, 12);
    lv_style_set_arc_rounded(&s_rpm_arc_main_style, true);

    lv_style_init(&s_rpm_arc_indicator_style);
    lv_style_set_arc_color(&s_rpm_arc_indicator_style, lv_color_hex(0xf0a52b));
    lv_style_set_arc_width(&s_rpm_arc_indicator_style, 12);
    lv_style_set_arc_rounded(&s_rpm_arc_indicator_style, true);

    lv_style_init(&s_fuel_bar_main_style);
    lv_style_set_bg_color(&s_fuel_bar_main_style, lv_color_hex(0x252f3a));
    lv_style_set_bg_opa(&s_fuel_bar_main_style, LV_OPA_COVER);
    lv_style_set_radius(&s_fuel_bar_main_style, 4);

    lv_style_init(&s_fuel_bar_indicator_style);
    lv_style_set_bg_color(&s_fuel_bar_indicator_style, lv_color_hex(0x4fd071));
    lv_style_set_bg_opa(&s_fuel_bar_indicator_style, LV_OPA_COVER);
    lv_style_set_radius(&s_fuel_bar_indicator_style, 4);
}

static void add_tick_labels(lv_obj_t *parent)
{
    static const struct {
        const char *text;
        lv_coord_t x;
        lv_coord_t y;
    } ticks[] = {
        {"0", -168, 116},
        {"60", -182, -4},
        {"120", -74, -120},
        {"180", 78, -120},
        {"240", 172, 116},
    };

    for (size_t i = 0; i < UI_ARRAY_SIZE(ticks); ++i) {
        lv_obj_t *label = create_label(parent,
                                       ticks[i].text,
                                       &lv_font_montserrat_16,
                                       lv_color_hex(0x52667d),
                                       LV_TEXT_ALIGN_CENTER);
        lv_obj_set_width(label, 54);
        lv_obj_align(label, LV_ALIGN_CENTER, ticks[i].x, ticks[i].y);
    }
}

static void create_dashboard(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_add_style(screen, &s_screen_style, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top_line = lv_obj_create(screen);
    lv_obj_remove_style_all(top_line);
    lv_obj_set_size(top_line, LCD_H_RES, 2);
    lv_obj_set_style_bg_color(top_line, lv_color_hex(0x16c7d6), 0);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_60, 0);
    lv_obj_align(top_line, LV_ALIGN_TOP_MID, 0, 0);

    s_status_label = create_label(screen,
                                  "OBD-II WAIT",
                                  &lv_font_montserrat_18,
                                  lv_color_hex(0x9fb0c3),
                                  LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_status_label, 220);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *speed_panel = lv_obj_create(screen);
    lv_obj_add_style(speed_panel, &s_panel_style, 0);
    lv_obj_set_size(speed_panel, 392, 392);
    lv_obj_align(speed_panel, LV_ALIGN_CENTER, 0, 34);
    lv_obj_clear_flag(speed_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_speed_arc = lv_arc_create(speed_panel);
    lv_obj_set_size(s_speed_arc, 354, 354);
    lv_obj_center(s_speed_arc);
    configure_arc(s_speed_arc, 0, SPEED_MAX_KPH, 140, 0, 260);
    lv_obj_add_style(s_speed_arc, &s_speed_arc_main_style, LV_PART_MAIN);
    lv_obj_add_style(s_speed_arc, &s_speed_arc_indicator_style, LV_PART_INDICATOR);

    add_tick_labels(speed_panel);

    s_speed_value_label = create_label(speed_panel,
                                       "--",
                                       &lv_font_montserrat_48,
                                       lv_color_hex(0xf8fbff),
                                       LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_speed_value_label, 210);
    lv_obj_align(s_speed_value_label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *speed_unit_label = create_label(speed_panel,
                                              "km/h",
                                              &lv_font_montserrat_24,
                                              lv_color_hex(0x8aa3b8),
                                              LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(speed_unit_label, 120);
    lv_obj_align(speed_unit_label, LV_ALIGN_CENTER, 0, 48);

    s_speed_state_label = create_label(speed_panel,
                                       "SPEED",
                                       &lv_font_montserrat_18,
                                       lv_color_hex(0x16c7d6),
                                       LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_speed_state_label, 160);
    lv_obj_align(s_speed_state_label, LV_ALIGN_BOTTOM_MID, 0, -34);

    lv_obj_t *rpm_panel = lv_obj_create(screen);
    lv_obj_add_style(rpm_panel, &s_panel_style, 0);
    lv_obj_set_size(rpm_panel, 188, 290);
    lv_obj_align(rpm_panel, LV_ALIGN_LEFT_MID, 18, 26);
    lv_obj_clear_flag(rpm_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rpm_title = create_label(rpm_panel,
                                       "RPM",
                                       &lv_font_montserrat_18,
                                       lv_color_hex(0xf0a52b),
                                       LV_TEXT_ALIGN_LEFT);
    lv_obj_align(rpm_title, LV_ALIGN_TOP_LEFT, 2, 0);

    s_rpm_arc = lv_arc_create(rpm_panel);
    lv_obj_set_size(s_rpm_arc, 150, 150);
    lv_obj_align(s_rpm_arc, LV_ALIGN_TOP_MID, 0, 42);
    configure_arc(s_rpm_arc, 0, RPM_MAX / 100, 135, 0, 270);
    lv_obj_add_style(s_rpm_arc, &s_rpm_arc_main_style, LV_PART_MAIN);
    lv_obj_add_style(s_rpm_arc, &s_rpm_arc_indicator_style, LV_PART_INDICATOR);

    s_rpm_value_label = create_label(rpm_panel,
                                     "--",
                                     &lv_font_montserrat_32,
                                     lv_color_hex(0xf3f7fb),
                                     LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_rpm_value_label, 140);
    lv_obj_align(s_rpm_value_label, LV_ALIGN_CENTER, 0, -4);

    s_rpm_state_label = create_label(rpm_panel,
                                     "x1000",
                                     &lv_font_montserrat_16,
                                     lv_color_hex(0x8595a7),
                                     LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_rpm_state_label, 110);
    lv_obj_align(s_rpm_state_label, LV_ALIGN_BOTTOM_MID, 0, -22);

    lv_obj_t *fuel_panel = lv_obj_create(screen);
    lv_obj_add_style(fuel_panel, &s_panel_style, 0);
    lv_obj_set_size(fuel_panel, 188, 290);
    lv_obj_align(fuel_panel, LV_ALIGN_RIGHT_MID, -18, 26);
    lv_obj_clear_flag(fuel_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fuel_title = create_label(fuel_panel,
                                        "FUEL",
                                        &lv_font_montserrat_18,
                                        lv_color_hex(0x4fd071),
                                        LV_TEXT_ALIGN_LEFT);
    lv_obj_align(fuel_title, LV_ALIGN_TOP_LEFT, 2, 0);

    s_fuel_value_label = create_label(fuel_panel,
                                      "--%",
                                      &lv_font_montserrat_32,
                                      lv_color_hex(0xf3f7fb),
                                      LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_fuel_value_label, 150);
    lv_obj_align(s_fuel_value_label, LV_ALIGN_TOP_MID, 0, 64);

    s_fuel_bar = lv_bar_create(fuel_panel);
    lv_obj_set_size(s_fuel_bar, 34, 152);
    lv_obj_align(s_fuel_bar, LV_ALIGN_CENTER, 0, 38);
    lv_bar_set_range(s_fuel_bar, 0, 100);
    lv_bar_set_value(s_fuel_bar, 0, LV_ANIM_OFF);
    lv_obj_add_style(s_fuel_bar, &s_fuel_bar_main_style, LV_PART_MAIN);
    lv_obj_add_style(s_fuel_bar, &s_fuel_bar_indicator_style, LV_PART_INDICATOR);

    lv_obj_t *fuel_empty_label = create_label(fuel_panel,
                                              "E",
                                              &lv_font_montserrat_14,
                                              lv_color_hex(0x7d8ea3),
                                              LV_TEXT_ALIGN_CENTER);
    lv_obj_align_to(fuel_empty_label, s_fuel_bar, LV_ALIGN_OUT_LEFT_BOTTOM, -14, 4);

    lv_obj_t *fuel_full_label = create_label(fuel_panel,
                                             "F",
                                             &lv_font_montserrat_14,
                                             lv_color_hex(0x7d8ea3),
                                             LV_TEXT_ALIGN_CENTER);
    lv_obj_align_to(fuel_full_label, s_fuel_bar, LV_ALIGN_OUT_LEFT_TOP, -14, -4);

    s_fuel_state_label = create_label(fuel_panel,
                                      "LEVEL",
                                      &lv_font_montserrat_16,
                                      lv_color_hex(0x8595a7),
                                      LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(s_fuel_state_label, 130);
    lv_obj_align(s_fuel_state_label, LV_ALIGN_BOTTOM_MID, 0, -22);

    lv_obj_t *bottom_band = lv_obj_create(screen);
    lv_obj_remove_style_all(bottom_band);
    lv_obj_set_size(bottom_band, 764, 42);
    lv_obj_set_style_bg_color(bottom_band, lv_color_hex(0x0d131b), 0);
    lv_obj_set_style_bg_opa(bottom_band, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bottom_band, 1, 0);
    lv_obj_set_style_border_color(bottom_band, lv_color_hex(0x25313f), 0);
    lv_obj_set_style_radius(bottom_band, 6, 0);
    lv_obj_align(bottom_band, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_t *hint = create_label(bottom_band,
                                  "CAN 500K  |  ESP32-S3  |  800x480 RGB",
                                  &lv_font_montserrat_16,
                                  lv_color_hex(0x7d8ea3),
                                  LV_TEXT_ALIGN_CENTER);
    lv_obj_set_width(hint, 520);
    lv_obj_center(hint);
}

static esp_err_t init_rgb_lcd(lv_display_t **out_display)
{
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH,
            .hsync_back_porch = LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH,
            .vsync_back_porch = LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = LCD_VSYNC_FRONT_PORCH,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .data_width = LCD_RGB_DATA_WIDTH,
        .dma_burst_size = 64,
        .num_fbs = LCD_NUM_FB,
        .hsync_gpio_num = LCD_PIN_NUM_HSYNC,
        .vsync_gpio_num = LCD_PIN_NUM_VSYNC,
        .de_gpio_num = LCD_PIN_NUM_DE,
        .pclk_gpio_num = LCD_PIN_NUM_PCLK,
        .disp_gpio_num = LCD_PIN_NUM_DISP_EN,
        .data_gpio_nums = {
            14, 38, 18, 17, 10,
            39, 0, 45, 48, 47, 21,
            1, 2, 42, 41, 40,
        },
        .flags.fb_in_psram = true,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&panel_config, &panel_handle), TAG, "install RGB panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), TAG, "reset RGB panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), TAG, "init RGB panel failed");

    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_NO_MEM, TAG, "create LVGL display failed");
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    void *buf = esp_lcd_rgb_alloc_draw_buffer(panel_handle, LCD_DRAW_BUF_SIZE, 0);
    ESP_RETURN_ON_FALSE(buf != NULL, ESP_ERR_NO_MEM, TAG, "allocate LVGL draw buffer failed");
    lv_display_set_buffers(display, buf, NULL, LCD_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, lvgl_flush_cb);

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &callbacks, display),
                        TAG,
                        "register RGB callbacks failed");

    const esp_timer_create_args_t tick_timer_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_timer_args, &tick_timer), TAG, "create LVGL tick timer failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_PERIOD_MS * 1000),
                        TAG,
                        "start LVGL tick timer failed");

    *out_display = display;
    return ESP_OK;
}

static void update_status_label(const obd2_vehicle_data_t *data)
{
    if (data->speed_valid || data->rpm_valid || data->fuel_valid) {
        lv_label_set_text(s_status_label, "OBD-II LIVE");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x4fd071), 0);
    } else {
        lv_label_set_text(s_status_label, "OBD-II WAIT");
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xf0a52b), 0);
    }
}

static void update_speed(const obd2_vehicle_data_t *data)
{
    if (!data->speed_valid) {
        lv_arc_set_value(s_speed_arc, 0);
        lv_label_set_text(s_speed_value_label, "--");
        lv_label_set_text(s_speed_state_label, "SPEED");
        lv_obj_set_style_text_color(s_speed_state_label, lv_color_hex(0x7d8ea3), 0);
        return;
    }

    uint16_t speed = MIN(data->speed_kph, SPEED_MAX_KPH);
    char value[8];
    snprintf(value, sizeof(value), "%u", data->speed_kph);
    lv_arc_set_value(s_speed_arc, speed);
    lv_label_set_text(s_speed_value_label, value);
    lv_label_set_text(s_speed_state_label, "SPEED");
    lv_obj_set_style_text_color(s_speed_state_label, lv_color_hex(0x16c7d6), 0);
}

static void update_rpm(const obd2_vehicle_data_t *data)
{
    if (!data->rpm_valid) {
        lv_arc_set_value(s_rpm_arc, 0);
        lv_label_set_text(s_rpm_value_label, "--");
        lv_label_set_text(s_rpm_state_label, "x1000");
        lv_obj_set_style_text_color(s_rpm_value_label, lv_color_hex(0xf3f7fb), 0);
        return;
    }

    uint16_t rpm = MIN(data->rpm, RPM_MAX);
    char value[8];
    snprintf(value, sizeof(value), "%u.%u", rpm / 1000, (rpm % 1000) / 100);
    lv_arc_set_value(s_rpm_arc, rpm / 100);
    lv_label_set_text(s_rpm_value_label, value);
    lv_label_set_text(s_rpm_state_label, "x1000");

    if (rpm >= RPM_WARN) {
        lv_obj_set_style_text_color(s_rpm_value_label, lv_color_hex(0xef4e4e), 0);
        lv_obj_set_style_arc_color(s_rpm_arc, lv_color_hex(0xef4e4e), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_text_color(s_rpm_value_label, lv_color_hex(0xf3f7fb), 0);
        lv_obj_set_style_arc_color(s_rpm_arc, lv_color_hex(0xf0a52b), LV_PART_INDICATOR);
    }
}

static void update_fuel(const obd2_vehicle_data_t *data)
{
    if (!data->fuel_valid) {
        lv_bar_set_value(s_fuel_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(s_fuel_value_label, "--%");
        lv_label_set_text(s_fuel_state_label, "LEVEL");
        lv_obj_set_style_text_color(s_fuel_value_label, lv_color_hex(0xf3f7fb), 0);
        lv_obj_set_style_bg_color(s_fuel_bar, lv_color_hex(0x4fd071), LV_PART_INDICATOR);
        return;
    }

    uint8_t fuel = MIN(data->fuel_percent, 100);
    char value[8];
    snprintf(value, sizeof(value), "%u%%", fuel);
    lv_bar_set_value(s_fuel_bar, fuel, LV_ANIM_OFF);
    lv_label_set_text(s_fuel_value_label, value);

    if (fuel <= FUEL_LOW_PERCENT) {
        lv_label_set_text(s_fuel_state_label, "LOW");
        lv_obj_set_style_text_color(s_fuel_value_label, lv_color_hex(0xef4e4e), 0);
        lv_obj_set_style_bg_color(s_fuel_bar, lv_color_hex(0xef4e4e), LV_PART_INDICATOR);
    } else {
        lv_label_set_text(s_fuel_state_label, "LEVEL");
        lv_obj_set_style_text_color(s_fuel_value_label, lv_color_hex(0xf3f7fb), 0);
        lv_obj_set_style_bg_color(s_fuel_bar, lv_color_hex(0x4fd071), LV_PART_INDICATOR);
    }
}

void ui_init(void)
{
    ESP_LOGI(TAG, "Initialize 800x480 RGB LCD and LVGL dashboard");

    esp_err_t err = init_rgb_lcd(&s_display);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UI initialization failed: %s", esp_err_to_name(err));
        return;
    }

    err = init_board_backlight();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LCD backlight control failed: %s", esp_err_to_name(err));
    }

    _lock_acquire(&s_lvgl_lock);
    init_styles();
    create_dashboard(s_display);
    _lock_release(&s_lvgl_lock);

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    s_ui_ready = true;
}

void ui_render_vehicle_data(const obd2_vehicle_data_t *data)
{
    if (data == NULL || !s_ui_ready) {
        return;
    }

    _lock_acquire(&s_lvgl_lock);
    update_status_label(data);
    update_speed(data);
    update_rpm(data);
    update_fuel(data);
    _lock_release(&s_lvgl_lock);
}
