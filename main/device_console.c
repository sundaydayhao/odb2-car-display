#include "device_services.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "linenoise/linenoise.h"
#include "cloud_service.h"
#include "recorder.h"
#include "voice_service.h"
#include "camera_service.h"
#include "elm327_ble.h"
#include "media_service.h"
#include "ui.h"
#include <stdlib.h>

static int command(int argc, char **argv)
{
    int result = 1;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    device_settings_t settings;
    device_settings_get(&settings);
    const char *cmd = argv[0];
    if (!strcmp(cmd, "status") && argc == 1) {
        device_status_t status;
        device_status_get(&status);
        printf("device=%s network=%d clock=%d dropped=%lu\n", device_id(), device_network_ready(),
               device_clock_ready(), (unsigned long)recorder_dropped());
        for (int i = 0; i < SERVICE_COUNT; ++i) printf("service[%d]=%d %s\n", i, status.state[i], status.detail[i]);
        elm_ble_status_t bt=elm_ble_status();
        printf("bluetooth available=%d enabled=%d state=%d peer=%s\n",bt.available,bt.enabled,bt.state,*bt.address?bt.address:"none");
        const uint32_t dram_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
        printf("dram free=%u largest=%u\n", (unsigned)heap_caps_get_free_size(dram_caps),
               (unsigned)heap_caps_get_largest_free_block(dram_caps));
        media_status_t media;
        media_service_status(&media);
        printf("media state=%u source=%u volume=%u frames=%lu played_ms=%lu received=%lu buffered=%lu\n",
               media.state, media.source, media.volume, (unsigned long)media.decoded_frames,
               (unsigned long)media.played_ms, (unsigned long)media.received_bytes, (unsigned long)media.buffered_bytes);
        err = ESP_OK;
    } else if (!strcmp(cmd, "media") && argc >= 2) {
        if (argc == 2 && !strcmp(argv[1], "open")) {
            err = ui_open_media() ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
        media_request_t request = {0};
        bool valid = false;
        if (argc == 3 && (!strcmp(argv[1], "play") || !strcmp(argv[1], "volume"))) {
            char *end;
            unsigned long value = strtoul(argv[2], &end, 10);
            request.action = !strcmp(argv[1], "play") ? MEDIA_SELECT : MEDIA_VOLUME;
            request.value = value;
            valid = *argv[2] && !*end && value <= 100;
        } else if (argc == 2 && (!strcmp(argv[1], "toggle") || !strcmp(argv[1], "stop"))) {
            request.action = !strcmp(argv[1], "stop") ? MEDIA_STOP : MEDIA_TOGGLE; valid = true;
        } else if (argc == 4 && !strcmp(argv[1], "url") && (!strcmp(argv[2], "music") || !strcmp(argv[2], "radio"))) {
            request.action = MEDIA_CUSTOM_SAVE; request.value = !strcmp(argv[2], "radio");
            if (strlen(argv[3]) < MEDIA_URL_SIZE) { strcpy(request.url, argv[3]); valid = true; }
        }
        if (valid) err = media_service_request(&request) ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else if (!strcmp(cmd,"wifi-scan") && argc==1) {
        err = device_network_scan_request();
    } else if (!strcmp(cmd,"wifi-scan") && argc==2 && !strcmp(argv[1],"status")) {
        wifi_networks_t networks;
        device_network_scan_get(&networks);
        unsigned secured=0, unsupported=0;
        for (unsigned i=0;i<networks.count;++i) {
            if (networks.items[i].security!=NETWORK_OPEN) ++secured;
            if (networks.items[i].security==NETWORK_UNSUPPORTED) ++unsupported;
        }
        printf("wifi_scan state=%u count=%u secured=%u unsupported=%u revision=%lu connected=%d\n",
               networks.state,networks.count,secured,unsupported,(unsigned long)networks.revision,networks.connected);
        err=ESP_OK;
    } else if (!strcmp(cmd,"bluetooth") && argc==2) {
        if (!strcmp(argv[1],"on") || !strcmp(argv[1],"off")) err=elm_ble_set_enabled(!strcmp(argv[1],"on"));
        else if (!strcmp(argv[1],"reconnect")) err=elm_ble_reconnect();
    } else if (!strcmp(cmd, "wifi") && argc == 3 && strlen(argv[1]) <= 32 && strlen(argv[2]) <= 63) {
        strlcpy(settings.ssid, argv[1], sizeof(settings.ssid));
        strlcpy(settings.password, argv[2], sizeof(settings.password));
        err = device_settings_save(&settings);
        if (err == ESP_OK) device_network_reconfigure();
    } else if (!strcmp(cmd, "endpoint") && argc == 3 && strlen(argv[2]) < 256) {
        char *target = !strcmp(argv[1], "upload") ? settings.upload_url : !strcmp(argv[1], "ota") ? settings.ota_url : NULL;
        if (target) { strlcpy(target, argv[2], 256); err = device_settings_save(&settings); }
    } else if (!strcmp(cmd, "token") && argc == 2 && strlen(argv[1]) < sizeof(settings.token)) {
        strlcpy(settings.token, argv[1], sizeof(settings.token));
        err = device_settings_save(&settings);
    } else if (!strcmp(cmd, "upload") && argc == 2) {
        if (!strcmp(argv[1], "now")) { recorder_seal(); cloud_request_upload(); err = ESP_OK; }
        else if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")) {
            settings.upload_enabled = !strcmp(argv[1], "on");
            err = device_settings_save(&settings);
        }
    } else if (!strcmp(cmd, "record") && argc == 2 && !strcmp(argv[1], "seal")) {
        recorder_seal(); err = ESP_OK;
    } else if (!strcmp(cmd, "ota")) {
        if (argc == 2 && !strcmp(argv[1], "check")) err = cloud_request_ota(false, false);
        if (argc == 3 && !strcmp(argv[1], "install")) {
            if (!strcmp(argv[2], "parked-confirmed")) err = cloud_request_ota(true, false);
#if CONFIG_DEVICE_BENCH_MAINTENANCE
            if (!strcmp(argv[2], "bench-confirmed")) err = cloud_request_ota(true, true);
#endif
        }
    } else if (!strcmp(cmd, "voice") && argc == 2 && (!strcmp(argv[1], "start") || !strcmp(argv[1], "stop"))) {
        voice_set_pressed(!strcmp(argv[1], "start")); err = ESP_OK;
    } else if (!strcmp(cmd, "camera") && argc == 2 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) {
        camera_set_enabled(!strcmp(argv[1], "on")); err = ESP_OK;
    }
    if (err == ESP_OK) result = 0;
    printf("%s\n", esp_err_to_name(err));
    memset(&settings, 0, sizeof(settings));
    /* The lab console is local-only; do not retain credentials in history. */
    linenoiseHistoryFree();
    return result;
}

esp_err_t device_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "vehicle>";
    config.max_cmdline_length = 512;
    config.task_stack_size = 8192;
    config.history_save_path = NULL;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&dev, &config, &repl);
#else
    esp_console_dev_uart_config_t dev = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&dev, &config, &repl);
#endif
    if (err != ESP_OK) return err;
    const char *names[] = {"status", "wifi", "endpoint", "token", "upload", "record", "ota", "voice", "camera", "bluetooth", "wifi-scan", "media"};
    const char *help[] = {"Show redacted device status", "wifi \"SSID\" \"password\" (local console echoes input)",
        "endpoint upload|ota https://...", "token <bearer> (local console echoes input)", "upload on|off|now",
        "record seal", "ota check | ota install parked-confirmed", "voice start|stop (30s limit)", "camera on|off", "bluetooth on|off|reconnect",
        "wifi-scan | wifi-scan status (redacted scan summary)",
        "media open | play 0..5 | toggle | stop | volume 0..100 | url music|radio URL (echoes URL)"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        esp_console_cmd_t entry = {.command = names[i], .help = help[i], .func = command};
        err = esp_console_cmd_register(&entry);
        if (err != ESP_OK) { repl->del(repl); return err; }
    }
    return esp_console_start_repl(repl);
}
