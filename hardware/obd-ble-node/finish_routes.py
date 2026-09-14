"""Finish the constrained local connections on the integrated A1 prototype.

Grid search only proposes tracks. KiCad's full DRC and schematic parity are the
acceptance gate; this script does not report electrical success on its own.
Requires KiCad Python (pcbnew, numpy). Run after the matching SES import.
"""
from pathlib import Path
import heapq,json,math,time
import numpy as np
import pcbnew as k

ROOT=Path(__file__).resolve().parent;FILE=ROOT/'obd_ble_node.kicad_pcb'
STEP=.025;NX=1541;NY=665;LAYERS=[k.F_Cu,k.In2_Cu,k.B_Cu]
def v(x,y):return k.VECTOR2I(round(x*1e6),round(y*1e6))
def xy(p):return k.ToMM(p.x),k.ToMM(p.y)
def grid(p):return round((p[0]-100)/STEP),round((p[1]-100)/STEP)
def world(x,y):return 100+x*STEP,100+y*STEP
def width(t):return k.ToMM(t.GetWidth(k.F_Cu) if isinstance(t,k.PCB_VIA) else t.GetWidth())
def route(b,ref1,pin1,ref2,pin2,trace_width=.127):
    fps={f.GetReference():f for f in b.GetFootprints()}
    p1=next(p for p in fps[ref1].Pads() if p.GetNumber()==str(pin1));p2=next(p for p in fps[ref2].Pads() if p.GetNumber()==str(pin2))
    assert p1.GetNetname()==p2.GetNetname()
    net=p1.GetNetname();blocked=np.zeros((3,NY,NX),dtype=np.bool_);via_block=np.zeros((NY,NX),dtype=np.bool_)
    track_margin=.127+trace_width/2+.012;via_margin=.327+.012
    def paint(mask,box,distance,margin):
        x0,y0,x1,y1=box
        a=max(0,int((x0-margin-100)/STEP)-1);c=max(0,int((y0-margin-100)/STEP)-1)
        d=min(NX,int((x1+margin-100)/STEP)+2);e=min(NY,int((y1+margin-100)/STEP)+2)
        if a>=d or c>=e:return
        xx=100+np.arange(a,d)*STEP;yy=100+np.arange(c,e)*STEP
        mask[c:e,a:d]|=distance(xx[None,:],yy[:,None])<margin
    def rectangle(box,radius=0):
        x0,y0,x1,y1=box;cx=(x0+x1)/2;cy=(y0+y1)/2
        return lambda x,y:np.hypot(np.maximum(np.abs(x-cx)-(x1-x0)/2+radius,0),np.maximum(np.abs(y-cy)-(y1-y0)/2+radius,0))-radius
    for f in b.GetFootprints():
        for p in f.Pads():
            if not p.GetNumber() or p.GetNetname()==net:continue
            bb=p.GetBoundingBox();box=tuple(k.ToMM(n) for n in [bb.GetLeft(),bb.GetTop(),bb.GetRight(),bb.GetBottom()])
            radius=0
            if p.GetShape() in [k.PAD_SHAPE_OVAL,k.PAD_SHAPE_CIRCLE]:radius=min(box[2]-box[0],box[3]-box[1])/2
            elif p.GetShape()==k.PAD_SHAPE_ROUNDRECT:radius=k.ToMM(p.GetRoundRectCornerRadius())
            distance=rectangle(box,radius)
            for i,layer in enumerate(LAYERS):
                if p.IsOnLayer(layer):paint(blocked[i],box,distance,track_margin)
            paint(via_block,box,distance,via_margin)
        for z in f.Zones():
            if not z.GetIsRuleArea():continue
            bb=z.GetBoundingBox();box=tuple(k.ToMM(n) for n in [bb.GetLeft(),bb.GetTop(),bb.GetRight(),bb.GetBottom()]);distance=rectangle(box)
            for i,layer in enumerate(LAYERS):
                if z.IsOnLayer(layer):paint(blocked[i],box,distance,trace_width/2+.012)
            paint(via_block,box,distance,.212)
    for t in b.GetTracks():
        if t.GetNetname()==net:continue
        ax,ay=xy(t.GetStart());bx,by=xy(t.GetEnd());tw=width(t)
        def distance(x,y,ax=ax,ay=ay,bx=bx,by=by,tw=tw):
            den=(bx-ax)**2+(by-ay)**2
            u=np.clip(((x-ax)*(bx-ax)+(y-ay)*(by-ay))/den,0,1) if den else 0
            return np.hypot(x-ax-u*(bx-ax),y-ay-u*(by-ay))-tw/2
        box=(min(ax,bx)-tw/2,min(ay,by)-tw/2,max(ax,bx)+tw/2,max(ay,by)+tw/2)
        for i,layer in enumerate(LAYERS):
            if t.IsOnLayer(layer):paint(blocked[i],box,distance,track_margin)
        paint(via_block,box,distance,via_margin)
    boundary=math.ceil((.25+trace_width/2+.012)/STEP)
    blocked[:,:boundary,:]=True;blocked[:,-boundary:,:]=True;blocked[:,:,:boundary]=True;blocked[:,:,-boundary:]=True
    vbound=math.ceil(.462/STEP);via_block[:vbound,:]=True;via_block[-vbound:,:]=True;via_block[:,:vbound]=True;via_block[:,-vbound:]=True
    sx,sy=grid(xy(p1.GetPosition()));gx,gy=grid(xy(p2.GetPosition()))
    sl=LAYERS.index(fps[ref1].GetLayer());gl=LAYERS.index(fps[ref2].GetLayer())
    assert not blocked[sl,sy,sx] and not blocked[gl,gy,gx],('blocked endpoint',net)
    def key(x,y,l):return (l*NY+y)*NX+x
    def decode(n):l,rest=divmod(n,NX*NY);y,x=divmod(rest,NX);return x,y,l
    def h(x,y,l):
        dx=abs(x-gx);dy=abs(y-gy)
        return max(dx,dy)+.41421356*min(dx,dy)+(0 if l==gl else 25)
    start=key(sx,sy,sl);goal=key(gx,gy,gl);cost={start:0};prev={};queue=[(h(sx,sy,sl),0,start)]
    visited=0;t0=time.monotonic()
    while queue:
        _,score,node=heapq.heappop(queue)
        if score!=cost.get(node):continue
        if node==goal:break
        visited+=1
        if visited>1200000 or time.monotonic()-t0>90:raise RuntimeError(('route search budget',net,visited))
        x,y,l=decode(node)
        neighbors=[]
        for dx,dy,extra in [(-1,0,1),(1,0,1),(0,-1,1),(0,1,1),(-1,-1,1.41421356),(-1,1,1.41421356),(1,-1,1.41421356),(1,1,1.41421356)]:
            nx=x+dx;ny=y+dy
            if 0<=nx<NX and 0<=ny<NY and not blocked[l,ny,nx]:neighbors.append((nx,ny,l,extra))
        if not via_block[y,x]:
            for nl in range(3):
                if nl!=l and not blocked[nl,y,x]:neighbors.append((x,y,nl,25))
        for nx,ny,nl,extra in neighbors:
            nxt=key(nx,ny,nl);new=score+extra
            if new<cost.get(nxt,math.inf):cost[nxt]=new;prev[nxt]=node;heapq.heappush(queue,(new+h(nx,ny,nl),new,nxt))
    else:raise RuntimeError(('no route',net))
    path=[goal]
    while path[-1]!=start:path.append(prev[path[-1]])
    path=[decode(n) for n in reversed(path)]
    # Collapse collinear grid steps and retain each layer transition.
    simple=[path[0]]
    for i in range(1,len(path)-1):
        a=path[i-1];c=path[i];d=path[i+1]
        if (c[0]-a[0],c[1]-a[1],c[2]-a[2])!=(d[0]-c[0],d[1]-c[1],d[2]-c[2]):simple.append(c)
    simple.append(path[-1]);added_vias=0
    def segment(a,c,layer):
        if math.dist(a,c)<.000001:return
        t=k.PCB_TRACK(b);t.SetStart(v(*a));t.SetEnd(v(*c));t.SetWidth(k.FromMM(trace_width));t.SetLayer(layer);t.SetNet(p1.GetNet());b.Add(t)
    segment(xy(p1.GetPosition()),world(sx,sy),LAYERS[sl])
    for a,c in zip(simple,simple[1:]):
        if a[2]!=c[2]:
            t=k.PCB_VIA(b);t.SetPosition(v(*world(a[0],a[1])));t.SetWidth(k.FromMM(.4));t.SetDrill(k.FromMM(.2));t.SetViaType(k.VIATYPE_THROUGH);t.SetLayerPair(k.F_Cu,k.B_Cu);t.SetNet(p1.GetNet());b.Add(t);added_vias+=1
        else:segment(world(a[0],a[1]),world(c[0],c[1]),LAYERS[a[2]])
    segment(world(gx,gy),xy(p2.GetPosition()),LAYERS[gl])
    report={'net':net,'from':ref1+'.'+str(pin1),'to':ref2+'.'+str(pin2),'vias':added_vias,'grid_steps':len(path),'search_nodes':visited}
    print(report,flush=True);return report

if __name__=='__main__':
    b=k.LoadBoard(str(FILE));reports=[]
    fps={f.GetReference():f for f in b.GetFootprints()}
    # C16 must decouple pin 18, adjacent to its actual right-side pad.
    old=(129.8,106.52);orphan_vias=[];removed=[]
    for t in list(b.GetTracks()):
        if isinstance(t,k.PCB_VIA) or t.GetNetname()!='/GND' or t.GetLayer()!=k.F_Cu:continue
        a=xy(t.GetStart());c=xy(t.GetEnd())
        if math.dist(a,old)<.001 or math.dist(c,old)<.001:
            orphan_vias.append(c if math.dist(a,old)<.001 else a);removed.append(t);b.Remove(t)
    for t in list(b.GetTracks()):
        if isinstance(t,k.PCB_VIA) and t.GetNetname()=='/GND' and any(math.dist(xy(t.GetPosition()),p)<.001 for p in orphan_vias):removed.append(t);b.Remove(t)
    fps['C16'].SetPosition(v(129.8,109.2));fps['C16'].SetOrientationDegrees(90)
    fps['C14'].SetPosition(v(130.15,110.85));fps['C14'].SetOrientationDegrees(0)
    pairs=[('U1',4,'C2',1),('U5',18,'C16',1),('U5',17,'C14',1),('U3',5,'U5',17),('U5',2,'FB1',2),('C14',2,'C16',2),('F1',2,'D1',2)]
    for pair in pairs:
        reports.append(route(b,*pair,.25 if pair[0]=='F1' else .127))
    k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(FILE),b,True)
    (ROOT/'build/route-finishing.json').write_text(json.dumps(reports,indent=2))
