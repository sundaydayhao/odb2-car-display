#include "ui.h"
#include "dashboard.h"

#include <assert.h>
#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
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
#define TOUCH_INT_GPIO               4

#define CH422G_ADDR_WR_SET           0x24
#define CH422G_ADDR_WR_IO            0x38
#define CH422G_WR_SET_IO_OE          0x01
#define CH422G_EXIO_TP_RST           BIT1
#define CH422G_EXIO_LCD_BL           BIT2
#define CH422G_EXIO_LCD_RST          BIT3
#define CH422G_EXIO_SD_CS            BIT4
#define CH422G_EXIO_USB_SEL          BIT5
#define CH422G_OUTPUT_LCD_ON_CAN     (CH422G_EXIO_TP_RST | CH422G_EXIO_LCD_BL | CH422G_EXIO_LCD_RST | \
                                      CH422G_EXIO_SD_CS | CH422G_EXIO_USB_SEL)

#define LVGL_TICK_PERIOD_MS          2
#define LVGL_TASK_STACK_SIZE         (8 * 1024)
#define LVGL_TASK_PRIORITY           2
#define LVGL_TASK_MAX_DELAY_MS       500
#define LVGL_TASK_MIN_DELAY_MS       (1000 / CONFIG_FREERTOS_HZ)

static _lock_t s_lvgl_lock;
static lv_display_t *s_display;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_touch_handle_t s_touch_handle;
static esp_lcd_panel_io_handle_t s_touch_io_handle;
static bool s_ui_ready;

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

static esp_err_t init_board_i2c_bus(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

static esp_err_t write_board_outputs(uint8_t value)
{
    esp_err_t ret = ESP_OK;
    i2c_master_dev_handle_t ch422g_wr_set = NULL;
    i2c_master_dev_handle_t ch422g_wr_io = NULL;

    ESP_RETURN_ON_ERROR(init_board_i2c_bus(), TAG, "create I2C bus failed");
    ESP_GOTO_ON_ERROR(i2c_add_device(s_i2c_bus, CH422G_ADDR_WR_SET, &ch422g_wr_set), cleanup, TAG,
                      "add CH422G WR_SET device failed");
    ESP_GOTO_ON_ERROR(i2c_add_device(s_i2c_bus, CH422G_ADDR_WR_IO, &ch422g_wr_io), cleanup, TAG,
                      "add CH422G WR_IO device failed");

    ESP_GOTO_ON_ERROR(i2c_write_u8(ch422g_wr_set, CH422G_WR_SET_IO_OE), cleanup, TAG,
                      "enable CH422G output failed");
    ESP_GOTO_ON_ERROR(i2c_write_u8(ch422g_wr_io, value), cleanup, TAG,
                      "set CH422G LCD backlight and CAN mode failed");

cleanup:
    if (ch422g_wr_io != NULL) {
        i2c_master_bus_rm_device(ch422g_wr_io);
    }
    if (ch422g_wr_set != NULL) {
        i2c_master_bus_rm_device(ch422g_wr_set);
    }

    return ret;
}

static esp_err_t init_board_io_expander(void)
{
    return write_board_outputs(CH422G_OUTPUT_LCD_ON_CAN);
}

static esp_err_t pulse_touch_reset(void)
{
    ESP_RETURN_ON_ERROR(write_board_outputs(CH422G_OUTPUT_LCD_ON_CAN & ~CH422G_EXIO_TP_RST),
                        TAG,
                        "drive touch reset low failed");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(write_board_outputs(CH422G_OUTPUT_LCD_ON_CAN), TAG, "drive touch reset high failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    esp_lcd_touch_point_data_t touch_points[1];
    uint8_t touch_count = 0;

    if (s_touch_handle == NULL || esp_lcd_touch_read_data(s_touch_handle) != ESP_OK ||
        esp_lcd_touch_get_data(s_touch_handle, touch_points, &touch_count, 1) != ESP_OK || touch_count == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->point.x = touch_points[0].x;
    data->point.y = touch_points[0].y;
    data->state = LV_INDEV_STATE_PRESSED;
}

static esp_err_t init_gt911_touch(lv_display_t *display)
{
    ESP_RETURN_ON_ERROR(init_board_i2c_bus(), TAG, "create I2C bus failed");
    ESP_RETURN_ON_ERROR(pulse_touch_reset(), TAG, "reset GT911 touch failed");

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &s_touch_io_handle),
                        TAG,
                        "create GT911 panel IO failed");

    esp_lcd_touch_io_gt911_config_t gt911_config = {
        .dev_addr = io_config.dev_addr,
    };
    esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt911_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(s_touch_io_handle, &touch_config, &s_touch_handle),
                        TAG,
                        "init GT911 touch failed");

    lv_indev_t *indev = lv_indev_create();
    ESP_RETURN_ON_FALSE(indev != NULL, ESP_ERR_NO_MEM, TAG, "create LVGL touch indev failed");
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    lv_indev_set_display(indev, display);
    ESP_LOGI(TAG, "GT911 touch registered to LVGL");
    return ESP_OK;
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

    void *buf = heap_caps_malloc(LCD_DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
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

void ui_init(void)
{
    ESP_LOGI(TAG, "Initialize 800x480 RGB LCD and LVGL dashboard");

    esp_err_t err = init_rgb_lcd(&s_display);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UI initialization failed: %s", esp_err_to_name(err));
        return;
    }

    err = init_board_io_expander();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Board IO expander setup failed: %s", esp_err_to_name(err));
    }

    err = init_gt911_touch(s_display);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GT911 touch setup failed: %s", esp_err_to_name(err));
    }

    _lock_acquire(&s_lvgl_lock);
    dashboard_create(s_display);
    _lock_release(&s_lvgl_lock);
    ESP_LOGI(TAG, "Dashboard ready: five pages, three palettes, live mode; demo available in settings");

    xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);
    s_ui_ready = true;
}

void ui_render_vehicle_data(const obd2_vehicle_data_t *data)
{
    if (data == NULL || !s_ui_ready) {
        return;
    }

    const dashboard_data_t view = {
        .value = {
            [DASH_SPEED] = data->speed_kph,
            [DASH_RPM] = data->rpm,
            [DASH_FUEL] = data->fuel_percent,
            [DASH_COOLANT] = data->coolant_temp_c,
            [DASH_VOLTAGE] = data->control_voltage_mv,
            [DASH_LOAD] = data->engine_load_percent,
            [DASH_THROTTLE] = data->throttle_percent,
        },
        .valid = {
            [DASH_SPEED] = data->speed_valid,
            [DASH_RPM] = data->rpm_valid,
            [DASH_FUEL] = data->fuel_valid,
            [DASH_COOLANT] = data->coolant_temp_valid,
            [DASH_VOLTAGE] = data->control_voltage_valid,
            [DASH_LOAD] = data->engine_load_valid,
            [DASH_THROTTLE] = data->throttle_valid,
        },
    };
    _lock_acquire(&s_lvgl_lock);
    dashboard_update(&view);
    _lock_release(&s_lvgl_lock);
}
