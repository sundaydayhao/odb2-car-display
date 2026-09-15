#include "photo_bmp.h"
#include <string.h>

static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(8*i)); }

bool photo_bmp_write(FILE *file, const uint16_t *pixels, unsigned w, unsigned h,
                     unsigned stride, uint8_t *row, size_t capacity)
{
    if (!file || !pixels || !row || !w || !h || w>2048 || h>2048 || stride<w*2) return false;
    unsigned bytes=(w*3+3)&~3u;
    if (capacity<bytes) return false;
    uint8_t header[54]={0}; header[0]='B'; header[1]='M'; header[26]=1; header[28]=24;
    put32(header+2,54+bytes*h); put32(header+10,54); put32(header+14,40);
    put32(header+18,w); put32(header+22,h); put32(header+34,bytes*h);
    if (fwrite(header,1,sizeof(header),file)!=sizeof(header)) return false;
    for (unsigned y=0;y<h;++y) {
        const uint16_t *src=(const uint16_t *)((const uint8_t *)pixels+(h-1-y)*stride);
        memset(row,0,bytes);
        for (unsigned x=0;x<w;++x) {
            uint16_t p=src[x]; unsigned b=p&31, g=(p>>5)&63, r=p>>11;
            row[x*3]=(b<<3)|(b>>2); row[x*3+1]=(g<<2)|(g>>4); row[x*3+2]=(r<<3)|(r>>2);
        }
        if (fwrite(row,1,bytes,file)!=bytes) return false;
    }
    return !ferror(file);
}

bool photo_bmp_preview(FILE *file, uint16_t *pixels, unsigned out_w, unsigned out_h,
                       uint8_t *row, size_t capacity)
{
    uint8_t header[54];
    if (!file || !pixels || !row || !out_w || !out_h || out_w>2048 || out_h>2048 ||
        fread(header,1,sizeof(header),file)!=sizeof(header)) return false;
    uint32_t w=u32(header+18), h=u32(header+22), offset=u32(header+10);
    if (header[0]!='B' || header[1]!='M' || u32(header+14)!=40 || header[26]!=1 ||
        header[27] || header[28]!=24 || header[29] || u32(header+30) ||
        !w || !h || w>2048 || h>2048 || offset<54 || offset>4096) return false;
    unsigned bytes=(w*3+3)&~3u;
    if (capacity<bytes || fseek(file,0,SEEK_END)) return false;
    long size=ftell(file);
    if (size<0 || (uint32_t)size<offset+bytes*h) return false;
    unsigned dw=out_w, dh=h*dw/w;
    if (dh>out_h) { dh=out_h; dw=w*dh/h; }
    if (!dw || !dh) return false;
    unsigned left=(out_w-dw)/2, top=(out_h-dh)/2;
    memset(pixels,0,out_w*out_h*2);
    for (unsigned y=0;y<dh;++y) {
        if (fseek(file,offset+(h-1-y*h/dh)*bytes,SEEK_SET) || fread(row,1,bytes,file)!=bytes) return false;
        for (unsigned x=0;x<dw;++x) {
            const uint8_t *p=row+(x*w/dw)*3;
            pixels[(y+top)*out_w+x+left]=(uint16_t)((p[2]>>3)<<11 | (p[1]>>2)<<5 | p[0]>>3);
        }
    }
    return true;
}
