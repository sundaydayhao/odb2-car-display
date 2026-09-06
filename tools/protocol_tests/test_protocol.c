#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "elm327_protocol.h"
#include "obd2_decode.h"

static unsigned checks;
static elm_response_t response(const char *text)
{
    elm_response_t r;
    elm_response_reset(&r);
    elm_response_feed(&r,(const uint8_t *)text,strlen(text));
    return r;
}

static void expect(const char *text, uint8_t pid, uint32_t ecu, const uint8_t *bytes, size_t length)
{
    /* Every two-fragment boundary must decode identically. */
    for (size_t split=0;split<=strlen(text);++split) {
        elm_response_t r; elm_response_reset(&r);
        elm_response_feed(&r,(const uint8_t *)text,split);
        elm_response_feed(&r,(const uint8_t *)text+split,strlen(text)-split);
        elm_pid_reply_t reply;
        assert(elm_decode_pid(&r,pid,ecu,&reply));
        assert(reply.length==length);
        assert(!memcmp(reply.data,bytes,length));
        if (ecu!=ELM_ECU_ANY) assert(reply.ecu==ecu);
        ++checks;
    }
}

static void reject(const char *text, uint8_t pid, uint32_t ecu)
{
    elm_response_t r=response(text);
    elm_pid_reply_t reply;
    assert(!elm_decode_pid(&r,pid,ecu,&reply));
    ++checks;
}

int main(void)
{
    expect("010C\rSEARCHING...\r7E8 04 41 0C 26 48\r>",0x0C,0x7E8,(uint8_t[]){0x26,0x48},2);
    expect("7e804410c2648\r\n>",0x0C,0x7E8,(uint8_t[]){0x26,0x48},2);
    expect("7E8 8 04 41 0C 26 48 00 00 00\r>",0x0C,0x7E8,(uint8_t[]){0x26,0x48},2);
    expect("18DAF11004410C2648\r>",0x0C,0x18DAF110,(uint8_t[]){0x26,0x48},2);
    expect("18 DA F1 10 03 41 0D 56 00 00 00 00\r>",0x0D,0x18DAF110,(uint8_t[]){86},1);
    expect("41 0D 56\r>",0x0D,0,(uint8_t[]){86},1);
    expect("SEARCHING...7E8 03 41 05 81\r>",0x05,0x7E8,(uint8_t[]){129},1);
    expect("7E9 03 41 0D 10\r7E8 03 41 0D 56\r>",0x0D,ELM_ECU_ANY,(uint8_t[]){86},1);
    expect("7E8 03 41 0D 56\r7E9 03 41 0D 10\r>",0x0D,0x7E9,(uint8_t[]){16},1);
    expect("7E8 10 0A 41 0C 01 02 03 04\r7E9 10 0A 41 0C AA BB CC DD\r7E8 21 05 06 07 08 00 00 00\r>",0x0C,0x7E8,(uint8_t[]){1,2,3,4,5,6,7,8},8);
    reject("7E8 04 41 0C 26 48\r",0x0C,0x7E8);
    reject("7E8 04 41 0C 26\r>",0x0C,0x7E8);
    reject("7E8 04 41 0C 26 48 00 00 00 00\r>",0x0C,0x7E8);
    reject("7E9 04 41 0C 26 48\r>",0x0C,0x7E8);
    reject("NO DATA\r>",0x0C,ELM_ECU_ANY);
    reject("UNABLE TO CONNECT\r>",0x0C,ELM_ECU_ANY);
    reject("CAN ERROR\rBUS INIT: ERROR\rSTOPPED\r?\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 03 7F 01 12\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 04 41 05 26 48\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 04 41 0C Z6 48\r>",0x0C,ELM_ECU_ANY);
    reject("garbage 7E8 04 41 0C 26 48\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 10 0A 41 0C 01 02 03 04\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 10 0A 41 0C 01 02 03 04\r7E8 22 05 06 07 08\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 1F FF 41 0C 01 02 03 04\r7E8 21 05 06 07 08\r>",0x0C,ELM_ECU_ANY);
    reject("7E8 04 41 0C 26 48\r>7E8 03 41 0D 56\r>",0x0C,ELM_ECU_ANY);
    elm_response_t r=response("ATE0\rOK\r>");
    assert(elm_response_is_ok(&r));
    r=response("NOTOK\r>"); assert(!elm_response_is_ok(&r));
    r=response("ATZ\rELM327 v1.5\r>"); assert(elm_response_is_adapter(&r));
    r=response("ATDPN\rA6\r>"); assert(elm_response_is_can_protocol(&r));
    r=response("ATDPN\r3\r>"); assert(!elm_response_is_can_protocol(&r));
    r=response("ATDPN\rA6\r"); assert(!elm_response_is_can_protocol(&r));
    uint8_t flood[ELM_RESPONSE_CAPACITY+8]; memset(flood,'A',sizeof(flood));
    elm_response_reset(&r); elm_response_feed(&r,flood,sizeof(flood));
    assert(r.invalid && r.length==ELM_RESPONSE_CAPACITY-1);
    elm_pid_reply_t bitmap={.length=4,.data={0x80,0,0,1}};
    assert(elm_supported_pid(&bitmap,0,1));
    assert(elm_supported_pid(&bitmap,0,32));
    assert(!elm_supported_pid(&bitmap,0,0));
    assert(!elm_supported_pid(&bitmap,0,33));
    assert(!elm_supported_pid(&bitmap,0,12));
    assert(elm_supported_pid(&bitmap,32,33));
    assert(elm_supported_pid(&bitmap,32,64));
    bitmap.length=3; assert(!elm_supported_pid(&bitmap,0,1));
    obd_cache_t cache={0};
    const uint8_t values[][2]={{86,0},{0x26,0x48},{255,0},{255,0},{0,0},{255,0},{0x37,0x78},{72,0},{138,0},{84,0}};
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) {
        elm_pid_reply_t reply={.length=obd_parameters[i].bytes};
        memcpy(reply.data,values[i],2);
        assert(obd_cache_apply(&cache,i,&reply,100));
        reply.length=0; assert(!obd_cache_apply(&cache,i,&reply,100));
    }
    assert(cache.data.speed_kph==86 && cache.data.rpm==2450);
    assert(cache.data.coolant_temp_c==-40 && cache.data.oil_temp_c==98 && cache.data.intake_temp_c==32);
    assert(cache.data.control_voltage_mv==14200 && cache.data.manifold_pressure_kpa==84);
    assert(cache.data.engine_load_percent==100 && cache.data.throttle_percent==100 && cache.data.fuel_percent==100);
    obd_cache_expire(&cache,2100);
    assert(!cache.data.speed_valid && !cache.data.rpm_valid);
    assert(cache.data.fuel_valid && cache.data.coolant_temp_valid);
    obd_cache_expire(&cache,15100);
    assert(!cache.data.fuel_valid && !cache.data.coolant_temp_valid);
    /* Invalid packets cannot turn unavailable data into a real zero. */
    elm_pid_reply_t zero={.length=1,.data={0}};
    assert(obd_cache_apply(&cache,0,&zero,16000));
    assert(cache.data.speed_valid && cache.data.speed_kph==0);
    obd_cache_invalidate(&cache,0); assert(!cache.data.speed_valid);
    uint32_t random=0x9E3779B9;
    for (unsigned attempt=0;attempt<10000;++attempt) {
        elm_response_reset(&r);
        for (unsigned i=0;i<256;++i) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            uint8_t byte=(uint8_t)random;
            elm_response_feed(&r,&byte,1);
        }
        elm_pid_reply_t reply;
        if (elm_decode_pid(&r,0x0C,ELM_ECU_ANY,&reply)) assert(reply.length<=ELM_PAYLOAD_CAPACITY);
        assert(r.length<ELM_RESPONSE_CAPACITY);
    }
    printf("Protocol tests passed: %u fragmentation/response cases; bitmap, decoding and per-field expiration checks passed.\n",checks);
    return 0;
}
