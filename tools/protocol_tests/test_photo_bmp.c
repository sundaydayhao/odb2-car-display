#include "photo_bmp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void roundtrip(void)
{
    /* Odd width and source padding exercise BMP row alignment and vertical orientation. */
    const uint16_t source[]={0xf800,0x07e0,0x001f,0xdead, 0xffff,0x0000,0x39e7,0xbeef};
    uint16_t out[6]; uint8_t row[12];
    FILE *f=tmpfile(); assert(f);
    assert(photo_bmp_write(f,source,3,2,8,row,sizeof(row)));
    assert(ftell(f)==78); rewind(f);
    uint8_t bytes[78]; assert(fread(bytes,1,78,f)==78);
    assert(bytes[0]=='B' && bytes[1]=='M' && bytes[2]==78 && bytes[10]==54 && bytes[28]==24);
    assert(bytes[54]==255 && bytes[55]==255 && bytes[56]==255); /* Bottom-left white. */
    assert(bytes[66]==0 && bytes[67]==0 && bytes[68]==255); /* Top-left red. */
    assert(bytes[75]==0 && bytes[76]==0 && bytes[77]==0);
    rewind(f); assert(photo_bmp_preview(f,out,3,2,row,sizeof(row)));
    assert(!memcmp(out,source,6)); assert(!memcmp(out+3,source+4,6)); fclose(f);
}
static void letterbox(void)
{
    uint16_t source[]={0xf800,0x07e0,0x001f,0xffff,0x0000,0x39e7};
    uint16_t out[12]; uint8_t row[12]; FILE *f=tmpfile(); assert(f);
    assert(photo_bmp_write(f,source,3,2,6,row,sizeof(row))); rewind(f);
    assert(photo_bmp_preview(f,out,3,4,row,sizeof(row)));
    for (unsigned i=0;i<3;++i) { assert(out[i]==0); assert(out[i+9]==0); }
    assert(!memcmp(out+3,source,sizeof(source))); fclose(f);
}
static void invalid(void)
{
    uint16_t source[6]={0},out[6]; uint8_t row[12],bytes[78];
    FILE *valid=tmpfile(); assert(valid);
    assert(!photo_bmp_write(valid,source,3,2,4,row,sizeof(row)));
    assert(!photo_bmp_write(valid,source,2049,2,4098,row,sizeof(row)));
    assert(!photo_bmp_write(valid,source,3,2,6,row,11));
    assert(photo_bmp_write(valid,source,3,2,6,row,sizeof(row))); rewind(valid);
    assert(fread(bytes,1,sizeof(bytes),valid)==sizeof(bytes)); fclose(valid);
    /* Every truncation, including a complete header but partial pixels, must fail. */
    for (unsigned n=0;n<sizeof(bytes);++n) {
        FILE *f=tmpfile(); assert(f); assert(fwrite(bytes,1,n,f)==n); rewind(f);
        assert(!photo_bmp_preview(f,out,3,2,row,sizeof(row))); fclose(f);
    }
    const unsigned offsets[]={0,10,14,18,22,26,27,28,29,30};
    for (unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i) {
        uint8_t corrupted[78]; memcpy(corrupted,bytes,sizeof(bytes)); corrupted[offsets[i]]=0xff;
        FILE *f=tmpfile(); assert(f); assert(fwrite(corrupted,1,78,f)==78); rewind(f);
        assert(!photo_bmp_preview(f,out,3,2,row,sizeof(row))); fclose(f);
    }
}
int main(void) { roundtrip(); letterbox(); invalid(); puts("BMP roundtrip, padding, letterbox, malformed and truncated input passed."); }
