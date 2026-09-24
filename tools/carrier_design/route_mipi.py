"""Route the reviewed carrier's six MIPI pairs on its two outer signal layers.

Only operates on a scratch project. Native DRC and separate connectivity,
coupling, reference-plane and delay-budget review must follow generation.
Dimensions are millimetres relative to the board's (50,50) origin.
"""
import argparse,json,math
from pathlib import Path
import pcbnew as k

WIDTH=.112; GAP=.15; HALF=(WIDTH+GAP)/2
VIA=.5; DRILL=.25
F=k.F_Cu; B=k.B_Cu

def add(a,b):return (a[0]+b[0],a[1]+b[1])
def sub(a,b):return (a[0]-b[0],a[1]-b[1])
def mul(a,s):return (a[0]*s,a[1]*s)
def cross(a,b):return a[0]*b[1]-a[1]*b[0]
def offset(points,d):
    lines=[]
    for a,b in zip(points,points[1:]):
        v=sub(b,a);length=math.hypot(*v);assert length>1e-8
        normal=(-v[1]/length,v[0]/length)
        lines.append((add(a,mul(normal,d)),add(b,mul(normal,d))))
    out=[lines[0][0]]
    for (a,b),(c,e) in zip(lines,lines[1:]):
        v=sub(b,a);w=sub(e,c);den=cross(v,w)
        out.append(b if abs(den)<1e-9 else add(a,mul(v,cross(sub(c,a),w)/den)))
    return out+[lines[-1][1]]

def geometry(d0_top=32.5,clk_right=79.2,csi_clk_bottom=35.0,csi_d0_bottom=35.8,phase=None):
    phase=phase or {}
    paths={};vias={};coupled={}
    def path(net,layer,pts):paths.setdefault(net,[]).append((layer,pts))
    def pair(name,center,p_side,layer):
        coupled.setdefault(name,[]).append(dict(center=center,side=p_side,layer=layer))
        for pol,side in [('P',p_side),('N',-p_side)]:
            pts=offset(center,side*HALF);path(name+'_'+pol,layer,pts)
        return [offset(center,s*HALF) for s in [p_side,-p_side]]
    def via(net,xy):vias.setdefault(net,[]).append(xy)
    # DSI D0 reverses the pair order with a coupled U-turn under the module.
    name='DSI_D0';pp=pair(name,[(74,35.1),(74,d0_top+.5),(73.5,d0_top),(72.5,d0_top),(72,d0_top+.5),(72,38.2),(70.5,39.7),(70.5,42.3)],-1,B)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;sv=(73.5+i,35.8);ev=(70.85-.7*i,43.1)
        via(n,sv);via(n,ev);path(n,F,[(73.5+i,34.373),sv]);path(n,B,[sv]+([(sv[0]-phase[name],sv[1]-.5)] if i==0 and name in phase else [])+[pp[i][0]])
        path(n,B,[pp[i][-1],ev]);path(n,F,[ev,(70.75-.5*i,43.2),(70.75-.5*i,44.55)])
    # DSI D1 also reverses direction as a coupled pair before returning to F.Cu.
    name='DSI_D1';bb=pair(name,[(70,35.1),(70,33.4),(69.4,32.8),(68.4,32.8),(67.8,33.4),(67.8,35.5),(70,37.7),(70,38.0)],-1,B)
    center=[(70,39.5),(70,40.1),(72,42.1),(72,43.2)]
    pp=pair(name,center,-1,F)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;sv=(69.5+i,35.8);ev=(70.35-.7*i,38.8)
        via(n,sv);via(n,ev);path(n,F,[(69.5+i,34.373),sv])
        path(n,B,[sv]+([(sv[0]-phase[name],sv[1]-.5)] if i==0 and name in phase else [])+[bb[i][0]])
        path(n,B,[bb[i][-1],ev])
        start=pp[i][0]
        path(n,F,[ev,(start[0],ev[1]+abs(start[0]-ev[0])),start])
        end=pp[i][-1];dest=(72.25-.5*i,44.55)
        path(n,F,[end,(dest[0],end[1]+abs(dest[0]-end[0])),dest])
    # Clock pair stays on F.Cu; common detour supplies the inter-lane budget.
    name='DSI_CLK';center=[(72,36),(72,37),(74.5,39.5),(clk_right-.7,39.5),(clk_right,40.2),(clk_right,41),(clk_right-.7,41.7),(74.7,41.7),(73.5,42.9),(73.5,43.2)]
    pp=pair(name,center,-1,F)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;src=(72.5-i,34.373);dest=(73.75-.5*i,44.55)
        path(n,F,[src,(src[0],35.25),pp[i][0]])
        end=pp[i][-1];path(n,F,[end,(dest[0],end[1]+abs(dest[0]-end[0])),dest])
    # CSI D0 starts toward the module on B.Cu, reversing the endpoint order.
    name='CSI_D0';pp=pair(name,[(77,35.1),(77,34.7),(78,33.7),(80,33.7),(81,34.7),(81,csi_d0_bottom-1),(82,csi_d0_bottom),(83,csi_d0_bottom),(84,csi_d0_bottom-1),(84,34.7),(85,33.7),(94,33.7),(95.5,35.2),(95.5,35.4)],1,B)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;sv=(77.5-i,35.8);ev=(95.15+.7*i,36.1)
        via(n,sv);via(n,ev);path(n,F,[(77.5-i,34.373),sv]);path(n,B,[sv]+([(sv[0]+phase[name],sv[1]-.5)] if i==0 and name in phase else [])+[pp[i][0]])
        path(n,B,[pp[i][-1],ev]);path(n,F,[ev,(95.25+.5*i,36.2),(95.25+.5*i,37.375)])
    # CSI D1 reverses direction beneath the module before traversing the top lane.
    name='CSI_D1';pp=pair(name,[(82.1,32),(81.2,32),(80.5,31.3),(80.5,30),(81.2,29.3),(95.7,29.3),(98.5,32.1),(98.5,35.4)],1,B)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;sv=(83,31.5+i);ev=(98.15+.7*i,36.1)
        via(n,sv);via(n,ev);path(n,F,[(81.373,31.5+i),sv]);path(n,B,[sv]+([(sv[0]+phase[name],sv[1]-phase[name])] if i==0 and name in phase else [])+[pp[i][0]])
        path(n,B,[pp[i][-1],ev]);path(n,F,[ev,(98.25+.5*i,36.2),(98.25+.5*i,37.375)])
    name='CSI_CLK';center=[(79,35.7),(79,36.1),(80,37.1),(84,37.1),(85,36.1),(85,33),(86,32),(87,32),(88,33),(88,csi_clk_bottom-1),(89,csi_clk_bottom),(90,csi_clk_bottom),(91,csi_clk_bottom-1),(91,33),(92,32),(96,32),(97,33),(97,36.0)]
    pp=pair(name,center,1,F)
    for i,pol in enumerate(['P','N']):
        n=name+'_'+pol;src=(78.5+i,34.373);dest=(96.75+.5*i,37.375)
        path(n,F,[src,(src[0],35.1),pp[i][0]])
        end=pp[i][-1];path(n,F,[end,(dest[0],end[1]+abs(dest[0]-end[0])),dest])
    return paths,vias,coupled

def lengths(paths):return {n:sum(math.dist(a,b) for _,pts in chunks for a,b in zip(pts,pts[1:])) for n,chunks in paths.items()}

def tuned_geometry():
    # Match the outer-layer trace delays; a two-through-via lane is allotted
    # 3.65 mm of equivalent outer-layer delay. This lies between geometric
    # 3.06 mm barrel length and the ~3.75 mm dielectric delay estimate.
    # Native KiCad geometric lengths are checked independently after saving.
    settings={};phase={};base=lengths(geometry()[0]);via_budget=3.65
    dsi=base['DSI_D1_N'];csi=base['CSI_D1_N']
    settings['d0_top']=32.5-(dsi-base['DSI_D0_N'])/2
    settings['clk_right']=79.2+(dsi+via_budget-base['DSI_CLK_N'])/2
    settings['csi_d0_bottom']=35.8+(csi-base['CSI_D0_N'])/2
    settings['csi_clk_bottom']=35+(csi+via_budget-base['CSI_CLK_N'])/2
    for pair in ['DSI_D0','DSI_D1','CSI_D0','CSI_D1']:
        lo=0.;hi=1.0
        for _ in range(45):
            mid=(lo+hi)/2;phase[pair]=mid
            ls=lengths(geometry(**settings,phase=phase)[0])
            if ls[pair+'_P']<ls[pair+'_N']:lo=mid
            else:hi=mid
        phase[pair]=(lo+hi)/2
    paths,vias,coupled=geometry(**settings,phase=phase)
    return paths,vias,coupled,dict(settings=settings,phase=phase,via_delay_equivalent_mm=via_budget)
def main(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    assert not b.GetTracks(),'Start from the reviewed unrouted baseline'
    paths,vias,coupled,tuning=tuned_geometry()
    def vec(p):return k.VECTOR2I(k.FromMM(p[0]+50),k.FromMM(p[1]+50))
    nets={p.GetNetname():p.GetNetCode() for f in b.GetFootprints() for p in f.Pads()}
    for net,chunks in paths.items():
        for layer,pts in chunks:
            for a,c in zip(pts,pts[1:]):
                if math.dist(a,c)<1e-7:continue
                t=k.PCB_TRACK(b);t.SetStart(vec(a));t.SetEnd(vec(c));t.SetWidth(k.FromMM(WIDTH));t.SetLayer(layer);t.SetNetCode(nets[net]);t.SetLocked(True);b.Add(t)
    for net,pts in vias.items():
        for xy in pts:
            v=k.PCB_VIA(b);v.SetPosition(vec(xy));v.SetWidth(k.FromMM(VIA));v.SetDrill(k.FromMM(DRILL));v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(F,B);v.SetNetCode(nets[net]);v.SetLocked(True);b.Add(v)
    b.GetTitleBlock().SetRevision('Q - routing in progress')
    k.SaveBoard(str(project/'display_carrier.kicad_pcb'),b)
    ls=lengths(paths)
    report=dict(width=WIDTH,gap=GAP,via_diameter=VIA,via_drill=DRILL,planar_lengths_mm=ls,via_counts={n:len(vias.get(n,[])) for n in paths},coupled=coupled,paths=paths,vias=vias,tuning=tuning,status='GEOMETRY_REQUIRES_DRC_AND_TUNING')
    (project/'MIPI_ROUTING.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({n:round(v,4) for n,v in ls.items()},indent=2))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--project',type=Path,required=True)
    main(ap.parse_args().project)
