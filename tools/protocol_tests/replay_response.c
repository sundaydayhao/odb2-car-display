#include <stdio.h>
#include <stdlib.h>
#include "elm327_protocol.h"
#include "obd2_decode.h"

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
    putchar('"');
    obd_cache_t cache = {0};
    for (unsigned i = 0; i < OBD_PARAMETER_COUNT; ++i) {
        if (obd_parameters[i].pid != pid || !obd_cache_apply(&cache, i, &reply, 0)) continue;
        const char *parameter = NULL, *unit = NULL;
        int value = 0;
        switch (pid) {
        case 0x0D: parameter="speed_kph"; unit="km/h"; value=cache.data.speed_kph; break;
        case 0x0C: parameter="rpm"; unit="rpm"; value=cache.data.rpm; break;
        case 0x04: parameter="engine_load_percent"; unit="%"; value=cache.data.engine_load_percent; break;
        case 0x11: parameter="throttle_percent"; unit="%"; value=cache.data.throttle_percent; break;
        case 0x05: parameter="coolant_temp_c"; unit="degC"; value=cache.data.coolant_temp_c; break;
        case 0x2F: parameter="fuel_percent"; unit="%"; value=cache.data.fuel_percent; break;
        case 0x42: parameter="control_voltage_mv"; unit="mV"; value=cache.data.control_voltage_mv; break;
        case 0x0F: parameter="intake_temp_c"; unit="degC"; value=cache.data.intake_temp_c; break;
        case 0x5C: parameter="oil_temp_c"; unit="degC"; value=cache.data.oil_temp_c; break;
        case 0x0B: parameter="manifold_pressure_kpa"; unit="kPa"; value=cache.data.manifold_pressure_kpa; break;
        }
        if (parameter) printf(",\"parameter\":\"%s\",\"value\":%d,\"unit\":\"%s\"",parameter,value,unit);
        break;
    }
    puts("}");
    return 0;
}
