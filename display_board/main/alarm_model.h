#pragma once
#include <stdint.h>
#include <stdbool.h>
enum { ALARM_LIMIT=4,ALARM_LABEL_SIZE=48 };
typedef struct { int64_t due; uint32_t id; uint8_t hour,minute; bool daily,enabled; char label[ALARM_LABEL_SIZE]; } alarm_item_t;
typedef struct { uint32_t version,next_id; alarm_item_t items[ALARM_LIMIT]; } alarm_store_t;
typedef struct { alarm_store_t store; bool clock_ready,ringing; uint32_t ringing_id,revision; char detail[96]; } alarm_status_t;
int64_t alarm_next(int64_t now,unsigned hour,unsigned minute);
bool alarm_store_valid(const alarm_store_t *store);
/* delay_minutes=0 means next clock time; otherwise a one-shot timer. UTC+8. */
int alarm_put(alarm_store_t *store,int64_t now,unsigned hour,unsigned minute,bool daily,unsigned delay_minutes,const char *label);
bool alarm_remove(alarm_store_t *store,uint32_t id);
/* Returns a due ID; advances/disables expired entries without replaying old alarms. */
uint32_t alarm_due(alarm_store_t *store,int64_t now,bool *changed);
