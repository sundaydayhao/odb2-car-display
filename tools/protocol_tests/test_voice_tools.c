/* Executes production MCP registration/callbacks. Only SDK transport and device
 * I/O are stubbed; this does not prove cloud intent recognition or hardware I/O. */
#include "voice_tools.h"
#include "media_service.h"
#include "media_library.h"
#include "fm_service.h"
#include "alarm_service.h"
#include "device_services.h"
#include "obd2.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct esp_mcp_property_s {char *name;cJSON *value;};
struct esp_mcp_property_list_s {cJSON *args;};
struct esp_mcp_tool_result_s {char *text;bool error;};
struct esp_mcp_tool_s {char *name;esp_mcp_tool_callback_ex_t cb;esp_mcp_property_t *props[5];unsigned count;};
struct esp_mcp_s {esp_mcp_tool_t *tools[20];unsigned count;};
static unsigned alloc_count,fail_alloc;
static esp_mcp_property_t *prop(const char *name,cJSON *value){
    if(++alloc_count==fail_alloc){cJSON_Delete(value);return NULL;}
    esp_mcp_property_t *p=calloc(1,sizeof(*p));p->name=strdup(name);p->value=value;return p;
}
esp_mcp_property_t *esp_mcp_property_create_with_string(const char *n,const char *v){return prop(n,cJSON_CreateString(v));}
esp_mcp_property_t *esp_mcp_property_create_with_bool(const char *n,bool v){return prop(n,cJSON_CreateBool(v));}
esp_mcp_property_t *esp_mcp_property_create_with_int_and_range(const char *n,int v,int lo,int hi){assert(v>=lo&&v<=hi);return prop(n,cJSON_CreateNumber(v));}
esp_mcp_property_t *esp_mcp_property_create_with_range(const char *n,int lo,int hi){(void)lo;(void)hi;return prop(n,NULL);}
esp_err_t esp_mcp_property_destroy(esp_mcp_property_t *p){free(p->name);cJSON_Delete(p->value);free(p);return ESP_OK;}
const char *esp_mcp_property_list_get_property_string(const esp_mcp_property_list_t *p,const char *n){return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(p->args,n));}
int esp_mcp_property_list_get_property_int(const esp_mcp_property_list_t *p,const char *n){cJSON *v=cJSON_GetObjectItemCaseSensitive(p->args,n);return v?v->valueint:0;}
bool esp_mcp_property_list_get_property_bool(const esp_mcp_property_list_t *p,const char *n){return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p->args,n));}
esp_mcp_tool_t *esp_mcp_tool_create_ex(const char *name,const char *title,const char *description,esp_mcp_tool_callback_ex_t cb){
    (void)title;assert(description&&*description);esp_mcp_tool_t *t=calloc(1,sizeof(*t));t->name=strdup(name);t->cb=cb;return t;
}
esp_err_t esp_mcp_tool_add_property(esp_mcp_tool_t *t,esp_mcp_property_t *p){assert(t->count<5);t->props[t->count++]=p;return ESP_OK;}
esp_err_t esp_mcp_tool_destroy(esp_mcp_tool_t *t){for(unsigned i=0;i<t->count;++i)esp_mcp_property_destroy(t->props[i]);free(t->name);free(t);return ESP_OK;}
esp_err_t esp_mcp_add_tool(esp_mcp_t *e,esp_mcp_tool_t *t){assert(e->count<20);e->tools[e->count++]=t;return ESP_OK;}
esp_err_t esp_mcp_tool_result_set_is_error(esp_mcp_tool_result_t *r,bool error){r->error=error;return ESP_OK;}
esp_err_t esp_mcp_tool_result_add_text(esp_mcp_tool_result_t *r,const char *text){free(r->text);r->text=strdup(text);return ESP_OK;}
static void destroy(esp_mcp_t *e){for(unsigned i=0;i<e->count;++i)esp_mcp_tool_destroy(e->tools[i]);memset(e,0,sizeof(*e));}
static esp_mcp_tool_result_t call(esp_mcp_t *e,const char *name,const char *args){
    esp_mcp_tool_t *t=NULL;for(unsigned i=0;i<e->count;++i)if(!strcmp(e->tools[i]->name,name))t=e->tools[i];assert(t);
    cJSON *o=cJSON_Parse(args);assert(o);
    for(unsigned i=0;i<t->count;++i)if(!cJSON_HasObjectItem(o,t->props[i]->name)&&t->props[i]->value)cJSON_AddItemToObject(o,t->props[i]->name,cJSON_Duplicate(t->props[i]->value,true));
    esp_mcp_property_list_t p={o};esp_mcp_tool_result_t r={0};assert(t->cb(&p,&r)==ESP_OK);assert(r.text);cJSON_Delete(o);return r;
}
static int64_t clock_us=1000000;
int64_t esp_timer_get_time(void){return clock_us;}
static bool online=true,clock_ready=true,alarm=false,camera=false,accept=true,ota=false;
static unsigned media_calls,fm_calls,photos,ui_page;
static media_request_t last_media;static fm_request_t last_fm;
static fm_status_t radio={.state=FM_READY,.available=true,.frequency=987};
static av_library_status_t library={.storage=true,.music_count=2,.revision=7};
bool cloud_ota_busy(void){return ota;}
bool device_network_ready(void){return online;}
bool device_clock_ready(void){return clock_ready;}
unsigned audio_device_get_volume(void){return 35;}
bool audio_device_alarm_waiting(void){return alarm;}
bool camera_is_enabled(void){return camera;}
void camera_set_enabled(bool enabled){camera=enabled;}
bool camera_take_photo(void){++photos;return accept;}
unsigned camera_photo_state(void){return photos?1:0;}
bool ui_open_av(av_tab_t tab){ui_page=100+tab;return true;}
bool ui_open_vehicle_page(unsigned page){ui_page=page;return true;}
bool ui_open_alarms(void){ui_page=200;return true;}
void device_status_get(device_status_t *out){memset(out,0,sizeof(*out));}
void media_service_status(media_status_t *out){*out=(media_status_t){.volume=35};}
bool media_service_request(const media_request_t *r){if(!accept)return false;last_media=*r;++media_calls;return true;}
void fm_service_status(fm_status_t *out){*out=radio;}
bool fm_service_request(const fm_request_t *r){last_fm=*r;++fm_calls;return accept;}
bool media_service_online_saved(char *u,size_t us,char *t,size_t ts){(void)u;(void)us;(void)t;(void)ts;return false;}
void media_library_status(av_library_status_t *out){*out=library;}
size_t media_library_files(av_file_kind_t kind,unsigned first,av_file_t *files,size_t capacity,uint32_t revision){
    (void)kind;if(!capacity || first>=library.music_count || revision!=library.revision)return 0;
    snprintf(files[0].name,sizeof(files[0].name),"%s",first?"Second.mp3":"Beyond.mp3");return 1;
}
esp_err_t obd2_read_vehicle_data(obd2_vehicle_data_t *out){*out=(obd2_vehicle_data_t){.link_state=OBD_LINK_ACTIVE,.coolant_temp_valid=true,.coolant_temp_c=92};return ESP_OK;}
void alarm_service_status(alarm_status_t *out){*out=(alarm_status_t){.clock_ready=clock_ready};}
int alarm_service_set(unsigned h,unsigned m,bool daily,unsigned delay,const char *label){(void)daily;(void)delay;(void)label;return clock_ready&&h<24&&m<60?1:-1;}
bool alarm_service_delete(uint32_t id){return id==1;}
void alarm_service_dismiss(void){alarm=false;}
#define CALL(name,args,ok) do{esp_mcp_tool_result_t r=call(&engine,name,args);assert(r.error!=ok);free(r.text);}while(0)
int main(void)
{
    esp_mcp_t engine={0};assert(voice_tools_register(&engine)==ESP_OK && engine.count==15);
    esp_mcp_tool_result_t r=call(&engine,"self.vehicle.get_metrics","{\"metric\":\"coolant_temperature\",\"show\":true}");
    assert(!r.error&&strstr(r.text,"92")&&ui_page==1);free(r.text);
    r=call(&engine,"self.vehicle.get_metrics","{\"metric\":\"oil_temperature\"}");assert(strstr(r.text,"\"value\":null")&&!r.error);free(r.text);
    CALL("self.vehicle.get_metrics","{\"metric\":\"invented\"}",false);
    CALL("self.audio_speaker.set_volume","{\"volume\":101}",false);
    CALL("self.audio_speaker.adjust_volume","{\"delta\":-10}",true);assert(last_media.action==MEDIA_VOLUME_DELTA&&last_media.delta==-10);
    accept=false;CALL("self.audio_speaker.set_volume","{\"volume\":40}",false);accept=true;
    unsigned before=media_calls;
    CALL("self.music.play_local","{\"name\":\"Beyond\"}",true);assert(voice_tools_pending()&&media_calls==before);
    voice_tools_run_pending();assert(media_calls==before+1&&last_media.action==MEDIA_SELECT_LOCAL&&last_media.value==0&&last_media.revision==7);
    voice_tools_run_pending();assert(media_calls==before+1);
    CALL("self.music.play_local","{\"name\":\"mp3\"}",false);
    CALL("self.music.play_local","{}",true);voice_tools_cancel();voice_tools_run_pending();assert(media_calls==before+1);
    CALL("self.music.play_local","{}",true);clock_us+=31000000;assert(!voice_tools_pending());voice_tools_run_pending();assert(media_calls==before+1);
    CALL("self.music.play_online","{}",false);
    CALL("self.music.play_online","{\"url\":\"file:///secret\"}",false);
    CALL("self.music.play_online","{\"url\":\"https://example.org/song.mp3\",\"title\":\"song\"}",true);
    voice_tools_run_pending();assert(last_media.action==MEDIA_SELECT_ONLINE&&!strcmp(last_media.title,"song"));
    radio.available=false;CALL("self.radio.control","{}",false);assert(!fm_calls);
    radio.available=true;CALL("self.radio.control","{}",true);assert(!fm_calls);voice_tools_run_pending();assert(fm_calls==1&&last_fm.action==FM_LISTEN&&last_fm.frequency==987);
    library.storage=false;CALL("self.camera.take_photo","{}",false);assert(!photos);
    library.storage=true;CALL("self.camera.take_photo","{}",true);assert(camera&&photos==1&&ui_page==100+AV_CAMERA);
    clock_ready=false;CALL("self.alarm.set","{\"hour\":7,\"minute\":30}",false);
    clock_ready=true;CALL("self.alarm.set","{\"hour\":7,\"minute\":30}",true);assert(ui_page==200);
    alarm=true;CALL("self.music.play_local","{}",false);CALL("self.alarm.dismiss","{}",true);assert(!alarm);
    destroy(&engine);fail_alloc=alloc_count+3;assert(voice_tools_register(&engine)==ESP_ERR_NO_MEM);destroy(&engine);
    puts("Production MCP callbacks: telemetry, queued playback, timeout/cancel, volume, FM, camera, alarms and registration failure passed");return 0;
}
