#include "obd2.h"

#include <stdio.h>
#include <string.h>
#include "elm327_ble.h"
#include "obd2_decode.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG="obd2";
static portMUX_TYPE s_cache_lock=portMUX_INITIALIZER_UNLOCKED;
static obd_cache_t s_cache;
static uint32_t s_cache_generation;
static bool s_started;
static elm_response_t s_response;

static int64_t now_ms(void) { return esp_timer_get_time()/1000; }

static void state(obd2_link_state_t next, bool clear)
{
    portENTER_CRITICAL(&s_cache_lock);
    if (clear) memset(&s_cache,0,sizeof(s_cache));
    s_cache.data.link_state=next;
    portEXIT_CRITICAL(&s_cache_lock);
}

static esp_err_t command(const char *cmd, uint32_t timeout)
{
    esp_err_t err=elm_ble_command(cmd,&s_response,timeout);
    if (err!=ESP_OK) ESP_LOGW(TAG,"Command %s failed: %s",cmd,esp_err_to_name(err));
    return err;
}

static esp_err_t configure_adapter(void)
{
    state(OBD_LINK_INITIALIZING,true);
    if (command("ATZ",4000)!=ESP_OK) return ESP_FAIL;
    if (command("ATI",2000)!=ESP_OK || !elm_response_is_adapter(&s_response)) {
        ESP_LOGW(TAG,"The selected UART device did not identify as ELM327/STN");
        return ESP_ERR_NOT_SUPPORTED;
    }
    static const char *commands[]={"ATE0","ATL0","ATS1","ATH1","ATCAF1","ATCFC1","ATAT1","ATST64","ATSP0"};
    for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
        if (command(commands[i],2000)!=ESP_OK || !elm_response_is_ok(&s_response)) {
            ESP_LOGW(TAG,"Required adapter command rejected: %s",commands[i]);
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    ESP_LOGI(TAG,"Adapter initialized; discovering vehicle PID capabilities");
    return ESP_OK;
}

static bool discover_vehicle(bool supported[OBD_PARAMETER_COUNT], uint32_t *ecu)
{
    memset(supported,0,OBD_PARAMETER_COUNT*sizeof(*supported));
    elm_pid_reply_t reply;
    /* Auto protocol search can take seconds; it has a separate bounded deadline. */
    if (command("0100",CONFIG_OBD2_ELM327_SEARCH_TIMEOUT_MS)!=ESP_OK || !elm_decode_pid(&s_response,0,ELM_ECU_ANY,&reply) || reply.length<4 || !reply.ecu) return false;
    *ecu=reply.ecu;
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) supported[i]=elm_supported_pid(&reply,0,obd_parameters[i].pid);
    bool next=elm_supported_pid(&reply,0,0x20);
    if (command("ATDPN",2000)!=ESP_OK || !elm_response_is_can_protocol(&s_response)) {
        ESP_LOGW(TAG,"Vehicle is not using a supported ISO 15765-4 protocol");
        return false;
    }
    for (unsigned base=0x20;next && base<=0x40;base+=0x20) {
        char request[8]; snprintf(request,sizeof(request),"01%02X",base);
        if (command(request,CONFIG_OBD2_RESPONSE_TIMEOUT_MS)!=ESP_OK) return false;
        if (!elm_decode_pid(&s_response,base,*ecu,&reply) || reply.length<4) break;
        for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) supported[i] |= elm_supported_pid(&reply,base,obd_parameters[i].pid);
        next=elm_supported_pid(&reply,base,base+0x20);
    }
    unsigned count=0;
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) count+=supported[i];
    ESP_LOGI(TAG,"Vehicle ECU=%08lX, %u dashboard parameters supported",(unsigned long)*ecu,count);
    return count>0;
}

static void worker(void *arg)
{
    (void)arg;
    uint32_t generation=0, ecu=ELM_ECU_ANY;
    bool initialized=false, vehicle=false, supported[OBD_PARAMETER_COUNT]={0};
    unsigned turn=0, slow=2, failures=0;
    int64_t probe_after=0;
    for (;;) {
        elm_ble_status_t link=elm_ble_status();
        if (link.state!=ELM_BLE_READY) {
            initialized=false; vehicle=false;
            state(!link.enabled || !link.available ? OBD_LINK_WAITING : link.state==ELM_BLE_SCANNING ? OBD_LINK_SCANNING : OBD_LINK_CONNECTING,true);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (generation!=link.generation) {
            generation=link.generation; initialized=false; vehicle=false; probe_after=0;
            portENTER_CRITICAL(&s_cache_lock);
            memset(&s_cache,0,sizeof(s_cache));
            s_cache_generation=generation;
            s_cache.data.link_state=OBD_LINK_INITIALIZING;
            portEXIT_CRITICAL(&s_cache_lock);
        }
        if (!initialized) {
            if (configure_adapter()!=ESP_OK) {
                if (elm_ble_status().state==ELM_BLE_READY) elm_ble_reset_link();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            initialized=true;
        }
        if (!vehicle) {
            state(OBD_LINK_WAITING,true);
            if (now_ms()<probe_after) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
            vehicle=discover_vehicle(supported,&ecu);
            if (!vehicle) { probe_after=now_ms()+3000; continue; }
            failures=0; turn=0; slow=2;
            state(OBD_LINK_ACTIVE,false);
        }
        /* Two fast readings for each rotating secondary reading, without starving slow PIDs. */
        unsigned index;
        if (turn%3<2) index=turn%3;
        else { index=slow++; if (slow==OBD_PARAMETER_COUNT) slow=2; }
        ++turn;
        if (!supported[index]) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        char request[8]; snprintf(request,sizeof(request),"01%02X",obd_parameters[index].pid);
        esp_err_t err=command(request,CONFIG_OBD2_RESPONSE_TIMEOUT_MS);
        elm_pid_reply_t reply;
        bool valid=err==ESP_OK && elm_decode_pid(&s_response,obd_parameters[index].pid,ecu,&reply);
        portENTER_CRITICAL(&s_cache_lock);
        if (valid) valid=obd_cache_apply(&s_cache,index,&reply,now_ms());
        if (!valid) obd_cache_invalidate(&s_cache,index);
        obd_cache_expire(&s_cache,now_ms());
        portEXIT_CRITICAL(&s_cache_lock);
        if (valid) failures=0; else ++failures;
        if (failures>=12) { vehicle=false; probe_after=now_ms()+3000; state(OBD_LINK_WAITING,true); }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_OBD2_POLL_INTERVAL_MS));
    }
}

esp_err_t obd2_init(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    state(OBD_LINK_SCANNING,true);
    esp_err_t err=elm_ble_init();
    if (err!=ESP_OK) return err;
    if (xTaskCreate(worker,"elm_poll",8192,NULL,3,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    s_started=true;
    return ESP_OK;
}

esp_err_t obd2_read_vehicle_data(obd2_vehicle_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    elm_ble_status_t link=elm_ble_status();
    portENTER_CRITICAL(&s_cache_lock);
    obd_cache_expire(&s_cache,now_ms());
    *out=s_cache.data;
    bool same_session=s_cache_generation==link.generation;
    portEXIT_CRITICAL(&s_cache_lock);
    if (link.state!=ELM_BLE_READY || !same_session) {
        memset(out,0,sizeof(*out));
        out->link_state=!link.enabled || !link.available ? OBD_LINK_WAITING : link.state==ELM_BLE_SCANNING ? OBD_LINK_SCANNING : link.state==ELM_BLE_READY ? OBD_LINK_INITIALIZING : OBD_LINK_CONNECTING;
    }
    return ESP_OK;
}
