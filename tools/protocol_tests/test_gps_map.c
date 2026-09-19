#include "gps_model.h"
#include "map_model.h"
#include "map_render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void map_render_reference(uint16_t *,const map_model_t *,const map_colors_t *,void *,bool);

static bool feed(gps_nmea_t *p,gps_fix_t *f,const char *s,uint32_t now)
{
    bool result=false;
    while (*s) result=gps_nmea_feed(p,*s++,now,f)||result;
    return result;
}
static bool sentence(gps_nmea_t *p,gps_fix_t *f,const char *body,uint32_t now)
{
    unsigned sum=0; for (const char *s=body;*s;++s) sum^=(unsigned char)*s;
    char line[256]; snprintf(line,sizeof(line),"$%s*%02X\r\n",body,sum);
    return feed(p,f,line,now);
}
static void parser_tests(void)
{
    gps_nmea_t p={0}; gps_fix_t f={0};
    /* Literal sample from the user's ATGM336H-5N manual, page 6. */
    const char *rmc="$GNRMC,084852.000,A,2236.9453,N,11408.4790,E,0.53,292.44,141216,,,A*75\r\n";
    assert(feed(&p,&f,rmc,100));
    assert(f.valid && f.sequence==1 && fabs(f.latitude-22.615755)<1e-7);
    assert(fabs(f.longitude-114.1413166667)<1e-7 && fabs(f.speed_kph-.98156)<1e-5);
    assert(gps_fix_current(&f,10099) && !gps_fix_current(&f,10100));
    assert(!feed(&p,&f,"$GNRMC,084852.000,A,2236.9453,N,11408.4790,E,0.53,292.44,141216,,,A*74\n",200));
    assert(f.received_ms==100 && f.sequence==1);
    assert(!feed(&p,&f,"$GPRMC,123519,A,4807.038,N,",300));
    assert(feed(&p,&f,"01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n",300));
    assert(fabs(f.latitude-48.1173)<1e-6 && fabs(f.speed_kph-41.4848)<1e-4);
    assert(sentence(&p,&f,"BDRMC,120000,A,3300.000,S,07000.000,W,,0,200926,,,D",400));
    assert(f.latitude==-33 && f.longitude==-70 && !f.speed_valid);
    const char *invalid[]={
        "GNRMC,240000,A,2236.9453,N,11408.4790,E,0,0,200926,,,A",
        "GNRMC,126000,A,2236.9453,N,11408.4790,E,0,0,200926,,,A",
        "GNRMC,120000,A,2260.0000,N,11408.4790,E,0,0,200926,,,A",
        "GNRMC,120000,A,9000.0001,N,11408.4790,E,0,0,200926,,,A",
        "GNRMC,120000,A,2236.9453,N,18000.0001,E,0,0,200926,,,A",
        "GNRMC,120000,A,2236.9453,X,11408.4790,E,0,0,200926,,,A",
        "GNRMC,120000,A,2236.9453,N,11408.4790,E,NaN,0,200926,,,A",
        "GNRMC,120000,A,2236.9453,N,11408.4790,E,1e10,0,200926,,,A",
        "GNRMC,120000,Z,2236.9453,N,11408.4790,E,0,0,200926,,,A",
        "GNGGA,084852.000,2236.9453,N,11408.4790,E,1,05,3.1,89.7,M,0.0,M,,"
    };
    uint32_t seq=f.sequence;
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) assert(!sentence(&p,&f,invalid[i],500));
    assert(f.sequence==seq && f.received_ms==400);
    assert(sentence(&p,&f,"GNRMC,120000,V,,,,,,,200926,,,N",600) && !f.valid);
    assert(sentence(&p,&f,"GNRMC,,V,,,,,,,,,,N",650) && !f.valid);
    assert(sentence(&p,&f,"GNRMC,120001,A,2236.9453,N,11408.4790,E,0,0,200926,,,S",700) && !f.valid);
    gps_nmea_feed(&p,'$',800,&f);
    for (unsigned i=0;i<300;++i) assert(!gps_nmea_feed(&p,'A',800,&f));
    assert(!feed(&p,&f,"\r\n",800));
    assert(feed(&p,&f,rmc,UINT32_MAX-100));
    assert(gps_fix_current(&f,100) && !gps_fix_current(&f,10000));
    f.latitude=NAN; assert(!gps_fix_current(&f,100));
}
static void model_tests(void)
{
    map_model_t m; map_model_init(&m,724,420);
    assert(m.x==104.5 && m.zoom==1 && !m.valid && !m.count);
    assert(fabs(map_unproject_lat(map_project_lat(35))-35)<1e-9);
    map_model_zoom(&m,2); map_model_pan(&m,80,20); assert(m.zoom==2 && m.x<104.5);
    map_model_china(&m); assert(m.zoom==1 && m.x==104.5);
    gps_fix_t f={.enabled=true,.valid=true,.latitude=22.6,.longitude=114.1,.sequence=1,.received_ms=1000};
    assert(map_model_update(&m,&f,1000) && m.following && m.count==1 && m.zoom==1024);
    assert(map_model_point(&m,0)->start);
    assert(!map_model_update(&m,&f,1000));
    for (unsigned i=0;i<20;++i) { ++f.sequence; f.received_ms+=1000; map_model_update(&m,&f,f.received_ms); }
    assert(m.count==1); /* Stationary fresh fixes must not create periodic segments. */
    f.longitude+=.0001; ++f.sequence; f.received_ms+=1000;
    map_model_update(&m,&f,f.received_ms);
    assert(m.count==2 && m.distance_m>9 && m.distance_m<12 && !map_model_point(&m,1)->start);
    map_model_pan(&m,100,0); double x=m.x;
    ++f.sequence; f.received_ms+=1000; f.longitude+=.0001;
    map_model_update(&m,&f,f.received_ms); assert(!m.following && m.x==x);
    map_model_follow(&m); assert(m.following && m.x==f.longitude);
    double travelled=m.distance_m;
    map_model_update(&m,&f,f.received_ms+GPS_FIX_TTL_MS);
    assert(!m.valid && m.zoom==1 && m.x==104.5);
    f.received_ms+=GPS_FIX_TTL_MS+1000; ++f.sequence; f.longitude+=1;
    map_model_update(&m,&f,f.received_ms);
    assert(m.valid && m.following && m.distance_m==travelled && map_model_point(&m,m.count-1)->start);
    ++f.sequence; f.received_ms+=1000; f.longitude+=1;
    map_model_update(&m,&f,f.received_ms);
    assert(m.distance_m==travelled && map_model_point(&m,m.count-1)->start);
    for (unsigned i=0;i<600;++i) {
        ++f.sequence; f.received_ms+=1000; f.longitude+=.0001;
        map_model_update(&m,&f,f.received_ms);
    }
    assert(m.count==MAP_TRACK_CAPACITY && map_model_point(&m,MAP_TRACK_CAPACITY)==NULL);
    assert(m.distance_m>travelled+5900 && m.distance_m<travelled+6600);
    map_model_zoom(&m,1e10); assert(m.zoom==4096);
    map_model_zoom(&m,1e-10); assert(m.zoom==1);
    f.valid=false; ++f.sequence; assert(map_model_update(&m,&f,f.received_ms));
    assert(!m.valid && m.x==104.5 && m.zoom==1);
}
static void render_tests(void)
{
    enum {W=724,H=420};
    uint16_t *guard=malloc((W*H+2)*sizeof(*guard)); assert(guard);
    map_render_work_t *work=malloc(sizeof(*work)); assert(work);
    void *reference_work=malloc(3*MAP_MAX_RING*sizeof(float));assert(reference_work);
    uint16_t *reference=malloc(W*H*sizeof(*reference));assert(reference);
    map_model_t m; map_model_init(&m,W,H);
    map_colors_t colors={1,2,3,4,5,6,7};
    guard[0]=0xa55a; guard[W*H+1]=0x5aa5;
    map_render(guard+1,&m,&colors,work,true);
    unsigned land=0,water=0;
    for (unsigned i=1;i<=W*H;++i) { land+=guard[i]==3; water+=guard[i]==1; }
    assert(land>10000 && water>10000);
    printf("China raster: %lu active-edge intersections, %lu bounded steps\n",
        (unsigned long)work->intersections,(unsigned long)work->steps);
    clock_t old_ticks=0,new_ticks=0;
    unsigned max_changed=0;
    for (unsigned i=0;i<MAP_RING_COUNT;++i) assert(map_rings[i].count<=MAP_MAX_RING);
    for (unsigned i=0;i<40;++i) {
        m.x=70+i*2; m.y=map_project_lat(20+i); m.zoom=i%2?4096:1;
        clock_t start=clock();map_render_reference(reference,&m,&colors,reference_work,i%2);old_ticks+=clock()-start;
        start=clock();map_render(guard+1,&m,&colors,work,i%2);new_ticks+=clock()-start;
        unsigned changed=0;
        for(unsigned j=0;j<W*H;++j)changed+=reference[j]!=guard[j+1];
        if(changed>max_changed)max_changed=changed;
        /* Only single-precision edge-boundary rounding may differ. */
        assert(changed<W*H/2000);
        assert(guard[0]==0xa55a && guard[W*H+1]==0x5aa5);
    }
    m.zoom=1; m.valid=true; m.fix.latitude=30; m.fix.longitude=105;
    m.track[0]=(map_track_point_t){-170,-180,true}; m.track[1]=(map_track_point_t){5,180,false}; m.count=2;
    map_render(guard+1,&m,&colors,work,true);
    assert(guard[0]==0xa55a && guard[W*H+1]==0x5aa5);
    memcpy(reference,guard+1,W*H*sizeof(*reference));
    map_render_begin(work,&m);
    unsigned steps=0;
    while(!map_render_step(guard+1,&m,&colors,work,true))assert(++steps<100000);
    assert(!memcmp(reference,guard+1,W*H*sizeof(*reference)));
    /* Abandoning a partial image and beginning another job resets all cursors. */
    map_render_begin(work,&m);
    for(unsigned i=0;i<450;++i)map_render_step(guard+1,&m,&colors,work,true);
    map_model_china(&m);map_render_begin(work,&m);
    while(!map_render_step(guard+1,&m,&colors,work,false)){}
    map_render(reference,&m,&colors,work,false);
    assert(!memcmp(reference,guard+1,W*H*sizeof(*reference)));
    printf("40 host views: reference %.2fms, active edges %.2fms; max pixel difference %u/%u\n",
        old_ticks*1000.0/CLOCKS_PER_SEC,new_ticks*1000.0/CLOCKS_PER_SEC,max_changed,W*H);
    free(reference_work);free(reference);free(work); free(guard);
}
int main(void)
{
    parser_tests(); model_tests(); render_tests();
    puts("GPS NMEA, map state, track continuity and clipped rendering passed"); return 0;
}
