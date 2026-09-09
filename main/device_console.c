#include "device_services.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "cloud_service.h"
#include "recorder.h"
#include "voice_service.h"
#include "camera_service.h"

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
        err = ESP_OK;
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
    const char *names[] = {"status", "wifi", "endpoint", "token", "upload", "record", "ota", "voice", "camera"};
    const char *help[] = {"Show redacted device status", "wifi \"SSID\" \"password\" (local console echoes input)",
        "endpoint upload|ota https://...", "token <bearer> (local console echoes input)", "upload on|off|now",
        "record seal", "ota check | ota install parked-confirmed", "voice start|stop (30s limit)", "camera on|off"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        esp_console_cmd_t entry = {.command = names[i], .help = help[i], .func = command};
        err = esp_console_cmd_register(&entry);
        if (err != ESP_OK) { repl->del(repl); return err; }
    }
    return esp_console_start_repl(repl);
}
