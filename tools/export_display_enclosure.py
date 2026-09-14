"""Export enclosure access coordinates and a drawing from the real KiCad PCB.

Run using KiCad's Python. Does not modify CAD or invent unqualified z dimensions.
"""
from pathlib import Path
import argparse, json, math, html, hashlib
import pcbnew as k

repo = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--project', type=Path, default=repo/'hardware/display-singleboard')
ap.add_argument('--output', type=Path, default=repo/'build/display-enclosure/mechanical')
a = ap.parse_args()
cfg = json.loads((a.project/'enclosure.json').read_text(encoding='utf8'))
pcb = a.project/'display_singleboard.kicad_pcb'
b = k.LoadBoard(str(pcb))
fps = {f.GetReference(): f for f in b.GetFootprints()}
a.output.mkdir(parents=True, exist_ok=True)

def point(p):
    return [round(k.ToMM(p.x), 4), round(k.ToMM(p.y), 4)]

def bounds(f):
    bb = f.GetBoundingBox(False, False)
    return [*point(bb.GetPosition()), *point(bb.GetEnd())]

def distance_to_box(x, y, box):
    return math.hypot(max(box[0]-x, 0, x-box[2]), max(box[1]-y, 0, y-box[3]))

errors = []
power = cfg['rear_power']
sw = fps[power['reference']]
if point(sw.GetPosition()) != power['center_mm'] or sw.GetOrientationDegrees() != power['angle_deg']:
    errors.append('SW3 no longer matches the rear opening coordinates')
x, y = point(sw.GetPosition())
w, h = power['body_xy_mm']
body = [x-w/2, y-h/2, x+w/2, y+h/2]
outline = cfg['pcb_envelope_mm']
top_margin = body[1]-outline[1]
if top_margin < 0.4 or top_margin > 1.0:
    errors.append('SW3 body should be 0.4 to 1.0 mm inside the upper board edge')
for ref, f in fps.items():
    if ref == 'SW3' or f.IsDNP():
        continue
    bb = bounds(f)
    if f.GetLayer() == k.F_Cu and body[0] < bb[2] and body[2] > bb[0] and body[1] < bb[3] and body[3] > bb[1]:
        errors.append(f'SW3 body overlaps fitted component {ref}')

mounts = []
radius = cfg['mounting']['maximum_local_support_or_head_diameter_mm']/2
for ref in cfg['mounting']['hole_references']:
    f = fps[ref]
    x, y = point(f.GetPosition())
    near = sorted((distance_to_box(x, y, bounds(other)), name)
                  for name, other in fps.items()
                  if name != ref and not other.IsDNP())
    clearance, neighbor = near[0]
    if clearance < radius+0.2:
        errors.append(f'{ref} has insufficient clearance for the proposed support/head')
    mounts.append({'reference': ref, 'center_mm': [x, y],
                   'relative_to_upper_left_mm': [round(x-outline[0],4),round(y-outline[1],4)],
                   'nearest_component': neighbor, 'clearance_from_5mm_support_mm': round(clearance-radius,4)})

access = []
for item in cfg['access']:
    f = fps[item['reference']]
    access.append({**item, 'origin_mm': point(f.GetPosition()), 'angle_deg': f.GetOrientationDegrees(),
                   'side': 'F' if f.GetLayer()==k.F_Cu else 'B', 'footprint_bbox_mm': bounds(f)})
result = {'pcb_sha256': hashlib.sha256(pcb.read_bytes()).hexdigest(),
          'switch_body_upper_edge_gap_mm': round(top_margin,4), 'mounting': mounts,
          'access': access, 'errors': errors,
          'limits': ['2D placement/access checks only; not a 3D clash check',
                     'Actual port heights, plug overmolds, mating FPC and case tolerances are not qualified',
                     'Large USB-A / adjacent HS Type-C plug reservations overlap; controller roles are mutually exclusive']}
(a.output/'access-coordinates.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf8')

# Engineering access map: component footprints come from the PCB, colored boxes
# are case/ribbon reservations, not additional copper or PCB keepout constraints.
svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1800" height="1575" viewBox="-43 -107 160 140">',
       '<rect x="-43" y="-107" width="160" height="140" fill="#f8fafc"/>',
       '<style>text{font-family:Arial,sans-serif;fill:#1f2937}.label{font-size:1.45px}.small{font-size:1.2px}</style>',
       f'<text x="-37" y="-102" font-size="3">{html.escape(cfg["revision"])} / REAR ENCLOSURE ACCESS</text>',
       '<text x="-37" y="-98.5" font-size="1.5">120 x 74 mm PCB / component side faces rear cover / coordinates in mm</text>',
       '<text x="-37" y="-95.7" font-size="1.3">Colored areas reserve case and cable space. Prototype clearances; not tooling dimensions.</text>',
       ]
edges=[d for d in b.GetDrawings() if isinstance(d,k.PCB_SHAPE) and d.GetLayer()==k.Edge_Cuts]
current=point(edges[0].GetStart());start=current[:];commands=[f'M{current[0]},{current[1]}']
while edges:
    candidates=[(i,False) for i,d in enumerate(edges) if math.dist(current,point(d.GetStart()))<.0002]
    candidates += [(i,True) for i,d in enumerate(edges) if math.dist(current,point(d.GetEnd()))<.0002]
    if not candidates:raise RuntimeError('Board outline is not a connected contour')
    i,reverse=candidates[0];d=edges.pop(i);end=point(d.GetStart() if reverse else d.GetEnd())
    if d.GetShape()==k.SHAPE_T_ARC:
        mid=point(d.GetArcMid());center=point(d.GetCenter());radius=math.dist(current,center)
        sweep=int((mid[0]-current[0])*(end[1]-mid[1])-(mid[1]-current[1])*(end[0]-mid[0])>0)
        large=int(abs(d.GetArcAngle().AsDegrees())>180)
        commands.append(f'A{radius},{radius} 0 {large},{sweep} {end[0]},{end[1]}')
    else:commands.append(f'L{end[0]},{end[1]}')
    current=end
if math.dist(current,start)>.0002:raise RuntimeError('Open board outline')
svg.append(f'<path d="{" ".join(commands)} Z" fill="#e8efee" stroke="#334155" stroke-width=".25"/>')

def rect(box, color, opacity=.14):
    x,y,xx,yy=box
    svg.append(f'<rect x="{x}" y="{y}" width="{xx-x}" height="{yy-y}" fill="{color}" fill-opacity="{opacity}" stroke="{color}" stroke-width=".18"/>')

for ref, f in fps.items():
    if f.IsDNP() or ref.startswith('H'):continue
    rect(bounds(f), '#8f9ea6', .16)
for item in access:
    if 'reservation_mm' not in item:continue
    color = '#2563eb' if item['kind'].startswith('external') else '#0f8b74'
    rect(item['reservation_mm'], color, .10)
    x,y=point(fps[item['reference']].GetPosition())
    svg.append(f'<text x="{x}" y="{y}" class="label" text-anchor="middle">{html.escape(item["reference"])}</text>')
    dx,dy={'-X':(-6,0),'+X':(6,0),'-Y':(0,-6),'+Y':(0,6)}.get(item['direction'],(0,0))
    x2,y2=x+dx,y+dy
    svg.append(f'<path d="M{x},{y} L{x2},{y2}" stroke="{color}" stroke-width=".35"/>')
    if dx:arrow=[(x2,y2),(x2-math.copysign(1,dx),y2-.6),(x2-math.copysign(1,dx),y2+.6)]
    else:arrow=[(x2,y2),(x2-.6,y2-math.copysign(1,dy)),(x2+.6,y2-math.copysign(1,dy))]
    svg.append(f'<polygon points="{" ".join(f"{p},{q}" for p,q in arrow)}" fill="{color}"/>')

x,y=power['center_mm'];rw,rh=power['rear_recess_plan_mm']
rect([x-rw/2,y-rh/2,x+rw/2,y+rh/2], '#dc6721', .15)
rect(body,'#dc6721',.35)
slw,slh=power['actuator_slot_plan_mm']
rect([x-slw/2,y-slh/2,x+slw/2,y+slh/2],'#9a3412',.2)
svg.append(f'<text x="{x}" y="{y+.6}" class="label" text-anchor="middle">SW3</text>')
svg.append('<path d="M-22.5,-85.6 V-91 H-4" fill="none" stroke="#dc6721" stroke-width=".2"/>')
svg.append('<text x="-3" y="-90.4" class="label">TOP REAR RECESS / 2.2 mm SLIDE</text>')
for item in mounts:
    x,y=item['center_mm']
    svg.append(f'<circle cx="{x}" cy="{y}" r="2.5" fill="#fff" stroke="#334155" stroke-width=".2"/><circle cx="{x}" cy="{y}" r="1.5" fill="#cbd5e1"/>')
    svg.append(f'<text x="{x}" y="{y+4}" class="small" text-anchor="middle">{item["reference"]}</text>')
for ref,label in [('SW1','RESET'),('SW2','BOOT'),('J7','MIC')]:
    x,y=point(fps[ref].GetPosition())
    svg.append(f'<circle cx="{x}" cy="{y}" r="1.4" fill="#8b5cf6" fill-opacity=".3" stroke="#7c3aed" stroke-width=".2"/><text x="{x+3}" y="{y+.5}" class="label">{label}</text>')
ant=cfg['antenna']['antenna_region_mm'];rect(ant,'#a21caf',.3)
clear=cfg['antenna']['added_object_clearance_target_mm']
rect([ant[0]-clear,ant[1]-clear,ant[2]+clear,ant[3]+clear],'#a21caf',.025)
svg.append('<text x="91" y="-81" class="label">ANTENNA</text><text x="91" y="-78" class="small">15 mm target</text><text x="91" y="-75" class="small">new metal / wiring</text>')
svg.append('<text x="-30" y="21" class="label">Internal FPCs: assemble before rear cover. Screen metal must clear RF region.</text>')
svg.append('<text x="-30" y="24" class="label">USB: bottom openings. microSD: right opening on B.Cu side.</text>')
svg.append('<text x="-30" y="27" class="label">SW1 / SW2: recessed service access. Small insulated mounting hardware, OD &lt;= 5 mm.</text>')
svg.append('<text x="-30" y="30" class="label">Switch: 13 x 6.8 body, 6.4 body height + 3 handle; verify slot and detents on sample.</text>')
svg.append('</svg>')
(a.output/'enclosure-access.svg').write_text('\n'.join(svg),encoding='utf8')
print(json.dumps({'errors':errors,'mounts':len(mounts),'access_items':len(access),'top_gap_mm':top_margin,'output':str(a.output)},ensure_ascii=False))
if errors:raise SystemExit(1)
