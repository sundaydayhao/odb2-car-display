#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board_pins.h"
#include "node_core.h"
#include "node_ble.h"
#include "node_capture.h"
#include "node_power.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

typedef struct { uint32_t generation; uint16_t len; uint8_t data[256]; } input_t;
static QueueHandle_t input_queue;
static node_core_t core;
static atomic_bool input_fault;
static atomic_uint input_dropped;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration;
static bool installed, usb_owner;
static node_bus_mode_t bus_mode;
static unsigned bitrate, rx_count, tx_requested, tx_failed, bus_off, mode_changes;
static uint32_t rx_missed, rx_overrun, bus_errors, discarded_on_switch;
static node_power_t power_policy;
static RTC_DATA_ATTR uint32_t unattended_wakes;
static RTC_DATA_ATTR bool low_battery_latched;
static uint32_t wake_window_ms;

static void inhibit(void) { gpio_set_level(NODE_PIN_TX_INHIBIT, 1); }
static void rx_ble(const uint8_t *data, size_t len, uint32_t generation) {
    if (!len) { inhibit(); atomic_store(&input_fault, true); return; }
    input_t input = {.generation = generation, .len = len};
    memcpy(input.data, data, len);
    if (!xQueueSend(input_queue, &input, 0)) {
        inhibit(); atomic_store(&input_fault, true); atomic_fetch_add(&input_dropped, 1);
    }
}
static void accumulate_status(void) {
    twai_status_info_t s;
    if (installed && twai_get_status_info(&s) == ESP_OK) {
        rx_missed += s.rx_missed_count; rx_overrun += s.rx_overrun_count;
        bus_errors += s.bus_error_count; discarded_on_switch += s.msgs_to_rx;
    }
}
static bool set_bus(void *ctx, node_bus_mode_t mode, unsigned rate) {
    (void)ctx;
    if (installed && mode == bus_mode && rate == bitrate) {
        gpio_set_level(NODE_PIN_TX_INHIBIT, mode != NODE_BUS_ACTIVE || atomic_load(&input_fault));
        return true;
    }
    inhibit(); gpio_set_level(NODE_PIN_CAN_STANDBY, 1);
    if (installed) {
        accumulate_status();
        /* twai_stop may return INVALID_STATE after bus-off; uninstall accepts bus-off. */
        twai_stop();
        if (twai_driver_uninstall() != ESP_OK) return false;
        installed = false;
    }
    bus_mode = NODE_BUS_OFF; mode_changes++;
    if (mode == NODE_BUS_OFF) return true;
    if (rate != 250000 && rate != 500000) return false;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(NODE_PIN_CAN_TX, NODE_PIN_CAN_RX,
        mode == NODE_BUS_ACTIVE ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY);
    g.tx_queue_len = 1; g.rx_queue_len = 256;
    g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_TX_FAILED;
    twai_timing_config_t t500 = TWAI_TIMING_CONFIG_500KBITS();
    twai_timing_config_t t250 = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, rate == 500000 ? &t500 : &t250, &f) != ESP_OK) return false;
    installed = true;
    if (twai_start() != ESP_OK) { twai_driver_uninstall(); installed = false; return false; }
    bitrate = rate; bus_mode = mode;
    gpio_set_level(NODE_PIN_CAN_STANDBY, 0);
    /* TCAN1044A standby exit is below this 1 ms settling interval. */
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(NODE_PIN_TX_INHIBIT, mode != NODE_BUS_ACTIVE || atomic_load(&input_fault));
    return true;
}
static bool transmit(void *ctx, const node_frame_t *f) {
    (void)ctx;
    if (!installed || bus_mode != NODE_BUS_ACTIVE || f->dlc > 8 || f->rtr ||
        atomic_load(&input_fault) || (!usb_owner && !node_ble_connected())) return false;
    twai_message_t m = {.identifier = f->id, .data_length_code = f->dlc, .extd = f->extended, .ss = 1};
    memcpy(m.data, f->data, f->dlc);
    if (twai_transmit(&m, 0) != ESP_OK) return false;
    tx_requested++; node_capture_frame(f, esp_timer_get_time(), true);
    return true; /* Queued for one attempt; this is not a successful bus-transmission assertion. */
}
static bool emit(void *ctx, const char *s, size_t n) {
    (void)ctx;
    if (usb_owner) return usb_serial_jtag_write_bytes(s, n, pdMS_TO_TICKS(50)) == (int)n;
    for (size_t pos = 0; pos < n;) {
        size_t count = n - pos; if (count > 240) count = 240;
        if (!node_ble_send(s + pos, count)) { inhibit(); atomic_store(&input_fault, true); return false; }
        pos += count;
    }
    return true;
}
static int voltage(void *ctx) {
    (void)ctx; int sum = 0;
    if (!adc || !calibration) return -1;
    for (int i = 0; i < 16; i++) {
        int raw, mv;
        if (adc_oneshot_read(adc, ADC_CHANNEL_0, &raw) != ESP_OK ||
            adc_cali_raw_to_voltage(calibration, raw, &mv) != ESP_OK) return -1;
        sum += mv;
    }
    return (sum / 16) * NODE_BATTERY_DIVIDER_NUM / NODE_BATTERY_DIVIDER_DEN;
}
static bool record(void *ctx, bool enabled) { (void)ctx; return node_capture_enable(enabled); }
static bool log_command(void *ctx, bool format) {
    if (!usb_owner) return false;
    return format ? node_capture_format() : node_capture_export(emit, ctx);
}
static void stats(void *ctx, char *s, size_t n) {
    (void)ctx; twai_status_info_t t = {0};
    if (installed) twai_get_status_info(&t);
    snprintf(s, n, "RX=%u TX_REQ=%u TX_FAIL=%u BUS_OFF=%u RX_MISS=%lu RX_OVR=%lu BUS_ERR=%lu SWITCH_DROP=%lu BLE_DROP=%lu IN_DROP=%u MODE_CHG=%u\r",
        rx_count, tx_requested, tx_failed, bus_off,
        (unsigned long)(rx_missed + t.rx_missed_count), (unsigned long)(rx_overrun + t.rx_overrun_count),
        (unsigned long)(bus_errors + t.bus_error_count), (unsigned long)discarded_on_switch,
        (unsigned long)node_ble_dropped(), atomic_load(&input_dropped), mode_changes);
    size_t used = strlen(s);
    if (used < n) node_capture_status(s + used, n - used);
}
static void init_adc(void) {
    adc_oneshot_unit_init_cfg_t u = {.unit_id = ADC_UNIT_1};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &adc));
    adc_oneshot_chan_cfg_t c = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL_0, &c));
    adc_cali_curve_fitting_config_t cal = {.unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cal, &calibration) != ESP_OK)
        ESP_LOGW("node", "ADC calibration unavailable; ATRV returns NO DATA");
}
static bool power_command(void *ctx, const char *cmd, char *reply, size_t size) {
    (void)ctx;
    if (!strcmp(cmd, "ATPOWER?")) {
        snprintf(reply, size, "AUTO=1 IDLE=%lu WAKE=%lu VLOW_PROTECTED_MV=%lu USB_BYPASS=%u",
            (unsigned long)power_policy.config.idle_s, (unsigned long)power_policy.config.wake_s,
            (unsigned long)power_policy.config.cutoff_mv, usb_serial_jtag_is_connected()); return true;
    }
    if (!strcmp(cmd, "ATSLEEP")) { power_policy.manual = true; snprintf(reply,size,"OK"); return true; }
    /* Persisted limits are USB-fixture-only; unauthenticated BLE clients cannot weaken them. */
    if (!usb_owner || !usb_serial_jtag_is_connected()) return false;
    if (strlen(cmd) <= 6 || strlen(cmd) > 11) return false;
    for (const char *p = cmd + 6; *p; p++) if (*p < '0' || *p > '9') return false;
    unsigned long value = strtoul(cmd + 6, NULL, 10);
    node_power_config_t next = power_policy.config;
    if (!strncmp(cmd, "ATIDLE", 6)) next.idle_s = value;
    else if (!strncmp(cmd, "ATWAKE", 6)) next.wake_s = value;
    else if (!strncmp(cmd, "ATVLOW", 6)) next.cutoff_mv = value;
    else return false;
    if (!node_power_config_valid(&next)) return false;
    nvs_handle_t nvs;
    if (nvs_open("node_power", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(nvs, "limits", &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs); if (err != ESP_OK) return false;
    power_policy.config = next; node_power_touch(&power_policy, esp_timer_get_time()/1000);
    snprintf(reply,size,"OK"); return true;
}
static void enter_sleep(node_sleep_reason_t reason, bool ble_started) {
    inhibit();
    if (ble_started) {
        node_ble_stop(); node_ble_disconnect(); node_capture_enable(false);
        set_bus(NULL,NODE_BUS_OFF,500000);
    }
    gpio_set_level(NODE_PIN_CAN_STANDBY,1); gpio_set_level(NODE_PIN_LED,0);
    low_battery_latched = reason == NODE_SLEEP_LOW_BATTERY;
    vTaskDelay(pdMS_TO_TICKS(5));
    esp_sleep_enable_timer_wakeup(power_policy.config.wake_s * 1000000ULL);
    /* WUP on CAN_RX can wake C3 GPIO3. Suppress chatter/latched-low wake storms. */
    if (!low_battery_latched && unattended_wakes < 3 && gpio_get_level(NODE_PIN_CAN_RX))
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(1ULL << NODE_PIN_CAN_RX, ESP_GPIO_WAKEUP_GPIO_LOW));
    /* External pullups also enforce these levels through reset and sleep. */
    gpio_hold_en(NODE_PIN_TX_INHIBIT); gpio_hold_en(NODE_PIN_CAN_STANDBY);
    gpio_hold_en(NODE_PIN_LED); gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
}
static void init_power(void) {
    node_power_config_t c = {.idle_s=60,.wake_s=900,.cutoff_mv=12000};
    nvs_handle_t nvs;
    if (nvs_open("node_power",NVS_READONLY,&nvs)==ESP_OK) {
        node_power_config_t stored; size_t len=sizeof(stored);
        if (nvs_get_blob(nvs,"limits",&stored,&len)==ESP_OK && len==sizeof(stored) && node_power_config_valid(&stored)) c=stored;
        nvs_close(nvs);
    }
    esp_sleep_wakeup_cause_t cause=esp_sleep_get_wakeup_cause();
    uint32_t window = c.idle_s*1000;
    if (cause==ESP_SLEEP_WAKEUP_GPIO) { unattended_wakes++; window=15000; }
    else if (cause==ESP_SLEEP_WAKEUP_TIMER) { window=3000; }
    else { unattended_wakes=0; low_battery_latched=false; }
    node_power_init(&power_policy,&c,esp_timer_get_time()/1000,window);
    wake_window_ms=window;
    /* Give the high-impedance ADC divider/capacitor time to settle. */
    vTaskDelay(pdMS_TO_TICKS(300));
    int mv=voltage(NULL);
    /* A timer alone must not reset the wake-storm limit every 15 minutes. */
    if (mv >= 13200) unattended_wakes=0; /* Protected-rail charging indication, not an ignition signal. */
    if (low_battery_latched && (mv<0 || mv<(int)c.cutoff_mv+300) && !usb_serial_jtag_is_connected())
        enter_sleep(NODE_SLEEP_LOW_BATTERY,false);
}
static void usb_command(const uint8_t *data, size_t len, uint64_t now) {
    /* USB is a bench console and cannot take over a live BLE connection. */
    if (node_ble_connected()) return;
    if (!usb_owner) { node_core_disconnect(&core); usb_owner = true; }
    node_power_touch(&power_policy,now);
    node_core_feed(&core, data, len, now);
}
void app_main(void) {
    gpio_config_t pins = {.pin_bit_mask = (1ULL << NODE_PIN_TX_INHIBIT) |
        (1ULL << NODE_PIN_CAN_STANDBY) | (1ULL << NODE_PIN_LED), .mode = GPIO_MODE_OUTPUT};
    inhibit(); gpio_set_level(NODE_PIN_CAN_STANDBY,1); gpio_set_level(NODE_PIN_LED,0);
    ESP_ERROR_CHECK(gpio_config(&pins));
    gpio_deep_sleep_hold_dis(); gpio_hold_dis(NODE_PIN_TX_INHIBIT);
    gpio_hold_dis(NODE_PIN_CAN_STANDBY); gpio_hold_dis(NODE_PIN_LED);
    gpio_config_t rx_pin={.pin_bit_mask=1ULL<<NODE_PIN_CAN_RX,.mode=GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&rx_pin));
    /* Do not silently erase NVS if it contains bonds or configuration. */
    ESP_ERROR_CHECK(nvs_flash_init());
    init_adc();
    init_power();
    input_queue = xQueueCreate(16, sizeof(input_t)); assert(input_queue);
    ESP_ERROR_CHECK(node_capture_init());
    usb_serial_jtag_driver_config_t usb = {.rx_buffer_size = 512, .tx_buffer_size = 1024};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    node_io_t io = {.emit = emit, .bus = set_bus, .transmit = transmit, .voltage_mv = voltage,
        .record = record, .stats = stats, .log_command = log_command, .power_command = power_command};
    node_core_init(&core, &io);
    if (!set_bus(NULL, NODE_BUS_LISTEN, 500000)) ESP_LOGE("node", "CAN unavailable");
    ESP_ERROR_CHECK(node_ble_init(rx_ble));
    power_policy.deadline=esp_timer_get_time()/1000+wake_window_ms;
    uint32_t generation = node_ble_generation(); uint64_t retry_after = 0, power_check_at = 0;
    for (;;) {
        uint64_t now = esp_timer_get_time() / 1000;
        uint32_t current = node_ble_generation();
        bool fault = atomic_exchange(&input_fault, false);
        if (current != generation || fault) {
            inhibit(); node_core_disconnect(&core); usb_owner = false;
            if (current == generation) node_ble_disconnect();
            generation = current;
        }
        input_t input;
        for (unsigned k = 0; k < 4 && xQueueReceive(input_queue, &input, 0); k++) {
            if (input.generation == generation && node_ble_connected()) {
                node_power_touch(&power_policy,now); unattended_wakes=0;
                usb_owner = false; node_core_feed(&core, input.data, input.len, now);
            }
        }
        uint8_t usb_data[128]; int bytes = usb_serial_jtag_read_bytes(usb_data, sizeof(usb_data), 0);
        if (bytes > 0) usb_command(usb_data, bytes, now);
        if (installed) {
            uint32_t alerts = 0; twai_read_alerts(&alerts, 0);
            if (alerts & (TWAI_ALERT_BUS_OFF | TWAI_ALERT_TX_FAILED)) {
                if (alerts & TWAI_ALERT_BUS_OFF) bus_off++;
                if (alerts & TWAI_ALERT_TX_FAILED) tx_failed++;
                inhibit(); node_core_bus_error(&core); retry_after = now + 1000;
            }
        }
        if (installed) {
            twai_message_t m;
            for (unsigned k = 0; k < 64 && twai_receive(&m, 0) == ESP_OK; k++) {
                if (m.data_length_code > 8) continue;
                node_frame_t f = {.id = m.identifier, .dlc = m.data_length_code, .extended = m.extd, .rtr = m.rtr};
                if (!f.rtr) memcpy(f.data, m.data, f.dlc);
                uint64_t us = esp_timer_get_time(); rx_count++;
                node_capture_frame(&f, us, false); node_core_receive(&core, &f, us / 1000);
            }
        } else if (retry_after && now >= retry_after) {
            retry_after = 0; set_bus(NULL, NODE_BUS_LISTEN, node_core_bitrate(&core));
        }
        node_core_tick(&core, esp_timer_get_time() / 1000);
        if (now >= power_check_at) {
            power_check_at=now+1000; node_capture_tick(now*1000);
            node_sleep_reason_t reason=node_power_check(&power_policy,now,voltage(NULL),usb_serial_jtag_is_connected());
            if (reason != NODE_AWAKE) enter_sleep(reason,true);
        }
        gpio_set_level(NODE_PIN_LED, node_ble_connected() ? 1 : (now % 2000 < 30));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
