#pragma once
#include "obd2_data.h"
#include <stddef.h>
typedef struct { const char *key,*unit; double value; bool valid,calculated; unsigned page; } vehicle_metric_t;
enum { VEHICLE_METRIC_COUNT=21 };
size_t vehicle_metrics(const obd2_vehicle_data_t *data,vehicle_metric_t *out,size_t capacity);
