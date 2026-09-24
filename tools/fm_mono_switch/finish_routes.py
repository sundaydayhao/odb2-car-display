"""Historical A2 QFN routing only. Do not run on the A5 MSOP board."""
"""Finish the switch fanout while retaining continuous ground planes."""
from pathlib import Path
import sys,shutil
import pcbnew as k
raise SystemExit('Historical A2 routing script; incompatible with A5 MSOP-10. Use the delivered routed PCB.')
ROOT=Path(__file__).resolve().parents[2];W=ROOT/'build/fm-mono-switch'
P=ROOT/'display_board/hardware/fm-mono-switch/fm_mono_switch.kicad_pcb'
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp
V=lambda x,y:k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
def near(a,c):return abs(a[0]-c[0])<.002 and abs(a[1]-c[1])<.002
doc=sexp.load_file(str(W/'routed-before-finish.kicad_pcb'))
for tr in list(doc):
    if not isinstance(tr,list) or sexp.tag_of(tr) not in ['segment','via']:continue
    isvia=sexp.tag_of(tr)=='via'
    a=tuple(float(v)-50 for v in sexp.atoms(sexp.find(tr,'at' if isvia else 'start')))
    z=a if isvia else tuple(float(v)-50 for v in sexp.atoms(sexp.find(tr,'end')))
    net=sexp.atoms(sexp.find(tr,'net'))[0];remove=False
    if net=='GND':
        remove=(not sexp.find(tr,'locked') and not isvia) or near(a,(26.5,18)) or near(z,(26.5,18))
    if net=='/SEL':
        remove=(not isvia and sexp.atoms(sexp.find(tr,'layer'))[0]=='B.Cu' and a[0]<25.6 and z[0]<25.6) or near(a,(25.49,18.9683)) or near(z,(25.49,18.9683))
    if remove:doc.remove(tr)
sexp.save_file(str(W/'stripped.kicad_pcb'),doc)
shutil.copyfile(P.with_suffix('.kicad_pro'),W/'stripped.kicad_pro')
b=k.LoadBoard(str(W/'stripped.kicad_pcb'));nets={n.GetNetname():n for n in b.GetNetsByNetcode().values()}
def track(net,pts,layer=k.F_Cu,w=.2):
    for a,z in zip(pts,pts[1:]):
        t=k.PCB_TRACK(b);t.SetStart(V(*a));t.SetEnd(V(*z));t.SetWidth(k.FromMM(w));t.SetLayer(layer);t.SetNet(nets[net]);t.SetLocked(True);b.Add(t)
def via(net,x,y):
    v=k.PCB_VIA(b);v.SetPosition(V(x,y));v.SetWidth(k.FromMM(.6));v.SetDrill(k.FromMM(.3));v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(k.F_Cu,k.B_Cu);v.SetNet(nets[net]);v.SetLocked(True);b.Add(v)
# Move the C4 ground return above the capacitor to open the signal corridor.
track('GND',[(25.775,18),(25.7,17.925),(25.7,17.15)],w=.18);via('GND',25.7,17.15)
# Drive the second selector pin from the upper branch instead of looping below C4.
via('/SEL',29.825,10.5)
track('/SEL',[(29.825,10.5),(26.5,10.5),(22.5218,14.4782),(22.1273,14.4782)],k.B_Cu)
track('/FM_MONO',[(25.2,15.5685),(25.2,16.2),(25.6,16.6),(26.3,16.6),(26.8,17.1),(26.8,22.5),(23.675,22.5),(23.675,23.7)])
# Two capacitor supply branches. The short U1-C4 connection remains unchanged.
track('/VCC_SW',[(24.225,18),(23.3,18)]);via('/VCC_SW',23.3,18)
track('/VCC_SW',[(20.825,7),(20.6,6.775),(20.6,6)]);via('/VCC_SW',20.6,6)
track('/VCC_SW',[(23.3,18),(20.6,15.3),(20.6,6)],k.B_Cu)
track('/VCC_SW',[(27.725,18),(27.5,17.775),(27.5,17.5)]);via('/VCC_SW',27.5,17.5)
track('/VCC_SW',[(27.5,17.5),(26.6,17.5),(26.6,16.9),(25.8,16.1),(25.8,15.5),(24.5,14.2),(23.3,14.2),(22.7,14.8),(22.7,17.4),(23.3,18)],k.B_Cu)
for fp in b.GetFootprints():
    for p in fp.Pads():
        if p.GetNetname()=='GND' and p.GetAttribute()==k.PAD_ATTRIB_SMD:p.SetLocalZoneConnection(k.ZONE_CONNECTION_FULL)
b.BuildConnectivity();k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(P),b)
print('Final fanout paths added and planes refilled.')
