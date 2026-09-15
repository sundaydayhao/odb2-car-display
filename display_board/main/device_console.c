#include "device_services.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "linenoise/linenoise.h"
#include "cloud_service.h"
#include "recorder.h"
#include "voice_service.h"
#include "voice_strings.h"
#include "camera_service.h"
#include "elm327_ble.h"
#include "media_service.h"
#include "media_library.h"
#include "feature_config.h"
#include "ui.h"
#include <stdlib.h>

static int command(int argc, char **argv)
{
    int result = 1;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    device_settings_t settings;
    device_settings_get(&settings);
    const char *cmd = argv[0];
    if (!DEVICE_CAMERA_AVAILABLE && !strcmp(cmd,"camera")) err=ESP_ERR_NOT_SUPPORTED;
    else if (!strcmp(cmd, "status") && argc == 1) {
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
        printf("media state=%u index=%u volume=%u frames=%lu played_ms=%lu received=%lu buffered=%lu\n",
               media.state, media.local_index, media.volume, (unsigned long)media.decoded_frames,
               (unsigned long)media.played_ms, (unsigned long)media.received_bytes, (unsigned long)media.buffered_bytes);
        err = ESP_OK;
    } else if (!strcmp(cmd, "media") && argc >= 2) {
        if (argc==2 && !strcmp(argv[1],"files")) {
            av_library_status_t library; media_library_status(&library);
            printf("library ready=%d scanning=%d music=%u photos=%u revision=%lu photo_status=%s\n",
                library.storage,library.scanning,library.music_count,library.photo_count,(unsigned long)library.revision,library.photo_detail);
            err=ESP_OK;
        } else if (argc==2 && !strcmp(argv[1],"refresh")) { media_library_refresh(); err=ESP_OK; }
        else if (argc==3 && !strcmp(argv[1],"tab")) {
            int tab=!strcmp(argv[2],"music")?AV_MUSIC:!strcmp(argv[2],"camera")?AV_CAMERA:!strcmp(argv[2],"voice")?AV_VOICE:!strcmp(argv[2],"fm")?AV_FM:-1;
            if (tab>=0) err=ui_open_av(tab)?ESP_OK:ESP_ERR_INVALID_STATE;
        }
        if (argc == 2 && !strcmp(argv[1], "open")) {
            err = ui_open_media() ? ESP_OK : ESP_ERR_INVALID_STATE;
        }
        media_request_t request = {0};
        bool valid = false;
        if (argc==3 && (!strcmp(argv[1],"local") || !strcmp(argv[1],"photo"))) {
            unsigned index; char extra;
            av_library_status_t library; media_library_status(&library);
            if (sscanf(argv[2],"%u%c",&index,&extra)==1 && index<AV_FILE_LIMIT) {
                if (!strcmp(argv[1],"photo")) err=media_library_view_photo(index,library.revision)?ESP_OK:ESP_ERR_INVALID_STATE;
                else { request.action=MEDIA_SELECT_LOCAL; request.value=index; request.revision=library.revision; valid=true; }
            }
        } else if (argc == 3 && !strcmp(argv[1], "volume")) {
            char *end;
            unsigned long value = strtoul(argv[2], &end, 10);
            request.action = MEDIA_VOLUME;
            request.value = value;
            valid = *argv[2] && !*end && value <= 100;
        } else if (argc==2 && (!strcmp(argv[1],"prev") || !strcmp(argv[1],"next"))) {
            request.action=!strcmp(argv[1],"prev")?MEDIA_PREVIOUS:MEDIA_NEXT; valid=true;
        } else if (argc == 2 && (!strcmp(argv[1], "toggle") || !strcmp(argv[1], "stop"))) {
            request.action = !strcmp(argv[1], "stop") ? MEDIA_STOP : MEDIA_TOGGLE; valid = true;

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
        else if (!strcmp(argv[1],"scan")) err=elm_ble_scan();
        else if (!strcmp(argv[1],"reconnect")) err=elm_ble_reconnect();
        else if (!strcmp(argv[1],"disconnect")) err=elm_ble_disconnect();
        else if (!strcmp(argv[1],"forget")) err=elm_ble_forget();
        else if (!strcmp(argv[1],"list")) {
            ble_devices_t devices;
            elm_ble_devices_get(&devices);
            printf("bluetooth_scan scanning=%d count=%u saved=%d revision=%lu\n",
                devices.scanning,devices.count,devices.has_saved,(unsigned long)devices.revision);
            for (unsigned i=0;i<devices.count;++i) {
                char address[18]; ble_device_address(&devices.items[i],address);
                printf("bluetooth_device index=%u address=%s type=%u rssi=%d name=%s\n",
                    i,address,devices.items[i].type,devices.items[i].rssi,devices.items[i].name);
            }
            err=ESP_OK;
        }
    } else if (!strcmp(cmd,"bluetooth") && argc==3 && !strcmp(argv[1],"connect")) {
        ble_devices_t devices;
        elm_ble_devices_get(&devices);
        unsigned index; char extra;
        if (sscanf(argv[2],"%u%c",&index,&extra)==1 && index<devices.count)
            err=elm_ble_connect_device(&devices.items[index]);
    } else if (!strcmp(cmd, "wifi") && argc == 2 && !strcmp(argv[1], "status")) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_dhcp_status_t dhcp = ESP_NETIF_DHCP_INIT;
        esp_netif_ip_info_t ip = {0};
        wifi_ap_record_t ap = {0};
        esp_err_t associated = esp_wifi_sta_get_ap_info(&ap);
        wifi_ps_type_t ps=WIFI_PS_MIN_MODEM;
        esp_err_t ps_err=esp_wifi_get_ps(&ps);
        esp_err_t dhcp_err = netif ? esp_netif_dhcpc_get_status(netif, &dhcp) : ESP_ERR_INVALID_STATE;
        if (netif) esp_netif_get_ip_info(netif, &ip);
        printf("wifi associated=%d rssi=%d channel=%u netif_up=%d dhcp=%d dhcp_result=%s ip_ready=%d ps=%d ps_result=%s\n",
               associated == ESP_OK, associated == ESP_OK ? ap.rssi : 0,
               associated == ESP_OK ? ap.primary : 0, netif && esp_netif_is_netif_up(netif),
               (int)dhcp, esp_err_to_name(dhcp_err), ip.ip.addr != 0,(int)ps,esp_err_to_name(ps_err));
        err = ESP_OK;
    } else if (!strcmp(cmd,"wifi") && argc==2 && !strcmp(argv[1],"retry")) {
        device_network_reconfigure(); err=ESP_OK;
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
    } else if (!strcmp(cmd, "voice") && argc == 2 && !strcmp(argv[1], "status")) {
        printf("wake_word=%s enabled=%d active=%d stack_min_free=%lu\n", VOICE_WAKE_WORD, voice_is_enabled(), voice_is_active(),
               (unsigned long)voice_stack_min_free()); err = ESP_OK;
        voice_log_diagnostics();
    } else if (!strcmp(cmd,"voice") && argc==2) {
        if (!strcmp(argv[1],"on") || !strcmp(argv[1],"off")) { voice_set_enabled(!strcmp(argv[1],"on")); err=ESP_OK; }
        else if (!strcmp(argv[1],"start")) err=voice_begin_conversation()?ESP_OK:ESP_ERR_INVALID_STATE;
        else if (!strcmp(argv[1],"end")) { voice_end_conversation(); err=ESP_OK; }
    } else if (!strcmp(cmd,"camera") && argc==2 && !strcmp(argv[1],"photo")) {
        err=camera_take_photo()?ESP_OK:ESP_ERR_INVALID_STATE;
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
    const char *help[] = {"Show redacted device status", "wifi status | wifi retry | wifi \"SSID\" \"password\" (local console echoes input)",
        "endpoint upload|ota https://...", "token <bearer> (local console echoes input)", "upload on|off|now",
        "record seal", "ota check | ota install parked-confirmed", "voice status|on|off|start|end", "camera on|off|photo", "bluetooth on|off|scan|list|connect INDEX|reconnect|disconnect|forget",
        "wifi-scan | wifi-scan status (redacted scan summary)",
        "media open | tab music|fm|voice | files | refresh | local INDEX | toggle | stop | prev | next | volume 0..100"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!DEVICE_CAMERA_AVAILABLE && !strcmp(names[i],"camera")) continue;
        esp_console_cmd_t entry = {.command = names[i], .help = help[i], .func = command};
        err = esp_console_cmd_register(&entry);
        if (err != ESP_OK) { repl->del(repl); return err; }
    }
    return esp_console_start_repl(repl);
}
