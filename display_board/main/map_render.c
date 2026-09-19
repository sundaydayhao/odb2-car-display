#include "map_render.h"
#include <math.h>
#include <stdlib.h>

static int first_row(const void *a,const void *b)
{ const map_scan_edge_t *x=a,*y=b; return (x->first>y->first)-(x->first<y->first); }
static int crossing(const void *a,const void *b)
{ const map_scan_edge_t *x=a,*y=b; return (x->crossing>y->crossing)-(x->crossing<y->crossing); }
static int code(double x,double y,int w,int h)
{ return (x<0?1:x>w-1?2:0)|(y<0?4:y>h-1?8:0); }
/* Clip before integer conversion: deep zooms can project far outside the screen. */
static void line(uint16_t *p,int w,int h,double x1,double y1,double x2,double y2,uint16_t c,int radius)
{
    int a=code(x1,y1,w,h),b=code(x2,y2,w,h);
    for (unsigned attempt=0;a|b;++attempt) {
        if ((a&b) || attempt>=8) return;
        int out=a?a:b; double x,y;
        if (out&8) { y=h-1; x=x1+(x2-x1)*(y-y1)/(y2-y1); }
        else if (out&4) { y=0; x=x1+(x2-x1)*(y-y1)/(y2-y1); }
        else if (out&2) { x=w-1; y=y1+(y2-y1)*(x-x1)/(x2-x1); }
        else { x=0; y=y1+(y2-y1)*(x-x1)/(x2-x1); }
        if (out==a) { x1=x; y1=y; a=code(x,y,w,h); } else { x2=x; y2=y; b=code(x,y,w,h); }
    }
    int x=(int)lround(x1),y=(int)lround(y1),ex=(int)lround(x2),ey=(int)lround(y2);
    int dx=abs(ex-x),dy=-abs(ey-y),sx=x<ex?1:-1,sy=y<ey?1:-1,err=dx+dy;
    for (;;) {
        for (int v=y-radius;v<=y+radius;++v) if (v>=0 && v<h)
            for (int u=x-radius;u<=x+radius;++u) if (u>=0 && u<w) p[v*w+u]=c;
        if (x==ex && y==ey) break;
        int e=2*err; if (e>=dy) { err+=dy; x+=sx; } if (e<=dx) { err+=dx; y+=sy; }
    }
}
enum { CLEAR, RING, PROJECT, EDGES, FILL, BORDER, TRACK, MARKER, DONE };
void map_render_begin(map_render_work_t *work,const map_model_t *m)
{
    work->phase=CLEAR;work->ring=0;work->row=0;work->track=1;
    work->intersections=work->steps=0;
    work->scale=m->base_scale*m->zoom;
    work->ox=m->width/2.0-m->x*work->scale;
    work->oy=m->height/2.0+m->y*work->scale;
}
bool map_render_step(uint16_t *p,const map_model_t *m,const map_colors_t *c,map_render_work_t *work,bool show_track)
{
    int w=m->width,h=m->height;
    double scale=work->scale,ox=work->ox,oy=work->oy;
    const map_ring_t *ring=work->ring<MAP_RING_COUNT?&map_rings[work->ring]:NULL;
    ++work->steps;
    switch(work->phase) {
    case CLEAR:
        for(int x=0;x<w;++x)p[work->row*w+x]=c->water;
        if(++work->row>=h)work->phase=RING;
        break;
    case RING: {
        if(!ring){work->phase=TRACK;break;}
        double left=ox+ring->x1*.0001*scale,right=ox+ring->x2*.0001*scale;
        double top=oy-ring->y2*.0001*scale,bottom=oy-ring->y1*.0001*scale;
        if(right<0 || left>=w || bottom<0 || top>=h){++work->ring;break;}
        work->row=(int)fmax(0,ceil(top));work->last_row=(int)fmin(h-1,floor(bottom));
        work->vertex=0;work->edge_count=work->next_edge=work->active_count=0;
        work->phase=PROJECT;break;
    }
    case PROJECT:
        for(unsigned n=0;n<32 && work->vertex<ring->count;++n,++work->vertex){
            const map_vertex_t *v=&map_vertices[ring->first+work->vertex];
            /* Keep geographic projection in double precision, only once per
             * vertex. Scan rows below use precomputed single-precision edges. */
            work->x[work->vertex]=(float)(ox+v->x*.0001*scale);
            work->y[work->vertex]=(float)(oy-v->y*.0001*scale);
        }
        if(work->vertex==ring->count){work->vertex=0;work->phase=ring->layer<2?EDGES:BORDER;}
        break;
    case EDGES:
        for(unsigned n=0;n<32 && work->vertex<ring->count;++n,++work->vertex){
            unsigned i=work->vertex,j=i?i-1:ring->count-1u;
            float a=work->y[i],b=work->y[j];
            if(a==b)continue;
            float first=fmaxf((float)work->row,ceilf(fminf(a,b)-.5f));
            float last=fminf((float)work->last_row,ceilf(fmaxf(a,b)-.5f)-1);
            if(first>last)continue;
            work->edges[work->edge_count++]=(map_scan_edge_t){
                .first=(int)first,.last=(int)last,.x=work->x[i],.y=a,
                .dx=(work->x[j]-work->x[i])/(b-a)};
        }
        if(work->vertex==ring->count){
            qsort(work->edges,work->edge_count,sizeof(work->edges[0]),first_row);
            work->vertex=1;work->phase=FILL;
        }
        break;
    case FILL: {
        if(work->row>work->last_row){work->phase=BORDER;work->vertex=1;break;}
        unsigned count=0;
        for(unsigned i=0;i<work->active_count;++i)
            if(work->active[i].last>=work->row)work->active[count++]=work->active[i];
        while(work->next_edge<work->edge_count && work->edges[work->next_edge].first<=work->row)
            work->active[count++]=work->edges[work->next_edge++];
        work->active_count=count;
        for(unsigned i=0;i<count;++i){
            map_scan_edge_t *e=&work->active[i];
            e->crossing=e->x+((float)work->row+.5f-e->y)*e->dx;
        }
        work->intersections+=count;
        qsort(work->active,count,sizeof(work->active[0]),crossing);
        uint16_t fill=ring->hole?c->water:ring->layer?c->region:c->land;
        for(unsigned i=0;i+1<count;i+=2){
            int x1=(int)fmaxf(0,ceilf(work->active[i].crossing));
            int x2=(int)fminf(w-1,floorf(work->active[i+1].crossing));
            for(int x=x1;x<=x2;++x)p[work->row*w+x]=fill;
        }
        ++work->row;break;
    }
    case BORDER:
        if(work->vertex==0)work->vertex=1;
        for(unsigned n=0;n<8 && work->vertex<ring->count;++n,++work->vertex){
            unsigned i=work->vertex;
            line(p,w,h,work->x[i-1],work->y[i-1],work->x[i],work->y[i],ring->layer==2?c->province:c->border,0);
        }
        if(work->vertex==ring->count){++work->ring;work->phase=RING;}
        break;
    case TRACK:
        if(!show_track || work->track>=m->count){work->phase=MARKER;break;}
        for(unsigned n=0;n<8 && work->track<m->count;++n,++work->track){
            const map_track_point_t *a=map_model_point(m,work->track-1),*b=map_model_point(m,work->track);
            if(!b->start && fabs(a->x-b->x)<180)
                line(p,w,h,ox+a->x*scale,oy-a->y*scale,ox+b->x*scale,oy-b->y*scale,c->track,2);
        }
        break;
    case MARKER:
        if(m->valid){
            double x=ox+m->fix.longitude*scale,y=oy-map_project_lat(m->fix.latitude)*scale;
            if(x>=-10 && x<w+10 && y>=-10 && y<h+10){
                int cx=(int)lround(x),cy=(int)lround(y);
                for(int dy=-9;dy<=9;++dy)for(int dx=-9;dx<=9;++dx){
                    int d=dx*dx+dy*dy;
                    if(d<=81 && cx+dx>=0 && cx+dx<w && cy+dy>=0 && cy+dy<h)
                        p[(cy+dy)*w+cx+dx]=d<=25?c->marker:c->track;
                }
            }
        }
        work->phase=DONE;break;
    default: return true;
    }
    return work->phase==DONE;
}
void map_render(uint16_t *p,const map_model_t *m,const map_colors_t *c,map_render_work_t *work,bool show_track)
{
    map_render_begin(work,m);
    while(!map_render_step(p,m,c,work,show_track)){}
}
