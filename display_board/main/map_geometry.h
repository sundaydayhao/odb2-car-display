#pragma once
#include <stdint.h>
typedef struct { int32_t x,y; } map_vertex_t;
typedef struct { uint32_t first; uint16_t count; uint8_t layer,hole; int32_t x1,y1,x2,y2; } map_ring_t;
typedef struct { const char *name; float x,y; } map_place_t;
enum { MAP_RING_COUNT=270, MAP_PLACE_COUNT=34, MAP_MAX_RING=921 };
extern const map_vertex_t map_vertices[];
extern const map_ring_t map_rings[MAP_RING_COUNT];
extern const map_place_t map_places[MAP_PLACE_COUNT];
