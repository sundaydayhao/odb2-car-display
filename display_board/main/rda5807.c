#include "rda5807.h"
#include <string.h>

enum { ENABLE=1, RESET=2, NEW_METHOD=4, SEEK=0x100, SEEKUP=0x200,
       DMUTE=0x4000, DHIZ=0x8000, TUNE=0x10 };
static bool cancelled(rda5807_t *r)
{ return r->io.cancelled && r->io.cancelled(r->io.context); }
static rda_result_t write_regs(rda5807_t *r)
{
    uint8_t bytes[8];
    for (unsigned i=0;i<4;++i) { bytes[2*i]=(uint8_t)(r->reg[i]>>8); bytes[2*i+1]=(uint8_t)r->reg[i]; }
    return r->io.write(r->io.context,bytes,sizeof(bytes))?RDA_OK:RDA_IO;
}
static void audio_bits(rda5807_t *r)
{
    unsigned step=r->volume?(r->volume*15+99)/100:0;
    r->reg[3]=(uint16_t)((r->reg[3]&~15u)|step);
    r->reg[0]&=~DMUTE;
    if (r->listening && r->volume) r->reg[0]|=DMUTE;
}
rda_result_t rda5807_read(rda5807_t *r,rda_status_t *status)
{
    uint8_t bytes[4];
    if (!r->io.read(r->io.context,bytes,sizeof(bytes))) return RDA_IO;
    uint16_t a=(uint16_t)(bytes[0]<<8|bytes[1]),b=(uint16_t)(bytes[2]<<8|bytes[3]);
    if (a==0xffff && b==0xffff) return RDA_IO;
    *status=(rda_status_t){.frequency=(uint16_t)(870+(a&0x3ff)),.rssi=(uint8_t)(b>>9),
        .stereo=(a&0x400)!=0,.station=(b&0x100)!=0,.stc=(a&0x4000)!=0,.seek_failed=(a&0x2000)!=0};
    return RDA_OK;
}
rda_result_t rda5807_stop(rda5807_t *r)
{
    r->listening=false; r->reg[0]&=~(SEEK|DMUTE); r->reg[1]&=~TUNE;
    return write_regs(r);
}
rda_result_t rda5807_audio(rda5807_t *r,bool listening,unsigned volume)
{
    if (volume>100) return RDA_RANGE;
    r->listening=listening; r->volume=(uint8_t)volume; audio_bits(r);
    rda_result_t result=write_regs(r);
    if (result!=RDA_OK) r->listening=false;
    return result;
}
static rda_result_t finish(rda5807_t *r,rda_result_t result,unsigned frequency)
{
    r->reg[0]&=~SEEK; r->reg[1]&=~TUNE;
    if (result==RDA_OK) { r->frequency=(uint16_t)frequency; r->reg[1]=(uint16_t)((frequency-870)<<6); }
    else r->listening=false;
    audio_bits(r);
    if (write_regs(r)!=RDA_OK) return RDA_IO;
    return result;
}
static rda_result_t operation(rda5807_t *r,bool seek,unsigned frequency)
{
    if (frequency<RDA5807_MIN || frequency>RDA5807_MAX) return RDA_RANGE;
    if (cancelled(r)) return finish(r,RDA_CANCELLED,r->frequency);
    /* Tune and seek run muted. No concurrent register writes are allowed. */
    r->reg[0]&=~(DMUTE|SEEK); r->reg[1]&=~TUNE;
    if (seek) r->reg[0]|=SEEK|SEEKUP; /* wrap inside the chip's 87..108 MHz band */
    else r->reg[1]=(uint16_t)(((frequency-870)<<6)|TUNE);
    if (write_regs(r)!=RDA_OK) return finish(r,RDA_IO,r->frequency);
    bool saw_busy=false;
    unsigned limit=seek?15000:3000;
    for (unsigned elapsed=0;elapsed<limit;elapsed+=40) {
        r->io.wait_ms(r->io.context,40);
        if (cancelled(r)) return finish(r,RDA_CANCELLED,r->frequency);
        rda_status_t status;
        if (rda5807_read(r,&status)!=RDA_OK) return finish(r,RDA_IO,r->frequency);
        if (!status.stc) saw_busy=true;
        /* Reject an old STC=1 result from the preceding command. */
        if (elapsed<40 || !status.stc) continue;
        if (!seek && status.frequency!=frequency) continue;
        if (seek && !saw_busy && !status.seek_failed && status.frequency==r->frequency) continue;
        if (seek && (status.seek_failed || status.frequency<RDA5807_MIN || status.frequency>RDA5807_MAX)) {
            bool listening=r->listening;
            rda_result_t cleared=finish(r,RDA_NO_STATION,r->frequency);
            if (cleared!=RDA_NO_STATION) return cleared;
            /* Hardware seeks from 87.0 MHz; restore the last valid UI channel
             * on SF or a result outside the application's 87.5..108.0 band. */
            rda_result_t restored=rda5807_tune(r,r->frequency);
            if (restored!=RDA_OK) return restored;
            if (rda5807_audio(r,listening,r->volume)!=RDA_OK) return RDA_IO;
            return RDA_NO_STATION;
        }
        return finish(r,RDA_OK,status.frequency);
    }
    return finish(r,RDA_TIMEOUT,r->frequency);
}
rda_result_t rda5807_tune(rda5807_t *r,unsigned frequency)
{ return operation(r,false,frequency); }
rda_result_t rda5807_seek(rda5807_t *r)
{ return operation(r,true,r->frequency); }
rda_result_t rda5807_init(rda5807_t *r,const rda_io_t *io,unsigned volume)
{
    if (!r || !io || !io->write || !io->read || !io->wait_ms || volume>100) return RDA_RANGE;
    memset(r,0,sizeof(*r)); r->io=*io; r->volume=(uint8_t)volume; r->frequency=RDA5807_MIN;
    const uint8_t reset[2]={0,RESET};
    if (!io->write(io->context,reset,sizeof(reset))) return RDA_IO;
    io->wait_ms(io->context,50);
    if (cancelled(r)) return RDA_CANCELLED;
    r->reg[0]=DHIZ|ENABLE|NEW_METHOD; /* 32.768 kHz crystal, stereo, RDS off */
    r->reg[1]=(RDA5807_MIN-870)<<6;   /* 100 kHz spacing, band 87..108 MHz */
    r->reg[2]=0x0a00;                /* 50 us de-emphasis and soft mute */
    r->reg[3]=0x8880;                /* SNR threshold 8, LNAP input */
    audio_bits(r);
    if (write_regs(r)!=RDA_OK) return RDA_IO;
    io->wait_ms(io->context,150);
    return rda5807_tune(r,RDA5807_MIN);
}
