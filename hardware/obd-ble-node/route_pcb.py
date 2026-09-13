"""Local KiCad routing preparation / SES import. No remote routing service.

In1.Cu is a continuous ground reference. RF matching and buck hot-loop segments
are routed before the signal router; the router must preserve these segments.
"""
import argparse,json,math
from pathlib import Path
import pcbnew as k

ROOT=Path(__file__).resolve().parent
FILE=ROOT/'obd_ble_node.kicad_pcb'
def v(x,y):return k.VECTOR2I(round(x*1e6),round(y*1e6))
def xy(point):return (k.ToMM(point.x),k.ToMM(point.y))
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--import-ses',type=Path);a=ap.parse_args()
    b=k.LoadBoard(str(FILE));out=ROOT/'build';out.mkdir(exist_ok=True)
    if a.import_ses:
        assert k.ImportSpecctraSES(b,str(a.import_ses))
        k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(FILE),b,True)
        print('Imported SES; ground plane refilled.');return
    assert not list(b.GetTracks()),'Regenerate staged PCB before preparing routing.'
    net=b.FindNet('/GND');fps={f.GetReference():f for f in b.GetFootprints()}
    pads={(f.GetReference(),p.GetNumber()):p for f in b.GetFootprints() for p in f.Pads() if p.GetNumber()}
    def track(name,points,layer=k.F_Cu,width=.2):
        for x,y in zip(points,points[1:]):
            if x==y:continue
            t=k.PCB_TRACK(b);t.SetStart(v(*x));t.SetEnd(v(*y));t.SetWidth(k.FromMM(width));t.SetLayer(layer);t.SetNet(b.FindNet('/'+name));t.SetLocked(True);b.Add(t)
    def via(name,pt):
        t=k.PCB_VIA(b);t.SetPosition(v(*pt));t.SetWidth(k.FromMM(.45));t.SetDrill(k.FromMM(.2));t.SetViaType(k.VIATYPE_THROUGH);t.SetLayerPair(k.F_Cu,k.B_Cu);t.SetNet(b.FindNet('/'+name));t.SetLocked(True);b.Add(t)
    def pt(ref,n):return xy(pads[(ref,str(n))].GetPosition())
    # RF is kept on F.Cu over the dedicated In1 ground reference.
    track('RF_SOC',[pt('U5',1),(123.1,106.55),(122.77,106.22),pt('C20',1),(121.64,106.22),pt('L2',1)],width=.18)
    track('RF50',[pt('L2',2),pt('C21',1),(119.82,106.4),(119.445,106.775),pt('J2',1)],width=.18)
    track('VDD_RF',[pt('U5',2),(123.05,107.05),(122.78,107.32),pt('C12',1)],width=.2)
    track('VDD_RF',[pt('U5',3),(123.05,107.55),(123.05,107.05)],width=.2)
    # Buck high-current local loop; the input and output bulk caps stay close.
    track('VIN_PROT',[pt('C1',1),pt('U1',4),pt('U1',5)],k.B_Cu,.5)
    track('SW',[pt('U1',6),(110.6,107.35),(110.6,108.15),(110.45,108.3),(110.45,108.5)],k.B_Cu,.25)
    track('SW',[pt('L1',1),(110.45,108.5)],k.F_Cu,.5)
    via('SW',(110.45,108.5))
    track('+5V',[pt('L1',2),(114.1,108.3),pt('C4',1)],width=.5)
    # Ground fanout: each pad gets a local through via without crossing another
    # net's pad on either outer layer. KiCad DRC remains authoritative.
    nonground=[p for f in b.GetFootprints() for p in f.Pads() if p.GetNumber() and p.GetNetname()!='/GND']
    keepouts=[z.GetBoundingBox() for f in b.GetFootprints() for z in f.Zones() if z.GetIsRuleArea()]
    critical=[t for t in b.GetTracks() if t.GetNetname()!='/GND']
    def track_width(t):return k.ToMM(t.GetWidth(k.F_Cu) if isinstance(t,k.PCB_VIA) else t.GetWidth())
    def dist_segment(x,y,t):
        ax,ay=xy(t.GetStart());bx,by=xy(t.GetEnd())
        den=(bx-ax)**2+(by-ay)**2
        u=max(0,min(1,((x-ax)*(bx-ax)+(y-ay)*(by-ay))/den)) if den else 0
        return math.hypot(x-ax-u*(bx-ax),y-ay-u*(by-ay))
    def dist_box(x,y,bb):
        return math.hypot(max(k.ToMM(bb.GetLeft())-x,0,x-k.ToMM(bb.GetRight())),max(k.ToMM(bb.GetTop())-y,0,y-k.ToMM(bb.GetBottom())))
    def clear_point(x,y,r=.375):
        return 100.5<x<138 and 100.5<y<116.1 and all(dist_box(x,y,p.GetBoundingBox())>=r for p in nonground) and all(dist_box(x,y,bb)>=.25 for bb in keepouts) and all(dist_segment(x,y,t)>=r+track_width(t)/2 for t in critical)
    def clear_stub(start,end,layer):
        n=max(1,math.ceil(math.dist(start,end)/.04))
        for i in range(n+1):
            x=start[0]+(end[0]-start[0])*i/n;y=start[1]+(end[1]-start[1])*i/n
            if any(p.IsOnLayer(layer) and dist_box(x,y,p.GetBoundingBox())<.26 for p in nonground):return False
            if any(dist_box(x,y,bb)<.11 for bb in keepouts):return False
            if any(t.IsOnLayer(layer) and dist_segment(x,y,t)<.25+track_width(t)/2 for t in critical):return False
        return True
    ground_vias=[];omitted=[]
    for f in b.GetFootprints():
        for p in f.Pads():
            if not p.GetNumber() or p.GetNetname()!='/GND' or p.GetAttribute()==k.PAD_ATTRIB_PTH:continue
            start=xy(p.GetPosition());layer=f.GetLayer()
            options=[]
            if p.GetNumber() in ['33','9']:options.append(start)
            for r in [.55,.65,.8,1.0,1.2,1.5]:
                for deg in range(0,360,15):options.append((round(start[0]+r*math.cos(math.radians(deg)),3),round(start[1]+r*math.sin(math.radians(deg)),3)))
            for end in options:
                if clear_point(*end) and clear_stub(start,end,layer) and all(math.dist(end,p)>.5 for p in ground_vias):
                    track('GND',[start,end],layer,.2);via('GND',end);ground_vias.append(end);break
            else:omitted.append(f.GetReference()+'.'+p.GetNumber())
    # The EPAD ground grid also conducts heat; through holes require the assembly
    # process to control solder wicking, verified during prototype assembly.
    for x,y in [(125.35,107.65),(126.65,107.65),(125.35,108.95),(126.65,108.95)]:
        if clear_point(x,y) and all(math.dist((x,y),p)>.5 for p in ground_vias):via('GND',(x,y));ground_vias.append((x,y))
    z=k.ZONE(b);z.SetLayer(k.In1_Cu);z.SetNet(net);z.SetLocalClearance(k.FromMM(.15));z.SetPadConnection(k.ZONE_CONNECTION_FULL);z.SetMinThickness(k.FromMM(.15))
    poly=z.Outline();poly.NewOutline()
    for x,y in [(100.25,100.25),(138.25,100.25),(138.25,116.35),(100.25,116.35)]:poly.Append(int(round(x*1e6)),int(round(y*1e6)))
    b.Add(z);k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(FILE),b,True)
    assert k.ExportSpecctraDSN(b,str(out/'integrated.dsn'))
    dsn=(out/'integrated.dsn').read_text()
    source='(layer In1.Cu\n      (type signal)'
    assert dsn.count(source)==1
    (out/'integrated.dsn').write_text(dsn.replace(source,'(layer In1.Cu\n      (type power)',1))
    (out/'routing-preparation.json').write_text(json.dumps({'ground_vias':len(ground_vias),'ground_fanout_deferred':omitted,'ground_reference':'In1.Cu','critical_tracks_locked':True},indent=2))
    print('Routing prepared; ground fanout deferred:',omitted)
if __name__=='__main__':main()
