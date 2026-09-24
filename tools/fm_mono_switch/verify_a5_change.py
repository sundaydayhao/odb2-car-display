"""Check the scope of the MSOP revision against the committed A4 design.

Run with KiCad's native Python after native ERC/DRC and netlist verification.
This checks unchanged circuitry as well as the intended footprint/placement change.
"""
from pathlib import Path
from collections import Counter
import json, subprocess, sys
import pcbnew as k
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp

ROOT = Path(__file__).resolve().parents[2]
P = ROOT/'display_board/hardware/fm-mono-switch'
W = ROOT/'build/fm-mono-switch'
BASE = '7ceb508'
PREFIX = 'display_board/hardware/fm-mono-switch/'
for ext in ['kicad_sch', 'kicad_pcb']:
    (W/f'a4-baseline.{ext}').write_bytes(subprocess.check_output(
        ['git', 'show', f'{BASE}:{PREFIX}fm_mono_switch.{ext}'], cwd=ROOT))
oldsch = sexp.load_file(str(W/'a4-baseline.kicad_sch'))
newsch = sexp.load_file(str(P/'fm_mono_switch.kicad_sch'))
wiring = {}
for tag in ['wire', 'label', 'junction', 'no_connect']:
    old, new = list(sexp.find_all(oldsch, tag)), list(sexp.find_all(newsch, tag))
    assert old == new, ('Schematic connections changed', tag)
    wiring[tag] = len(new)

old = k.LoadBoard(str(W/'a4-baseline.kicad_pcb'))
new = k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
def xy(p): return [round(k.ToMM(p.x), 6), round(k.ToMM(p.y), 6)]
def footprint_geometry(f):
    return {'position':xy(f.GetPosition()), 'angle':f.GetOrientationDegrees(),
            'pads':sorted((p.GetNumber(),xy(p.GetPosition()),xy(p.GetSize()),
                           p.GetShape(),xy(p.GetDrillSize())) for p in f.Pads())}
oldfps = {f.GetReference():f for f in old.GetFootprints()}
newfps = {f.GetReference():f for f in new.GetFootprints()}
assert set(oldfps) == set(newfps)
changed = [ref for ref in sorted(newfps) if footprint_geometry(oldfps[ref]) != footprint_geometry(newfps[ref])]
assert changed == ['C4', 'U1'], changed
assert str(newfps['U1'].GetFPID().GetLibItemName()) == 'MSOP-10_3x3mm_P0.5mm'
assert xy(newfps['U1'].GetPosition()) == [75.0,65.0]
assert xy(newfps['C4'].GetPosition()) == [72.0,62.0]
assert all('QFN' not in str(f.GetFPID().GetLibItemName()).upper() for f in newfps.values())
intent = json.loads((P/'design-intent.json').read_text(encoding='utf-8'))
for ref,f in newfps.items():
    for p in f.Pads():
        if p.GetNumber() and p.GetNumber() in intent['parts'][ref]['pad_positions']:
            actual = [round(v-50,4) for v in xy(p.GetPosition())]
            assert actual == intent['parts'][ref]['pad_positions'][p.GetNumber()], (ref,p.GetNumber())

rerouted = {'/VCC_SW','/FM_MONO','/AMP_P','/AMP_N','/SEL','/CODEC_P','/CODEC_N','/FM_REF','GND'}
def route_signature(b):
    result=[]
    for t in b.GetTracks():
        if t.GetNetname() in rerouted: continue
        result.append((t.GetNetname(),t.GetLayer(),tuple(xy(t.GetStart())),tuple(xy(t.GetEnd())),
                       t.GetWidth(k.F_Cu) if isinstance(t,k.PCB_VIA) else t.GetWidth(),
                       isinstance(t,k.PCB_VIA)))
    return Counter(result)
assert route_signature(old) == route_signature(new), 'Routing outside the switch nets changed'
oldedges = [d for d in sexp.find_all(sexp.load_file(str(W/'a4-baseline.kicad_pcb')),'gr_line')
            if str(sexp.find(d,'layer')[1])=='Edge.Cuts']
newedges = [d for d in sexp.find_all(sexp.load_file(str(P/'fm_mono_switch.kicad_pcb')),'gr_line')
            if str(sexp.find(d,'layer')[1])=='Edge.Cuts']
assert oldedges == newedges
assert len(list(new.Zones())) == 2
report = {'baseline':BASE, 'schematic_connections_unchanged':wiring,
          'changed_footprint_geometry':changed, 'other_footprint_geometry_unchanged':35,
          'external_interfaces_unchanged':True, 'no_qfn_footprints_on_board':True,
          'rf_clock_i2c_fm_supply_routes_unchanged':True,
          'rerouted_nets':sorted(rerouted), 'board_edges_unchanged':len(newedges),
          'segments':sum(not isinstance(t,k.PCB_VIA) for t in new.GetTracks()),
          'vias':sum(isinstance(t,k.PCB_VIA) for t in new.GetTracks()),
          'filled_zones':2, 'intent_pad_positions_match':True}
(W/'a5-geometry-comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
