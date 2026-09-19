#include "rda5807.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned now,started,writes,reads,fail_write,fail_read,cancel_at,stale_ms;
    unsigned frequency,target,seek_target;
    uint16_t reg[4];
    bool pending,seek,seek_fail,never_complete;
} fake_t;
static bool write_bus(void *ctx,const uint8_t *p,size_t size)
{
    fake_t *f=ctx;
    if (++f->writes==f->fail_write) return false;
    assert(size==2 || size==8);
    if (size==2) { assert(p[0]==0 && p[1]==2); return true; }
    for (unsigned i=0;i<4;++i) f->reg[i]=(uint16_t)(p[2*i]<<8|p[2*i+1]);
    if ((f->reg[0]&0x100) || (f->reg[1]&0x10)) {
        assert(!(f->reg[0]&0x4000)); /* RF movement must be muted. */
        f->pending=true; f->seek=(f->reg[0]&0x100)!=0;
        f->started=f->now; f->target=f->seek?f->seek_target:870u+(f->reg[1]>>6);
    } else f->pending=false;
    return true;
}
static bool read_bus(void *ctx,uint8_t *p,size_t size)
{
    fake_t *f=ctx; assert(size==4);
    if (++f->reads==f->fail_read) return false;
    bool busy=f->pending && (f->now-f->started<120 || f->never_complete);
    bool stale=f->pending && f->now-f->started<f->stale_ms;
    if (!busy && !stale && f->pending) f->frequency=f->target;
    uint16_t a=(uint16_t)((f->frequency-870)|0x0400);
    if (!busy || stale) a|=0x4000;
    if (!busy && f->seek && f->seek_fail) a|=0x2000;
    uint16_t b=(52<<9)|0x0180;
    p[0]=(uint8_t)(a>>8); p[1]=(uint8_t)a; p[2]=(uint8_t)(b>>8); p[3]=(uint8_t)b;
    return true;
}
static void wait_bus(void *ctx,unsigned ms) { ((fake_t *)ctx)->now+=ms; }
static bool cancel_bus(void *ctx)
{ fake_t *f=ctx; return f->cancel_at && f->now>=f->cancel_at; }
static void setup(rda5807_t *r,fake_t *f)
{
    *f=(fake_t){.frequency=875,.seek_target=987};
    rda_io_t io={f,write_bus,read_bus,wait_bus,cancel_bus};
    assert(rda5807_init(r,&io,35)==RDA_OK);
    assert(f->now>=320 && r->frequency==875 && !r->listening);
    assert(f->reg[0]==0x8005 && f->reg[1]==0x0140 && f->reg[2]==0x0a00 && f->reg[3]==0x8886);
}
int main(void)
{
    rda5807_t r; fake_t f; setup(&r,&f);
    assert(rda5807_audio(&r,true,100)==RDA_OK && (f.reg[0]&0x4000) && (f.reg[3]&15)==15);
    assert(rda5807_audio(&r,true,0)==RDA_OK && !(f.reg[0]&0x4000) && !(f.reg[3]&15));
    assert(rda5807_audio(&r,true,1)==RDA_OK && (f.reg[3]&15)==1);
    unsigned writes=f.writes;
    assert(rda5807_audio(&r,true,101)==RDA_RANGE && f.writes==writes);
    assert(rda5807_tune(&r,874)==RDA_RANGE && rda5807_tune(&r,1081)==RDA_RANGE && f.writes==writes);
    f.stale_ms=160;
    unsigned start=f.now;
    assert(rda5807_tune(&r,1080)==RDA_OK && f.now-start>=160);
    assert(r.frequency==1080 && f.reg[1]==0x3480 && r.listening && (f.reg[0]&0x4000));
    rda_status_t status; assert(rda5807_read(&r,&status)==RDA_OK);
    assert(status.frequency==1080 && status.rssi==52 && status.stereo && status.station);
    assert(rda5807_seek(&r)==RDA_OK && r.frequency==987 && !(f.reg[0]&0x100));
    assert(f.reg[1]==((987-870)<<6));
    f.seek_fail=true; f.seek_target=1080;
    assert(rda5807_seek(&r)==RDA_NO_STATION && r.frequency==987 && f.frequency==987 && r.listening);
    f.seek_fail=false; f.seek_target=871;
    assert(rda5807_seek(&r)==RDA_NO_STATION && r.frequency==987 && f.frequency==987);
    assert(rda5807_stop(&r)==RDA_OK && !r.listening && !(f.reg[0]&0x4100) && !(f.reg[1]&0x10));

    setup(&r,&f); f.never_complete=true;
    start=f.now; assert(rda5807_tune(&r,988)==RDA_TIMEOUT && f.now-start==3000 && !r.listening);
    assert(!(f.reg[0]&0x4100) && !(f.reg[1]&0x10));
    setup(&r,&f); f.never_complete=true;
    start=f.now; assert(rda5807_seek(&r)==RDA_TIMEOUT && f.now-start==15000);
    setup(&r,&f); f.cancel_at=f.now+80;
    assert(rda5807_seek(&r)==RDA_CANCELLED && !r.listening && !(f.reg[0]&0x4100));
    setup(&r,&f); f.fail_read=f.reads+1;
    assert(rda5807_tune(&r,980)==RDA_IO && !r.listening && !(f.reg[0]&0x4000));
    setup(&r,&f); f.fail_write=f.writes+1;
    assert(rda5807_seek(&r)==RDA_IO && !r.listening && !(f.reg[0]&0x4100));
    setup(&r,&f); f.fail_write=f.writes+1;
    assert(rda5807_audio(&r,true,50)==RDA_IO && !r.listening);
    f=(fake_t){.fail_write=1,.frequency=875};
    rda_io_t io={&f,write_bus,read_bus,wait_bus,cancel_bus};
    assert(rda5807_init(&r,&io,35)==RDA_IO);
    puts("RDA5807 sequential I2C, tune/seek, stale STC, cancellation, failures and volume passed");
    return 0;
}
