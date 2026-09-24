"""Add continuous inner reference planes and short outer-layer ground returns.

Uses conservative pad rectangles for placement; native zone filling and DRC
remain mandatory. Do not use this as a replacement for current-loop review.
"""
import argparse,json,math
from pathlib import Path
import pcbnew as k
from cad import sexp
from route_mipi import offset

def main(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    assert all(z.GetIsRuleArea() for z in b.Zones()),'Run once before plane filling'
    pads=[p for f in b.GetFootprints() for p in f.Pads()]
    net=next(p.GetNetCode() for p in pads if p.GetNetname()=='GND')
    def xy(v):return (k.ToMM(v.x)-50,k.ToMM(v.y)-50)
    def vec(p):return k.VECTOR2I(k.FromMM(p[0]+50),k.FromMM(p[1]+50))
    def box(z):
        q=z.GetBoundingBox();return tuple(k.ToMM(v)-50 for v in [q.GetX(),q.GetY(),q.GetRight(),q.GetBottom()])
    pp=[(p,box(p)) for p in pads]
    pad_obstacles=[(bb,p.GetNetname()=='GND',bool(p.GetLayerSet().Contains(k.F_Cu) or p.GetDrillSize().x)) for p,bb in pp]
    segs=[(xy(t.GetStart()),xy(t.GetEnd()),k.ToMM(t.GetWidth(k.F_Cu) if isinstance(t,k.PCB_VIA) else t.GetWidth())/2,t.GetNetname(),t.GetLayer(),isinstance(t,k.PCB_VIA)) for t in b.GetTracks()]
    zones=[z for z in b.Zones()]+[z for f in b.GetFootprints() for z in f.Zones()]
    keep=[box(z) for z in zones if z.GetIsRuleArea() and (z.GetDoNotAllowTracks() or z.GetDoNotAllowVias())]
    no_pour=[box(z) for z in zones if z.GetIsRuleArea() and z.GetDoNotAllowZoneFills()]
    outline=k.SHAPE_POLY_SET();assert b.GetBoardPolygonOutlines(outline,False)
    holes=[]
    for i in range(outline.HoleCount(0)):
        q=outline.Hole(0,i).BBox();holes.append(tuple(k.ToMM(v)-50 for v in [q.GetX(),q.GetY(),q.GetRight(),q.GetBottom()]))
    def dbox(p,q):return math.hypot(max(q[0]-p[0],0,p[0]-q[2]),max(q[1]-p[1],0,p[1]-q[3]))
    def dseg(p,a,c):
        dx,dy=c[0]-a[0],c[1]-a[1];den=dx*dx+dy*dy
        t=min(1,max(0,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/den)) if den else 0
        return math.hypot(p[0]-a[0]-t*dx,p[1]-a[1]-t*dy)
    def legal(p,r,via=False,controlled_gap=.45):
        if not (r+.31<p[0]<145-r-.31 and r+.31<p[1]<85-r-.31):return False
        if any(dbox(p,q)<r+.31 for q in holes):return False
        if any(dbox(p,q)<r+.015 for q in keep):return False
        if via and any(dbox(p,q)<r+.015 for q in no_pour):return False
        for bb,gnd,front in pad_obstacles:
            if gnd and not via:continue
            if (via or front) and dbox(p,bb)<r+.14:return False
        for a,c,s,n,l,v in segs:
            if n=='GND':continue
            if not via and l!=k.F_Cu and not v:continue
            gap=controlled_gap if n.startswith(('DSI_','CSI_')) and not v else .14
            if dseg(p,a,c)<r+s+gap:return False
        return True
    gndvias=[]
    def via(p):
        for prior in gndvias:
            if math.dist(prior,p)<.51:return False
        if not legal(p,.25,True):return False
        v=k.PCB_VIA(b);v.SetPosition(vec(p));v.SetWidth(k.FromMM(.5));v.SetDrill(k.FromMM(.25));v.SetViaType(k.VIATYPE_THROUGH)
        v.SetLayerPair(k.F_Cu,k.B_Cu);v.SetNetCode(net);v.SetLocked(True);b.Add(v);gndvias.append(p);return True
    def track(a,c,w=.2):
        t=k.PCB_TRACK(b);t.SetStart(vec(a));t.SetEnd(vec(c));t.SetWidth(k.FromMM(w));t.SetLayer(k.F_Cu);t.SetNetCode(net);t.SetLocked(True);b.Add(t)
    def legal_line(a,c):
        def hits(bb,r):
            lo=0.;hi=1.
            for axis in [0,1]:
                v=c[axis]-a[axis];mn=bb[axis]-r;mx=bb[axis+2]+r
                if abs(v)<1e-10:
                    if not mn<=a[axis]<=mx:return False
                else:
                    x=(mn-a[axis])/v;y=(mx-a[axis])/v
                    lo=max(lo,min(x,y));hi=min(hi,max(x,y))
                    if lo>hi:return False
            return True
        if not all(.43<x<144.57 and .43<y<84.57 for x,y in [a,c]):return False
        if any(hits(q,.43) for q in holes) or any(hits(q,.14) for q in keep):return False
        if any(not gnd and front and hits(bb,.26) for bb,gnd,front in pad_obstacles):return False
        for x,y,s,n,l,v in segs:
            if n=='GND' or (l!=k.F_Cu and not v):continue
            gap=.45 if n.startswith(('DSI_','CSI_')) and not v else .14
            r=.12+s+gap
            if not hits((min(x[0],y[0]),min(x[1],y[1]),max(x[0],y[0]),max(x[1],y[1])),r):continue
            steps=max(1,math.ceil(math.dist(a,c)/.03))
            if any(dseg((a[0]+(c[0]-a[0])*i/steps,a[1]+(c[1]-a[1])*i/steps),x,y)<r for i in range(steps+1)):return False
        return True
    # A pair of return vias for every pair transition, tied to both planes.
    m=json.loads((project/'MIPI_ROUTING.json').read_text('utf8'));returns=[]
    for name in ['DSI_D0','DSI_D1','CSI_D0','CSI_D1']:
        for i,(a,c) in enumerate(zip(m['vias'][name+'_P'],m['vias'][name+'_N'])):
            center=((a[0]+c[0])/2,(a[1]+c[1])/2)
            placed=[q for q in gndvias if math.dist(q,center)<1.75][:2]
            candidates=[(center[0]+r*math.cos(t*math.pi/24),center[1]+r*math.sin(t*math.pi/24)) for r in [.75,1,1.25,1.5,1.75,2] for t in range(48)]
            for p in candidates:
                if len(placed)==2:break
                if placed and math.dist(placed[0],p)<1:continue
                if via(p):placed.append(p)
                if len(placed)==2:break
            assert len(placed)==2,(name,i)
            returns.append(dict(pair=name,transition=i,signal_vias=[a,c],ground_vias=placed))
    # Guard the coupled clock sections, maintaining >=0.45 mm edge clearance
    # so the selected non-coplanar JLC model remains a useful starting point.
    guards=[]
    for name in ['DSI_CLK','CSI_CLK']:
        for chunk in m['coupled'][name]:
            for side in [-1,1]:
                pts=offset(chunk['center'],side*.96)
                for a,c in zip(pts,pts[1:]):
                    length=math.dist(a,c);trim=min(.7,length*.3)
                    delta=((c[0]-a[0])*trim/length,(c[1]-a[1])*trim/length)
                    a=(a[0]+delta[0],a[1]+delta[1]);c=(c[0]-delta[0],c[1]-delta[1])
                    if not legal_line(a,c):continue
                    if math.dist(a,c)<.6:continue
                    if not all(legal(p,.25,True) and not any(.19<math.dist(p,q)<.51 for q in gndvias) for p in [a,c]):continue
                    for p in [a,c]:
                        if not any(math.dist(p,q)<.19 for q in gndvias):assert via(p)
                    track(a,c);guards.append(dict(clock=name,a=a,b=c))
    # Local grounding. No open via-in-pad, including exposed QFN pads.
    escaped=[];pending=[]
    for pad,bb in sorted(pp,key=lambda p:(p[0].GetParentFootprint().GetReference(),p[0].GetNumber())):
        if pad.GetNetname()!='GND' or pad.GetDrillSize().x:continue
        a=xy(pad.GetPosition());found=False
        for q in sorted(gndvias,key=lambda q:math.dist(a,q))[:12]:
            if math.dist(a,q)<2.1 and legal_line(a,q):track(a,q);found=True;break
        if not found:
            for r in [.7,.85,1,1.25,1.5,1.75,2]:
                for t in range(16):
                    q=(a[0]+r*math.cos(t*math.pi/8),a[1]+r*math.sin(t*math.pi/8))
                    if legal_line(a,q) and via(q):track(a,q);found=True;break
                if found:break
        (escaped if found else pending).append(pad.GetParentFootprint().GetReference()+'.'+pad.GetNumber())
    for layer in [k.In1_Cu,k.In2_Cu]:
        z=k.ZONE(b);z.SetLayer(layer);z.SetNetCode(net);z.SetZoneName('GND_REFERENCE_'+b.GetLayerName(layer));z.SetLocalClearance(k.FromMM(.127));z.SetMinThickness(k.FromMM(.1));z.SetPadConnection(k.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(k.ISLAND_REMOVAL_MODE_ALWAYS);z.SetMinIslandArea(k.FromMM(1)**2)
        poly=z.Outline();poly.NewOutline()
        for p in [(0,0),(145,0),(145,85),(0,85)]:poly.Append(vec(p))
        b.Add(z)
    k.ZONE_FILLER(b).Fill(b.Zones())
    k.SaveBoard(str(project/'display_carrier.kicad_pcb'),b)
    result=dict(return_vias=returns,ground_via_count=len(gndvias),clock_guard_segments=guards,ground_pads_fanned_out=escaped,ground_pads_pending=pending)
    (project/'GROUND_ROUTING.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print('Ground vias',len(gndvias),'ground pads escaped',len(escaped),'pending',len(pending),'guards',len(guards))

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--project',type=Path,required=True);main(a.parse_args().project)
