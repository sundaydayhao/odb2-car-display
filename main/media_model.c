#include "media_model.h"
#include <ctype.h>
#include <string.h>

const media_source_t media_sources[MEDIA_SOURCE_COUNT] = {
    {MEDIA_MUSIC, "音乐试听 1", "乐鑫官方音频示例", "https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3"},
    {MEDIA_MUSIC, "音乐试听 2", "乐鑫官方音频示例", "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3"},
    {MEDIA_MUSIC, "我的音乐", "使用设置中保存的音乐地址", ""},
    {MEDIA_RADIO, "北京交通广播", "蜻蜓 FM · 交通资讯", "https://lhttp.qingting.fm/live/336/64k.mp3"},
    {MEDIA_RADIO, "广东音乐之声", "蜻蜓 FM · 音乐电台", "https://lhttp.qingting.fm/live/1260/64k.mp3"},
    {MEDIA_RADIO, "我的电台", "使用设置中保存的电台地址", ""},
};

bool media_url_valid(const char *url)
{
    if (!url) return false;
    size_t length = strlen(url);
    if (length >= MEDIA_URL_SIZE) return false;
    size_t prefix = !strncmp(url, "https://", 8) ? 8 : !strncmp(url, "http://", 7) ? 7 : 0;
    if (!prefix || length <= prefix || url[prefix] == '/' || url[prefix] == '?' || url[prefix] == '#') return false;
    const char *end = strpbrk(url + prefix, "/?#");
    if (!end) end = url + length;
    for (const char *p = url; *p; ++p)
        if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127 || *p == '\\' || *p == '#') return false;
    for (const char *p = url + prefix; p < end; ++p) if (*p == '@') return false;
    return true;
}

unsigned media_adjacent_source(unsigned current, int direction, bool custom_available)
{
    if (current >= MEDIA_SOURCE_COUNT) current = 0;
    unsigned first = current >= 3 ? 3 : 0;
    int index = (int)current - (int)first;
    int step = direction < 0 ? -1 : 1;
    index = (index + step + 3) % 3;
    if (index == 2 && !custom_available) index = (index + step + 3) % 3;
    return first + (unsigned)index;
}

void media_icy_init(media_icy_t *icy, uint32_t interval)
{
    *icy = (media_icy_t){.interval = interval, .audio_left = interval};
}
size_t media_icy_filter(media_icy_t *icy, uint8_t *data, size_t length)
{
    if (!icy->interval) return length;
    size_t output = 0;
    for (size_t i = 0; i < length; ++i) {
        if (icy->metadata_left) {
            if (!--icy->metadata_left) icy->audio_left = icy->interval;
        } else if (icy->length_pending) {
            icy->metadata_left = data[i] * 16u;
            icy->length_pending = false;
            if (!icy->metadata_left) icy->audio_left = icy->interval;
        } else {
            data[output++] = data[i];
            if (!--icy->audio_left) icy->length_pending = true;
        }
    }
    return output;
}

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
