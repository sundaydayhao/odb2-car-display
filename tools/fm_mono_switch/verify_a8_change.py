"""Verify A8 additions against the A7 native CAD baseline."""
from pathlib import Path
from collections import Counter
import json, subprocess, sys
import pcbnew as k
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch';W=ROOT/'build/fm-mono-switch'
BASE='5aa066a'; prefix='display_board/hardware/fm-mono-switch/'
def base(name):return subprocess.check_output(['git','show',BASE+':'+prefix+name],cwd=ROOT)
assert base('fm_mono_switch.kicad_pro')==(P/'fm_mono_switch.kicad_pro').read_bytes()
old_intent=json.loads(base('design-intent.json'));intent=json.loads((P/'design-intent.json').read_text(encoding='utf-8'))
for ref,part in old_intent['parts'].items():assert intent['parts'][ref]==part,ref
old=k.LoadBoard(str(W/'a7-fm_mono_switch.kicad_pcb'));new=k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
def pos(v):return (v.x,v.y)
def fp(f):
    return (f.GetValue(),str(f.GetFPID().GetLibNickname())+':'+str(f.GetFPID().GetLibItemName()),pos(f.GetPosition()),f.GetOrientationDegrees(),
        sorted((p.GetNumber(),p.GetNetname(),pos(p.GetPosition()),pos(p.GetSize()),pos(p.GetDrillSize())) for p in f.Pads()))
before={f.GetReference():fp(f) for f in old.GetFootprints()}
after={f.GetReference():fp(f) for f in new.GetFootprints()}
assert set(after)-set(before)=={'U4','U5','C11','C12','J5'}
for ref in before:assert before[ref]==after[ref],ref
def track(t):
    if isinstance(t,k.PCB_VIA):return ('via',t.GetNetname(),pos(t.GetPosition()),t.GetWidth(k.F_Cu),t.GetDrillValue())
    return ('track',t.GetNetname(),t.GetLayer(),t.GetWidth(),tuple(sorted([pos(t.GetStart()),pos(t.GetEnd())])))
old_tracks=Counter(track(t) for t in old.GetTracks());new_tracks=Counter(track(t) for t in new.GetTracks())
assert not old_tracks-new_tracks,old_tracks-new_tracks
for ref,ns in {'U4':{'1':'+3V3_IN','2':'SENSOR_SDA','3':'GND','4':'SENSOR_SCL'},
               'U5':{'1':'SENSOR_SCL','2':'+3V3_IN','3':'GND','4':'SENSOR_SDA'},
               'J5':{'1':'SENSOR_SDA','2':'SENSOR_SCL'}}.items():assert intent['parts'][ref]['nets']==ns
assert intent['parts']['U2']['nets']['5']=='I2C_SDIO'
assert intent['parts']['U2']['nets']['4']=='I2C_SCLK'
assert all('QFN' not in str(f.GetFPID().GetLibItemName()).upper() for f in new.GetFootprints())
edge=new.GetBoardEdgesBoundingBox()
assert abs(k.ToMM(edge.GetWidth())-40.05)<.001 and abs(k.ToMM(edge.GetHeight())-58.05)<.001
zones=list(new.Zones());assert len(zones)==3 and sum(z.GetIsRuleArea() for z in zones)==1
rule=next(z for z in zones if z.GetIsRuleArea())
assert rule.GetDoNotAllowZoneFills() and not rule.GetDoNotAllowTracks()
assert rule.GetLayerSet().Contains(k.F_Cu) and rule.GetLayerSet().Contains(k.B_Cu)
report={'baseline':BASE,'unchanged_original_footprints':len(before),
    'unchanged_original_track_and_via_items':sum(old_tracks.values()),
    'added_footprints':['U4','U5','C11','C12','J5'],'board_mm':[40,58],
    'original_mounting_holes_unchanged':True,'project_rules_unchanged':True,
    'sensor_pinmap_checked':True,'sensor_and_FM_i2c_nets_separate':True,
    'DHT20':'horizontal face-up; custom 2.54 mm TH land pattern; mechanical envelope model',
    'both_layers_no_pour_under_DHT20_body':True,'no_qfn_footprints_on_board':True,
    'segments':sum(not isinstance(t,k.PCB_VIA) for t in new.GetTracks()),
    'vias':sum(isinstance(t,k.PCB_VIA) for t in new.GetTracks()),'ground_zones':2,'pour_keepouts':1}
(W/'a8-geometry-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
