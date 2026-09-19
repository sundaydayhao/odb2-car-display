#include "alarm_model.h"
#include <string.h>
#include <stdio.h>
int64_t alarm_next(int64_t now,unsigned hour,unsigned minute)
{
    if(now<1735689600 || hour>23 || minute>59)return 0;
    int64_t due=(now+28800)/86400*86400-28800+hour*3600+minute*60;
    return due<=now?due+86400:due;
}
bool alarm_store_valid(const alarm_store_t *s)
{
    if(!s || s->version!=1 || !s->next_id || s->next_id>0x7fffffff)return false;
    for(unsigned i=0;i<ALARM_LIMIT;++i){
        const alarm_item_t *a=&s->items[i];
        if(a->hour>23 || a->minute>59 || !memchr(a->label,0,sizeof(a->label)))return false;
        if(a->id && (a->id>=s->next_id || a->due<1735689600))return false;
        for(unsigned j=0;a->id && j<i;++j)if(a->id==s->items[j].id)return false;
    }
    return true;
}
int alarm_put(alarm_store_t *s,int64_t now,unsigned hour,unsigned minute,bool daily,unsigned delay,const char *label)
{
    if(!s || now<1735689600 || hour>23 || minute>59 || delay>10080 || (daily&&delay) ||
       !label || strlen(label)>=ALARM_LABEL_SIZE || !alarm_store_valid(s) || s->next_id>=0x7fffffff)return -1;
    int64_t due=delay?now+delay*60:alarm_next(now,hour,minute);
    /* Repeated identical tool calls must not create duplicate alarms. */
    for(unsigned i=0;i<ALARM_LIMIT;++i){alarm_item_t *a=&s->items[i];
        if(a->enabled && a->due==due && a->daily==daily && !strcmp(a->label,label))return (int)a->id;
    }
    for(unsigned i=0;i<ALARM_LIMIT;++i)if(!s->items[i].id || !s->items[i].enabled){
        alarm_item_t a={.due=due,.id=s->next_id++,.daily=daily,.enabled=true};
        unsigned tod=(unsigned)((due+28800)%86400);a.hour=tod/3600;a.minute=tod/60%60;
        snprintf(a.label,sizeof(a.label),"%s",label);s->items[i]=a;return (int)a.id;
    }
    return -1;
}
bool alarm_remove(alarm_store_t *s,uint32_t id)
{
    if(!s || !id)return false;
    for(unsigned i=0;i<ALARM_LIMIT;++i)if(s->items[i].id==id){memset(&s->items[i],0,sizeof(s->items[i]));return true;}
    return false;
}
uint32_t alarm_due(alarm_store_t *s,int64_t now,bool *changed)
{
    if(changed)*changed=false;
    if(!s || now<1735689600)return 0;
    uint32_t ringing=0;
    for(unsigned i=0;i<ALARM_LIMIT;++i){alarm_item_t *a=&s->items[i];
        if(!a->enabled || now<a->due)continue;
        if(now-a->due<120 && !ringing)ringing=a->id;
        else if(now-a->due<120)continue; /* deliver another simultaneous alarm on next poll */
        if(a->daily)a->due=alarm_next(now,a->hour,a->minute);else a->enabled=false;
        if(changed)*changed=true;
    }
    return ringing;
}
