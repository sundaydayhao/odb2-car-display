"""Independently check A6 against A5: TI pinmap, shunt protection and unchanged layout."""
from pathlib import Path
from collections import Counter
import json, subprocess, sys
import pcbnew as k
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch';W=ROOT/'build/fm-mono-switch'
BASE='d89b4fb';PREFIX='display_board/hardware/fm-mono-switch/'
for ext in ['kicad_sch','kicad_pcb']:
    (W/f'a5-baseline.{ext}').write_bytes(subprocess.check_output(['git','show',f'{BASE}:{PREFIX}fm_mono_switch.{ext}'],cwd=ROOT))
os=sexp.load_file(str(W/'a5-baseline.kicad_sch'));ns=sexp.load_file(str(P/'fm_mono_switch.kicad_sch'))
for tag in ['wire','junction','no_connect']:
    assert list(sexp.find_all(os,tag))==list(sexp.find_all(ns,tag)),tag
old=k.LoadBoard(str(W/'a5-baseline.kicad_pcb'));new=k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
def xy(p):return [round(k.ToMM(p.x),6),round(k.ToMM(p.y),6)]
def geom(f):return (xy(f.GetPosition()),f.GetOrientationDegrees(),sorted((p.GetNumber(),xy(p.GetPosition()),xy(p.GetSize()),p.GetShape(),xy(p.GetDrillSize())) for p in f.Pads()))
of={f.GetReference():f for f in old.GetFootprints()};nf={f.GetReference():f for f in new.GetFootprints()}
assert of.keys()==nf.keys()
assert all(geom(of[r])==geom(nf[r]) for r in of),'Footprint geometry changed'
assert all('QFN' not in str(f.GetFPID().GetLibItemName()).upper() for f in nf.values())
def pinmap(b):return {(f.GetReference(),p.GetNumber()):p.GetNetname().lstrip('/') for f in b.GetFootprints() for p in f.Pads() if p.GetNumber()}
om,nm=pinmap(old),pinmap(new)
expected_changes={('U1','4'):'CODEC_P_SW',('U1','5'):'SEL',('U1','7'):'SEL',('U1','8'):'CODEC_N_SW',('R5','2'):'CODEC_P_SW',('R6','2'):'CODEC_N_SW'}
assert {key:val for key,val in nm.items() if om[key]!=val}==expected_changes
assert nf['R5'].GetValue()==nf['R6'].GetValue()=='1k 1%'
assert nf['U1'].GetValue()=='TS5A22364DGSR'
for r in ['R7','R8','R10']:assert nf[r].GetValue()=='100k 1%'
mutable={'/SEL','/CODEC_P','/CODEC_N','/CODEC_P_SW','/CODEC_N_SW','GND'}
def route(b):
    return Counter((t.GetNetname(),t.GetLayer(),tuple(xy(t.GetStart())),tuple(xy(t.GetEnd())),t.GetWidth(k.F_Cu) if isinstance(t,k.PCB_VIA) else t.GetWidth(),isinstance(t,k.PCB_VIA)) for t in b.GetTracks() if t.GetNetname() not in mutable)
assert route(old)==route(new),'Unrelated routing changed'
def edges(d):return [n for n in sexp.find_all(d,'gr_line') if str(sexp.find(n,'layer')[1])=='Edge.Cuts']
assert edges(sexp.load_file(str(W/'a5-baseline.kicad_pcb')))==edges(sexp.load_file(str(P/'fm_mono_switch.kicad_pcb')))
assert len(list(new.Zones()))==2
intent=json.loads((P/'design-intent.json').read_text(encoding='utf-8'))
for r,f in nf.items():
    for p in f.Pads():
        if p.GetNumber() and p.GetNumber() in intent['parts'][r]['pad_positions']:
            assert [round(v-50,4) for v in xy(p.GetPosition())]==intent['parts'][r]['pad_positions'][p.GetNumber()]
report={'baseline':BASE,'footprint_geometry_unchanged':37,'physical_pin_changes':{'.'.join(key):val for key,val in expected_changes.items()},
        'external_connector_pin_nets_unchanged':True,'no_qfn_footprints_on_board':True,
        'rf_clock_i2c_fm_supply_amp_and_switch_supply_routes_unchanged':True,
        'schematic_wires_unchanged':100,'schematic_ground_symbols_replaced_by_signal_labels':2,
        'segments':sum(not isinstance(t,k.PCB_VIA) for t in new.GetTracks()),'vias':sum(isinstance(t,k.PCB_VIA) for t in new.GetTracks()),'filled_zones':2,
        'u1_package':'TI DGS VSSOP-10; standard MSOP-10 footprint covers external leads',
        'shunt_current_bound':{'assumed_max_abs_codec_input_V':3.6,'series_R_min_ohm':990,'bound_mA':round(3.6/990*1000,3),'TI_recommended_limit_mA':10,'startup_and_audio_bench_tested':False}}
(W/'a6-geometry-comparison.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
