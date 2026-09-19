#pragma once
#include "map_model.h"
#include "map_geometry.h"
#include <stdint.h>
typedef struct { int first,last; float x,y,dx,crossing; } map_scan_edge_t;
typedef struct {
    float x[MAP_MAX_RING],y[MAP_MAX_RING];
    map_scan_edge_t edges[MAP_MAX_RING],active[MAP_MAX_RING];
    unsigned phase,ring,vertex,edge_count,next_edge,active_count,track;
    int row,last_row;
    double scale,ox,oy;
    /* Diagnostics count actual edge intersections, excluding invisible edges. */
    uint32_t intersections,steps;
} map_render_work_t;
typedef struct { uint16_t water,land,region,border,province,track,marker; } map_colors_t;
void map_render(uint16_t *pixels,const map_model_t *model,const map_colors_t *colors,map_render_work_t *work,bool show_track);
/* The model/colors/output must stay owned by this job until it completes.
 * One step is a row, up to 32 projected vertices, or up to 8 clipped lines. */
void map_render_begin(map_render_work_t *work,const map_model_t *model);
bool map_render_step(uint16_t *pixels,const map_model_t *model,const map_colors_t *colors,map_render_work_t *work,bool show_track);
