#include "obd2_decode.h"

#include <stddef.h>

const obd_parameter_t obd_parameters[OBD_PARAMETER_COUNT] = {
    {0x0D,1,2000,offsetof(obd2_vehicle_data_t,speed_valid)},
    {0x0C,2,2000,offsetof(obd2_vehicle_data_t,rpm_valid)},
    {0x04,1,4000,offsetof(obd2_vehicle_data_t,engine_load_valid)},
    {0x11,1,4000,offsetof(obd2_vehicle_data_t,throttle_valid)},
    {0x05,1,10000,offsetof(obd2_vehicle_data_t,coolant_temp_valid)},
    {0x2F,1,15000,offsetof(obd2_vehicle_data_t,fuel_valid)},
    {0x42,2,10000,offsetof(obd2_vehicle_data_t,control_voltage_valid)},
    {0x0F,1,10000,offsetof(obd2_vehicle_data_t,intake_temp_valid)},
    {0x5C,1,10000,offsetof(obd2_vehicle_data_t,oil_temp_valid)},
    {0x0B,1,4000,offsetof(obd2_vehicle_data_t,manifold_pressure_valid)},
};

void obd_cache_invalidate(obd_cache_t *cache, unsigned index)
{
    if (index<OBD_PARAMETER_COUNT) *(bool *)((uint8_t *)&cache->data+obd_parameters[index].valid_offset)=false;
}

void obd_cache_expire(obd_cache_t *cache, int64_t now)
{
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i) if (now-cache->updated[i]>=obd_parameters[i].stale_ms) obd_cache_invalidate(cache,i);
}

bool obd_cache_apply(obd_cache_t *cache, unsigned index, const elm_pid_reply_t *reply, int64_t now)
{
    if (index>=OBD_PARAMETER_COUNT || reply->length<obd_parameters[index].bytes) return false;
    unsigned a=reply->data[0], b=reply->length>1 ? reply->data[1] : 0;
    obd2_vehicle_data_t *out=&cache->data;
    switch (obd_parameters[index].pid) {
    case 0x0D: out->speed_kph=a; break;
    case 0x0C: out->rpm=(a*256+b)/4; break;
    case 0x04: out->engine_load_percent=a*100/255; break;
    case 0x11: out->throttle_percent=a*100/255; break;
    case 0x05: out->coolant_temp_c=(int)a-40; break;
    case 0x2F: out->fuel_percent=a*100/255; break;
    case 0x42: out->control_voltage_mv=a*256+b; break;
    case 0x0F: out->intake_temp_c=(int)a-40; break;
    case 0x5C: out->oil_temp_c=(int)a-40; break;
    case 0x0B: out->manifold_pressure_kpa=a; break;
    default: return false;
    }
    *(bool *)((uint8_t *)out+obd_parameters[index].valid_offset)=true;
    cache->updated[index]=now;
    out->last_update_ms=now;
    return true;
}
