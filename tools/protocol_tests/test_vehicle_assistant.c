#include "vehicle_metrics.h"
#include "alarm_model.h"
#include "media_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    obd2_vehicle_data_t d={.link_state=OBD_LINK_ACTIVE,.coolant_temp_valid=true,.coolant_temp_c=93,
        .speed_valid=true,.control_voltage_valid=true,.control_voltage_mv=14320,.trip_valid=true,
        .trip_distance_tenths=123,.fuel_rate_valid=true,.fuel_rate_20_lph=53};
    vehicle_metric_t metrics[VEHICLE_METRIC_COUNT];
    assert(vehicle_metrics(&d,metrics,VEHICLE_METRIC_COUNT)==VEHICLE_METRIC_COUNT);
    assert(metrics[0].valid && metrics[0].value==93 && !strcmp(metrics[0].unit,"C"));
    assert(metrics[1].valid && metrics[1].value==0); /* stationary is a real reading */
    assert(!metrics[8].valid && !metrics[20].valid); /* unsupported is not zero */
    assert(metrics[6].value>14.319 && metrics[6].value<14.321);
    assert(metrics[11].value==2.65 && metrics[15].value==12.3 && metrics[15].calculated);
    d.coolant_temp_valid=false;vehicle_metrics(&d,metrics,VEHICLE_METRIC_COUNT);assert(!metrics[0].valid);
    d.link_state=OBD_LINK_WAITING;vehicle_metrics(&d,metrics,VEHICLE_METRIC_COUNT);
    for(unsigned i=0;i<VEHICLE_METRIC_COUNT;++i)assert(!metrics[i].valid);

    const int64_t now=1735689600; /* 2025-01-01 08:00 Beijing */
    assert(alarm_next(now,8,1)==now+60 && alarm_next(now,7,0)==now+23*3600);
    assert(alarm_next(now,8,0)==now+86400 && !alarm_next(0,8,0) && !alarm_next(now,24,0));
    alarm_store_t s={.version=1,.next_id=1};assert(alarm_store_valid(&s));
    int once=alarm_put(&s,now,8,1,false,0,"one");assert(once==1);
    assert(alarm_put(&s,now,8,1,false,0,"one")==once && s.next_id==2);
    int daily=alarm_put(&s,now,8,2,true,0,"daily");assert(daily==2);
    bool changed=true;assert(!alarm_due(&s,now+59,&changed) && !changed);
    assert(alarm_due(&s,now+60,&changed)==1 && changed && !s.items[0].enabled);
    assert(!alarm_due(&s,now+60,&changed) && !changed);
    assert(alarm_due(&s,now+120,&changed)==2 && s.items[1].due==now+120+86400);
    assert(!alarm_due(&s,now-3600,&changed)); /* backwards clock does not replay */
    int timer=alarm_put(&s,now,0,0,false,10,"timer");assert(timer==3);
    assert(!alarm_due(&s,now+900,&changed) && changed); /* stale timer skipped */
    assert(alarm_remove(&s,2) && !alarm_remove(&s,2) && !alarm_remove(&s,0));
    assert(alarm_store_valid(&s));
    assert(alarm_put(&s,now,24,0,false,0,"bad")==-1);
    assert(alarm_put(&s,now,0,0,true,1,"bad")==-1);
    assert(alarm_put(&s,0,8,0,false,0,"bad")==-1);
    char long_label[64];memset(long_label,'x',63);long_label[63]=0;
    assert(alarm_put(&s,now,8,0,false,0,long_label)==-1);
    s=(alarm_store_t){.version=1,.next_id=1};
    for(unsigned i=0;i<ALARM_LIMIT;++i)assert(alarm_put(&s,now,9,i,false,0,"alarm")>0);
    assert(alarm_put(&s,now,10,0,false,0,"full")==-1);
    s.items[1].id=s.items[0].id;assert(!alarm_store_valid(&s));
    assert(media_url_valid("https://example.org/music.mp3?token=abc%20def"));
    assert(media_url_valid("http://192.168.1.2:8000/song.mp3"));
    const char *bad[]={"file:///music.mp3","http://","https:///file","https://user:secret@example.org/a.mp3",
        "https://example.org/a\r\nHeader:x","https://example.org/a b.mp3","https://example.org/a#b","https://host\\evil/a"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!media_url_valid(bad[i]));
    puts("Vehicle availability/scaling, persistent alarm scheduling and online URL validation passed");return 0;
}
