#include "alarm_service.h"
#include "audio_device.h"
#include "device_services.h"
#include "cloud_service.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <time.h>
#include <stdio.h>
#include <stdatomic.h>
static SemaphoreHandle_t s_lock;
static alarm_status_t s_status={.store={.version=1,.next_id=1}};
static atomic_bool s_dismiss;
static bool s_owned;
static int64_t s_until,s_check;
static bool save(const alarm_store_t *store)
{
    nvs_handle_t nvs;esp_err_t e=nvs_open("alarms",NVS_READWRITE,&nvs);
    if(e!=ESP_OK)return false;
    e=nvs_set_blob(nvs,"v1",store,sizeof(*store));if(e==ESP_OK)e=nvs_commit(nvs);nvs_close(nvs);return e==ESP_OK;
}
esp_err_t alarm_service_start(void)
{
    s_lock=xSemaphoreCreateMutex();if(!s_lock)return ESP_ERR_NO_MEM;
    nvs_handle_t nvs;
    if(nvs_open("alarms",NVS_READONLY,&nvs)==ESP_OK){
        alarm_store_t saved;size_t size=sizeof(saved);
        if(nvs_get_blob(nvs,"v1",&saved,&size)==ESP_OK && size==sizeof(saved) && alarm_store_valid(&saved))s_status.store=saved;
        nvs_close(nvs);
    }
    return ESP_OK;
}
void alarm_service_status(alarm_status_t *out)
{
    if(!out)return;
    if(s_lock)xSemaphoreTake(s_lock,portMAX_DELAY);
    *out=s_status;out->clock_ready=device_clock_ready();
    if(s_lock)xSemaphoreGive(s_lock);
}
bool alarm_service_try_status(alarm_status_t *out)
{
    if (!out) return false;
    if (s_lock && xSemaphoreTake(s_lock, 0) != pdTRUE) return false;
    *out=s_status;out->clock_ready=device_clock_ready();
    if (s_lock) xSemaphoreGive(s_lock);
    return true;
}
int alarm_service_set(unsigned hour,unsigned minute,bool daily,unsigned delay,const char *label)
{
    if(!s_lock || !device_clock_ready())return -1;
    xSemaphoreTake(s_lock,portMAX_DELAY);alarm_store_t next=s_status.store;
    int id=alarm_put(&next,(int64_t)time(NULL),hour,minute,daily,delay,label);
    if(id>0 && save(&next)){s_status.store=next;++s_status.revision;}else id=-1;
    xSemaphoreGive(s_lock);return id;
}
bool alarm_service_delete(uint32_t id)
{
    if(!s_lock)return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);alarm_store_t next=s_status.store;
    bool ok=alarm_remove(&next,id) && save(&next);
    if(ok){s_status.store=next;++s_status.revision;if(s_status.ringing_id==id)atomic_store(&s_dismiss,true);}
    xSemaphoreGive(s_lock);return ok;
}
void alarm_service_dismiss(void){atomic_store(&s_dismiss,true);}
void alarm_service_tick(void)
{
    if(!s_lock)return;
    int64_t now=esp_timer_get_time();
    if(now<s_check && !atomic_load(&s_dismiss))return;
    s_check=now+200000;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(atomic_exchange(&s_dismiss,false) || (s_status.ringing && (now>=s_until || cloud_ota_busy()))){
        s_status.ringing=false;s_status.ringing_id=0;++s_status.revision;
    }
    if(!s_status.ringing && !cloud_ota_busy() && device_clock_ready()){
        bool changed=false;uint32_t id=alarm_due(&s_status.store,(int64_t)time(NULL),&changed);
        if(changed){bool ok=save(&s_status.store);snprintf(s_status.detail,sizeof(s_status.detail),"%s",ok?"":"闹钟状态保存失败");++s_status.revision;}
        if(id){s_status.ringing=true;s_status.ringing_id=id;s_until=now+120000000;++s_status.revision;}
    }
    audio_device_alarm_priority(s_status.ringing);
    xSemaphoreGive(s_lock);
}
void alarm_service_audio(void)
{
    bool play=audio_device_alarm_waiting() && !audio_device_voice_waiting() && !cloud_ota_busy();
    if(!play){if(s_owned)audio_device_release(AUDIO_OWNER_ALARM);s_owned=false;return;}
    if(!s_owned)s_owned=audio_device_acquire(AUDIO_OWNER_ALARM);
    if(!s_owned)return;
    const int16_t wave[16]={0,765,1414,1848,2000,1848,1414,765,0,-765,-1414,-1848,-2000,-1848,-1414,-765};
    int16_t pcm[320];bool tone=esp_timer_get_time()%1000000<300000;
    for(unsigned i=0;i<320;++i)pcm[i]=tone?wave[i%16]:0;
    if(!audio_device_configure(AUDIO_OWNER_ALARM,false,16000) || !audio_device_write(AUDIO_OWNER_ALARM,pcm,sizeof(pcm))){
        audio_device_release(AUDIO_OWNER_ALARM);s_owned=false;
    }
}
