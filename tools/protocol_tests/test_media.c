#include "media_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const uint8_t mp3[] = {0xff,0xfb,0x90,0x00};
    assert(media_mp3_frame_size(mp3, 3) == 0);
    assert(media_mp3_frame_size(mp3, 4) == 417);
    uint8_t header[4]; memcpy(header, mp3, 4); header[2] |= 2;
    assert(media_mp3_frame_size(header, 4) == 418);
    header[1] = 0xf3; header[2] = 0x80;
    assert(media_mp3_frame_size(header, 4) == 208);
    header[1] = 0xeb; assert(media_mp3_frame_size(header, 4) == -1);
    header[1] = 0xfb; header[2] = 0xfc; assert(media_mp3_frame_size(header, 4) == -1);
    uint8_t tag[] = {'I','D','3',4,0,0,0,0,1,0};
    assert(media_id3_size(tag, 9) == 0); assert(media_id3_size(tag, 10) == 138);
    tag[5] = 0x10; assert(media_id3_size(tag, 10) == 148);
    tag[6] = 0x80; assert(media_id3_size(tag, 10) == -1);
    puts("MP3 frame and ID3 header tests passed");
    return 0;
}
