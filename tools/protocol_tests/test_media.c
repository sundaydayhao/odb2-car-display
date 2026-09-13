#include "media_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(media_url_valid("https://example.org/song.mp3?token=a%20b"));
    assert(media_url_valid("http://192.168.1.2:8080/radio"));
    const char *invalid[] = {"", "https://", "ftp://host/song.mp3", "https:///song.mp3",
        "https://user:pass@host/song.mp3", "https://host/a\r\nHeader:x", "https://host/a b", "http://host/\\x", "https://host/#fragment"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) assert(!media_url_valid(invalid[i]));
    char long_url[MEDIA_URL_SIZE + 1]; memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "https://", 8); long_url[sizeof(long_url)-1] = 0;
    assert(!media_url_valid(long_url));
    assert(media_adjacent_source(0, -1, false) == 1);
    assert(media_adjacent_source(1, 1, false) == 0);
    assert(media_adjacent_source(4, 1, true) == 5);
    assert(media_adjacent_source(5, 1, true) == 3);
    assert(media_adjacent_source(5, 1, false) == 3);

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
    /* Metadata length byte, 16-byte block and zero-length block all cross reads. */
    uint8_t wire[64]; size_t length = 0;
    memcpy(wire + length, "abcd", 4); length += 4;
    wire[length++] = 1; memset(wire + length, 'x', 16); length += 16;
    memcpy(wire + length, "efgh", 4); length += 4;
    wire[length++] = 0; memcpy(wire + length, "ijkl", 4); length += 4;
    for (size_t split = 1; split <= length; ++split) {
        media_icy_t icy; media_icy_init(&icy, 4);
        uint8_t output[64]; size_t used = 0;
        for (size_t offset = 0; offset < length; offset += split) {
            size_t count = length - offset; if (count > split) count = split;
            uint8_t block[64]; memcpy(block, wire + offset, count);
            size_t filtered = media_icy_filter(&icy, block, count);
            memcpy(output + used, block, filtered); used += filtered;
        }
        assert(used == 12 && !memcmp(output, "abcdefghijkl", 12));
    }
    media_icy_t icy; media_icy_init(&icy, 0);
    assert(media_icy_filter(&icy, wire, length) == length);
    puts("Media URL, stream framing and fragmented ICY tests passed");
    return 0;
}
