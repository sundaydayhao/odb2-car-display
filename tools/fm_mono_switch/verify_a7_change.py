"""Verify A7 changes only D1 metadata and printed revision relative to A6."""
from pathlib import Path
import json, subprocess, sys
import pcbnew as k
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp

ROOT = Path(__file__).resolve().parents[2]
P = ROOT/'display_board/hardware/fm-mono-switch'
W = ROOT/'build/fm-mono-switch'
BASE = '9f0668c'
PREFIX = 'display_board/hardware/fm-mono-switch/'

def baseline(name):
    return subprocess.check_output(['git', 'show', f'{BASE}:{PREFIX}{name}'], cwd=ROOT)

def properties(n):
    return {str(p[1]): str(p[2]) for p in sexp.find_all(n, 'property')}

def check_tree(old, new, kind):
    # Compare the entire native trees, removing only explicitly allowed fields.
    old_parts = list(sexp.find_all(old, 'symbol' if kind == 'sch' else 'footprint'))
    new_parts = list(sexp.find_all(new, 'symbol' if kind == 'sch' else 'footprint'))
    assert len(old_parts) == len(new_parts)
    for a, b in zip(old_parts, new_parts):
        ap, bp = properties(a), properties(b)
        if ap.get('Reference') == 'D1':
            assert bp['MPN'] == bp['Value'] == 'PESD5V0F1BL,315'
            assert bp['LCSC'] == 'C45961'
            for name in ['Value', 'MPN', 'Manufacturer', 'LCSC', 'Datasheet', 'Description']:
                av = next((p for p in sexp.find_all(a, 'property') if str(p[1]) == name), None)
                bv = next(p for p in sexp.find_all(b, 'property') if str(p[1]) == name)
                if av is None:
                    assert name == 'Description', name
                    b.remove(bv)
                else:
                    bv[2] = av[2]
    if kind == 'sch':
        sexp.find(sexp.find(new, 'title_block'), 'rev')[1] = sexp.find(sexp.find(old, 'title_block'), 'rev')[1]
        for a, b in zip(sexp.find_all(old, 'text'), sexp.find_all(new, 'text')):
            if str(a[1]).startswith('PROTO A6:'):
                assert str(b[1]).startswith('PROTO A7:')
                b[1] = a[1]
    else:
        for a, b in zip(sexp.find_all(old, 'gr_text'), sexp.find_all(new, 'gr_text')):
            if str(a[1]) == 'MONO FM A6':
                assert str(b[1]) == 'MONO FM A7'
                b[1] = a[1]
    assert old == new, f'Unexpected native {kind} change beyond D1 fields/revision'

for kind, ext in [('sch', 'kicad_sch'), ('pcb', 'kicad_pcb')]:
    name = f'fm_mono_switch.{ext}'
    path = W/f'a6-baseline.{ext}'
    path.write_bytes(baseline(name))
    check_tree(sexp.load_file(str(path)), sexp.load_file(str(P/name)), kind)
assert baseline('fm_mono_switch.kicad_pro') == (P/'fm_mono_switch.kicad_pro').read_bytes()
old_catalog = json.loads(baseline('procurement.json'))
new_catalog = json.loads((P/'procurement.json').read_text(encoding='utf-8'))
assert [g for g in old_catalog['groups'] if g['refs'] != ['D1']] == [g for g in new_catalog['groups'] if g['refs'] != ['D1']]
board = k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
fps = list(board.GetFootprints())
assert all('QFN' not in str(f.GetFPID().GetLibItemName()).upper() for f in fps)
report = {'baseline': BASE, 'allowed_changes': ['D1 sourcing fields/description', 'printed A7 revision'],
          'native_tree_comparison': 'PASS; all other schematic and PCB nodes exactly equal',
          'footprint_geometry_unchanged': len(fps), 'all_pin_nets_tracks_vias_zones_unchanged': True,
          'other_procurement_groups_unchanged': True, 'no_qfn_footprints_on_board': True,
          'segments': sum(not isinstance(t, k.PCB_VIA) for t in board.GetTracks()),
          'vias': sum(isinstance(t, k.PCB_VIA) for t in board.GetTracks()), 'zones': len(list(board.Zones())),
          'D1': {'mpn': 'PESD5V0F1BL,315', 'lcsc': 'C45961', 'land_mm': [0.4, 0.7], 'land_center_pitch_mm': 0.7}}
(W/'a7-geometry-comparison.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report, indent=2))
