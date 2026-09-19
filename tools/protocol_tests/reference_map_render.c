/* Host-only baseline rasterizer for pixel regression; never linked into firmware. */
#include "map_render.h"
typedef struct { float x[MAP_MAX_RING],y[MAP_MAX_RING],crossings[MAP_MAX_RING]; } reference_work_t;
#include <math.h>
#include <stdlib.h>

static int compare(const void *a,const void *b)
{ float x=*(const float *)a,y=*(const float *)b; return (x>y)-(x<y); }
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
void map_render_reference(uint16_t *p,const map_model_t *m,const map_colors_t *c,void *scratch,bool show_track)
{
    reference_work_t *work=scratch;
    int w=m->width,h=m->height; double scale=m->base_scale*m->zoom;
    double ox=w/2.0-m->x*scale,oy=h/2.0+m->y*scale;
    for (int i=0;i<w*h;++i) p[i]=c->water;
    for (unsigned r=0;r<MAP_RING_COUNT;++r) {
        const map_ring_t *ring=&map_rings[r];
        double left=ox+ring->x1*.0001*scale,right=ox+ring->x2*.0001*scale;
        double top=oy-ring->y2*.0001*scale,bottom=oy-ring->y1*.0001*scale;
        if (right<0 || left>=w || bottom<0 || top>=h) continue;
        for (unsigned i=0;i<ring->count;++i) {
            const map_vertex_t *v=&map_vertices[ring->first+i];
            work->x[i]=(float)(ox+v->x*.0001*scale); work->y[i]=(float)(oy-v->y*.0001*scale);
        }
        if (ring->layer<2) {
            uint16_t fill=ring->hole?c->water:ring->layer?c->region:c->land;
            int y1=(int)fmax(0,ceil(top)),y2=(int)fmin(h-1,floor(bottom));
            for (int y=y1;y<=y2;++y) {
                unsigned n=0; double scan=y+.5;
                for (unsigned i=0,j=ring->count-1;i<ring->count;j=i++) {
                    double a=work->y[i],b=work->y[j];
                    if ((a>scan)!=(b>scan)) work->crossings[n++]=(float)(work->x[i]+(scan-a)*(work->x[j]-work->x[i])/(b-a));
                }
                qsort(work->crossings,n,sizeof(float),compare);
                for (unsigned i=0;i+1<n;i+=2) {
                    int x1=(int)fmax(0,ceil(work->crossings[i])),x2=(int)fmin(w-1,floor(work->crossings[i+1]));
                    for (int x=x1;x<=x2;++x) p[y*w+x]=fill;
                }
            }
        }
        for (unsigned i=1;i<ring->count;++i)
            line(p,w,h,work->x[i-1],work->y[i-1],work->x[i],work->y[i],ring->layer==2?c->province:c->border,0);
    }
    if (show_track) for (unsigned i=1;i<m->count;++i) {
        const map_track_point_t *a=map_model_point(m,i-1),*b=map_model_point(m,i);
        if (!b->start && fabs(a->x-b->x)<180)
            line(p,w,h,ox+a->x*scale,oy-a->y*scale,ox+b->x*scale,oy-b->y*scale,c->track,2);
    }
    if (m->valid) {
        double x=ox+m->fix.longitude*scale,y=oy-map_project_lat(m->fix.latitude)*scale;
        if (x>=-10 && x<w+10 && y>=-10 && y<h+10) {
            int cx=(int)lround(x),cy=(int)lround(y);
            for (int dy=-9;dy<=9;++dy) for (int dx=-9;dx<=9;++dx) {
                int d=dx*dx+dy*dy;
                if (d<=81 && cx+dx>=0 && cx+dx<w && cy+dy>=0 && cy+dy<h)
                    p[(cy+dy)*w+cx+dx]=d<=25?c->marker:c->track;
            }
        }
    }
}
