"""Generate dimensioned reference models; these are not manufacturer CAD files.

Requires CadQuery 2.6+. Coordinates use KiCad model axes: +Y opposes footprint Y.
Sources, assumptions and limitations are recorded in display-carrier/3D_MODELS.md.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import cadquery as cq

BLACK = (0.10, 0.105, 0.11)
METAL = (0.70, 0.71, 0.73)
WHITE = (0.87, 0.85, 0.78)


def box(x, y, z, sx, sy, sz):
    return cq.Workplane('XY').box(sx, sy, sz, centered=(True, True, False)).translate((x, y, z)).val()


def dot(x, y, z):
    return cq.Workplane('XY').circle(.12).extrude(.001).translate((x, y, z)).val()


def save(out, name, parts, expected):
    assembly = cq.Assembly(name=name)
    for i, (shape, color) in enumerate(parts):
        assert shape.isValid(), (name, i)
        assembly.add(shape, name=f'part_{i}', color=cq.Color(*color))
    dest = out / (name + '.step')
    assembly.export(str(dest))
    shape = cq.importers.importStep(str(dest)).val()
    assert shape.isValid(), name
    b = shape.BoundingBox()
    bounds = [b.xmin, b.xmax, b.ymin, b.ymax, b.zmin, b.zmax]
    assert all(abs(a - b) < .002 for a, b in zip(bounds, expected)), (name, bounds, expected)
    print(name, [round(v, 4) for v in bounds], flush=True)
    return {'file': dest.name, 'bounds_mm': bounds, 'solids': len(shape.Solids()), 'valid': True}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--windows-fast-exit', action='store_true')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []

    # TI DML0010A: 3 x 2 mm, max H=.8; pins 8/9/10 share an output bar.
    # Pin 1 is at native (-1,+.99), thus model (-1,-.99).
    parts = [(box(0, 0, .05, 3, 2, .75), BLACK),
             (box(0, 0, 0, 2.6, 1.1, .15), METAL), (dot(-1.15, -.65, .799), METAL)]
    for x in [-1, -.5, 0, .5, 1]:
        parts.append((box(x, -.875, 0, .24, .25, .2), METAL))
    for x in [1, .5]:
        parts.append((box(x, .875, 0, .24, .25, .2), METAL))
    parts.append((box(-.5, .875, 0, 1.28, .25, .2), METAL))
    rows.append(save(args.output, 'TPS22990_DML10_reference', parts, [-1.5,1.5,-1,1,0,.8]))

    # TI RHL0020A: nominal 3.5 x 4.5; H=1 max; 8+8+2+2 pins, EP=2.05x3.05.
    parts = [(box(0, 0, .05, 3.5, 4.5, .95), BLACK),
             (box(0, 0, 0, 2.05, 3.05, .15), METAL), (dot(-1.25, 1.9, .999), METAL)]
    for x in [-1.55, 1.55]:
        for y in [-1.75,-1.25,-.75,-.25,.25,.75,1.25,1.75]:
            parts.append((box(x, y, 0, .4, .24, .2), METAL))
    for y in [-2.05, 2.05]:
        for x in [-.75, .75]:
            parts.append((box(x, y, 0, .24, .4, .2), METAL))
        # Exposed-pad tie bars; these are not additional signal pins.
        for x in [-.275, .275]:
            parts.append((box(x, y, 0, .2, .4, .2), METAL))
    rows.append(save(args.output, 'TPS61088_RHL20_reference', parts, [-1.75,1.75,-2.25,2.25,0,1]))

    # Bourns SRP1038C lead-frame version: body10x10, terminal overall11, H3.8.
    body = cq.Workplane('XY').box(10,10,3.6,centered=(True,True,False)).edges('|Z').fillet(.5).translate((0,0,.2)).val()
    parts = [(body, BLACK)]
    for x in [-4.4, 4.4]:
        parts.append((box(x, 0, 0, 2.2, 3, .25), METAL))
    rows.append(save(args.output, 'SRP1038C_reference', parts, [-5.5,5.5,-5,5,0,3.8]))

    # JST B2P-VH standard top-entry: 7.86x8.5, H10.9, tail3.7, pitch3.96.
    # Outline follows native Fab: X -1.95..5.91, Y -3.7..4.8 (opposite here).
    base = box(1.98,-1.4,0,7.86,6.8,3.2)
    parts = [(base, WHITE), (box(1.98,2.35,0,5.46,2.7,3.2), WHITE),
             (box(1.98,2.7,3.2,5.46,1.0,6.5), WHITE),
             (box(1.98,3.35,7.5,5.46,.7,1.6), WHITE)]
    for x in [0,3.96]:
        pin = cq.Workplane('XY').box(1.14,1.14,14.6,centered=(True,True,False)).faces('>Z').edges().chamfer(.18).translate((x,0,-3.7)).val()
        parts.append((pin,METAL))
    rows.append(save(args.output, 'JST_B2P_VH_reference', parts, [-1.95,5.91,-4.8,3.7,-3.7,10.9]))
    (args.output/'reference-geometry.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    if args.windows_fast_exit and sys.platform == 'win32':
        # Opt-in only after successful export/re-import: avoid local OCP shutdown crash.
        sys.stdout.flush(); sys.stderr.flush(); os._exit(0)


if __name__ == '__main__':
    main()
