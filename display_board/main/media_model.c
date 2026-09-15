#include "media_model.h"
#include <string.h>

int media_mp3_frame_size(const uint8_t *p, size_t length)
{
    if (length < 4) return 0;
    if (p[0] != 0xff || (p[1] & 0xe0) != 0xe0) return -1;
    unsigned version = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3;
    unsigned bitrate = p[2] >> 4, sample = (p[2] >> 2) & 3;
    if (version == 1 || layer != 1 || !bitrate || bitrate == 15 || sample == 3) return -1;
    static const unsigned rates[] = {44100, 48000, 32000};
    static const unsigned mpeg1[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const unsigned mpeg2[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    unsigned rate = rates[sample] / (version == 3 ? 1 : version == 2 ? 2 : 4);
    return (int)((version == 3 ? 144000 : 72000) * (version == 3 ? mpeg1[bitrate] : mpeg2[bitrate]) / rate + ((p[2] >> 1) & 1));
}
int media_id3_size(const uint8_t *p, size_t length)
{
    if (length < 10) return 0;
    if (memcmp(p, "ID3", 3) || p[3] < 2 || p[3] > 4 || ((p[6]|p[7]|p[8]|p[9]) & 0x80)) return -1;
    unsigned bytes = ((unsigned)p[6]<<21) | ((unsigned)p[7]<<14) | ((unsigned)p[8]<<7) | p[9];
    if (bytes > 1024 * 1024) return -1;
    return (int)(bytes + 10 + (p[3] == 4 && (p[5] & 0x10) ? 10 : 0));
}
