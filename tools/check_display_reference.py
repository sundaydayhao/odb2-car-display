"""Check filled GND coverage beneath the display board's MIPI routes.

Run with KiCad's Python. This checks geometry, not impedance or signal integrity.
The input board is never written. A scratch copy is refilled by native KiCad
with its matching project rules before sampling.
"""
from pathlib import Path
import argparse, collections, hashlib, json, math, shutil, subprocess, sys
import pcbnew as k

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--project', type=Path, default=Path(__file__).resolve().parents[1]/'display_board/hardware/display-singleboard')
ap.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'build/display-reference-verification')
a = ap.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
pcb = a.project/'display_singleboard.kicad_pcb'
b = k.PCB_IO_MGR.Load(k.PCB_IO_MGR.KICAD_SEXP, str(pcb))
intent = {t.m_Uuid.AsString(): t.GetNetname() for t in b.GetTracks()}
scratch=a.output/'refill-project'
scratch.mkdir(exist_ok=True)
for suffix in ('.kicad_pcb','.kicad_pro','.kicad_dru'):
    src=pcb.with_suffix(suffix)
    if src.exists():shutil.copy2(src,scratch/src.name)
if (a.project/'fp-lib-table').exists():shutil.copy2(a.project/'fp-lib-table',scratch/'fp-lib-table')
for library in a.project.glob('*.pretty'):
    shutil.copytree(library,scratch/library.name,dirs_exist_ok=True)
cli=Path(sys.executable).with_name('kicad-cli.exe' if sys.platform=='win32' else 'kicad-cli')
result=subprocess.run([str(cli),'pcb','drc','--refill-zones','--save-board','--format','json',
                       '--output',str(scratch/'drc.json'),str(scratch/pcb.name)],capture_output=True,text=True)
if result.returncode:raise RuntimeError(result.stdout+'\n'+result.stderr)
b=k.PCB_IO_MGR.Load(k.PCB_IO_MGR.KICAD_SEXP,str(scratch/pcb.name))
changed = [{'uuid':t.m_Uuid.AsString(), 'file_net':intent.get(t.m_Uuid.AsString()), 'loaded_net':t.GetNetname()}
           for t in b.GetTracks() if intent.get(t.m_Uuid.AsString()) != t.GetNetname()]
if changed:
    (a.output/'reference.json').write_text(json.dumps({'errors':['copper net identity changed'], 'changed':changed}, indent=2))
    raise SystemExit('Copper net identity changed; fix physical intersections before checking planes.')

def mm(p):
    return (k.ToMM(p.x), k.ToMM(p.y))

def highspeed(net):
    return net.startswith(('DSI_', 'CSI_'))

signal_vias = [t for t in b.GetTracks() if isinstance(t,k.PCB_VIA) and highspeed(t.GetNetname())]
ground_vias = [mm(t.GetPosition()) for t in b.GetTracks() if isinstance(t,k.PCB_VIA) and t.GetNetname()=='GND']
signal_positions = [mm(t.GetPosition()) for t in signal_vias]
by_net = collections.defaultdict(list)
for t in signal_vias:
    by_net[t.GetNetname()].append(mm(t.GetPosition()))
rows = []
errors = []
for prefix in ('DSI_', 'CSI_'):
    for signal_layer, reference_layer in ((k.F_Cu,k.In1_Cu),(k.B_Cu,k.In2_Cu)):
        ground = [z.GetFilledPolysList(reference_layer) for z in b.Zones()
                  if z.IsOnLayer(reference_layer) and not z.GetIsRuleArea() and z.GetNetname()=='GND']
        counts = collections.Counter()
        gaps = []
        for track in b.GetTracks():
            if isinstance(track,k.PCB_VIA) or track.GetLayer()!=signal_layer or not track.GetNetname().startswith(prefix):
                continue
            start,end = mm(track.GetStart()),mm(track.GetEnd())
            length = math.dist(start,end)
            if length < 1e-8:
                continue
            dx,dy = (end[0]-start[0])/length,(end[1]-start[1])/length
            steps = max(1,math.ceil(length/.05))
            half = k.ToMM(track.GetWidth())/2+.5
            missed = []
            for i in range(steps+1):
                x,y = start[0]+(end[0]-start[0])*i/steps,start[1]+(end[1]-start[1])*i/steps
                if any(math.dist((x,y),v)<.5 for v in by_net[track.GetNetname()]):
                    counts['own_via_transition_samples_excluded'] += 1
                    continue
                cover = []
                for offset in (-half,-half*2/3,-half/3,0,half/3,half*2/3,half):
                    point = (x-offset*dy,y+offset*dx)
                    if any(math.dist(point,v)<.4 for v in signal_positions):
                        counts['local_signal_via_antipad_samples_excluded'] += 1
                        cover.append(True)
                    else:
                        pos = k.VECTOR2I(k.FromMM(point[0]),k.FromMM(point[1]))
                        cover.append(any(poly.Contains(pos) for poly in ground))
                counts['center_samples'] += 1
                if all(cover):
                    counts['full_corridor_GND'] += 1
                elif cover[3]:
                    counts['center_GND_edge_gap'] += 1
                else:
                    counts['center_gap'] += 1
                if not cover[3] or signal_layer==k.B_Cu and not all(cover):
                    missed.append({'position_mm':[round(x,4),round(y,4)],'center_GND':cover[3]})
            if missed:
                gaps.append({'uuid':track.m_Uuid.AsString(),'net':track.GetNetname(),'uncovered':missed})
        row = {'interface':prefix.rstrip('_'),'signal_layer':b.GetLayerName(signal_layer),
               'reference_layer':b.GetLayerName(reference_layer),'counts':dict(counts),'uncovered_segments':gaps}
        rows.append(row)
        if gaps:
            errors.append([row['interface'],row['signal_layer'],'uncovered reference',len(gaps)])

returns = []
for via in signal_vias:
    point=mm(via.GetPosition())
    nearest=min((math.dist(point,g) for g in ground_vias),default=None)
    returns.append({'uuid':via.m_Uuid.AsString(),'net':via.GetNetname(),'position_mm':point,
                    'nearest_ground_via_mm':None if nearest is None else round(nearest,4)})
    if nearest is None or nearest>2.0:
        errors.append(['signal transition needs nearby GND stitching',via.m_Uuid.AsString(),nearest])
report = {'pcb_sha256':hashlib.sha256(pcb.read_bytes()).hexdigest(),'sample_step_mm':.05,
          'requested_ground_beyond_trace_edge_mm':.5,'own_via_transition_exclusion_mm':.5,
          'signal_via_antipad_exclusion_mm':.4,'maximum_ground_return_distance_mm':2.0,
          'layers':rows,'via_returns':returns,'errors':errors,
          'limitations':['Geometric sampling does not qualify stackup, impedance, propagation delay, EMC or eye margins.',
                        'F/L2 must cover the trace centre; lateral gaps are reported. B/L3 requires the full requested corridor.',
                        'Local signal via antipads are explicitly excluded; their nearby return vias are checked separately.']}
(a.output/'reference.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'layers':[{key:value for key,value in row.items() if key!='uncovered_segments'} for row in rows],
                  'errors':errors},ensure_ascii=False,indent=2))
sys.exit(1 if errors else 0)
