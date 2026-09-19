#pragma once
#include "gps_model.h"
enum { MAP_TRACK_CAPACITY=512 };
typedef struct { double x,y; bool start; } map_track_point_t;
typedef struct {
    double x,y,base_scale,zoom,distance_m;
    int width,height;
    bool following,valid,have_sequence,have_last;
    uint32_t sequence,last_ms;
    double last_lat,last_lon;
    gps_fix_t fix;
    unsigned first,count;
    map_track_point_t track[MAP_TRACK_CAPACITY];
} map_model_t;
double map_project_lat(double latitude);
double map_unproject_lat(double y);
void map_model_init(map_model_t *m,int width,int height);
void map_model_resize(map_model_t *m,int width,int height);
void map_model_china(map_model_t *m);
void map_model_zoom(map_model_t *m,double factor);
void map_model_pan(map_model_t *m,double dx,double dy);
void map_model_follow(map_model_t *m);
bool map_model_update(map_model_t *m,const gps_fix_t *fix,uint32_t now);
const map_track_point_t *map_model_point(const map_model_t *m,unsigned index);
