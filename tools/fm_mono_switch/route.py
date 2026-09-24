"""Preserve short RF/clock/decoupling routes; export/import a local router session."""
from pathlib import Path
import sys,json
import pcbnew as k
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch/fm_mono_switch.kicad_pcb'
W=ROOT/'build/fm-mono-switch'
V=lambda x,y:k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
b=k.LoadBoard(str(P));nets={n.GetNetname():n for n in b.GetNetsByNetcode().values()}
def tr(net,pts,width=.2,layer=k.F_Cu):
    for a,z in zip(pts,pts[1:]):
        t=k.PCB_TRACK(b);t.SetStart(V(*a));t.SetEnd(V(*z));t.SetWidth(k.FromMM(width));t.SetLayer(layer);t.SetNet(nets[net]);t.SetLocked(True);b.Add(t)
def via(net,x,y):
    v=k.PCB_VIA(b);v.SetPosition(V(x,y));v.SetWidth(k.FromMM(.6));v.SetDrill(k.FromMM(.3));v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(k.F_Cu,k.B_Cu);v.SetNet(nets[net]);v.SetLocked(True);b.Add(v)
if sys.argv[1]=='export':
    assert not list(b.GetTracks()),'Export only an unrouted generated board.'
    tr('/FMIN',[(7.075,24),(8.9,24)])
    tr('/RCLK',[(13.1,25.5),(13.7,25.5),(14.2,26),(14.2,27.5),(13.7,28)])
    tr('/FM_VDD',[(13.1,25),(14.4,25),(15.225,25.825),(15.225,26.5)])
    tr('/VCC_SW',[(24.8,15.5185),(24.8,16.4),(24.225,16.975),(24.225,18)])
    for pts in [[(25.2,14.4315),(25.2,13.4)],[(8.9,23.5),(8.2,23.5),(7.85,23.15)],
                [(8.9,24.5),(8.0,24.5),(7.55,24.95),(7.55,25.4)],
                [(13.1,24.5),(14.3,24.5),(14.5,24.3)],[(5.5,20.65),(5.5,20)],
                [(25.775,18),(26.5,18)],[(16.775,26.5),(17.8,26.5)]]:
        tr('GND',pts,.18);via('GND',*pts[-1])
    k.SaveBoard(str(P),b)
    assert k.ExportSpecctraDSN(b,str(W/'board.dsn'))
    print('Pre-routes saved and DSN exported.')
elif sys.argv[1]=='import':
    assert k.ImportSpecctraSES(b,str(W/'routed.ses'))
    k.SaveBoard(str(P),b)
    print('Imported',len(list(b.GetTracks())),'track/via objects')
elif sys.argv[1]=='fill':
    for layer in [k.F_Cu,k.B_Cu]:
        z=k.ZONE(b);z.SetLayer(layer);z.SetNet(nets['GND']);z.SetLocalClearance(k.FromMM(.2));z.SetPadConnection(k.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(k.FromMM(.25));z.SetThermalReliefSpokeWidth(k.FromMM(.25));z.SetMinThickness(k.FromMM(.2))
        outline=z.Outline();outline.NewOutline()
        for x,y in [(.5,.5),(39.5,.5),(39.5,35.5),(.5,35.5)]:outline.Append(V(x,y).x,V(x,y).y)
        b.Add(z)
    b.BuildConnectivity();k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(P),b)
    print('Filled both GND planes.')
