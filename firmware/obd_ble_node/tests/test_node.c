#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "node_core.h"
#include "node_power.h"
#include "elm327_protocol.h"

typedef struct { char out[16000]; size_t len; node_bus_mode_t mode; unsigned rate, count; node_frame_t tx; bool fail_output, fail_bus; } fake_t;
static fake_t f; static node_core_t c;
static bool emit(void *ctx,const char *s,size_t n) { fake_t *x=ctx; if(x->fail_output) return false; assert(x->len+n<sizeof(x->out)); memcpy(x->out+x->len,s,n);x->len+=n;x->out[x->len]=0;return true; }
static bool bus(void *ctx,node_bus_mode_t m,unsigned b) { fake_t *x=ctx;x->mode=m;x->rate=b;return !x->fail_bus; }
static bool tx(void *ctx,const node_frame_t *frame) { fake_t *x=ctx;x->tx=*frame;x->count++;return true; }
static int voltage(void *ctx) { (void)ctx;return 13800; }
static bool record(void *ctx,bool on) { (void)ctx;(void)on;return true; }
static void stats(void *ctx,char *s,size_t n) { (void)ctx;snprintf(s,n,"TEST"); }
static void clear(void) { f.len=0;f.out[0]=0; }
static void cmd(const char *s,uint64_t ms) { node_core_feed(&c,(const uint8_t *)s,strlen(s),ms); }
static void setup(void) { memset(&f,0,sizeof(f));node_io_t io={.emit=emit,.bus=bus,.transmit=tx,.voltage_mv=voltage,.record=record,.stats=stats,.ctx=&f};node_core_init(&c,&io);cmd("ATE0\rATSP6\r",0);clear(); }
static void response_compatibility(void) {
    setup();cmd("ATZ\r",0);elm_response_t response;elm_response_reset(&response);
    elm_response_feed(&response,(const uint8_t *)f.out,f.len);assert(elm_response_is_adapter(&response));
    cmd("ATE0\rATSP6\r",0);clear();
    /* Fragmented GATT writes and two Camry ECU replies. */
    cmd("01",100);assert(!c.pending);cmd("0C\r",101);assert(c.pending && f.count==1);
    assert(f.tx.id==0x7DF && !f.tx.extended && f.tx.dlc==8 && f.tx.data[0]==2 && f.tx.data[2]==0x0C);
    node_frame_t frame={.id=0x7E8,.dlc=8,.data={4,0x41,0x0C,0x0B,0x0F,0,0,0}};
    node_core_receive(&c,&frame,150);frame.id=0x7E9;node_core_receive(&c,&frame,160);node_core_tick(&c,501);
    assert(!c.pending && f.mode==NODE_BUS_LISTEN && strstr(f.out,"7E8 04 41 0C 0B 0F\r"));
    elm_response_reset(&response);elm_response_feed(&response,(const uint8_t *)f.out,f.len);
    elm_pid_reply_t reply;assert(elm_decode_pid(&response,0x0C,0x7E8,&reply));
    assert(reply.length==2 && reply.data[0]==0x0B && reply.data[1]==0x0F);
    assert(((reply.data[0]<<8)|reply.data[1])/4.0==707.75);
    puts("PASS: fragmented input and production display parser / two ECUs");
}
static void reject_and_filter(void) {
    setup();const char *bad[]={"04\r","2701\r","2EF19000\r","ATSH123\r","ATSP5\r","010CFF\r","ATCAF0\r010C\r"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) cmd(bad[i],0);
    assert(f.count==0);cmd("ATCAF1\rATCRA7E8\r010D\r",10);clear();
    node_frame_t frame={.id=0x7E9,.dlc=8,.data={3,0x41,0x0D,42}};
    node_core_receive(&c,&frame,11);frame.id=0x7E8;frame.data[0]=0x10;node_core_receive(&c,&frame,12);
    frame.data[0]=3;frame.dlc=3;node_core_receive(&c,&frame,13);
    frame.dlc=8;frame.data[2]=5;node_core_receive(&c,&frame,14);
    assert(!c.received && f.len==0);node_core_tick(&c,410);assert(strstr(f.out,"NO DATA"));
    puts("PASS: write services, malformed frames and wrong ECU/PID rejected");
}
static void protocols_and_monitor(void) {
    setup();cmd("ATSP0\r0100\r",0);assert(f.rate==500000 && !f.tx.extended);
    node_core_tick(&c,400);assert(c.protocol==7 && f.tx.extended && f.tx.id==0x18DB33F1);
    node_core_tick(&c,800);assert(!c.pending && c.protocol==7 && f.rate==500000);
    /* Baud changes require an explicit host selection, never blind probing. */
    cmd("ATSP8\rATSP0\r0100\r",800);assert(c.protocol==8 && f.rate==250000 && !f.tx.extended);
    node_core_tick(&c,1200);assert(c.protocol==9 && f.rate==250000 && f.tx.extended);
    node_frame_t frame={.id=0x18DAF110,.extended=true,.dlc=8,.data={6,0x41,0,0xBE,0x1F,0xA0,0x13}};
    node_core_receive(&c,&frame,1201);node_core_tick(&c,1600);assert(c.auto_locked);
    clear();cmd("ATDPN\r",1700);assert(strstr(f.out,"A9"));
    cmd("ATMA\r",1800);assert(c.monitor && f.mode==NODE_BUS_LISTEN);clear();
    frame.id=0x123;frame.extended=false;frame.dlc=8;node_core_receive(&c,&frame,1810);assert(strstr(f.out,"123 "));
    cmd("\r",1900);assert(!c.monitor && strstr(f.out,"STOPPED"));
    cmd("ATMA\r",2000);f.fail_output=true;node_core_receive(&c,&frame,2001);node_core_tick(&c,2002);assert(!c.monitor);
    f.fail_output=false;cmd("0100\r",2100);node_core_bus_error(&c);assert(!c.pending && f.mode==NODE_BUS_OFF);
    node_core_disconnect(&c);assert(c.protocol==6 && c.echo && !c.filter_set && !c.header_set);
    puts("PASS: fixed-baud address search, explicit baud selection, monitor/error/disconnect");
}
static void power_tests(void) {
    node_power_config_t limits={60,900,12000};node_power_t p;
    node_power_init(&p,&limits,0,60000);assert(node_power_check(&p,59999,13000,false)==NODE_AWAKE);
    assert(node_power_check(&p,60000,13000,false)==NODE_SLEEP_IDLE);
    node_power_init(&p,&limits,0,60000);node_power_touch(&p,59000);
    assert(node_power_check(&p,60000,13000,false)==NODE_AWAKE);
    assert(node_power_check(&p,119000,13000,false)==NODE_SLEEP_IDLE);
    node_power_init(&p,&limits,0,60000);node_power_check(&p,0,11900,false);
    node_power_touch(&p,29000);assert(node_power_check(&p,29999,11900,false)==NODE_AWAKE);
    assert(node_power_check(&p,30000,11900,false)==NODE_SLEEP_LOW_BATTERY);
    node_power_init(&p,&limits,0,60000);node_power_check(&p,0,11900,false);
    node_power_check(&p,29000,12500,false);assert(node_power_check(&p,30000,11900,false)==NODE_AWAKE);
    assert(node_power_check(&p,60000,11900,true)==NODE_AWAKE);
    p.manual=true;assert(node_power_check(&p,60000,13000,true)==NODE_SLEEP_MANUAL);
    node_power_init(&p,&limits,0,60000);node_power_check(&p,0,-1,false);
    assert(node_power_check(&p,30000,-1,false)==NODE_SLEEP_LOW_BATTERY);
    limits.wake_s=1;assert(!node_power_config_valid(&limits));
    puts("PASS: idle timeout, host activity, low battery debounce, USB, ADC failure");
}
int main(void) { response_compatibility();reject_and_filter();protocols_and_monitor();power_tests();puts("All node tests passed.");return 0; }
