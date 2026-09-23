"""Geometry-only MIPI escape trial. Never a production differential router.

Uses existing netclass dimensions and native pad nets. Length/impedance,
coupling, via stubs and return paths are explicitly NOT qualified by A*.
Outputs a separate board; the placement board stays unrouted.
"""
import argparse,heapq,json,math,shutil
from pathlib import Path
import numpy as np
import pcbnew as k

STEP=.05;X0=67;Y0=30;X1=104;Y1=46
WIDTH=.15;CLEAR=.127;VIA=.6;DRILL=.3;MARGIN=.012
LAYERS=[k.F_Cu,k.In2_Cu,k.B_Cu]
PAIRS=['DSI_CLK','DSI_D1','DSI_D0','CSI_D1','CSI_CLK','CSI_D0']

def route(project,out):
 assert not out.exists()
 out.mkdir(parents=True)
 for p in project.iterdir():
  if p.is_file() and p.suffix in ['.kicad_pcb','.kicad_pro','.kicad_sch']:
   shutil.copyfile(p,out/p.name)
 # Local libraries remain linked through tables copied for native parity.
 for name in ['Carrier.kicad_sym','sym-lib-table','fp-lib-table']:
  shutil.copyfile(project/name,out/name)
 for name in ['Carrier.pretty','Carrier.3dshapes']:shutil.copytree(project/name,out/name)
 b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
 assert not len(b.GetTracks())
 fps={f.GetReference():f for f in b.GetFootprints()}
 nx=int(round((X1-X0)/STEP))+1;ny=int(round((Y1-Y0)/STEP))+1
 xx=X0+np.arange(nx)*STEP;yy=Y0+np.arange(ny)*STEP
 def position(p):return (k.ToMM(p.GetPosition().x)-50,k.ToMM(p.GetPosition().y)-50)
 def grid(p):return (int(round((p[0]-X0)/STEP)),int(round((p[1]-Y0)/STEP)))
 def real(p):return (X0+p[0]*STEP,Y0+p[1]*STEP)
 def vec(p):return k.VECTOR2I(k.FromMM(p[0]+50),k.FromMM(p[1]+50))
 def padbox(p):
  z=p.GetBoundingBox();return [k.ToMM(t)-50 for t in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()]]
 pads=[p for f in fps.values() for p in f.Pads()]
 segments=[];vias=[];rows=[]
 def rect(mask,bb,r):
  x1,y1,x2,y2=bb
  ix=np.flatnonzero((xx>=x1-r)&(xx<=x2+r));iy=np.flatnonzero((yy>=y1-r)&(yy<=y2+r))
  if len(ix) and len(iy):mask[np.ix_(iy,ix)]=True
 def capsule(mask,a,c,r):
  ix=np.flatnonzero((xx>=min(a[0],c[0])-r)&(xx<=max(a[0],c[0])+r))
  iy=np.flatnonzero((yy>=min(a[1],c[1])-r)&(yy<=max(a[1],c[1])+r))
  if not len(ix) or not len(iy):return
  px,py=np.meshgrid(xx[ix],yy[iy]);dx=c[0]-a[0];dy=c[1]-a[1]
  t=np.clip(((px-a[0])*dx+(py-a[1])*dy)/(dx*dx+dy*dy),0,1) if dx or dy else 0
  ok=(px-a[0]-t*dx)**2+(py-a[1]-t*dy)**2<=r*r
  mask[np.ix_(iy,ix)]|=ok
 def masks(net):
  blocked=np.zeros((3,ny,nx),dtype=bool);via_block=np.zeros((ny,nx),dtype=bool)
  for p in pads:
   bb=padbox(p)
   # No via-in-pad even on the current net.
   rect(via_block,bb,VIA/2+CLEAR+MARGIN)
   if p.GetNetname()==net:continue
   for li,layer in enumerate(LAYERS):
    if p.GetLayerSet().Contains(layer) or p.GetDrillSize().x:
     rect(blocked[li],bb,WIDTH/2+CLEAR+MARGIN)
  for s in segments:
   if s['net']==net:continue
   capsule(blocked[s['layer']],s['a'],s['b'],WIDTH+CLEAR+MARGIN)
   capsule(via_block,s['a'],s['b'],VIA/2+WIDTH/2+CLEAR+MARGIN)
  for v in vias:
   if v['net']==net:continue
   for li in range(3):capsule(blocked[li],v['xy'],v['xy'],VIA/2+WIDTH/2+CLEAR+MARGIN)
   capsule(via_block,v['xy'],v['xy'],VIA+CLEAR+MARGIN)
  for z in b.Zones():
   if z.GetIsRuleArea() and (z.GetDoNotAllowTracks() or z.GetDoNotAllowVias()):
    q=z.GetBoundingBox();bb=[k.ToMM(t)-50 for t in [q.GetX(),q.GetY(),q.GetRight(),q.GetBottom()]]
    rect(via_block,bb,VIA/2+MARGIN)
    for li,layer in enumerate(LAYERS):
     if z.GetLayerSet().Contains(layer):rect(blocked[li],bb,WIDTH/2+MARGIN)
  return blocked,via_block
 def astar(start,end,blocked,via_block,affinity):
  sx,sy=grid(start);tx,ty=grid(end)
  def ident(l,x,y):return (l*ny+y)*nx+x
  def decode(n):return n//(ny*nx),n%nx,(n//nx)%ny
  first=ident(0,sx,sy);last=ident(0,tx,ty)
  dist=np.full(3*ny*nx,np.inf,dtype=np.float32);prev=np.full(3*ny*nx,-1,dtype=np.int32)
  dist[first]=0;queue=[(math.hypot(tx-sx,ty-sy),0,first)]
  ds=[(1,0,1),(-1,0,1),(0,1,1),(0,-1,1),(1,1,math.sqrt(2)),(1,-1,math.sqrt(2)),(-1,1,math.sqrt(2)),(-1,-1,math.sqrt(2))]
  visits=0
  while queue:
   _,cost,n=heapq.heappop(queue)
   if cost>float(dist[n])+.0001:continue
   if n==last:break
   visits+=1;l,x,y=decode(n)
   for dx,dy,w in ds:
    u=x+dx;v=y+dy
    if not (0<=u<nx and 0<=v<ny) or blocked[l,v,u]:continue
    if dx and dy and (blocked[l,y,u] or blocked[l,v,x]):continue
    m=ident(l,u,v);c=cost+w*(1+float(affinity[l,v,u]))
    # Prefer straight segments over many alternating 45-degree corners.
    if prev[n]>=0:
     pl,px,py=decode(int(prev[n]))
     if pl==l and (x-px,y-py)!=(dx,dy):c+=.16
    if c+1e-4<float(dist[m]):
     dist[m]=c;prev[m]=n
     heapq.heappush(queue,(c+math.hypot(tx-u,ty-v)+(0 if l==0 else 35),c,m))
   if not via_block[y,x] and not any(blocked[:,y,x]):
    for ll in range(3):
     if ll==l:continue
     m=ident(ll,x,y);c=cost+75
     if c+1e-4<float(dist[m]):dist[m]=c;prev[m]=n;heapq.heappush(queue,(c+math.hypot(tx-x,ty-y)+(0 if ll==0 else 35),c,m))
  else:raise RuntimeError('No path')
  nodes=[];n=last
  while n>=0:nodes.append(decode(n));n=int(prev[n])
  return list(reversed(nodes)),visits
 def add_track(net,li,a,c):
  if math.dist(a,c)<.0001:return
  t=k.PCB_TRACK(b);t.SetStart(vec(a));t.SetEnd(vec(c));t.SetLayer(LAYERS[li]);t.SetWidth(k.FromMM(WIDTH));t.SetNetCode(netcode[net]);b.Add(t)
  segments.append(dict(net=net,layer=li,a=a,b=c))
 def add_via(net,p):
  v=k.PCB_VIA(b);v.SetPosition(vec(p));v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(k.F_Cu,k.B_Cu)
  v.SetWidth(k.FromMM(VIA));v.SetDrill(k.FromMM(DRILL));v.SetNetCode(netcode[net]);b.Add(v);vias.append(dict(net=net,xy=p))
 netcode={p.GetNetname():p.GetNetCode() for p in pads}
 for pair in PAIRS:
  for pol in ['P','N']:
   net=pair+'_'+pol
   source=next(p for p in fps['U100'].Pads() if p.GetNetname()==net)
   dest=next(p for p in fps['J1200' if pair.startswith('DSI') else 'J1220'].Pads() if p.GetNetname()==net)
   a=position(source);c=position(dest);blocked,via_block=masks(net)
   affinity=np.zeros((3,ny,nx),dtype=np.float32)
   if pol=='N':
    # Prefer the P route corridor where clearance permits. This is only an
    # adjacency heuristic; it does not enforce coupled spacing or length.
    px,py=np.meshgrid(xx,yy);near=np.full((3,ny,nx),100,dtype=np.float32)
    for s in segments:
     if s['net']!=pair+'_P':continue
     ax,ay=s['a'];bx,by=s['b'];dx=bx-ax;dy=by-ay
     t=np.clip(((px-ax)*dx+(py-ay)*dy)/(dx*dx+dy*dy),0,1)
     near[s['layer']]=np.minimum(near[s['layer']],np.hypot(px-ax-t*dx,py-ay-t*dy))
    affinity=.35*np.minimum(np.abs(near-.3)/.3,6)
   nodes,visits=astar(a,c,blocked,via_block,affinity)
   # Collapse collinear grid steps into native straight track segments.
   simplified=[nodes[0]]
   for i in range(1,len(nodes)-1):
    p,q,r=nodes[i-1:i+2]
    if p[0]!=q[0] or q[0]!=r[0] or (q[1]-p[1],q[2]-p[2])!=(r[1]-q[1],r[2]-q[2]):simplified.append(q)
   simplified.append(nodes[-1]);count_before=len(vias)
   add_track(net,0,a,real(simplified[0][1:]))
   for p,q in zip(simplified,simplified[1:]):
    pp=real(p[1:]);qq=real(q[1:])
    if p[0]!=q[0]:add_via(net,pp)
    else:add_track(net,p[0],pp,qq)
   add_track(net,0,real(simplified[-1][1:]),c)
   row=dict(net=net,length_mm=round(sum(math.dist(s['a'],s['b']) for s in segments if s['net']==net),4),vias=len(vias)-count_before,visited=visits)
   rows.append(row);print(json.dumps(row),flush=True)
 b.GetTitleBlock().SetRevision('N GEOMETRY TRIAL ONLY')
 k.SaveBoard(str(out/'display_carrier.kicad_pcb'),b)
 shutil.copyfile(project/'display_carrier.kicad_pro',out/'display_carrier.kicad_pro')
 report=dict(status='GEOMETRY_TRIAL_NOT_SIGNAL_INTEGRITY_APPROVED',track_width_mm=WIDTH,clearance_mm=CLEAR,
  via_mm=VIA,drill_mm=DRILL,used_layers=['F.Cu','In2.Cu','B.Cu'],nets=rows,
  pair_length_delta_mm={p:round(abs(next(r['length_mm'] for r in rows if r['net']==p+'_P')-next(r['length_mm'] for r in rows if r['net']==p+'_N')),4) for p in PAIRS},
  limitations=['Grid A* is an escape/congestion trial, not a coupled differential router.',
  'Unmatched lengths, pair spacing, return vias and actual stackup/100 ohm impedance require final engineering.',
  'Trial copper is not promoted into the unrouted placement board.'])
 (out/'ROUTING_TRIAL.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();route(a.project,a.output)
