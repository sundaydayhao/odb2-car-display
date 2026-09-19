#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "obd2_decode.h"

static void sample(obd_cache_t *cache, unsigned index, unsigned raw, int64_t now)
{
    elm_pid_reply_t reply={.length=obd_parameters[index].bytes};
    reply.data[0]=reply.length==2 ? raw>>8 : raw;
    reply.data[1]=raw&255;
    assert(obd_cache_apply(cache,index,&reply,now));
}
static void handshake(void)
{
    const char *challenges[]={"00000000","12345678","6997EB7C","FFFFFFFF","80000000"};
    const char *answers[]={"0E7E7E7E","DEFEFEAD","D89B4FAD","80FF7FFF","0EFE0000"};
    for (unsigned i=0;i<5;++i) {
        char input[256], command[20], expected[20];
        snprintf(input,sizeof(input),"SHENZHEN YUMING ELECTRONICS CO., LTD.\r\nDEVICE TYPE:B02-Z\r\nCRYPT:%s\r\n>",challenges[i]);
        snprintf(expected,sizeof(expected),"AT+SETCRYPT%s",answers[i]);
        /* Actual BLE notification fragmentation, using the production accumulator. */
        elm_response_t response; elm_response_reset(&response);
        for (unsigned j=0;j<strlen(input);++j) elm_response_feed(&response,(uint8_t *)input+j,1);
        assert(elm_adapter_handshake(&response,command)==1 && !strcmp(command,expected));
        assert(strlen(command)==19); /* 11-byte prefix + 8 hexadecimal digits. */
    }
    const char *bad[]={
        "OTHER\rCRYPT:12345678\r>",
        "SHENZHEN YUMING ELECTRONICS CO., LTD.\rDEVICE TYPE:B02-Z\rCRYPT:1234\r>",
        "SHENZHEN YUMING ELECTRONICS CO., LTD.\rDEVICE TYPE:B02-Z\rCRYPT:12345678\rCRYPT:87654321\r>",
        "SHENZHEN YUMING ELECTRONICS CO., LTD.\rDEVICE TYPE:OTHER\rCRYPT:12345678\r>",
        "SHENZHEN YUMING ELECTRONICS CO., LTD.\rDEVICE TYPE:B02-Z\rCRYPT:12345678ATZ\r>",
        "SHENZHEN YUMING ELECTRONICS CO., LTD.\rDEVICE TYPE:B02-Z\rCRYPT:12345678\r"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        elm_response_t r; char command[20]; elm_response_reset(&r);
        elm_response_feed(&r,(const uint8_t *)bad[i],strlen(bad[i]));
        assert(elm_adapter_handshake(&r,command)==-1 && !*command);
    }
    elm_response_t r; char command[20]; elm_response_reset(&r);
    elm_response_feed(&r,(const uint8_t *)"?\r>",3);
    assert(elm_adapter_handshake(&r,command)==0 && !*command);
}
int main(void)
{
    handshake();
    obd_cache_t c={0};
    for (int64_t t=1000;t<=61000;t+=100) {
        sample(&c,0,36,t); sample(&c,1,4000,t); sample(&c,11,72,t);
    }
    assert(c.data.trip_valid && c.data.trip_distance_tenths==6 && c.data.trip_minutes==1);
    assert(c.data.trip_speed_tenths==360 && c.data.trip_idle_seconds==0);
    assert(c.data.trip_fuel_valid && c.data.trip_fuel_hundredths==6);
    assert(c.data.instant_fuel_valid && c.data.instant_fuel_tenths==100);
    assert(c.data.average_fuel_valid && c.data.average_fuel_tenths==100);
    sample(&c,0,0,61100);
    assert(!c.data.instant_fuel_valid); /* Stationary fuel use is not L/100 km. */
    for (int64_t t=61200;t<=63100;t+=100) { sample(&c,0,0,t); sample(&c,1,3200,t); sample(&c,11,16,t); }
    assert(c.data.trip_idle_valid && c.data.trip_idle_seconds==2);
    obd_cache_invalidate(&c,0); obd_cache_expire(&c,63200);
    assert(!c.data.trip_valid && !c.data.average_fuel_valid && !c.data.trip_idle_valid);
    sample(&c,0,36,63300);
    assert(!c.data.trip_valid && c.data.trip_distance_tenths==0);
    sample(&c,0,36,63400);
    assert(c.data.trip_valid && !c.data.average_fuel_valid);
    obd_cache_expire(&c,70000);
    assert(!c.data.trip_valid && !c.data.trip_fuel_valid && !c.data.instant_fuel_valid);
    /* Time discontinuities and reconnection cannot integrate stale values. */
    obd_cache_expire(&c,100);
    assert(!c.data.speed_valid && !c.data.fuel_rate_valid);
    c=(obd_cache_t){0}; sample(&c,0,36,1000);
    assert(!c.data.trip_valid && c.data.trip_distance_tenths==0);

    obd_poll_t poll={0}; bool supported[OBD_PARAMETER_COUNT]; unsigned reads[OBD_PARAMETER_COUNT]={0};
    memset(supported,true,sizeof(supported));
    for (int64_t t=0;t<60000;t+=100) {
        int i=obd_poll_next(&poll,supported,t);
        if (i>=0) { ++reads[i]; obd_poll_result(&poll,i,true,t); }
    }
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) assert(reads[i]>=10);
    assert(reads[0]>reads[4] && reads[11]>reads[5]);
    memset(supported,0,sizeof(supported)); supported[0]=true;
    poll=(obd_poll_t){0};
    for (int64_t t=0;t<30000;t+=100) {
        int i=obd_poll_next(&poll,supported,t);
        if (i>=0) { assert(i==0); obd_poll_result(&poll,i,false,t); }
    }
    assert(poll.failures[0]==4);
    obd_poll_result(&poll,0,true,30000);
    assert(poll.failures[0]==0 && poll.due[0]==30250);
    puts("Adapter handshake, fresh-sample integration, expiry, PID scheduling/backoff passed.");
}
