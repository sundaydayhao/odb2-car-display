"""Generate explicitly simplified display-board mechanical models (CadQuery 2.6+).

These are visual envelopes, not vendor-certified assembly models. All dimensions
are millimetres, with KiCad's model coordinates (+Y opposite footprint Y).
See display_board/hardware/display-singleboard/3D_MODELS.md before enclosure tooling.
"""
import argparse
import os
import sys
from pathlib import Path
import cadquery as cq


def box(x, y, z, sx, sy, sz):
    return cq.Workplane('XY').box(sx, sy, sz, centered=(True, True, False)).translate((x, y, z)).val()


BLACK = (0.08, 0.085, 0.09)
SILVER = (0.68, 0.69, 0.70)
WHITE = (0.83, 0.81, 0.71)
GOLD = (0.72, 0.56, 0.19)


def save(out, name, parts):
    assembly = cq.Assembly(name=name)
    for i, (shape, color) in enumerate(parts):
        if not shape.isValid():
            raise ValueError(f'{name}: invalid solid {i}')
        assembly.add(shape, name=f'part_{i}', color=cq.Color(*color))
    assembly.export(str(out / (name + '.step')))
    content = (out / (name + '.step')).read_text(encoding='utf-8')
    if not content.startswith('ISO-10303-21;') or not content.rstrip().endswith('END-ISO-10303-21;'):
        raise ValueError(f'{name}: STEP export incomplete')
    print(name, flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--windows-fast-exit', action='store_true',
                    help='After validated exports, bypass the known local OCP/VTK native shutdown crash on Windows')
    args = ap.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)

    # XKB SS-12D10L3 drawing A1: body 13 x 6.8 x 6.4, actuator +3.
    # Lead section/length and metal cover detail are illustrative.
    body = box(0, 0, .4, 13, 6.8, 6).cut(box(0, 0, 5.85, 6.4, 4.1, 1))
    parts = [(box(0, 0, 0, 12.4, 6.2, .6), BLACK), (body, SILVER),
             (box(-1.1, 0, 5.8, 3.9, 3.9, 3.6), BLACK)]
    for x in [-4.7, 0, 4.7]:
        parts.append((box(x, 0, -4, 1, .5, 4.5), SILVER))
    save(out, 'SS-12D10L3_nominal', parts)

    # Actual B4SA-01 height must be confirmed against ordered samples.
    parts = [(box(0, 0, .1, 4.5, 4.5, 2.55), BLACK),
             (box(0, 0, 2.65, 4.5, 4.5, .25), SILVER),
             (cq.Workplane('XY').circle(1.45).extrude(1.4).translate((0, 0, 2.9)).val(), BLACK)]
    for x in [-2.9, 2.9]:
        for y in [-1.5, 1.5]:
            parts.append((box(x, y, 0, 1.8, .7, .15), SILVER))
    save(out, 'K2-1109SP_nominal_4p3mm', parts)

    # Goertek 4 mm diameter x 1.3 mm nominal body, acoustic opening represented.
    ring = cq.Workplane('XY').circle(2).circle(.35).extrude(1.2).translate((0, 0, .1)).val()
    parts = [(ring, SILVER), (cq.Workplane('XY').circle(1.8).extrude(.1).val(), GOLD),
             (cq.Workplane('XY').circle(.3).extrude(.03).translate((0, 0, 1.1)).val(), BLACK)]
    save(out, 'B4013AM422-042_nominal', parts)

    # J308 has no qualified MPN. XY follows existing footprint, H=2 is assumed.
    body = box(0, .25, .1, 7.2, 4.5, 1.45).cut(box(0, -1.7, .5, 3.4, 1.2, .55))
    parts = [(body, WHITE), (box(0, -1.35, 1.55, 7, 1.3, .45), BLACK)]
    for x in [-1.25, -.75, -.25, .25, .75, 1.25]:
        parts.append((box(x, 2.4, 0, .22, 1.15, .1), GOLD))
    for x in [-3.3, 3.3]:
        parts.append((box(x, .02, 0, .9, 1.9, .15), SILVER))
    save(out, 'FFC6P_upper_contact_nominal', parts)

    # J21: imported outline; height 1.85 is provisional. No inserted SD card.
    # Mouth is local -X; footprint is on B.Cu and rotated on the actual board.
    base = box(3.55, -.95, 0, 13.7, 14.75, .18)
    shell = box(3.55, -.95, .18, 13.7, 14.75, 1.67)
    shell = shell.cut(box(3.2, -.6, .35, 15, 12.2, 1.3))
    shell = shell.cut(box(-2.9, -.6, .2, 1.2, 12.2, 1.3))
    parts = [(base, BLACK), (shell, SILVER)]
    for i in range(9):
        parts.append((box(10.25, -3.2 + i * 1.1, 0, 1.1, .45, .12), GOLD))
    for x, y in [(10, 7.05), (.4, 6.8), (10, -7.8), (.4, -8.7)]:
        parts.append((box(x, y, 0, 1, 1.5, .15), SILVER))
    save(out, 'TF-1007F_outline_nominal', parts)
    return args.windows_fast_exit


if __name__ == '__main__':
    fast_exit = main()  # Export or validation exceptions still fail normally.
    if fast_exit and sys.platform == 'win32':
        # Both locally tested OCP/VTK wheel sets abort during interpreter
        # finalization, even on a bare CadQuery import. All STEP streams are
        # closed and validated above. This opt-in avoids native destructors;
        # it does not catch errors or convert failed exports into success.
        sys.stdout.flush()
        sys.stderr.flush()
        os._exit(0)
