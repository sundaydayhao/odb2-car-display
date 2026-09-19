#include "voice_tools.h"
#include "esp_mcp_tool.h"
#include "esp_mcp_property.h"
#include "cJSON.h"
#include "vehicle_metrics.h"
#include "obd2.h"
#include "media_service.h"
#include "media_library.h"
#include "fm_service.h"
#include "alarm_service.h"
#include "audio_device.h"
#include "camera_service.h"
#include "device_services.h"
#include "cloud_service.h"
#include "ui.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <strings.h>

static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static int s_pending;
static int64_t s_pending_until;
static media_request_t s_media;
static fm_request_t s_radio;
bool voice_tools_pending(void)
{
    portENTER_CRITICAL(&s_lock);
    if(esp_timer_get_time()>s_pending_until)s_pending=0;
    bool pending=s_pending!=0;portEXIT_CRITICAL(&s_lock);return pending;
}
void voice_tools_cancel(void){portENTER_CRITICAL(&s_lock);s_pending=0;portEXIT_CRITICAL(&s_lock);}
void voice_tools_run_pending(void)
{
    media_request_t media;fm_request_t radio;int kind;
    portENTER_CRITICAL(&s_lock);kind=esp_timer_get_time()<=s_pending_until?s_pending:0;
    media=s_media;radio=s_radio;s_pending=0;portEXIT_CRITICAL(&s_lock);
    if(cloud_ota_busy())return;
    if(kind==1){media_service_request(&media);ui_open_av(AV_MUSIC);}
    else if(kind==2){fm_service_request(&radio);ui_open_av(AV_FM);}
}
static void defer_media(const media_request_t *request)
{portENTER_CRITICAL(&s_lock);s_media=*request;s_pending=1;s_pending_until=esp_timer_get_time()+30000000;portEXIT_CRITICAL(&s_lock);}
static void defer_radio(const fm_request_t *request)
{portENTER_CRITICAL(&s_lock);s_radio=*request;s_pending=2;s_pending_until=esp_timer_get_time()+30000000;portEXIT_CRITICAL(&s_lock);}
static const char *str(const esp_mcp_property_list_t *p,const char *key)
{const char *v=esp_mcp_property_list_get_property_string(p,key);return v?v:"";}
static int num(const esp_mcp_property_list_t *p,const char *key){return esp_mcp_property_list_get_property_int(p,key);}
static esp_err_t answer(esp_mcp_tool_result_t *r,bool ok,const char *message)
{esp_mcp_tool_result_set_is_error(r,!ok);return esp_mcp_tool_result_add_text(r,message);}
static esp_err_t queued(esp_mcp_tool_result_t *r,bool ok,const char *message)
{return answer(r,ok,ok?message:"Device rejected the operation; it was not queued or completed. Retry after checking device status.");}
static esp_err_t json_answer(esp_mcp_tool_result_t *r,cJSON *data)
{
    if(!data)return ESP_ERR_NO_MEM;
    char *text=cJSON_PrintUnformatted(data);cJSON_Delete(data);
    if(!text)return ESP_ERR_NO_MEM;
    esp_err_t e=esp_mcp_tool_result_add_text(r,text);cJSON_free(text);return e;
}
static esp_err_t status_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    (void)p;media_status_t m;media_service_status(&m);fm_status_t f;fm_service_status(&f);
    cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(o,"volume",audio_device_get_volume());cJSON_AddBoolToObject(o,"network",device_network_ready());
    cJSON_AddBoolToObject(o,"clock_ready",device_clock_ready());cJSON_AddNumberToObject(o,"music_state",m.state);
    cJSON_AddStringToObject(o,"music_title",m.title);cJSON_AddStringToObject(o,"music_detail",m.detail);
    cJSON_AddBoolToObject(o,"online_music",m.online);cJSON_AddBoolToObject(o,"radio_available",f.available);
    cJSON_AddBoolToObject(o,"radio_listening",f.listening);cJSON_AddNumberToObject(o,"radio_mhz",f.frequency/10.0);
    cJSON_AddBoolToObject(o,"camera_enabled",camera_is_enabled());cJSON_AddBoolToObject(o,"alarm_ringing",audio_device_alarm_waiting());
    return json_answer(r,o);
}
static esp_err_t vehicle_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    obd2_vehicle_data_t data;if(obd2_read_vehicle_data(&data)!=ESP_OK)return answer(r,false,"Vehicle data unavailable.");
    vehicle_metric_t fields[VEHICLE_METRIC_COUNT];size_t count=vehicle_metrics(&data,fields,VEHICLE_METRIC_COUNT);
    const char *key=str(p,"metric");bool all=!strcmp(key,"all") || !*key,found=false;
    cJSON *o=cJSON_CreateObject(),*values=cJSON_CreateObject();if(!o || !values){cJSON_Delete(o);cJSON_Delete(values);return ESP_ERR_NO_MEM;}
    cJSON_AddBoolToObject(o,"connected",data.link_state==OBD_LINK_ACTIVE);
    cJSON_AddStringToObject(o,"vehicle","2019 Toyota Camry 2.5L gasoline");
    cJSON_AddStringToObject(o,"unavailable_meaning","Not supported, not received, disconnected or expired. Never substitute zero or infer a reading.");
    cJSON_AddItemToObject(o,"metrics",values);
    for(size_t i=0;i<count;++i)if(all || !strcmp(key,fields[i].key)){
        found=true;vehicle_metric_t *f=&fields[i];cJSON *v=cJSON_AddObjectToObject(values,f->key);
        cJSON_AddBoolToObject(v,"available",f->valid);
        if(f->valid)cJSON_AddNumberToObject(v,"value",f->value);else cJSON_AddNullToObject(v,"value");
        cJSON_AddStringToObject(v,"unit",f->unit);cJSON_AddBoolToObject(v,"calculated",f->calculated);
        if(!all && esp_mcp_property_list_get_property_bool(p,"show"))ui_open_vehicle_page(f->page);
    }
    if(!found){cJSON_Delete(o);return answer(r,false,"Unknown metric. Call with metric=all to list supported keys. Tire temperatures, transmission temperature and odometer are unavailable.");}
    return json_answer(r,o);
}
static esp_err_t volume_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    int volume=num(p,"volume");if(volume<0 || volume>100)return answer(r,false,"Volume must be 0..100.");
    media_request_t cmd={.action=MEDIA_VOLUME,.value=(unsigned)volume};
    return queued(r,media_service_request(&cmd),"Volume request queued; applies to voice, music, FM and alarms. 0 mutes all sounds.");
}
static esp_err_t adjust_volume_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    media_request_t cmd={.action=MEDIA_VOLUME_DELTA,.delta=num(p,"delta")};
    return queued(r,media_service_request(&cmd),"Relative volume request queued; result is clamped to 0..100.");
}
static esp_err_t radio_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    const char *a=str(p,"action");fm_status_t status;fm_service_status(&status);
    if(!strcmp(a,"status")){
        cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
        cJSON_AddBoolToObject(o,"available",status.available);cJSON_AddBoolToObject(o,"listening",status.listening);
        cJSON_AddNumberToObject(o,"frequency_mhz",status.frequency/10.0);cJSON_AddNumberToObject(o,"rssi",status.rssi);
        cJSON_AddBoolToObject(o,"stereo",status.stereo);cJSON_AddStringToObject(o,"detail",status.detail);return json_answer(r,o);
    }
    fm_request_t cmd={.frequency=status.frequency};
    if(!strcmp(a,"stop")){voice_tools_cancel();cmd.action=FM_STOP;return queued(r,fm_service_request(&cmd),"Radio stop requested.");}
    if(!strcmp(a,"retry")){cmd.action=FM_RETRY;return queued(r,fm_service_request(&cmd),"Receiver probe requested; read status to confirm.");}
    if(!status.available)return answer(r,false,"RDA5807M receiver is not connected. Cannot receive or play FM.");
    if(!strcmp(a,"listen"))cmd.action=FM_LISTEN;else if(!strcmp(a,"seek"))cmd.action=FM_SEEK;
    else return answer(r,false,"action must be listen, seek, stop, retry or status.");
    int f=num(p,"frequency_tenths");if(f){if(f<875 || f>1080)return answer(r,false,"Frequency must be 875..1080 in 0.1 MHz units.");cmd.frequency=(uint16_t)f;}
    if(audio_device_alarm_waiting())return answer(r,false,"Dismiss the ringing alarm before starting radio.");
    defer_radio(&cmd);return answer(r,true,"Radio operation scheduled after this spoken reply; not yet playing. FM audio uses the module analog L/R output.");
}
static esp_err_t list_music_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    av_library_status_t lib;media_library_status(&lib);int offset=num(p,"offset");
    if(offset<0 || offset>=AV_FILE_LIMIT)return answer(r,false,"Invalid list offset.");
    cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(o,"sd_ready",lib.storage);cJSON_AddNumberToObject(o,"total",lib.music_count);
    cJSON_AddNumberToObject(o,"revision",lib.revision);cJSON *items=cJSON_AddArrayToObject(o,"files");
    for(unsigned i=(unsigned)offset;i<lib.music_count && i<(unsigned)offset+10;++i){
        av_file_t file;if(media_library_files(AV_FILES_MUSIC,i,&file,1,lib.revision)!=1)break;
        cJSON *item=cJSON_CreateObject();cJSON_AddNumberToObject(item,"index",i);cJSON_AddStringToObject(item,"name",file.name);cJSON_AddItemToArray(items,item);
    }
    return json_answer(r,o);
}
static bool contains(const char *name,const char *query)
{for(;*name;++name)if(!strncasecmp(name,query,strlen(query)))return true;return false;}
static esp_err_t local_music_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    av_library_status_t lib;media_library_status(&lib);
    if(!lib.storage || !lib.music_count)return answer(r,false,"SD card has no playable MP3 files.");
    const char *name=str(p,"name");int index=num(p,"index");
    if(*name){unsigned matches=0;for(unsigned i=0;i<lib.music_count;++i){av_file_t file;
        if(media_library_files(AV_FILES_MUSIC,i,&file,1,lib.revision)!=1)return answer(r,false,"SD file list changed, retry listing.");
        if(contains(file.name,name)){index=(int)i;++matches;}}
        if(matches!=1)return answer(r,false,matches?"Multiple matching tracks; list files and ask which one.":"No matching local track.");
    } else if(index<0)index=0;
    if(index<0 || index>=lib.music_count)return answer(r,false,"Track index is outside the current SD list.");
    if(audio_device_alarm_waiting())return answer(r,false,"Dismiss the ringing alarm before starting music.");
    media_request_t cmd={.action=MEDIA_SELECT_LOCAL,.value=(unsigned)index,.revision=lib.revision};defer_media(&cmd);
    return answer(r,true,"Local music is queued to start after this spoken reply. It has not started yet.");
}
static esp_err_t online_music_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    if(!device_network_ready())return answer(r,false,"Online music needs Wi-Fi.");
    if(audio_device_alarm_waiting())return answer(r,false,"Dismiss the ringing alarm first.");
    media_request_t cmd={.action=MEDIA_SELECT_ONLINE};const char *url=str(p,"url"),*title=str(p,"title");
    if(!*url){if(!media_service_online_saved(cmd.url,sizeof(cmd.url),cmd.title,sizeof(cmd.title)))return answer(r,false,"No saved online source. Ask the user for a direct HTTP/HTTPS MP3 URL or music API; do not invent URLs or claim a song was found.");}
    else {
        if(!media_url_valid(url) || strlen(title)>=sizeof(cmd.title))return answer(r,false,"Invalid or overlong MP3 URL/title.");
        snprintf(cmd.url,sizeof(cmd.url),"%s",url);snprintf(cmd.title,sizeof(cmd.title),"%s",title);
    }
    defer_media(&cmd);return answer(r,true,"Online MP3 is queued after this spoken reply. Stream connection and playback are not yet confirmed; no music search/catalog service is configured.");
}
static esp_err_t music_control_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    const char *a=str(p,"action");media_request_t cmd={0};
    if(!strcmp(a,"stop"))cmd.action=MEDIA_STOP;else if(!strcmp(a,"pause"))cmd.action=MEDIA_PAUSE;
    else if(!strcmp(a,"resume"))cmd.action=MEDIA_RESUME;else if(!strcmp(a,"next"))cmd.action=MEDIA_NEXT;
    else if(!strcmp(a,"previous"))cmd.action=MEDIA_PREVIOUS;else return answer(r,false,"Unknown music action.");
    if(cmd.action==MEDIA_STOP || cmd.action==MEDIA_PAUSE){voice_tools_cancel();return queued(r,media_service_request(&cmd),"Music stop/pause requested.");}
    if(audio_device_alarm_waiting())return answer(r,false,"Dismiss the ringing alarm first.");
    defer_media(&cmd);return answer(r,true,"Music operation queued after this reply. Next/previous selects from the local SD list.");
}
static esp_err_t camera_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    const char *a=str(p,"action");
    if(!strcmp(a,"status")){device_status_t s;device_status_get(&s);av_library_status_t lib;media_library_status(&lib);
        cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
        cJSON_AddBoolToObject(o,"enabled",camera_is_enabled());cJSON_AddStringToObject(o,"camera",s.detail[SERVICE_CAMERA]);
        const char *states[]={"idle","queued","saving","saved","failed_or_cancelled"};unsigned state=camera_photo_state();
        cJSON_AddStringToObject(o,"photo_state",states[state<=4?state:4]);
        cJSON_AddStringToObject(o,"photo",state==3?lib.photo_detail:"");cJSON_AddBoolToObject(o,"sd_ready",lib.storage);return json_answer(r,o);}
    if(!strcmp(a,"close")){camera_set_enabled(false);return answer(r,true,"Camera close requested.");}
    if(strcmp(a,"open") && strcmp(a,"show"))return answer(r,false,"action must be open, show, close or status.");
    if(!ui_open_av(AV_CAMERA))return answer(r,false,"Camera page is unavailable.");
    if(!strcmp(a,"open"))camera_set_enabled(true);
    return answer(r,true,"Camera page opened; open starts local preview, show only displays controls. Check status for hardware readiness.");
}
static esp_err_t photo_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    (void)p;av_library_status_t lib;media_library_status(&lib);
    if(!lib.storage || lib.photo_count>=AV_FILE_LIMIT)return answer(r,false,"Photo needs an SD card with fewer than 128 photos.");
    if(!ui_open_av(AV_CAMERA))return answer(r,false,"Camera page unavailable.");
    camera_set_enabled(true);
    return queued(r,camera_take_photo(),"Photo capture requested, not yet saved. Check self.camera.control status for completion; the image stays on the local SD card.");
}
static esp_err_t list_alarms_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    (void)p;alarm_status_t status;alarm_service_status(&status);cJSON *o=cJSON_CreateObject();if(!o)return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(o,"clock_ready",status.clock_ready);cJSON_AddBoolToObject(o,"ringing",status.ringing);
    time_t local=time(NULL)+28800;struct tm tm;gmtime_r(&local,&tm);char current[32];strftime(current,sizeof(current),"%Y-%m-%d %H:%M:%S",&tm);
    cJSON_AddStringToObject(o,"beijing_time",status.clock_ready?current:"unavailable");cJSON_AddStringToObject(o,"timezone","UTC+08:00");
    cJSON *items=cJSON_AddArrayToObject(o,"alarms");for(unsigned i=0;i<ALARM_LIMIT;++i){alarm_item_t *a=&status.store.items[i];if(!a->id)continue;
        cJSON *v=cJSON_CreateObject();cJSON_AddNumberToObject(v,"id",a->id);cJSON_AddNumberToObject(v,"due_unix",a->due);
        cJSON_AddNumberToObject(v,"hour",a->hour);cJSON_AddNumberToObject(v,"minute",a->minute);
        cJSON_AddBoolToObject(v,"daily",a->daily);cJSON_AddBoolToObject(v,"enabled",a->enabled);cJSON_AddStringToObject(v,"label",a->label);cJSON_AddItemToArray(items,v);}
    return json_answer(r,o);
}
static esp_err_t set_alarm_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{
    int hour=num(p,"hour"),minute=num(p,"minute"),delay=num(p,"delay_minutes");bool daily=esp_mcp_property_list_get_property_bool(p,"daily");
    if(hour<0 || hour>23 || minute<0 || minute>59 || delay<0 || delay>10080)return answer(r,false,"Invalid alarm time.");
    int id=alarm_service_set(hour,minute,daily,delay,str(p,"label"));
    if(id<0)return answer(r,false,"Alarm was not saved: clock not synchronized, four active alarms already exist, label too long or storage failure.");
    ui_open_alarms();char text[160];snprintf(text,sizeof(text),"Alarm saved, id=%d. Uses Beijing time (UTC+8); the device must remain powered. Global volume 0 makes the alarm visual only.",id);return answer(r,true,text);
}
static esp_err_t delete_alarm_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{return answer(r,alarm_service_delete((uint32_t)num(p,"id")),"Alarm deletion result; false/error means no alarm was deleted.");}
static esp_err_t dismiss_alarm_tool(const esp_mcp_property_list_t *p,esp_mcp_tool_result_t *r)
{(void)p;alarm_service_dismiss();return answer(r,true,"Ringing alarm dismissal requested.");}

static bool s_property_failed;
static esp_mcp_property_t *property(esp_mcp_property_t *p){if(!p)s_property_failed=true;return p;}
static esp_err_t add(esp_mcp_t *engine,const char *name,const char *description,esp_mcp_tool_callback_ex_t callback,
                     esp_mcp_property_t *a,esp_mcp_property_t *b,esp_mcp_property_t *c,esp_mcp_property_t *d,esp_mcp_property_t *e)
{
    esp_mcp_tool_t *t=esp_mcp_tool_create_ex(name,NULL,description,callback);esp_mcp_property_t *props[]={a,b,c,d,e};
    if(!t || s_property_failed){for(unsigned i=0;i<5;++i)if(props[i])esp_mcp_property_destroy(props[i]);if(t)esp_mcp_tool_destroy(t);return ESP_ERR_NO_MEM;}
    for(unsigned i=0;i<5;++i)if(props[i] && esp_mcp_tool_add_property(t,props[i])!=ESP_OK){
        for(unsigned j=i;j<5;++j)if(props[j])esp_mcp_property_destroy(props[j]);
        esp_mcp_tool_destroy(t);return ESP_ERR_NO_MEM;}
    esp_err_t err=esp_mcp_add_tool(engine,t);if(err!=ESP_OK)esp_mcp_tool_destroy(t);return err;
}
#define S(k,v) property(esp_mcp_property_create_with_string(k,v))
#define I(k,v,lo,hi) property(esp_mcp_property_create_with_int_and_range(k,v,lo,hi))
#define B(k,v) property(esp_mcp_property_create_with_bool(k,v))
#define R(k,lo,hi) property(esp_mcp_property_create_with_range(k,lo,hi))
#define ADD(...) do {esp_err_t e=add(engine,__VA_ARGS__);if(e!=ESP_OK)return e;}while(0)
esp_err_t voice_tools_register(esp_mcp_t *engine)
{
    s_property_failed=false;
    ADD("self.get_device_status","Get actual device volume, music, FM, network, camera and ringing state. Queued commands are not completed playback.",status_tool,NULL,NULL,NULL,NULL,NULL);
    ADD("self.vehicle.get_metrics","Read fresh vehicle telemetry. For 水温 use coolant_temperature, 车速 speed, 转速 rpm, 油量 fuel_level, 电压 control_voltage, 油温 oil_temperature. metric=all lists other keys. Never invent unavailable readings. show=true displays its dashboard page.",vehicle_tool,S("metric","all"),B("show",false),NULL,NULL,NULL);
    ADD("self.audio_speaker.set_volume","Set global sound volume 0..100. Zero mutes voice, music, FM and alarm sounds.",volume_tool,R("volume",0,100),NULL,NULL,NULL,NULL);
    ADD("self.audio_speaker.adjust_volume","Increase or decrease global volume by delta. Use positive for louder, negative for quieter; default step 10.",adjust_volume_tool,I("delta",10,-100,100),NULL,NULL,NULL,NULL);
    ADD("self.radio.control","Control RDA5807M FM: action=listen,seek,stop,retry,status. frequency_tenths=987 means 98.7 MHz; 0 keeps current. Hardware receiver required. Reply before playback starts.",radio_tool,S("action","listen"),I("frequency_tenths",0,0,1080),NULL,NULL,NULL);
    ADD("self.music.list_local","List SD music names and zero-based indices, 10 per page. Use before selecting an ambiguous song.",list_music_tool,I("offset",0,0,127),NULL,NULL,NULL,NULL);
    ADD("self.music.play_local","Play local SD MP3 by unique filename substring or zero-based index. Empty name and index=-1 plays first track. Starts after spoken reply.",local_music_tool,S("name",""),I("index",-1,-1,127),NULL,NULL,NULL);
    ADD("self.music.play_online","Play a user-supplied direct HTTP/HTTPS MP3 URL, or saved source if url is empty. No song-search service is configured. Never invent a URL or claim to have found music. No DRM/HLS/playlist pages. Starts after reply.",online_music_tool,S("url",""),S("title",""),NULL,NULL,NULL);
    ADD("self.music.control","action=pause,stop,resume,next,previous. Resume and track changes begin after spoken reply; next/previous use the SD list.",music_control_tool,S("action","pause"),NULL,NULL,NULL,NULL);
    ADD("self.camera.control","action=open (preview), show (controls only), close, status. Camera UI is in media center. Images stay local; no remote visual analysis.",camera_tool,S("action","show"),NULL,NULL,NULL,NULL);
    ADD("self.camera.take_photo","Request one local photo saved to SD. Opens preview if needed. Queued is not saved; use camera status to check. No image upload.",photo_tool,NULL,NULL,NULL,NULL,NULL);
    ADD("self.alarm.list","Get synchronized Beijing time (UTC+8), saved alarms and ringing status. Query current time before interpreting relative clock requests.",list_alarms_tool,NULL,NULL,NULL,NULL,NULL);
    ADD("self.alarm.set","Save an alarm at the next hour/minute (Beijing time), daily if daily=true. For timers use delay_minutes=1..10080 and daily=false. Ask if time is ambiguous. Up to 4 active alarms. Device must remain powered. Label max 47 UTF-8 bytes.",set_alarm_tool,I("hour",0,0,23),I("minute",0,0,59),B("daily",false),I("delay_minutes",0,0,10080),S("label","提醒"));
    ADD("self.alarm.delete","Delete a saved alarm by id from self.alarm.list.",delete_alarm_tool,R("id",1,0x7ffffffe),NULL,NULL,NULL,NULL);
    ADD("self.alarm.dismiss","Stop the currently ringing alarm; keep daily schedules.",dismiss_alarm_tool,NULL,NULL,NULL,NULL,NULL);
    return ESP_OK;
}
