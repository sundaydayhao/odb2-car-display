#include "media_panel.h"
#include "feature_config.h"
#include "fonts/dashboard_fonts.h"
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
static lv_obj_t *s_parent, *s_title, *s_detail, *s_time, *s_play, *s_volume, *s_volume_text, *s_context;
static lv_obj_t *s_rows[5], *s_marks[5], *s_canvas, *s_canvas_note, *s_cam_toggle, *s_photo_button;
static lv_obj_t *s_voice_toggle, *s_voice_start, *s_voice_stop, *s_voice_heading, *s_notice;
static uint16_t *s_pixels;
static dashboard_palette_t s_palette;
static int s_width, s_height;
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
    lv_obj_t *obj=box(parent,x,y,w,h,primary?s_palette.accent:s_palette.tint,12);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,cb,LV_EVENT_CLICKED,(void *)id);
    lv_obj_t *text=label(obj,value,8,(h-24)/2,w-16,&dashboard_text_18,primary?s_palette.on_accent:s_palette.accent);
    lv_obj_set_style_text_align(text,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_opa(obj,LV_OPA_40,LV_STATE_DISABLED);
    return obj;
}
static void button_text(lv_obj_t *button,const char *value)
{ if (button) lv_label_set_text(lv_obj_get_child(button,0),value); }
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
    if (tab>AV_FM || (!DEVICE_CAMERA_AVAILABLE && tab==AV_CAMERA)) return;
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
    const media_action_t actions[]={MEDIA_PREVIOUS,MEDIA_TOGGLE,MEDIA_NEXT,MEDIA_STOP};
    emit(actions[(uintptr_t)lv_event_get_user_data(event)],0);
}
static void volume_changed(lv_event_t *event)
{
    unsigned volume=lv_slider_get_value(lv_event_get_target_obj(event));
    char text[16]; snprintf(text,sizeof(text),"%u%%",volume); lv_label_set_text(s_volume_text,text);
    if (lv_event_get_code(event)==LV_EVENT_RELEASED) emit(MEDIA_VOLUME,volume);
}
static void refresh_files(lv_event_t *event) { (void)event; emit_av(AV_REFRESH_FILES,0); }
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
static void voice_control(lv_event_t *event)
{
    unsigned action=(uintptr_t)lv_event_get_user_data(event);
    emit_av(action==0?(s_av.voice_enabled?AV_VOICE_OFF:AV_VOICE_ON):action==1?AV_VOICE_START:AV_VOICE_END,0);
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
    label(s_parent,"SD 卡音乐",28,80,330,&dashboard_text_22,s_palette.ink);
    button(s_parent,"刷新文件",858,70,138,42,false,refresh_files,0);
    av_file_t files[5];
    unsigned max_page=s_av.library.music_count?(s_av.library.music_count-1)/5:0;
    if (s_music_page>max_page) s_music_page=max_page;
    size_t n=s_files?s_files(AV_FILES_MUSIC,s_music_page*5,files,5,s_av.library.revision):0;
    for (unsigned i=0;i<n;++i) {
        lv_obj_t *row=s_rows[i]=box(s_parent,28,128+i*50,330,44,s_palette.panel,10);
        lv_obj_add_flag(row,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row,choose_source,LV_EVENT_CLICKED,(void *)(uintptr_t)(s_music_page*5+i));
        label(row,files[i].name,14,6,272,&dashboard_wifi_18,s_palette.ink);
        s_marks[i]=label(row,LV_SYMBOL_PLAY,298,12,20,&lv_font_montserrat_18,s_palette.accent);
    }
    if (!n) {
        lv_obj_t *empty=box(s_parent,28,128,330,244,s_palette.panel,14);
        label(empty,s_av.library.scanning?"正在查找音乐":"暂无 SD 卡音乐",20,52,290,&dashboard_text_22,s_palette.ink);
        lv_obj_t *hint=label(empty,s_av.library.storage?"将 MP3 放入卡根目录\n或 music 文件夹，再点刷新":"插入 SD 卡后可离线播放",20,105,290,&dashboard_text_18,s_palette.muted);
        lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP); lv_obj_set_height(hint,py(88));
    }
    pager(s_parent,28,388,330,s_music_page,s_av.library.music_count,5);
    lv_obj_t *player=box(s_parent,382,128,614,300,s_palette.panel,18);
    lv_obj_t *cover=box(player,22,22,140,124,s_palette.tint,16);
    lv_obj_t *disc=box(cover,18,10,104,104,s_palette.accent,LV_RADIUS_CIRCLE);
    lv_obj_t *ring=box(disc,25,25,54,54,s_palette.panel,LV_RADIUS_CIRCLE);
    box(ring,18,18,18,18,s_palette.accent,LV_RADIUS_CIRCLE);
    label(player,"播放器",184,16,400,&dashboard_text_18,s_palette.muted);
    s_title=label(player,"",184,46,400,&dashboard_wifi_18,s_palette.ink);
    s_detail=label(player,"",184,84,400,&dashboard_text_18,s_palette.muted); s_notice=s_detail;
    lv_label_set_long_mode(s_detail,LV_LABEL_LONG_WRAP); lv_obj_set_height(s_detail,py(60));
    s_time=label(player,"",24,182,242,&dashboard_text_18,s_palette.muted);
    const char *icons[]={LV_SYMBOL_PREV,LV_SYMBOL_PLAY,LV_SYMBOL_NEXT,LV_SYMBOL_STOP};
    for (unsigned i=0;i<4;++i) {
        lv_obj_t *b=box(player,282+i*78,168,64,58,i==1?s_palette.accent:s_palette.tint,16);
        lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(b,control,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        lv_obj_t *symbol=label(b,icons[i],0,16,64,&lv_font_montserrat_24,i==1?s_palette.on_accent:s_palette.accent);
        lv_obj_set_style_text_align(symbol,LV_TEXT_ALIGN_CENTER,0); if (i==1) s_play=symbol;
    }
    label(player,LV_SYMBOL_VOLUME_MAX,24,253,28,&lv_font_montserrat_24,s_palette.muted);
    s_volume=lv_slider_create(player); lv_obj_set_pos(s_volume,px(76),py(263)); lv_obj_set_size(s_volume,px(422),py(8));
    lv_slider_set_range(s_volume,0,100);
    lv_obj_set_style_bg_color(s_volume,lv_color_hex(s_palette.track),LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume,lv_color_hex(s_palette.accent),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume,lv_color_hex(s_palette.accent),LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume,volume_changed,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_add_event_cb(s_volume,volume_changed,LV_EVENT_RELEASED,NULL);
    s_volume_text=label(player,"",530,254,64,&dashboard_text_18,s_palette.ink);
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
static void voice_page(void)
{
    lv_obj_t *panel=box(s_parent,28,84,968,344,s_palette.panel,18);
    lv_obj_t *disc=box(panel,40,58,184,184,s_palette.tint,LV_RADIUS_CIRCLE);
    lv_obj_t *name=label(disc,"小智",12,78,160,&dashboard_text_22,s_palette.accent);
    lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
    label(panel,"语音助手",40,266,184,&dashboard_text_18,s_palette.muted);
    s_voice_heading=label(panel,"",280,26,660,&dashboard_text_22,s_palette.ink);
    s_detail=label(panel,"",280,76,660,&dashboard_text_22,s_palette.accent); s_notice=s_detail;
    s_voice_toggle=button(panel,"关闭助手",280,140,202,52,true,voice_control,0);
    s_voice_start=button(panel,"开始对话",502,140,202,52,false,voice_control,1);
    s_voice_stop=button(panel,"结束对话",724,140,202,52,false,voice_control,2);
    lv_obj_t *hint=label(panel,"开启后说“你好小智”，或点击开始对话。\n回复结束后可以直接追问，无需再次唤醒。",280,222,660,&dashboard_text_18,s_palette.muted);
    lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP); lv_obj_set_height(hint,py(58));
    label(panel,"关闭助手后停止唤醒监听；开启状态会自动保存",280,298,660,&dashboard_text_18,s_palette.muted);
}
static void refresh(void)
{
    if (!s_parent) return;
    char context[96]; snprintf(context,sizeof(context),"%s  ·  %s",s_av.library.storage?"SD 卡已就绪":"未检测到 SD 卡",s_status.network?"Wi-Fi 已连接":"Wi-Fi 未连接");
    lv_label_set_text(s_context,context);
    if (s_tab==AV_MUSIC) {
        bool active=s_status.state==MEDIA_PLAYING || s_status.state==MEDIA_BUFFERING || s_status.state==MEDIA_CONNECTING;
        lv_label_set_text(s_title,*s_status.title?s_status.title:"请选择 SD 卡音乐");
        lv_label_set_text(s_detail,*s_status.detail?s_status.detail:"选择 SD 卡音乐开始播放");
        lv_obj_set_style_text_color(s_detail,lv_color_hex(s_status.state==MEDIA_ERROR?s_palette.red:s_palette.muted),0);
        lv_label_set_text(s_play,active?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
        char time[64]; unsigned seconds=s_status.played_ms/1000;
        snprintf(time,sizeof(time),"%02u:%02u · %s",seconds/60,seconds%60,"SD 卡");
        lv_label_set_text(s_time,time);
        if (!lv_obj_has_state(s_volume,LV_STATE_PRESSED)) {
            lv_slider_set_value(s_volume,s_status.volume,LV_ANIM_OFF);
            char volume[16]; snprintf(volume,sizeof(volume),"%u%%",s_status.volume); lv_label_set_text(s_volume_text,volume);
        }
        for (unsigned i=0;i<5;++i) if (s_rows[i]) {
            bool selected=s_status.selected && s_music_page*5+i==s_status.local_index;
            lv_obj_set_style_border_color(s_rows[i],lv_color_hex(s_palette.accent),0);
            lv_obj_set_style_border_width(s_rows[i],selected?2:0,0);
            lv_label_set_text(s_marks[i],selected && active?LV_SYMBOL_AUDIO:LV_SYMBOL_PLAY);
        }
    } else if (s_tab==AV_CAMERA) {
        if (!s_album) {
            button_text(s_cam_toggle,s_av.camera_enabled?"关闭摄像头":"开启摄像头");
            lv_label_set_text(s_detail,*s_av.camera_detail?s_av.camera_detail:"摄像头已关闭");
            enabled(s_photo_button,s_av.camera_enabled && s_av.camera_state==3 && s_av.library.storage);
            lv_label_set_text(s_notice,*s_av.library.photo_detail?s_av.library.photo_detail:s_av.library.storage?"照片保存在 SD 卡 photos 文件夹":"拍照需要插入 SD 卡");
        } else lv_label_set_text(s_notice,*s_av.library.photo_detail?s_av.library.photo_detail:"选择照片后在左侧查看");
    } else if (s_tab==AV_VOICE) {
        lv_label_set_text(s_voice_heading,s_av.voice_enabled?"助手已开启":"助手已关闭");
        lv_label_set_text(s_detail,*s_av.voice_detail?s_av.voice_detail:"正在准备语音助手");
        button_text(s_voice_toggle,s_av.voice_enabled?"关闭助手":"开启助手");
        enabled(s_voice_start,s_av.voice_enabled && !s_av.voice_active && s_status.network);
        enabled(s_voice_stop,s_av.voice_active);
    }
}
static void rebuild(void)
{
    if (!s_parent) return;
    lv_obj_clean(s_parent);
    memset(s_rows,0,sizeof(s_rows)); memset(s_marks,0,sizeof(s_marks));
    s_title=s_detail=s_time=s_play=s_volume=s_volume_text=s_canvas=s_canvas_note=NULL;
    s_cam_toggle=s_photo_button=s_voice_toggle=s_voice_start=s_voice_stop=s_voice_heading=s_notice=NULL;
    const char *tabs[]={"音乐","FM 电台","摄像头","语音助手"};
    const av_tab_t ids[]={AV_MUSIC,AV_FM,AV_CAMERA,AV_VOICE};
    unsigned visible_tab=0;
    int step=DEVICE_CAMERA_AVAILABLE?124:156;
    for (unsigned i=0;i<4;++i) {
        if (!DEVICE_CAMERA_AVAILABLE && ids[i]==AV_CAMERA) continue;
        button(s_parent,tabs[i],28+visible_tab++*step,10,step-8,44,s_tab==ids[i],choose_tab,ids[i]);
    }
    s_context=label(s_parent,"",530,22,466,&dashboard_text_18,s_palette.muted);
    lv_obj_set_style_text_align(s_context,LV_TEXT_ALIGN_RIGHT,0);
    if (s_tab==AV_MUSIC) music_page(); else if (s_tab==AV_CAMERA) camera_page(); else if (s_tab==AV_VOICE) voice_page();
    else if (s_tab==AV_FM) box(s_parent,28,84,968,344,s_palette.panel,18);
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
    bool changed=!s_status_ready || status->revision!=s_status.revision ||
        status->network!=s_status.network || status->state!=s_status.state ||
        status->selected!=s_status.selected || status->local_index!=s_status.local_index ||
        status->volume!=s_status.volume || strcmp(status->title,s_status.title) ||
        strcmp(status->detail,s_status.detail);
    s_status=*status; s_status_ready=true;
    if (changed) refresh();
}
void media_panel_av_update(const av_status_t *status)
{
    if (!status) return;
    bool library_changed=status->library.revision!=s_av.library.revision;
    bool changed=!s_av_ready || library_changed ||
        status->camera_enabled!=s_av.camera_enabled || status->voice_enabled!=s_av.voice_enabled ||
        status->voice_active!=s_av.voice_active || status->camera_state!=s_av.camera_state ||
        status->voice_state!=s_av.voice_state || strcmp(status->camera_detail,s_av.camera_detail) ||
        strcmp(status->voice_detail,s_av.voice_detail) ||
        status->library.storage!=s_av.library.storage || status->library.scanning!=s_av.library.scanning ||
        status->library.music_count!=s_av.library.music_count || status->library.photo_count!=s_av.library.photo_count ||
        strcmp(status->library.detail,s_av.library.detail) || strcmp(status->library.photo_detail,s_av.library.photo_detail);
    s_av=*status; s_av_ready=true;
    if (!changed) return;
    if (s_parent && library_changed && (s_tab==AV_MUSIC || (s_tab==AV_CAMERA && s_album))) rebuild();
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
