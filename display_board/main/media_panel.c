#include "media_panel.h"
#include "feature_config.h"
#include "fonts/dashboard_fonts.h"
#include "ui_assets.h"
#include "ui_update.h"
#include <stdio.h>
#include <string.h>

static media_action_cb_t s_action;
static av_action_cb_t s_av_action;
static av_files_cb_t s_files;
static media_status_t s_status={.volume=35};
static av_status_t s_av;
static bool s_status_ready, s_av_ready;
static av_tab_t s_tab;
static unsigned s_music_page, s_photo_page;
static bool s_album, s_have_frame, s_frame_photo;
static lv_obj_t *s_parent, *s_title, *s_detail, *s_time, *s_play;
static lv_obj_t *s_rows[5], *s_marks[5], *s_canvas, *s_canvas_note, *s_cam_toggle, *s_photo_button;
static lv_obj_t *s_notice;
static uint16_t *s_pixels;
static dashboard_palette_t s_palette;
static int s_width, s_height;
static uint16_t s_fm_frequency=FM_MIN_FREQUENCY;
static fm_presets_t s_fm_presets, s_fm_pending;
static fm_presets_save_cb_t s_fm_save;
static bool s_fm_saving;
static const char *s_fm_message="收藏自动保存";
static lv_obj_t *s_fm_number, *s_fm_slider, *s_fm_lower, *s_fm_upper;
static lv_obj_t *s_fm_slots[FM_PRESET_COUNT], *s_fm_count, *s_fm_note;
static lv_obj_t *s_fm_state,*s_fm_detail,*s_fm_listen,*s_fm_seek;
static fm_status_t s_fm_status={.state=FM_ABSENT,.frequency=FM_MIN_FREQUENCY};
static fm_action_cb_t s_fm_action;
static bool s_fm_pending_tune;
static uint32_t s_fm_pending_id;
static int px(int n) { return n*s_width/1024; }
static int py(int n) { return n*s_height/456; }
static void rebuild(void);

static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
    lv_obj_t *obj=lv_obj_create(parent); lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj,px(x),py(y)); lv_obj_set_size(obj,px(w),py(h));
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0); lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_radius(obj,radius,0); return obj;
}
static lv_obj_t *label(lv_obj_t *parent,const char *value,int x,int y,int w,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(parent); lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj,px(x),py(y)); lv_obj_set_width(obj,px(w));
    lv_obj_set_style_text_font(obj,font,0); lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT); lv_label_set_text(obj,value); return obj;
}
static lv_obj_t *button(lv_obj_t *parent,const char *value,int x,int y,int w,int h,
                         bool primary,lv_event_cb_t cb,uintptr_t id)
{
    lv_obj_t *obj=box(parent,x,y,w,h,primary?s_palette.tint:s_palette.panel,14);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj,LV_OPA_70,LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,(void *)id);
    lv_obj_t *text=label(obj,value,8,(h-24)/2,w-16,&dashboard_text_18,primary?s_palette.accent:s_palette.muted);
    lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_opa(obj,LV_OPA_40,LV_STATE_DISABLED);
    return obj;
}
static void button_text(lv_obj_t *button,const char *value)
{ if (button) ui_label_text(lv_obj_get_child(button,0),value); }
static void enabled(lv_obj_t *obj,bool on)
{ if (!obj) return; if (on) lv_obj_remove_state(obj,LV_STATE_DISABLED); else lv_obj_add_state(obj,LV_STATE_DISABLED); }
static bool emit(media_action_t action,unsigned value)
{
    media_request_t request={.action=action,.value=value,.revision=s_av.library.revision};
    bool ok=s_action && s_action(&request);
    if (!ok && s_notice) lv_label_set_text(s_notice,"操作未提交，请稍后重试");
    return ok;
}
static bool emit_av(av_action_t action,unsigned value)
{
    bool ok=s_av_action && s_av_action(action,value,s_av.library.revision);
    if (!ok && s_notice) lv_label_set_text(s_notice,"操作未提交，请稍后重试");
    return ok;
}
static void choose_tab(lv_event_t *event)
{
    av_tab_t tab=(av_tab_t)(uintptr_t)lv_event_get_user_data(event);
    if (s_tab==AV_CAMERA && tab!=AV_CAMERA) emit_av(AV_CAMERA_OFF,0);
    s_tab=tab; rebuild();
}
void media_panel_select(av_tab_t tab)
{
    if (tab > AV_FM || tab == AV_VOICE || (!DEVICE_CAMERA_AVAILABLE && tab == AV_CAMERA)) return;
    if (s_tab==AV_CAMERA && tab!=AV_CAMERA) emit_av(AV_CAMERA_OFF,0);
    s_tab=tab; if (s_parent) rebuild();
}
static void choose_source(lv_event_t *event)
{
    unsigned source=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    emit(MEDIA_SELECT_LOCAL,source);
}
static void control(lv_event_t *event)
{
    const media_action_t actions[]={MEDIA_PREVIOUS,MEDIA_TOGGLE,MEDIA_NEXT,MEDIA_STOP,MEDIA_SELECT_ONLINE};
    emit(actions[(uintptr_t)lv_event_get_user_data(event)],0);
}
static void change_page(lv_event_t *event)
{
    int step=(uintptr_t)lv_event_get_user_data(event)?1:-1;
    unsigned *page=s_tab==AV_CAMERA?&s_photo_page:&s_music_page;
    unsigned count=s_tab==AV_CAMERA?s_av.library.photo_count:s_av.library.music_count;
    unsigned per=s_tab==AV_CAMERA?4:5;
    if (step<0 && *page) --*page;
    if (step>0 && (*page+1)*per<count) ++*page;
    rebuild();
}
static void camera_mode(lv_event_t *event)
{
    bool album=(uintptr_t)lv_event_get_user_data(event)!=0;
    if (album) emit_av(AV_CAMERA_OFF,0);
    if (album!=s_album) s_have_frame=false;
    s_album=album; rebuild();
}
static void camera_toggle(lv_event_t *event)
{
    (void)event;
    if (!s_av.camera_enabled) { s_have_frame=false; if (s_canvas_note) lv_label_set_text(s_canvas_note,"正在启动摄像头"); }
    emit_av(s_av.camera_enabled?AV_CAMERA_OFF:AV_CAMERA_ON,0);
}
static void take_photo(lv_event_t *event) { (void)event; emit_av(AV_TAKE_PHOTO,0); }
static void choose_photo(lv_event_t *event)
{
    s_have_frame=false;
    if (s_canvas && s_pixels) { memset(s_pixels,0,AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2); lv_obj_invalidate(s_canvas); }
    if (s_canvas_note) { lv_label_set_text(s_canvas_note,"正在读取照片"); lv_obj_remove_flag(s_canvas_note,LV_OBJ_FLAG_HIDDEN); }
    emit_av(AV_VIEW_PHOTO,(unsigned)(uintptr_t)lv_event_get_user_data(event));
}
static void pager(lv_obj_t *parent,int x,int y,int w,unsigned page,unsigned count,unsigned per)
{
    lv_obj_t *prev=button(parent,"上一页",x,y,82,42,false,change_page,0);
    lv_obj_t *next=button(parent,"下一页",x+w-82,y,82,42,false,change_page,1);
    enabled(prev,page>0); enabled(next,(page+1)*per<count);
    char text[32]; snprintf(text,sizeof(text),"%u / %u",count?page+1:0,(count+per-1)/per);
    lv_obj_t *n=label(parent,text,x+86,y+10,w-172,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(n,LV_TEXT_ALIGN_CENTER,0);
}
static void music_page(void)
{
    button(s_parent,"在线播放",816,12,184,42,false,control,4);
    lv_obj_t *list=box(s_parent,604,76,396,354,s_palette.panel,24);
    label(list,"播放列表",20,16,200,&dashboard_text_18,s_palette.ink);
    char count[24]; snprintf(count,sizeof(count),"%u 首",s_av.library.music_count);
    lv_obj_t *total=label(list,count,252,16,124,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(total,LV_TEXT_ALIGN_RIGHT,0);
    av_file_t files[5];
    unsigned max_page=s_av.library.music_count?(s_av.library.music_count-1)/5:0;
    if (s_music_page>max_page) s_music_page=max_page;
    size_t n=s_files?s_files(AV_FILES_MUSIC,s_music_page*5,files,5,s_av.library.revision):0;
    for (unsigned i=0;i<n;++i) {
        lv_obj_t *row=s_rows[i]=box(list,12,53+i*48,372,44,s_palette.panel,12);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(row,LV_OPA_70,LV_STATE_PRESSED);
        lv_obj_set_style_border_color(row,lv_color_hex(s_palette.accent),0);
        lv_obj_set_style_border_side(row,LV_BORDER_SIDE_LEFT,0);
        lv_obj_add_event_cb(row,choose_source,LV_EVENT_CLICKED,(void *)(uintptr_t)(s_music_page*5+i));
        char number[12]; snprintf(number,sizeof(number),"%02u",s_music_page*5+i+1);
        label(row,number,14,13,32,&lv_font_montserrat_14,s_palette.muted);
        label(row,files[i].name,53,8,274,&dashboard_wifi_18,s_palette.ink);
        s_marks[i]=label(row,LV_SYMBOL_PLAY,342,13,20,&lv_font_montserrat_18,s_palette.accent);
    }
    if (!n) {
        lv_obj_t *empty=box(list,12,53,372,244,s_palette.panel,14);
        label(empty,s_av.library.scanning?"正在查找音乐":"暂无音乐",20,52,332,&dashboard_text_22,s_palette.ink);
        lv_obj_t *hint=label(empty,s_av.library.storage?"将 MP3 放入卡根目录\n或 music 文件夹":"插入 SD 卡后自动显示音乐",20,105,332,&dashboard_text_18,s_palette.muted);
        lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP); lv_obj_set_height(hint,py(88));
    }
    pager(list,12,304,372,s_music_page,s_av.library.music_count,5);
    lv_obj_t *player=box(s_parent,24,76,562,354,s_palette.panel,24);
    label(player,"本地音乐",26,18,500,&dashboard_text_18,s_palette.muted);
    ui_asset_create(player,UI_ALBUM,px(26),py(54),0);
    s_title=label(player,"",194,58,342,&dashboard_wifi_18,s_palette.ink);
    s_detail=label(player,"",194,96,342,&dashboard_text_18,s_palette.muted); s_notice=s_detail;
    lv_label_set_long_mode(s_detail,LV_LABEL_LONG_WRAP); lv_obj_set_height(s_detail,py(48));
    s_time=label(player,"",194,156,342,&dashboard_text_18,s_palette.muted);
    const char *icons[]={LV_SYMBOL_PREV,LV_SYMBOL_PLAY,LV_SYMBOL_NEXT,LV_SYMBOL_STOP};
    for (unsigned i=0;i<4;++i) {
        const int x[]={148,246,344,468};
        int size=i==1?64:52;
        lv_obj_t *b=box(player,x[i],i==1?244:250,size,size,i==1?s_palette.accent:s_palette.panel,LV_RADIUS_CIRCLE);
        lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(b,control,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_set_style_bg_opa(b,LV_OPA_70,LV_STATE_PRESSED);
        lv_obj_t *symbol=label(b,icons[i],0,(size-24)/2,size,&lv_font_montserrat_24,i==1?s_palette.on_accent:s_palette.ink);
        lv_obj_set_style_text_align(symbol,LV_TEXT_ALIGN_CENTER,0); if (i==1) s_play=symbol;
    }
}
static void fm_refresh(void)
{
    if (!s_parent || s_tab!=AV_FM || !s_fm_number) return;
    char text[32];
    snprintf(text,sizeof(text),"%u.%u",s_fm_frequency/10,s_fm_frequency%10);
    ui_label_text(s_fm_number,text);
    lv_slider_set_value(s_fm_slider,s_fm_frequency,LV_ANIM_OFF);
    enabled(s_fm_lower,s_fm_frequency>FM_MIN_FREQUENCY);
    enabled(s_fm_upper,s_fm_frequency<FM_MAX_FREQUENCY);
    unsigned count=0;
    for (unsigned i=0;i<FM_PRESET_COUNT;++i) {
        uint16_t f=s_fm_presets.frequency[i];
        bool selected=f && f==s_fm_frequency;
        lv_obj_t *slot=s_fm_slots[i];
        if (f) { ++count; snprintf(text,sizeof(text),"%u.%u",f/10,f%10); }
        else snprintf(text,sizeof(text),"收藏当前");
        ui_label_text(lv_obj_get_child(slot,1),text);
        ui_text_font(lv_obj_get_child(slot,1),f?&dashboard_text_22:&dashboard_text_18);
        ui_text_color(lv_obj_get_child(slot,1),lv_color_hex(selected?s_palette.accent:s_palette.ink));
        ui_bg_color(slot,lv_color_hex(selected?s_palette.tint:s_palette.bg));
        enabled(slot,!s_fm_saving);
    }
    snprintf(text,sizeof(text),"%u / %u",count,FM_PRESET_COUNT); ui_label_text(s_fm_count,text);
    ui_label_text(s_fm_note,s_fm_saving?"正在保存收藏":s_fm_message);
    bool busy=s_fm_status.state==FM_TUNING || s_fm_status.state==FM_SEEKING;
    const char *state=s_fm_status.state==FM_ERROR?"接收器异常":!s_fm_status.available?"接收器未接入":
        s_fm_status.state==FM_SEEKING?"正在搜台":s_fm_status.state==FM_TUNING?"正在调谐":
        s_fm_status.listening?(s_fm_status.volume?"正在收听":"已静音"):"接收器就绪";
    ui_label_text(s_fm_state,state);
    button_text(s_fm_listen,!s_fm_status.available?"重试连接":busy?"停止":s_fm_status.listening?"停止收听":"开始收听");
    enabled(s_fm_listen,s_fm_action!=NULL);
    enabled(s_fm_seek,s_fm_status.available && !busy && s_fm_action);
    if (s_fm_status.available && !busy && s_fm_status.listening) {
        char detail[128];
        snprintf(detail,sizeof(detail),"信号 %u / 127 · %s · %s",s_fm_status.rssi,
            s_fm_status.stereo?"立体声":"单声道",s_fm_status.station?"已检测到电台":"当前信号较弱");
        ui_label_text(s_fm_detail,detail);
    } else ui_label_text(s_fm_detail,*s_fm_status.detail?s_fm_status.detail:"连接接收器后可搜台和收听");
}
static void fm_send(fm_action_t action)
{
    static uint32_t next_id;
    if (!++next_id) ++next_id;
    fm_request_t request={.action=action,.frequency=s_fm_frequency,.id=next_id};
    if (!s_fm_action || !s_fm_action(&request)) s_fm_message="操作未成功，请重试";
    else {
        s_fm_message="";
        s_fm_pending_tune=action==FM_TUNE || action==FM_LISTEN;
        s_fm_pending_id=request.id;
    }
}
static void fm_activate(lv_event_t *event)
{
    bool seek=(uintptr_t)lv_event_get_user_data(event)!=0;
    bool busy=s_fm_status.state==FM_TUNING || s_fm_status.state==FM_SEEKING;
    fm_send(seek?FM_SEEK:!s_fm_status.available?FM_RETRY:busy || s_fm_status.listening?FM_STOP:FM_LISTEN);
    fm_refresh();
}
static void fm_step(lv_event_t *event)
{
    bool up=(uintptr_t)lv_event_get_user_data(event)!=0;
    if (up && s_fm_frequency<FM_MAX_FREQUENCY) ++s_fm_frequency;
    if (!up && s_fm_frequency>FM_MIN_FREQUENCY) --s_fm_frequency;
    if (s_fm_status.available) fm_send(FM_TUNE);
    fm_refresh();
}
static void fm_slide(lv_event_t *event)
{
    s_fm_frequency=(uint16_t)lv_slider_get_value(lv_event_get_target_obj(event));
    if (lv_event_get_code(event)==LV_EVENT_RELEASED && s_fm_status.available) fm_send(FM_TUNE);
    fm_refresh();
}
static void fm_slot(lv_event_t *event)
{
    unsigned index=(uintptr_t)lv_event_get_user_data(event);
    if (index>=FM_PRESET_COUNT || s_fm_saving) return;
    bool clear=lv_event_get_code(event)==LV_EVENT_LONG_PRESSED;
    if (!clear && s_fm_presets.frequency[index]) {
        s_fm_frequency=s_fm_presets.frequency[index];
        if (s_fm_status.available) fm_send(FM_TUNE);
        fm_refresh(); return;
    }
    if (clear && !s_fm_presets.frequency[index]) return;
    if (!clear) for (unsigned i=0;i<FM_PRESET_COUNT;++i) {
        if (s_fm_presets.frequency[i]==s_fm_frequency) {
            s_fm_message="此频率已收藏"; fm_refresh(); return;
        }
    }
    s_fm_pending=s_fm_presets;
    s_fm_pending.frequency[index]=clear?0:s_fm_frequency;
    s_fm_saving=s_fm_save && s_fm_save(&s_fm_pending);
    if (!s_fm_saving) s_fm_message="收藏保存失败，请重试";
    fm_refresh();
}
static void fm_page(void)
{
    lv_obj_t *tuner=box(s_parent,24,76,624,354,s_palette.panel,24);
    ui_asset_create(tuner,UI_RADIO,px(26),py(21),s_palette.accent);
    label(tuner,"电台频率",66,22,200,&dashboard_text_18,s_palette.muted);
    lv_obj_t *state=box(tuner,440,18,158,34,s_palette.tint,17);
    lv_obj_t *state_text=s_fm_state=label(state,"接收器未接入",4,4,150,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(state_text,LV_TEXT_ALIGN_CENTER,0);
    s_fm_number=label(tuner,"",26,65,328,&dashboard_digits_72,s_palette.ink);
    label(tuner,"MHz",376,109,100,&dashboard_text_22,s_palette.muted);
    /* The ruler and slider share endpoints; each tick is 0.5 MHz. */
    for (unsigned i=0;i<=41;++i) {
        int h=i%5==0?24:12;
        box(tuner,40+i*544/41,191-h,2,h,s_palette.track,1);
    }
    s_fm_slider=lv_slider_create(tuner);
    lv_obj_set_ext_click_area(s_fm_slider,16);
    lv_obj_set_pos(s_fm_slider,px(40),py(207)); lv_obj_set_size(s_fm_slider,px(544),py(5));
    lv_slider_set_range(s_fm_slider,FM_MIN_FREQUENCY,FM_MAX_FREQUENCY);
    lv_obj_set_style_bg_color(s_fm_slider,lv_color_hex(s_palette.track),LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fm_slider,lv_color_hex(s_palette.accent),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_fm_slider,lv_color_hex(s_palette.accent),LV_PART_KNOB);
    lv_obj_add_event_cb(s_fm_slider,fm_slide,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(s_fm_slider,fm_slide,LV_EVENT_RELEASED,NULL);
    label(tuner,"87.5",26,226,80,&lv_font_montserrat_14,s_palette.muted);
    lv_obj_t *band=label(tuner,"FM · 0.1 MHz",184,225,256,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(band,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *upper=label(tuner,"108.0",518,226,80,&lv_font_montserrat_14,s_palette.muted);
    lv_obj_set_style_text_align(upper,LV_TEXT_ALIGN_RIGHT,0);
    s_fm_lower=button(tuner,"− 0.1",26,267,118,48,false,NULL,0);
    s_fm_upper=button(tuner,"+ 0.1",336,267,118,48,false,NULL,0);
    lv_obj_set_style_bg_color(s_fm_lower,lv_color_hex(s_palette.bg),0);
    lv_obj_set_style_bg_color(s_fm_upper,lv_color_hex(s_palette.bg),0);
    lv_obj_add_event_cb(s_fm_lower,fm_step,LV_EVENT_PRESSED,(void *)0);
    lv_obj_add_event_cb(s_fm_lower,fm_step,LV_EVENT_LONG_PRESSED_REPEAT,(void *)0);
    lv_obj_add_event_cb(s_fm_upper,fm_step,LV_EVENT_PRESSED,(void *)1);
    lv_obj_add_event_cb(s_fm_upper,fm_step,LV_EVENT_LONG_PRESSED_REPEAT,(void *)1);
    s_fm_listen=button(tuner,"开始收听",158,267,164,48,true,fm_activate,0);
    s_fm_seek=button(tuner,"自动搜台",468,267,130,48,false,fm_activate,1);
    s_fm_detail=label(tuner,"",26,324,572,&dashboard_text_18,s_palette.muted);

    lv_obj_t *presets=box(s_parent,668,76,332,354,s_palette.panel,24);
    label(presets,"常用频率",20,20,196,&dashboard_text_18,s_palette.ink);
    s_fm_count=label(presets,"",226,20,86,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(s_fm_count,LV_TEXT_ALIGN_RIGHT,0);
    for (unsigned i=0;i<FM_PRESET_COUNT;++i) {
        lv_obj_t *slot=s_fm_slots[i]=box(presets,18+(i%2)*154,64+(i/2)*74,142,62,s_palette.bg,14);
        lv_obj_add_flag(slot,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(slot,LV_OPA_40,LV_STATE_DISABLED);
        lv_obj_add_event_cb(slot,fm_slot,LV_EVENT_SHORT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_add_event_cb(slot,fm_slot,LV_EVENT_LONG_PRESSED,(void *)(uintptr_t)i);
        char num[8]; snprintf(num,sizeof(num),"%02u",i+1);
        label(slot,num,12,7,30,&lv_font_montserrat_14,s_palette.muted);
        lv_obj_t *name=label(slot,"",10,28,122,&dashboard_text_18,s_palette.ink);
        lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    }
    label(presets,"点空位收藏 · 长按移除",20,292,292,&dashboard_text_18,s_palette.muted);
    s_fm_note=label(presets,"",20,320,292,&dashboard_text_18,s_palette.muted);
    fm_refresh();
}
static void camera_page(void)
{
    lv_obj_t *preview=box(s_parent,28,84,624,344,0x080D10,16);
    if (!s_pixels) s_pixels=lv_malloc_zeroed(AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2);
    if (s_pixels) {
        s_canvas=lv_canvas_create(preview);
        lv_canvas_set_buffer(s_canvas,s_pixels,AV_PREVIEW_WIDTH,AV_PREVIEW_HEIGHT,LV_COLOR_FORMAT_RGB565);
        lv_obj_center(s_canvas); lv_image_set_scale(s_canvas,320);
        if (!s_have_frame || s_frame_photo!=s_album) memset(s_pixels,0,AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2);
    }
    s_canvas_note=label(preview,s_album?"选择照片查看":"点击开启摄像头",20,150,584,&dashboard_text_22,0xD7E6E2);
    lv_obj_set_style_text_align(s_canvas_note,LV_TEXT_ALIGN_CENTER,0);
    if (s_have_frame && s_frame_photo==s_album) lv_obj_add_flag(s_canvas_note,LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *controls=box(s_parent,676,84,320,344,s_palette.panel,16);
    button(controls,"实时预览",16,12,138,42,!s_album,camera_mode,0);
    button(controls,"相册",166,12,138,42,s_album,camera_mode,1);
    if (!s_album) {
        s_cam_toggle=button(controls,"开启摄像头",18,74,284,48,true,camera_toggle,0);
        s_detail=label(controls,"",18,136,284,&dashboard_text_18,s_palette.muted);
        lv_label_set_long_mode(s_detail,LV_LABEL_LONG_WRAP); lv_obj_set_height(s_detail,py(48));
        s_photo_button=button(controls,"拍照保存",18,190,284,48,false,take_photo,0);
        s_notice=label(controls,"",18,256,284,&dashboard_text_18,s_palette.muted);
        lv_label_set_long_mode(s_notice,LV_LABEL_LONG_WRAP); lv_obj_set_height(s_notice,py(70));
    } else {
        av_file_t files[4];
        unsigned max_page=s_av.library.photo_count?(s_av.library.photo_count-1)/4:0;
        if (s_photo_page>max_page) s_photo_page=max_page;
        size_t n=s_files?s_files(AV_FILES_PHOTOS,s_photo_page*4,files,4,s_av.library.revision):0;
        for (unsigned i=0;i<n;++i) {
            lv_obj_t *row=box(controls,16,66+i*44,288,40,s_palette.tint,8);
            lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row,choose_photo,LV_EVENT_CLICKED,(void *)(uintptr_t)(s_photo_page*4+i));
            label(row,files[i].name,10,4,268,&dashboard_wifi_18,s_palette.ink);
        }
        if (!n) label(controls,s_av.library.storage?"相册暂无照片":"请先插入 SD 卡",18,120,284,&dashboard_text_18,s_palette.muted);
        pager(controls,16,250,288,s_photo_page,s_av.library.photo_count,4);
        s_notice=label(controls,"",18,310,284,&dashboard_wifi_18,s_palette.muted);
    }
}
static void refresh(void)
{
    if (!s_parent) return;
    if (s_tab==AV_MUSIC) {
        bool active=s_status.state==MEDIA_PLAYING || s_status.state==MEDIA_BUFFERING || s_status.state==MEDIA_CONNECTING;
        ui_label_text(s_title,*s_status.title?s_status.title:"请选择音乐");
        ui_label_text(s_detail,*s_status.detail?s_status.detail:"选择音乐开始播放");
        ui_text_color(s_detail,lv_color_hex(s_status.state==MEDIA_ERROR?s_palette.red:s_palette.muted));
        ui_label_text(s_play,active?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
        char time[64]; unsigned seconds=s_status.played_ms/1000;
        snprintf(time,sizeof(time),"%02u:%02u · %s",seconds/60,seconds%60,s_status.online?"在线音乐":"SD 卡");
        ui_label_text(s_time,time);
        for (unsigned i=0;i<5;++i) if (s_rows[i]) {
            bool selected=s_status.selected && !s_status.online && s_music_page*5+i==s_status.local_index;
            if (lv_obj_get_style_border_width(s_rows[i],0)!=(selected?3:0))
                lv_obj_set_style_border_width(s_rows[i],selected?3:0,0);
            ui_bg_color(s_rows[i],lv_color_hex(selected?s_palette.tint:s_palette.panel));
            ui_label_text(s_marks[i],selected && active?LV_SYMBOL_AUDIO:LV_SYMBOL_PLAY);
            if (lv_obj_get_style_opa(s_marks[i],0)!=(selected?LV_OPA_COVER:LV_OPA_30))
                lv_obj_set_style_opa(s_marks[i],selected?LV_OPA_COVER:LV_OPA_30,0);
        }
    } else if (s_tab==AV_CAMERA) {
        if (!s_album) {
            button_text(s_cam_toggle,s_av.camera_enabled?"关闭摄像头":"开启摄像头");
            ui_label_text(s_detail,*s_av.camera_detail?s_av.camera_detail:"摄像头已关闭");
            enabled(s_photo_button,s_av.camera_enabled && s_av.camera_state==3 && s_av.library.storage);
            ui_label_text(s_notice,*s_av.library.photo_detail?s_av.library.photo_detail:s_av.library.storage?"照片保存在 SD 卡 photos 文件夹":"拍照需要插入 SD 卡");
        } else ui_label_text(s_notice,*s_av.library.photo_detail?s_av.library.photo_detail:"选择照片后在左侧查看");
    }
}
static void rebuild(void)
{
    if (!s_parent) return;
    lv_obj_clean(s_parent);
    memset(s_rows,0,sizeof(s_rows)); memset(s_marks,0,sizeof(s_marks));
    s_title=s_detail=s_time=s_play=s_canvas=s_canvas_note=NULL;
    s_cam_toggle=s_photo_button=s_notice=NULL;
    s_fm_number=s_fm_slider=s_fm_lower=s_fm_upper=s_fm_count=s_fm_note=NULL;
    s_fm_state=s_fm_detail=s_fm_listen=s_fm_seek=NULL;
    memset(s_fm_slots,0,sizeof(s_fm_slots));
    const char *tabs[]={"音乐","FM 电台","摄像头"};
    const av_tab_t ids[]={AV_MUSIC,AV_FM,AV_CAMERA};
    unsigned visible_tab=0;
    int step=124;
    for (unsigned i=0;i<3;++i) {
        if (!DEVICE_CAMERA_AVAILABLE && ids[i]==AV_CAMERA) continue;
        button(s_parent,tabs[i],24+visible_tab++*step,12,step-12,42,s_tab==ids[i],choose_tab,ids[i]);
    }
    if (s_tab==AV_MUSIC) music_page(); else if (s_tab==AV_CAMERA) camera_page();
    else if (s_tab==AV_FM) fm_page();
    refresh();
}
void media_panel_handlers(media_action_cb_t action) { s_action=action; }
void media_panel_av_handlers(av_action_cb_t action,av_files_cb_t files) { s_av_action=action; s_files=files; }
void media_panel_create(lv_obj_t *parent,const dashboard_palette_t *palette)
{
    s_parent=parent; s_palette=*palette; s_width=lv_obj_get_width(parent); s_height=lv_obj_get_height(parent); rebuild();
}
void media_panel_detach(void)
{
    if (s_parent && s_tab==AV_CAMERA && !s_album) emit_av(AV_CAMERA_OFF,0);
    s_parent=NULL; s_canvas=NULL; s_canvas_note=NULL;
}
void media_panel_update(const media_status_t *status)
{
    if (!status) return;
    bool changed=!s_status_ready || status->played_ms/1000!=s_status.played_ms/1000 ||
        status->online!=s_status.online ||
        status->network!=s_status.network || status->state!=s_status.state ||
        status->selected!=s_status.selected || status->local_index!=s_status.local_index ||
        strcmp(status->title,s_status.title) ||
        strcmp(status->detail,s_status.detail);
    s_status=*status; s_status_ready=true;
    if (changed) refresh();
}
void media_panel_av_update(const av_status_t *status)
{
    if (!status) return;
    bool library_changed=status->library.revision!=s_av.library.revision;
    bool changed=!s_av_ready || library_changed ||
        status->camera_enabled!=s_av.camera_enabled || status->camera_state!=s_av.camera_state ||
        strcmp(status->camera_detail,s_av.camera_detail) ||
        status->library.storage!=s_av.library.storage || status->library.scanning!=s_av.library.scanning ||
        status->library.music_count!=s_av.library.music_count || status->library.photo_count!=s_av.library.photo_count ||
        strcmp(status->library.detail,s_av.library.detail) || strcmp(status->library.photo_detail,s_av.library.photo_detail);
    s_av=*status; s_av_ready=true;
    if (!changed) return;
    if (s_parent && (s_tab==AV_MUSIC || (s_tab==AV_CAMERA && s_album))) rebuild();
    else refresh();
}
void media_panel_frame(const uint16_t *pixels,bool photo)
{
    if (!s_parent || s_tab!=AV_CAMERA || s_album!=photo || !s_canvas || !s_pixels) return;
    memcpy(s_pixels,pixels,AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2);
    s_have_frame=true; s_frame_photo=photo;
    lv_obj_add_flag(s_canvas_note,LV_OBJ_FLAG_HIDDEN); lv_obj_invalidate(s_canvas);
}
void media_panel_camera_clear(void)
{
    if (!s_parent || s_tab!=AV_CAMERA || s_album || !s_canvas) return;
    s_have_frame=false; memset(s_pixels,0,AV_PREVIEW_WIDTH*AV_PREVIEW_HEIGHT*2); lv_obj_invalidate(s_canvas);
    lv_label_set_text(s_canvas_note,"摄像头已关闭"); lv_obj_remove_flag(s_canvas_note,LV_OBJ_FLAG_HIDDEN);
}
void media_panel_fm_handler(fm_presets_save_cb_t save) { s_fm_save=save; }
void media_panel_show_camera(void){s_album=false;if(s_parent && s_tab==AV_CAMERA)rebuild();}
void media_panel_fm_control(fm_action_cb_t action) { s_fm_action=action; }
void media_panel_fm_update(const fm_status_t *status)
{
    if (!status) return;
    bool changed=status->state!=s_fm_status.state || status->frequency!=s_fm_status.frequency ||
        status->rssi!=s_fm_status.rssi || status->volume!=s_fm_status.volume ||
        status->available!=s_fm_status.available || status->listening!=s_fm_status.listening ||
        status->stereo!=s_fm_status.stereo || status->station!=s_fm_status.station ||
        strcmp(status->detail,s_fm_status.detail);
    uint16_t previous_frequency=s_fm_frequency;
    s_fm_status=*status;
    bool error=status->state==FM_ERROR || status->state==FM_ABSENT;
    if (error || status->completed_id==s_fm_pending_id) s_fm_pending_tune=false;
    if (!s_fm_pending_tune && status->available && status->state==FM_READY &&
        (!s_fm_slider || !s_parent || s_tab!=AV_FM || !lv_obj_has_state(s_fm_slider,LV_STATE_PRESSED)))
        s_fm_frequency=status->frequency;
    if (changed || previous_frequency!=s_fm_frequency) fm_refresh();
}
bool media_panel_fm_restore(const fm_presets_t *presets)
{
    if (!presets || s_fm_saving) return false;
    for (unsigned i=0;i<FM_PRESET_COUNT;++i) {
        uint16_t f=presets->frequency[i];
        if (f && (f<FM_MIN_FREQUENCY || f>FM_MAX_FREQUENCY)) return false;
        for (unsigned j=0;f && j<i;++j) if (presets->frequency[j]==f) return false;
    }
    s_fm_presets=*presets; fm_refresh(); return true;
}
void media_panel_fm_save_result(bool success)
{
    if (!s_fm_saving) return;
    if (success) s_fm_presets=s_fm_pending;
    s_fm_saving=false;
    s_fm_message=success?"收藏已保存":"收藏保存失败，请重试";
    fm_refresh();
}
