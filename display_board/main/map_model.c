#include "map_model.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846
static double clamp(double x,double lo,double hi) { return x<lo?lo:x>hi?hi:x; }
double map_project_lat(double latitude)
{ return log(tan(PI/4+clamp(latitude,-85.05112878,85.05112878)*PI/360))*180/PI; }
double map_unproject_lat(double y) { return (2*atan(exp(y*PI/180))-PI/2)*180/PI; }
void map_model_china(map_model_t *m)
{ m->x=104.5; m->y=(map_project_lat(16)+map_project_lat(55))/2; m->zoom=1; m->following=false; }
void map_model_resize(map_model_t *m,int width,int height)
{
    m->width=width; m->height=height;
    double a=fmax(1,width-70)/65.0,b=fmax(1,height-70)/(map_project_lat(55)-map_project_lat(16));
    m->base_scale=fmin(a,b);
}
void map_model_init(map_model_t *m,int width,int height)
{ memset(m,0,sizeof(*m)); map_model_resize(m,width,height); map_model_china(m); }
void map_model_zoom(map_model_t *m,double factor)
{ if (isfinite(factor) && factor>0) m->zoom=clamp(m->zoom*factor,1,4096); }
void map_model_pan(map_model_t *m,double dx,double dy)
{
    if (!isfinite(dx) || !isfinite(dy)) return;
    m->following=false;
    m->x=clamp(m->x-dx/(m->base_scale*m->zoom),-180,180);
    m->y=clamp(m->y+dy/(m->base_scale*m->zoom),-180,180);
}
void map_model_follow(map_model_t *m)
{
    if (!m->valid) return;
    m->following=true; m->x=m->fix.longitude; m->y=map_project_lat(m->fix.latitude);
    if (m->zoom<1024) m->zoom=1024;
}
static double distance(double lat1,double lon1,double lat2,double lon2)
{
    double a=(lat2-lat1)*PI/180,b=(lon2-lon1)*PI/180;
    double h=sin(a/2)*sin(a/2)+cos(lat1*PI/180)*cos(lat2*PI/180)*sin(b/2)*sin(b/2);
    return 12742000*asin(sqrt(clamp(h,0,1)));
}
const map_track_point_t *map_model_point(const map_model_t *m,unsigned i)
{ return i<m->count?&m->track[(m->first+i)%MAP_TRACK_CAPACITY]:NULL; }
bool map_model_update(map_model_t *m,const gps_fix_t *fix,uint32_t now)
{
    if (!fix) return false;
    bool valid=gps_fix_current(fix,now), previous=m->valid;
    uint32_t previous_ms=m->fix.received_ms;
    bool changed=valid!=m->valid || fix->error!=m->fix.error || fix->enabled!=m->fix.enabled;
    m->fix=*fix; m->valid=valid;
    if (!valid) {
        m->have_last=false;
        if (previous) map_model_china(m);
        return changed;
    }
    if (m->have_sequence && m->sequence==fix->sequence) return changed;
    m->have_sequence=true; m->sequence=fix->sequence;
    if (!previous) map_model_follow(m);
    if (m->following) { m->x=fix->longitude; m->y=map_project_lat(fix->latitude); }
    double metres=m->have_last?distance(m->last_lat,m->last_lon,fix->latitude,fix->longitude):0;
    uint32_t dt=fix->received_ms-m->last_ms;
    bool segment=!m->have_last || (uint32_t)(fix->received_ms-previous_ms)>=GPS_FIX_TTL_MS ||
        (metres>30 && (dt==0 || metres*1000/dt>90));
    if (segment || metres>=5) {
        if (m->count==MAP_TRACK_CAPACITY) { m->first=(m->first+1)%MAP_TRACK_CAPACITY; --m->count; }
        m->track[(m->first+m->count++)%MAP_TRACK_CAPACITY]=(map_track_point_t){fix->longitude,map_project_lat(fix->latitude),segment};
        if (!segment) m->distance_m+=metres;
        m->last_lat=fix->latitude; m->last_lon=fix->longitude; m->last_ms=fix->received_ms; m->have_last=true;
    }
    return true;
}
