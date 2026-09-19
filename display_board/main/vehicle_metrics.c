#include "vehicle_metrics.h"
size_t vehicle_metrics(const obd2_vehicle_data_t *d,vehicle_metric_t *out,size_t cap)
{
    if (!d || !out) return 0;
    const vehicle_metric_t values[]={
        {"coolant_temperature","C",d->coolant_temp_c,d->coolant_temp_valid,false,1},
        {"speed","km/h",d->speed_kph,d->speed_valid,false,0},
        {"rpm","rpm",d->rpm,d->rpm_valid,false,0},
        {"fuel_level","%",d->fuel_percent,d->fuel_valid,false,1},
        {"engine_load","%",d->engine_load_percent,d->engine_load_valid,false,1},
        {"throttle","%",d->throttle_percent,d->throttle_valid,false,1},
        {"control_voltage","V",d->control_voltage_mv/1000.0,d->control_voltage_valid,false,1},
        {"intake_temperature","C",d->intake_temp_c,d->intake_temp_valid,false,1},
        {"oil_temperature","C",d->oil_temp_c,d->oil_temp_valid,false,1},
        {"manifold_pressure","kPa",d->manifold_pressure_kpa,d->manifold_pressure_valid,false,1},
        {"barometric_pressure","kPa",d->barometric_pressure_kpa,d->barometric_pressure_valid,false,1},
        {"fuel_rate","L/h",d->fuel_rate_20_lph/20.0,d->fuel_rate_valid,false,4},
        {"boost","kPa",d->boost_kpa,d->boost_valid,true,4},
        {"instant_fuel","L/100km",d->instant_fuel_tenths/10.0,d->instant_fuel_valid,true,4},
        {"average_fuel","L/100km",d->average_fuel_tenths/10.0,d->average_fuel_valid,true,3},
        {"trip_distance","km",d->trip_distance_tenths/10.0,d->trip_valid,true,3},
        {"trip_duration","min",d->trip_minutes,d->trip_valid,true,3},
        {"trip_average_speed","km/h",d->trip_speed_tenths/10.0,d->trip_valid,true,3},
        {"trip_fuel","L",d->trip_fuel_hundredths/100.0,d->trip_fuel_valid,true,3},
        {"trip_idle","s",d->trip_idle_seconds,d->trip_idle_valid,true,3},
        {"tire_pressure","kPa",0,false,false,2},
    };
    size_t n=sizeof(values)/sizeof(*values); if(n>cap)n=cap;
    for(size_t i=0;i<n;++i){out[i]=values[i];out[i].valid=values[i].valid && d->link_state==OBD_LINK_ACTIVE;}
    return n;
}
