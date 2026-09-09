#include <stdio.h>
#include <stdlib.h>
#include "elm327_protocol.h"

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    char *end;
    unsigned long pid = strtoul(argv[1], &end, 16);
    if (*end || pid > 255) return 2;
    elm_response_t response;
    elm_response_reset(&response);
    uint8_t bytes[64];
    size_t n;
    while ((n = fread(bytes, 1, sizeof(bytes), stdin))) elm_response_feed(&response, bytes, n);
    elm_pid_reply_t reply;
    if (!elm_decode_pid(&response, pid, ELM_ECU_ANY, &reply)) {
        puts("{\"decoded\":false}");
        return 1;
    }
    printf("{\"decoded\":true,\"ecu\":%lu,\"payload\":\"", (unsigned long)reply.ecu);
    for (size_t i = 0; i < reply.length; ++i) printf("%02x", reply.data[i]);
    puts("\"}");
    return 0;
}
