"""Revision N: slot-preserving placement for DSI/CSI escape trials.

Run only on a fresh scratch copy. Native pad identities and all existing
mechanical holes are preserved. Stackup/impedance and flex fit remain open.
"""
import argparse,json,math,shutil
from pathlib import Path
import pcbnew as k

AREAS={
 'MIPI_DSI_ESCAPE':(68.5,35.1,80.3,43.55),
 'MIPI_CSI_TURN':(75.9,35.1,85.2,40.3),
 'MIPI_CSI_ESCAPE':(82.2,31.75,103,36.45),
 'CAMERA_LATCH_FLEX_CLEAR':(85.2,43.1,102.8,47.8),
}
def stage(baseline,out):
 assert not out.exists() and baseline.resolve()!=out.resolve()
 prior=json.loads((baseline/'SCREEN_BACK_LAYOUT.json').read_text(encoding='utf-8'))
 assert prior['revision']=='L1' and len(prior['slots'])==2,'Requires the pre-N L1 slot layout'
 b=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
 assert not len(b.GetTracks()) and not any(not z.GetIsRuleArea() for z in b.Zones())
 fps={f.GetReference():f for f in b.GetFootprints()}
 intent=json.loads((baseline/'design-intent.json').read_text(encoding='utf-8'))
 parts={p['ref']:p for p in intent['parts']}
 def vec(x,y):return k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
 def xy(f):return [round(k.ToMM(f.GetPosition().x)-50,6),round(k.ToMM(f.GetPosition().y)-50,6)]
 def state(f):return dict(xy_mm=xy(f),side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
 before={r:state(f) for r,f in fps.items()}
 def move(r,x,y,a=None):
  f=fps[r];f.SetPosition(vec(x,y))
  if a is not None:f.SetOrientationDegrees(a)
 def box(f):
  z=f.GetBoundingBox(False,False)
  return [k.ToMM(v)-50 for v in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()]]
 def overlap(a,b,g=.4):return a[0]<b[2]+g and a[2]>b[0]-g and a[1]<b[3]+g and a[3]>b[1]-g
 core={r for r in fps if parts.get(r,{}).get('sheet')=='01_core' and r.startswith(('R','C'))}
 for r in core:
  x,y=xy(fps[r]);move(r,x,y-6)
 active={'U100':(69,22,0),'J1220':(94,39,0),
  'U410':(97,51,180),'L410':(90,51,180),
  'U1200':(58,47,0),'U1201':(58,53,0)}
 for r,v in active.items():move(r,*v)
 movable=core|{r for r in fps if parts.get(r,{}).get('sheet')=='14_lcd_fpc' and not r.startswith('J')}|{
  'C410','C411','C412','C413','R410','R411','C312','D311','R402','R403','R404','R1301'}
 movable-=set(active)
 anchors={r:xy(f) for r,f in fps.items()}
 anchors.update({'R106':[53,20],'R110':[53,27],
  'R1301':[82.5,6],'C312':[107,25],'D311':[111,33],
  'R402':[111,40],'R403':[111,47],'R404':[112,50],
  'C410':[102,50.5],'C411':[89,55],'C412':[94,55],'C413':[99,55.5],
  'R410':[102.5,54],'R411':[103,56.5],
  'C1200':[54,45],'C1201':[62,47],'C1202':[62,50],'C1203':[62,53],
  'R1200':[54,39],'R1201':[54,47.5],'R1202':[54,50],'R1203':[58,40],
  'R1204':[54,53],'R1205':[54,55.5],'R1206':[58,37],'R1207':[62,40]})
 for r in movable:
  if parts[r]['sheet']=='14_lcd_fpc' and r.startswith(('R','C')):fps[r].SetOrientationDegrees(90)
 placed=[]
 old_areas=[]
 for z in b.Zones():
  if z.GetDoNotAllowFootprints() and z.GetLayerSet().Contains(k.F_Cu):
   q=z.GetBoundingBox();old_areas.append((z.GetZoneName(),[k.ToMM(v)-50 for v in [q.GetX(),q.GetY(),q.GetRight(),q.GetBottom()]]))
 def reasons(r):
  q=box(fps[r]);bad=[]
  if q[0]<.6 or q[1]<.6 or q[2]>144.4 or q[3]>84.4:bad.append('edge')
  for name,v in list(AREAS.items())+old_areas:
   if overlap(q,v,0):bad.append(name)
  for name,v in placed:
   if overlap(q,v):bad.append(name)
  return bad
 for r in sorted(set(fps)-movable-set(active)):
  assert not any(overlap(box(fps[r]),a,0) for a in AREAS.values()),(r,'in escape/latch area')
  placed.append((r,box(fps[r])))
 for r in active:
  assert not reasons(r),(r,reasons(r));placed.append((r,box(fps[r])))
 def region(r):
  if r in core:return (49,1,90,35) if r not in ['R106','R110'] else (49,12,55.4,35)
  if parts[r]['sheet']=='14_lcd_fpc':return (50.5,36,63.1,59.5)
  if r=='R1301':return (79.7,1,90,8.3)
  if r in ['C312','D311']:return (89,19,115,35.3)
  if r in ['R402','R403','R404']:return (103.5,35.5,120,54.5)
  return (86,48.4,105,58.2)
 for r in sorted(movable,key=lambda r:(r not in core,-((box(fps[r])[2]-box(fps[r])[0])*(box(fps[r])[3]-box(fps[r])[1])),r)):
  x1,y1,x2,y2=region(r);ax,ay=anchors[r];angle=fps[r].GetOrientationDegrees()
  angles=[angle,angle+90] if r.startswith(('R','C')) else [angle]
  def valid():
   q=box(fps[r]);return q[0]>=x1 and q[1]>=y1 and q[2]<=x2 and q[3]<=y2 and not reasons(r)
  move(r,ax,ay)
  if not valid():
   sites=sorted(((i/4,j/4) for i in range(math.ceil(x1*4),math.floor(x2*4)+1) for j in range(math.ceil(y1*4),math.floor(y2*4)+1)),key=lambda p:(p[0]-ax)**2+(p[1]-ay)**2)
   found=False
   for x,y in sites:
    for a in angles:
     move(r,x,y,a)
     if valid():found=True;break
    if found:break
   assert found,('No site',r,region(r))
  placed.append((r,box(fps[r])))
 for name,(x1,y1,x2,y2) in AREAS.items():
  z=k.ZONE(b);z.SetIsRuleArea(True);z.SetZoneName(name);z.SetLayer(k.F_Cu)
  z.SetDoNotAllowFootprints(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowTracks(False);z.SetDoNotAllowVias(False);z.SetDoNotAllowZoneFills(False)
  pts=k.VECTOR_VECTOR2I()
  for x,y in [(x1,y1),(x2,y1),(x2,y2),(x1,y2)]:pts.push_back(vec(x,y))
  z.AddPolygon(pts);b.Add(z)
 for g in b.GetDrawings():
  if isinstance(g,k.PCB_TEXT) and g.GetText()=='LCD':g.SetPosition(vec(74.5,50.8))
 b.GetTitleBlock().SetRevision('R0N MIPI PLACEMENT')
 shutil.copytree(baseline,out,ignore=shutil.ignore_patterns('.history','*.lck','*.kicad_prl','~*','fp-info-cache'))
 k.SaveBoard(str(out/'display_carrier.kicad_pcb'),b)
 shutil.copyfile(baseline/'display_carrier.kicad_pro',out/'display_carrier.kicad_pro')
 for p in intent['parts']:
  if p['ref'] in fps:
   f=fps[p['ref']];p.update(pcb=xy(f),side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
 (out/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 report=prior
 report.pop('collision_relocations',None)
 report.update(revision='N',baseline_commit='7f842ff',screen_gap_mm=3,
  status='UNROUTED_MIPI_PLACEMENT_TRIAL_REQUIRED',fpc_positions_mm={r:[*xy(fps[r]),fps[r].GetOrientationDegrees()] for r in ['J1200','J1210','J1220']},
  component_clear_areas_mm=AREAS,positions={r:state(f) for r,f in sorted(fps.items())},
  moves=[dict(ref=r,before=before[r],after=state(f)) for r,f in sorted(fps.items()) if state(f)!=before[r]])
 (out/'SCREEN_BACK_LAYOUT.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'moves':len(report['moves']),'moved_refs':[m['ref'] for m in report['moves']]},indent=2))
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--baseline',type=Path,required=True);a.add_argument('--output',type=Path,required=True)
 v=a.parse_args();stage(v.baseline,v.output)
