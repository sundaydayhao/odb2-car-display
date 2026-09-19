"""Build the small offline WGS84 overview from Natural Earth (public domain).

Inputs: official Natural Earth 1:50m admin-0/admin-1 GeoJSON in build/map-sources.
No network is used by the firmware. Bounds and simplification target an overview,
not road navigation. Coordinates are projected once at build time.
"""
import hashlib
import json
import math
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'build/map-sources'
OUT=ROOT/'display_board/main'
BOUNDS=(65, 4, 146, 61)

def merc(lat):
    return math.degrees(math.log(math.tan(math.pi/4+math.radians(lat)/2)))

def clip(points):
    for axis,limit,side in [(0,BOUNDS[0],1),(0,BOUNDS[2],-1),(1,BOUNDS[1],1),(1,BOUNDS[3],-1)]:
        result=[]
        if not points: break
        a=points[-1]; ai=(a[axis]-limit)*side>=0
        for b in points:
            bi=(b[axis]-limit)*side>=0
            if ai!=bi:
                t=(limit-a[axis])/(b[axis]-a[axis])
                q=[a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])]; q[axis]=limit; result.append(q)
            if bi: result.append(b)
            a=b; ai=bi
        points=result
    return points

def simplify(points,epsilon=.035):
    if len(points)<3: return points
    keep={0,len(points)-1}; stack=[(0,len(points)-1)]
    while stack:
        a,b=stack.pop(); ax,ay=points[a]; bx,by=points[b]
        dx,dy=bx-ax,by-ay; den=dx*dx+dy*dy; far=epsilon*epsilon; chosen=None
        for i in range(a+1,b):
            x,y=points[i]; t=max(0,min(1,((x-ax)*dx+(y-ay)*dy)/den)) if den else 0
            d=(x-ax-t*dx)**2+(y-ay-t*dy)**2
            if d>far: far=d; chosen=i
        if chosen is not None:
            keep.add(chosen); stack.extend([(a,chosen),(chosen,b)])
    return [points[i] for i in sorted(keep)]

vertices=[]; rings=[]; labels=[]
def add(geometry,layer):
    polygons=[geometry['coordinates']] if geometry['type']=='Polygon' else geometry['coordinates']
    for polygon in polygons:
        for hole,ring in enumerate(polygon):
            clipped=clip(ring)
            if len(clipped)<3: continue
            points=simplify([(p[0],merc(p[1])) for p in clipped]+[(clipped[0][0],merc(clipped[0][1]))])
            if len(points)<4: continue
            fixed=[(round(x*10000),round(y*10000)) for x,y in points]
            xs,ys=zip(*fixed)
            rings.append((len(vertices),len(fixed),layer,bool(hole),min(xs),min(ys),max(xs),max(ys)))
            vertices.extend(fixed)

country_path=SRC/'ne_50m_admin_0_countries.geojson'
province_path=SRC/'ne_50m_admin_1_states_provinces.geojson'
countries=json.loads(country_path.read_text(encoding='utf-8'))['features']
highlight={'CHN','TWN','HKG','MAC'}
for layer in [0,1]:
    for f in countries:
        if (f['properties']['ADM0_A3'] in highlight)==bool(layer): add(f['geometry'],layer)
for f in json.loads(province_path.read_text(encoding='utf-8'))['features']:
    p=f['properties']
    if p['adm0_a3']!='CHN': continue
    add(f['geometry'],2)
    name=p['name_zh'].replace('维吾尔自治区','').replace('壮族自治区','').replace('回族自治区','').replace('自治区','').removesuffix('省').removesuffix('市')
    labels.append((name,float(p['longitude']),merc(float(p['latitude']))))
labels.extend([('台湾',121.0,merc(23.7)),('香港',114.15,merc(22.3)),('澳门',113.54,merc(22.18))])
header='''#pragma once
#include <stdint.h>
typedef struct { int32_t x,y; } map_vertex_t;
typedef struct { uint32_t first; uint16_t count; uint8_t layer,hole; int32_t x1,y1,x2,y2; } map_ring_t;
typedef struct { const char *name; float x,y; } map_place_t;
'''
header+=f'enum {{ MAP_RING_COUNT={len(rings)}, MAP_PLACE_COUNT={len(labels)}, MAP_MAX_RING={max(r[1] for r in rings)} }};\n'
header+='extern const map_vertex_t map_vertices[];\nextern const map_ring_t map_rings[MAP_RING_COUNT];\nextern const map_place_t map_places[MAP_PLACE_COUNT];\n'
(OUT/'map_geometry.h').write_text(header,encoding='utf-8')
c='/* Generated from Natural Earth; see docs/MAP.md. */\n#include "map_geometry.h"\nconst map_vertex_t map_vertices[]={\n'
c+=''.join(f'{{{x},{y}}},\n' for x,y in vertices)+'};\nconst map_ring_t map_rings[MAP_RING_COUNT]={\n'
c+=''.join('{'+','.join(str(int(x)) for x in r)+'},\n' for r in rings)+'};\nconst map_place_t map_places[MAP_PLACE_COUNT]={\n'
c+=''.join(f'{{"{n}",{x:.6f}f,{y:.6f}f}},\n' for n,x,y in labels)+'};\n'
(OUT/'map_geometry.c').write_text(c,encoding='utf-8')
print(f'{len(vertices)} vertices, {len(rings)} rings; max ring {max(r[1] for r in rings)}; approx {len(vertices)*8+len(rings)*24} bytes')
for path in [country_path,province_path]: print(path.name,hashlib.sha256(path.read_bytes()).hexdigest())
