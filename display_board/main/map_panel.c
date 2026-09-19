#include "map_panel.h"
#include "map_model.h"
#include "map_render.h"
#include "fonts/dashboard_fonts.h"
#include "ui_assets.h"
#include "ui_update.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static map_model_t *s_model;
static map_render_work_t *s_work;
static uint16_t *s_pixels,*s_back;
static map_model_t *s_job;
static map_colors_t s_colors,s_job_colors;
static lv_timer_t *s_render_timer;
static bool s_busy,s_frame_valid,s_job_track;
static unsigned s_buffer_width,s_buffer_height;
static uint32_t s_revision=1,s_job_revision,s_frame_revision,s_started;
static map_panel_stats_t s_stats;
static lv_obj_t *s_parent,*s_canvas,*s_status,*s_coords,*s_distance,*s_speed,*s_points,*s_follow,*s_track,*s_scale,*s_loading;
static lv_obj_t *s_places[MAP_PLACE_COUNT],*s_bar;
static dashboard_palette_t s_palette;
static bool s_show_track=true;
static lv_point_t s_touch;
static int s_width,s_height;
static int px(int x) { return x*s_width/1024; }
static int py(int y) { return y*s_height/456; }
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius)
{
    lv_obj_t *o=lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,px(x),py(y)); lv_obj_set_size(o,px(w),py(h));
    lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0); return o;
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int w,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *o=lv_label_create(parent); lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o,px(x),py(y)); lv_obj_set_width(o,px(w));
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT); lv_label_set_text(o,text); return o;
}
static bool model_ready(void)
{
    if (!s_model) { s_model=lv_malloc(sizeof(*s_model)); if (s_model) map_model_init(s_model,724,420); }
    return s_model!=NULL;
}
static uint16_t rgb565(uint32_t rgb) { return (uint16_t)(((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x1f)); }
static uint32_t blend(uint32_t a,uint32_t b)
{ return (((a&0xfefefe)+(b&0xfefefe))>>1)+(a&b&0x010101); }
static void render_labels(void)
{
    if (!s_parent || !s_canvas || !s_model) return;
    const dashboard_palette_t *p=&s_palette;
    double scale=s_model->base_scale*s_model->zoom;
    lv_area_t occupied[MAP_PLACE_COUNT]; unsigned count=0;
    for (unsigned i=0;i<MAP_PLACE_COUNT;++i) {
        lv_obj_t *o=s_places[i];
        int w=lv_text_get_width(map_places[i].name,strlen(map_places[i].name),&dashboard_text_18,0)+2,h=dashboard_text_18.line_height;
        int x=(int)lround(s_model->width/2+(map_places[i].x-s_model->x)*scale)-w/2;
        int y=(int)lround(s_model->height/2-(map_places[i].y-s_model->y)*scale)-h/2;
        bool visible=s_frame_valid && x>=8 && y>=48 && x+w<s_model->width-8 && y+h<s_model->height-8;
        if (x+w>s_model->width-76 && y<164) visible=false;
        if (x<px(350) && y+h>py(346)) visible=false;
        if (x+w>px(580) && y+h>py(356)) visible=false;
        for (unsigned j=0;visible && j<count;++j)
            if (x<occupied[j].x2+6 && x+w>occupied[j].x1-6 && y<occupied[j].y2+3 && y+h>occupied[j].y1-3) visible=false;
        if (visible) {
            occupied[count++]=(lv_area_t){x,y,x+w,y+h}; lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
            lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
    }
    const gps_fix_t *f=&s_model->fix;
    ui_label_text(s_status,f->error?"定位串口异常":s_model->valid?"卫星定位正常":f->valid?"定位信号中断":"等待卫星定位");
    ui_text_color(s_status,lv_color_hex(s_model->valid?p->accent:p->muted));
    char text[80];
    if (s_model->valid) snprintf(text,sizeof(text),"纬度 %.5f\n经度 %.5f",f->latitude,f->longitude);
    else snprintf(text,sizeof(text),"暂无有效位置\n显示中国概览");
    ui_label_text(s_coords,text);
    snprintf(text,sizeof(text),"%.2f km",s_model->distance_m/1000); ui_label_text(s_distance,text);
    if (s_model->valid && f->speed_valid) snprintf(text,sizeof(text),"%.0f km/h",f->speed_kph);
    else snprintf(text,sizeof(text),"-- km/h");
    ui_label_text(s_speed,text);
    snprintf(text,sizeof(text),"轨迹点  %u / %u",s_model->count,MAP_TRACK_CAPACITY); ui_label_text(s_points,text);
    if (s_model->valid) lv_obj_remove_state(s_follow,LV_STATE_DISABLED); else lv_obj_add_state(s_follow,LV_STATE_DISABLED);
    ui_label_text(lv_obj_get_child(s_follow,0),s_model->following?"正在跟随":"跟随位置");
    ui_label_text(lv_obj_get_child(s_track,0),s_show_track?"隐藏轨迹":"显示轨迹");
    double per_pixel=111320*cos(map_unproject_lat(s_model->y)*3.141592653589793/180)/scale;
    double span=per_pixel*px(90),unit=pow(10,floor(log10(fmax(1,span))));
    double metres=(span/unit>=5?5:span/unit>=2?2:1)*unit;
    if (metres>=1000) snprintf(text,sizeof(text),"%.0f km",metres/1000); else snprintf(text,sizeof(text),"%.0f m",metres);
    ui_label_text(s_scale,text); lv_obj_set_width(s_bar,(int)fmax(1,metres/per_pixel));
}
static void bind_frame(void)
{
    lv_canvas_set_buffer(s_canvas,s_pixels,s_model->width,s_model->height,LV_COLOR_FORMAT_RGB565);
    /* A reused descriptor address must not retain an older image's dimensions. */
    lv_image_set_src(s_canvas,lv_canvas_get_draw_buf(s_canvas));
    lv_obj_invalidate(s_canvas);
}
static void render_slice(lv_timer_t *timer)
{
    if(!s_parent || !s_canvas){lv_timer_pause(timer);s_busy=false;return;}
    uint32_t slice_start=lv_tick_get();
    if(!s_busy){
        *s_job=*s_model;s_job_colors=s_colors;s_job_track=s_show_track;
        s_job_revision=s_revision;s_started=lv_tick_get();s_busy=true;
        map_render_begin(s_work,s_job);++s_stats.jobs;
    }
    bool done=false;
    /* Cooperate with the existing LVGL thread, no worker touching live widgets.
     * A fixed cap also bounds each call when the host test uses a virtual clock. */
    for(unsigned i=0;i<1024 && lv_tick_elaps(slice_start)<4;++i)
        if(map_render_step(s_back,s_job,&s_job_colors,s_work,s_job_track)){done=true;break;}
    uint32_t slice_ms=lv_tick_elaps(slice_start);
    if(slice_ms>s_stats.max_slice_ms)s_stats.max_slice_ms=slice_ms;
    ++s_stats.slices;
    if(!done)return;
    s_busy=false;
    if(s_job_revision==s_revision){
        uint16_t *old=s_pixels;s_pixels=s_back;s_back=old;
        s_frame_valid=true;s_frame_revision=s_revision;
        s_stats.last_render_ms=lv_tick_elaps(s_started);++s_stats.frames;
        bind_frame();render_labels();lv_obj_add_flag(s_loading,LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(timer);
    }
    /* If requests changed, the next slice starts one snapshot of the newest
     * state. An incomplete/obsolete frame is never exposed on the canvas. */
    slice_ms=lv_tick_elaps(slice_start);
    if(slice_ms>s_stats.max_slice_ms)s_stats.max_slice_ms=slice_ms;
}
static void request_render(bool cancel)
{
    ++s_revision;
    if(!s_parent || !s_canvas || !s_render_timer)return;
    if(cancel && s_busy){s_busy=false;++s_stats.cancelled;}
    render_labels();
    lv_timer_resume(s_render_timer);
}
bool map_panel_busy(void)
{ return s_parent && s_canvas && (!s_frame_valid || s_frame_revision!=s_revision); }
void map_panel_stats(map_panel_stats_t *out)
{
    if(!out)return;
    *out=s_stats;out->busy=map_panel_busy();out->visible=s_parent!=NULL;
}
static void control(lv_event_t *event)
{
    unsigned id=(uintptr_t)lv_event_get_user_data(event);
    if (id==0) map_model_zoom(s_model,2);
    if (id==1) map_model_zoom(s_model,.5);
    if (id==2) map_model_china(s_model);
    if (id==3) map_model_follow(s_model);
    if (id==4) s_show_track=!s_show_track;
    request_render(true);
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,unsigned id,bool primary)
{
    lv_obj_t *o=box(parent,x,y,w,h,primary?s_palette.tint:s_palette.panel,12);
    lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_opa(o,LV_OPA_40,LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(o,LV_OPA_70,LV_STATE_PRESSED);
    lv_obj_add_event_cb(o,control,LV_EVENT_CLICKED,(void *)(uintptr_t)id);
    lv_obj_t *l=label(o,text,4,(h-24)/2,w-8,&dashboard_text_18,primary?s_palette.accent:s_palette.ink);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0); return o;
}
static void drag(lv_event_t *event)
{
    lv_indev_t *input=lv_indev_active(); if (!input || !s_model) return;
    lv_point_t point; lv_indev_get_point(input,&point);
    if (lv_event_get_code(event)==LV_EVENT_PRESSED) s_touch=point;
    else if (lv_event_get_code(event)==LV_EVENT_PRESSING) {
        int dx=point.x-s_touch.x,dy=point.y-s_touch.y; s_touch=point;
        if (dx || dy) {
            map_model_pan(s_model,dx,dy);
            request_render(true);
        }
    }
}
void map_panel_create(lv_obj_t *parent,const dashboard_palette_t *palette)
{
    s_parent=parent; s_palette=*palette; s_width=lv_obj_get_width(parent); s_height=lv_obj_get_height(parent);
    if (!model_ready()) { label(parent,"地图内存不足",28,24,600,&dashboard_text_22,palette->muted); return; }
    int w=px(724)&~1,h=py(420);
    map_colors_t colors={rgb565(palette->bg),rgb565(palette->panel),rgb565(palette->tint),
        rgb565(palette->line),rgb565(blend(palette->muted,palette->tint)),rgb565(palette->accent),rgb565(palette->on_accent)};
    bool changed=memcmp(&colors,&s_colors,sizeof(colors)) || s_model->width!=w || s_model->height!=h;
    s_colors=colors;
    if((unsigned)w!=s_buffer_width || (unsigned)h!=s_buffer_height || !s_pixels || !s_back){
        lv_free(s_pixels);lv_free(s_back);s_pixels=s_back=NULL;s_frame_valid=false;
        s_pixels=lv_malloc((size_t)w*h*2);s_back=lv_malloc((size_t)w*h*2);
        s_buffer_width=w;s_buffer_height=h;changed=true;
    }
    if(!s_work)s_work=lv_malloc(sizeof(*s_work));
    if(!s_job)s_job=lv_malloc(sizeof(*s_job));
    if(!s_render_timer){s_render_timer=lv_timer_create(render_slice,8,NULL);if(s_render_timer)lv_timer_pause(s_render_timer);}
    if(!s_pixels || !s_back || !s_work || !s_job || !s_render_timer){
        lv_free(s_pixels);lv_free(s_back);s_pixels=s_back=NULL;s_frame_valid=false;
        label(parent,"地图内存不足",28,24,600,&dashboard_text_22,palette->muted);return;
    }
    map_model_resize(s_model,w,h);
    if(changed){++s_revision;s_frame_valid=false;}
    if(!s_frame_valid)for(int i=0;i<w*h;++i)s_pixels[i]=s_colors.water;
    lv_obj_t *map=box(parent,24,12,724,420,palette->bg,24);
    lv_obj_set_style_clip_corner(map,true,0);
    s_canvas=lv_canvas_create(map); lv_obj_remove_style_all(s_canvas);
    lv_obj_set_size(s_canvas,w,h);
    bind_frame();
    lv_obj_add_flag(s_canvas,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_canvas,drag,LV_EVENT_PRESSED,NULL);
    lv_obj_add_event_cb(s_canvas,drag,LV_EVENT_PRESSING,NULL);
    lv_obj_add_event_cb(s_canvas,drag,LV_EVENT_RELEASED,NULL);
    s_loading=label(map,"正在加载地图",160,190,400,&dashboard_text_22,palette->muted);
    lv_obj_set_style_text_align(s_loading,LV_TEXT_ALIGN_CENTER,0);
    if(s_frame_valid)lv_obj_add_flag(s_loading,LV_OBJ_FLAG_HIDDEN);
    for (unsigned i=0;i<MAP_PLACE_COUNT;++i) s_places[i]=label(map,map_places[i].name,0,0,100,&dashboard_text_18,palette->muted);
    lv_obj_t *tag=box(map,16,14,136,36,palette->panel,12);
    label(tag,"中国离线地图",12,5,116,&dashboard_text_18,palette->ink);
    button(map,"+",664,16,44,44,0,false); button(map,"-",664,68,44,44,1,false);
    s_track=button(map,"隐藏轨迹",588,364,120,40,4,false);
    s_scale=label(map,"",18,350,140,&dashboard_text_18,palette->muted);
    s_bar=box(map,18,378,90,2,palette->muted,0);
    label(map,"Natural Earth · 区域概览",18,389,340,&dashboard_text_18,palette->muted);
    lv_obj_t *side=box(parent,766,12,234,420,palette->panel,24);
    ui_asset_create(side,UI_MAP,px(18),py(21),palette->accent);
    s_status=label(side,"",54,22,164,&dashboard_text_18,palette->muted);
    s_coords=label(side,"",18,70,198,&dashboard_text_18,palette->muted);
    lv_label_set_long_mode(s_coords,LV_LABEL_LONG_WRAP); lv_obj_set_height(s_coords,py(62));
    label(side,"轨迹里程",18,146,198,&dashboard_text_18,palette->muted);
    s_distance=label(side,"",18,173,198,&lv_font_montserrat_32,palette->ink);
    label(side,"定位速度",18,224,198,&dashboard_text_18,palette->muted);
    s_speed=label(side,"",18,251,198,&dashboard_text_22,palette->ink);
    s_points=label(side,"",18,287,198,&dashboard_text_18,palette->muted);
    s_follow=button(side,"跟随位置",18,324,198,38,3,true);
    button(side,"全国视图",18,372,198,38,2,false);
    render_labels();
    if(s_frame_valid && s_frame_revision==s_revision){++s_stats.cache_hits;lv_timer_pause(s_render_timer);}
    else lv_timer_resume(s_render_timer);
}
void map_panel_detach(void)
{
    /* Stop the cooperative job before deleting its widgets. Keep one cached
     * frame in PSRAM so returning to an unchanged view needs no rasterization. */
    if(s_render_timer)lv_timer_pause(s_render_timer);
    if(s_busy)++s_stats.cancelled;
    s_busy=false;
    if (s_canvas) lv_obj_delete(s_canvas);
    s_canvas=NULL; s_parent=NULL;
    s_loading=NULL;
}
void map_panel_update(const gps_fix_t *fix,uint32_t now)
{ if (model_ready() && map_model_update(s_model,fix,now)) request_render(false); }
