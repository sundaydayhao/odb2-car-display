#include "obd2_decode.h"

#include <stddef.h>

const obd_parameter_t obd_parameters[OBD_PARAMETER_COUNT] = {
    {0x0D,1,2000,offsetof(obd2_vehicle_data_t,speed_valid),250},
    {0x0C,2,2000,offsetof(obd2_vehicle_data_t,rpm_valid),500},
    {0x04,1,4000,offsetof(obd2_vehicle_data_t,engine_load_valid),1000},
    {0x11,1,4000,offsetof(obd2_vehicle_data_t,throttle_valid),1000},
    {0x05,1,10000,offsetof(obd2_vehicle_data_t,coolant_temp_valid),2500},
    {0x2F,1,15000,offsetof(obd2_vehicle_data_t,fuel_valid),5000},
    {0x42,2,10000,offsetof(obd2_vehicle_data_t,control_voltage_valid),2500},
    {0x0F,1,10000,offsetof(obd2_vehicle_data_t,intake_temp_valid),2500},
    {0x5C,1,10000,offsetof(obd2_vehicle_data_t,oil_temp_valid),2500},
    {0x0B,1,4000,offsetof(obd2_vehicle_data_t,manifold_pressure_valid),1000},
    {0x33,1,15000,offsetof(obd2_vehicle_data_t,barometric_pressure_valid),5000},
    {0x5E,2,3000,offsetof(obd2_vehicle_data_t,fuel_rate_valid),500},
};

int obd_poll_next(const obd_poll_t *poll, const bool supported[OBD_PARAMETER_COUNT], int64_t now)
{
    int selected=-1;
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i)
        if (supported[i] && poll->due[i]<=now && (selected<0 || poll->due[i]<poll->due[selected])) selected=(int)i;
    return selected;
}
void obd_poll_result(obd_poll_t *poll, unsigned index, bool valid, int64_t now)
{
    if (index>=OBD_PARAMETER_COUNT) return;
    unsigned failures=valid ? 0 : poll->failures[index]<4 ? poll->failures[index]+1 : 4;
    poll->failures[index]=failures;
    unsigned delay=obd_parameters[index].poll_ms;
    if (failures && delay<(500U<<failures)) delay=500U<<failures;
    poll->due[index]=now+delay;
}

/* Integrate only contiguous fresh samples, with a bounded hold between replies.
 * Each interrupted source starts a new measured segment; there is no gap filling.
 * Economy uses a separate joint window so mismatched fuel/distance totals never mix. */
static void trip_update(obd_cache_t *cache, int64_t now)
{
    obd_trip_t *t=&cache->trip;
    obd2_vehicle_data_t *d=&cache->data;
    int64_t dt=now-t->last_ms;
    bool step=t->initialized && dt>=0 && dt<=1000;
    bool speed=d->speed_valid, fuel=d->fuel_rate_valid, idle=speed && d->rpm_valid;
    if (!step || !speed || !t->speed) { t->speed_area=0; t->speed_ms=0; }
    else { t->speed_area+=(uint64_t)t->last_speed*dt; t->speed_ms+=dt; }
    if (!step || !fuel || !t->fuel) { t->fuel_area=0; t->fuel_ms=0; }
    else { t->fuel_area+=(uint64_t)t->last_rate*dt; t->fuel_ms+=dt; }
    if (!step || !idle || !t->idle) t->idle_ms=0;
    else if (!t->last_speed && d->rpm>0) t->idle_ms+=dt;
    if (!step || !speed || !fuel || !t->speed || !t->fuel) {
        t->paired_speed_area=0; t->paired_fuel_area=0;
    } else {
        t->paired_speed_area+=(uint64_t)t->last_speed*dt;
        t->paired_fuel_area+=(uint64_t)t->last_rate*dt;
    }
    t->initialized=true; t->last_ms=now;
    t->speed=speed; t->fuel=fuel; t->idle=idle;
    t->last_speed=d->speed_kph; t->last_rate=d->fuel_rate_20_lph;
    d->trip_valid=speed && t->speed_ms>0;
    d->trip_distance_tenths=t->speed_area/360000;
    d->trip_minutes=t->speed_ms/60000;
    d->trip_speed_tenths=t->speed_ms ? t->speed_area*10/t->speed_ms : 0;
    d->trip_fuel_valid=fuel && t->fuel_ms>0;
    d->trip_fuel_hundredths=t->fuel_area/720000;
    d->trip_idle_valid=idle && t->speed_ms>0;
    d->trip_idle_seconds=t->idle_ms/1000;
    d->instant_fuel_valid=speed && fuel && d->speed_kph>=5;
    d->instant_fuel_tenths=d->instant_fuel_valid ? (uint32_t)d->fuel_rate_20_lph*50/d->speed_kph : 0;
    d->average_fuel_valid=speed && fuel && t->paired_speed_area>=360000; /* At least 100 m. */
    d->average_fuel_tenths=d->average_fuel_valid ? t->paired_fuel_area*50/t->paired_speed_area : 0;
    d->boost_valid=d->manifold_pressure_valid && d->barometric_pressure_valid;
    d->boost_kpa=(int)d->manifold_pressure_kpa-d->barometric_pressure_kpa;
}

void obd_cache_invalidate(obd_cache_t *cache, unsigned index)
{
    if (index<OBD_PARAMETER_COUNT) *(bool *)((uint8_t *)&cache->data+obd_parameters[index].valid_offset)=false;
}

void obd_cache_expire(obd_cache_t *cache, int64_t now)
{
    for (unsigned i=0;i<OBD_PARAMETER_COUNT;++i)
        if (now<cache->updated[i] || now-cache->updated[i]>=obd_parameters[i].stale_ms) obd_cache_invalidate(cache,i);
    trip_update(cache,now);
}

bool obd_cache_apply(obd_cache_t *cache, unsigned index, const elm_pid_reply_t *reply, int64_t now)
{
    if (index>=OBD_PARAMETER_COUNT || reply->length<obd_parameters[index].bytes) return false;
    obd_cache_expire(cache,now);
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
    case 0x33: out->barometric_pressure_kpa=a; break;
    case 0x5E: out->fuel_rate_20_lph=a*256+b; break;
    default: return false;
    }
    *(bool *)((uint8_t *)out+obd_parameters[index].valid_offset)=true;
    cache->updated[index]=now;
    out->last_update_ms=now;
    trip_update(cache,now);
    return true;
}
