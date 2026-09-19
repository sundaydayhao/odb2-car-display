#include "fm_service.h"
#include "rda5807.h"
#include "audio_device.h"
#include "cloud_service.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>

typedef struct { fm_request_t request; unsigned generation; } command_t;
static QueueHandle_t s_commands;
static atomic_uint s_generation;
static unsigned s_running;
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static rda5807_t s_radio;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static fm_status_t s_status={.state=FM_ABSENT,.frequency=FM_MIN_FREQUENCY};
static bool s_online,s_owned;

static bool bus_write(void *ctx,const uint8_t *bytes,size_t length)
{ (void)ctx; return i2c_master_transmit(s_device,bytes,length,50)==ESP_OK; }
static bool bus_read(void *ctx,uint8_t *bytes,size_t length)
{ (void)ctx; return i2c_master_receive(s_device,bytes,length,50)==ESP_OK; }
static void bus_wait(void *ctx,unsigned ms)
{
    (void)ctx; vTaskDelay(pdMS_TO_TICKS(ms));
    /* Completion must use a volume change made while tuning/seeking was muted. */
    s_radio.volume=(uint8_t)audio_device_get_volume();
}
static bool interrupted(void *ctx)
{
    (void)ctx;
    return atomic_load(&s_generation)!=s_running || cloud_ota_busy() ||
        (s_owned && (audio_device_voice_waiting() || audio_device_alarm_waiting() || !audio_device_radio_waiting()));
}
void fm_service_status(fm_status_t *status)
{ if (status) { portENTER_CRITICAL(&s_lock); *status=s_status; portEXIT_CRITICAL(&s_lock); } }
static void publish(fm_state_t state,const char *detail,const rda_status_t *rx)
{
    /* A cancelled operation may already have moved the tuner. Use the chip's
     * next valid channel report rather than retaining a stale UI frequency. */
    if (rx && rx->frequency>=FM_MIN_FREQUENCY && rx->frequency<=FM_MAX_FREQUENCY)
        s_radio.frequency=rx->frequency;
    portENTER_CRITICAL(&s_lock);
    s_status.state=state; s_status.available=s_online;
    s_status.listening=s_online && s_owned && s_radio.listening;
    s_status.frequency=s_radio.frequency?s_radio.frequency:FM_MIN_FREQUENCY;
    s_status.volume=(uint8_t)audio_device_get_volume();
    s_status.rssi=rx?rx->rssi:0; s_status.stereo=rx && rx->stereo; s_status.station=rx && rx->station;
    if (detail) snprintf(s_status.detail,sizeof(s_status.detail),"%s",detail);
    ++s_status.revision;
    portEXIT_CRITICAL(&s_lock);
}
static void release_output(void)
{
    if (s_owned) audio_device_release(AUDIO_OWNER_RADIO);
    s_owned=false;
}
static void disconnected(const char *detail)
{
    rda5807_stop(&s_radio); /* Best effort if the module stopped acknowledging. */
    s_online=false; audio_device_radio_priority(false); release_output();
    publish(FM_ERROR,detail,NULL);
}
bool fm_service_request(const fm_request_t *request)
{
    if (!request || !s_commands || request->action>=FM_ACTION_COUNT) return false;
    if ((request->action==FM_TUNE || request->action==FM_LISTEN) &&
        (request->frequency<FM_MIN_FREQUENCY || request->frequency>FM_MAX_FREQUENCY)) return false;
    fm_status_t status; fm_service_status(&status);
    if (!status.available && request->action!=FM_RETRY && request->action!=FM_STOP) return false;
    if (request->action==FM_STOP) audio_device_radio_priority(false);
    command_t command={.request=*request,.generation=atomic_fetch_add(&s_generation,1)+1};
    return xQueueOverwrite(s_commands,&command)==pdTRUE;
}
static bool connect_radio(void)
{
    s_running=atomic_load(&s_generation);
    if (i2c_master_probe(s_bus,RDA5807_ADDRESS,30)!=ESP_OK) {
        s_online=false; publish(FM_ABSENT,"接收器未接入，请检查接线",NULL); return false;
    }
    rda_io_t io={.write=bus_write,.read=bus_read,.wait_ms=bus_wait,.cancelled=interrupted};
    rda_result_t result=rda5807_init(&s_radio,&io,audio_device_get_volume());
    if (result!=RDA_OK) { s_online=false; publish(FM_ERROR,"接收器初始化失败，请重试",NULL); return false; }
    s_online=true;
    rda_status_t rx;
    if (rda5807_read(&s_radio,&rx)!=RDA_OK) { disconnected("接收器通信失败，请检查接线"); return false; }
    publish(FM_READY,"接收器就绪，点击开始收听",&rx); return true;
}
static bool claim_output(void)
{
    if (s_owned) return true;
    if (audio_device_voice_waiting() || audio_device_alarm_waiting() || cloud_ota_busy()) return false;
    audio_device_radio_priority(true);
    for (unsigned i=0;i<40;++i) {
        if (atomic_load(&s_generation)!=s_running || !audio_device_radio_waiting() ||
            audio_device_voice_waiting() || audio_device_alarm_waiting() || cloud_ota_busy()) break;
        if (audio_device_acquire(AUDIO_OWNER_RADIO)) { s_owned=true; return true; }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (atomic_load(&s_generation)==s_running || audio_device_voice_waiting() || cloud_ota_busy())
        audio_device_radio_priority(false);
    return false;
}
static void execute(const command_t *command)
{
    s_running=command->generation;
    if (s_running!=atomic_load(&s_generation)) return;
    fm_action_t action=command->request.action;
    if (action==FM_RETRY) { if (!s_online) connect_radio(); return; }
    if (action==FM_STOP) {
        rda_result_t result=s_online?rda5807_stop(&s_radio):RDA_OK;
        release_output();
        if (result!=RDA_OK) disconnected("接收器通信失败，请检查接线");
        else publish(s_online?FM_READY:FM_ABSENT,"电台已停止",NULL);
        return;
    }
    if (!s_online) return;
    if ((action==FM_LISTEN || audio_device_radio_waiting()) && !claim_output()) {
        publish(FM_READY,"音频使用中，请稍后重试",NULL); return;
    }
    publish(action==FM_SEEK?FM_SEEKING:FM_TUNING,action==FM_SEEK?"正在查找下一电台":"正在调谐",NULL);
    rda_result_t result=action==FM_SEEK?rda5807_seek(&s_radio):rda5807_tune(&s_radio,command->request.frequency);
    if (result==RDA_OK || result==RDA_NO_STATION) {
        if (s_owned && !interrupted(NULL) && rda5807_audio(&s_radio,true,audio_device_get_volume())!=RDA_OK) result=RDA_IO;
        else if (s_owned && interrupted(NULL)) result=RDA_CANCELLED;
    }
    if (result==RDA_CANCELLED) {
        rda_result_t muted=rda5807_stop(&s_radio); release_output();
        if (audio_device_voice_waiting() || audio_device_alarm_waiting() || cloud_ota_busy()) audio_device_radio_priority(false);
        if (muted!=RDA_OK) disconnected("接收器通信失败，请检查接线");
        else publish(FM_READY,"操作已中断，可重新开始收听",NULL);
    } else if (result!=RDA_OK && result!=RDA_NO_STATION) {
        disconnected(result==RDA_TIMEOUT?"调谐或搜台超时，请重试":"接收器通信失败，请检查接线");
    } else {
        rda_status_t rx;
        if (rda5807_read(&s_radio,&rx)!=RDA_OK) { disconnected("接收器通信失败，请检查接线"); return; }
        publish(FM_READY,result==RDA_NO_STATION?"未找到其他电台":s_radio.listening?"正在收听电台":"频率已调谐，点击开始收听",&rx);
    }
}
static void radio_task(void *arg)
{
    (void)arg;
    int64_t retry_at=0,poll_at=0;
    command_t command;
    for (;;) {
        if (xQueueReceive(s_commands,&command,pdMS_TO_TICKS(25))==pdTRUE) {
            execute(&command);
            portENTER_CRITICAL(&s_lock);
            s_status.completed_id=command.request.id; ++s_status.revision;
            portEXIT_CRITICAL(&s_lock);
        }
        int64_t now=esp_timer_get_time();
        if (!s_online) {
            if (now>=retry_at && !cloud_ota_busy()) { connect_radio(); retry_at=esp_timer_get_time()+5000000; }
            continue;
        }
        if (s_owned && (audio_device_voice_waiting() || audio_device_alarm_waiting() || !audio_device_radio_waiting() || cloud_ota_busy())) {
            rda_result_t result=rda5807_stop(&s_radio);
            audio_device_radio_priority(false); release_output();
            if (result!=RDA_OK) { disconnected("接收器通信失败，请检查接线"); continue; }
            publish(FM_READY,"电台已暂停，可重新开始收听",NULL);
        }
        unsigned volume=audio_device_get_volume();
        if (volume!=s_radio.volume && rda5807_audio(&s_radio,s_radio.listening,volume)!=RDA_OK) {
            disconnected("接收器音量设置失败，请重试"); continue;
        }
        if (now>=poll_at) {
            rda_status_t rx;
            if (rda5807_read(&s_radio,&rx)!=RDA_OK) disconnected("接收器通信中断，请检查接线");
            else publish(FM_READY,NULL,&rx);
            poll_at=now+250000;
        }
    }
}
esp_err_t fm_service_start(void)
{
    if (CONFIG_BSP_I2C_NUM==0 || CONFIG_DEVICE_FM_SDA_GPIO==CONFIG_DEVICE_FM_SCL_GPIO ||
        CONFIG_DEVICE_FM_SDA_GPIO==CONFIG_DEVICE_GPS_RX_GPIO || CONFIG_DEVICE_FM_SCL_GPIO==CONFIG_DEVICE_GPS_RX_GPIO)
        return ESP_ERR_INVALID_ARG;
    i2c_master_bus_config_t bus={.i2c_port=I2C_NUM_0,.sda_io_num=CONFIG_DEVICE_FM_SDA_GPIO,
        .scl_io_num=CONFIG_DEVICE_FM_SCL_GPIO,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=true};
    esp_err_t err=i2c_new_master_bus(&bus,&s_bus);
    if (err!=ESP_OK) return err;
    i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=RDA5807_ADDRESS,.scl_speed_hz=100000};
    err=i2c_master_bus_add_device(s_bus,&device,&s_device);
    if (err==ESP_OK) { s_commands=xQueueCreate(1,sizeof(command_t)); if (!s_commands) err=ESP_ERR_NO_MEM; }
    if (err==ESP_OK && xTaskCreate(radio_task,"fm_radio",4096,NULL,2,NULL)!=pdPASS) err=ESP_ERR_NO_MEM;
    if (err!=ESP_OK) {
        if (s_commands) { vQueueDelete(s_commands); s_commands=NULL; }
        if (s_device) { i2c_master_bus_rm_device(s_device); s_device=NULL; }
        i2c_del_master_bus(s_bus); s_bus=NULL;
        return err;
    }
    ESP_LOGI("fm","RDA5807M: I2C0 SDA=GPIO%d SCL=GPIO%d 100kHz, analog L/R output",CONFIG_DEVICE_FM_SDA_GPIO,CONFIG_DEVICE_FM_SCL_GPIO);
    return ESP_OK;
}
