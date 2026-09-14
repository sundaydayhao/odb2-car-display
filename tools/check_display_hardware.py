"""Validate the native single-board design with the installed KiCad Python.

Example on Windows:
  & 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_hardware.py
Reports go to build/display-singleboard-verification, never into the CAD project.
"""
from pathlib import Path
import argparse,collections,csv,hashlib,heapq,json,math,subprocess,sys,xml.etree.ElementTree as ET
import pcbnew as k

repo=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--project',type=Path,default=repo/'hardware/display-singleboard')
ap.add_argument('--output',type=Path,default=repo/'build/display-singleboard-verification')
ap.add_argument('--cli',type=Path,default=Path(sys.executable).with_name('kicad-cli.exe' if sys.platform=='win32' else 'kicad-cli'))
a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
name='display_singleboard';sch=a.project/(name+'.kicad_sch');pcb=a.project/(name+'.kicad_pcb')
def run(*args):
    p=subprocess.run([str(a.cli),*map(str,args)],text=True,encoding='utf-8',errors='replace',capture_output=True)
    if p.returncode:raise RuntimeError(p.stdout+'\n'+p.stderr)
    return p.stdout.strip()
version=run('version')
run('sch','erc','--format','json','--output',a.output/'erc.json',sch)
run('pcb','drc','--format','json','--output',a.output/'drc.json',pcb)
run('sch','export','netlist','--format','kicadxml','--output',a.output/'netlist.xml',sch)
erc=json.loads((a.output/'erc.json').read_text(encoding='utf-8'));drc=json.loads((a.output/'drc.json').read_text(encoding='utf-8'))
root=ET.parse(a.output/'netlist.xml').getroot();b=k.LoadBoard(str(pcb))
expected={};actual={};errors=[];refs=[]
def norm(net):return None if not net or net.startswith('unconnected-(') else net
for net in root.findall('./nets/net'):
    for node in net.findall('node'):
        if node.get('ref').startswith('#'):continue
        key=(node.get('ref'),node.get('pin'))
        if key in expected:errors.append(['duplicate native netlist node',key])
        expected[key]=norm(net.get('name'))
stamps={c.get('ref'):c.findtext('tstamps') for c in root.findall('./components/comp')}
for fp in b.GetFootprints():
    ref=fp.GetReference();refs.append(ref)
    stamp=stamps.get(ref)
    if not stamp or not fp.GetPath().AsString().endswith('/'+stamp):errors.append(['schematic path mismatch',ref,fp.GetPath().AsString(),stamp])
    for pad in fp.Pads():
        num=pad.GetNumber()
        if not num:continue
        key=(ref,num);value=norm(pad.GetNetname())
        if key in actual and actual[key]!=value:errors.append(['duplicate pad has conflicting net',key])
        actual[key]=value
for key in sorted(set(expected)|set(actual)):
    if key not in expected or key not in actual or expected.get(key)!=actual.get(key):errors.append(['SCH/PCB mismatch',key,expected.get(key,'MISSING'),actual.get(key,'MISSING')])
duplicates=[ref for ref,n in collections.Counter(refs).items() if n!=1]
required={('J302','17'):'DSI_A_CLKP',('J302','18'):'DSI_A_CLKN',('J302','20'):'DSI_A_DATA1P',('J302','21'):'DSI_A_DATA1N',('J302','23'):'DSI_A_DATA0P',('J302','24'):'DSI_A_DATA0N',
          ('J302','27'):'PANEL_RESET_LCD',('J302','28'):'PANEL_LCD_VDD',('U304','2'):'CNN_GPIO27',('U304','4'):'PANEL_RESET_LCD',('U304','5'):'ESP_3V3',
          ('J308','1'):'CNN_GPIO23',('J308','2'):'ESP_3V3',('J308','3'):'GND',('J308','4'):'CNN_GPIO22',('J308','5'):'N18412242',('J308','6'):'N18412230',('R341','1'):'CNN_GPIO26',('R341','2'):'GND'}
for key,value in required.items():
    if expected.get(key)!=value:errors.append(['integrated interface changed',key,expected.get(key),value])
for ref in ['J5','J301','J303','J305','J306','J307']:
    if ref in refs:errors.append(['obsolete inter-board / alternative connector',ref])

# Assembly data must name the same exact local footprint variants as the CAD.
with (a.project/'assembly.csv').open(encoding='utf-8-sig',newline='') as f:
    rows=list(csv.DictReader(f))
assembly={row['Reference']:row for row in rows}
if len(assembly)!=len(rows) or set(assembly)!=set(refs):
    errors.append(['assembly reference set mismatch'])
for fp in b.GetFootprints():
    row=assembly.get(fp.GetReference(),{})
    for key,value in [('Value',fp.GetValue()),('Footprint',str(fp.GetFPID().GetLibItemName())),('Populate','NO' if fp.IsDNP() else 'YES')]:
        if row.get(key)!=value:errors.append(['assembly mismatch',fp.GetReference(),key,row.get(key),value])

# Geometric MIPI path length: connected copper, fitted 0-ohm bridges, and vias.
# This is not an impedance or propagation-delay simulation and excludes the FPC.
graph=collections.defaultdict(list)
def edge(p,q,length):
    graph[p].append((q,length));graph[q].append((p,length))
def node(pt,layer,net):return (layer,pt.x,pt.y,net)
thickness=k.ToMM(b.GetDesignSettings().GetBoardThickness())
for track in b.GetTracks():
    if isinstance(track,k.PCB_VIA):
        edge(node(track.GetPosition(),k.F_Cu,track.GetNetCode()),node(track.GetPosition(),k.B_Cu,track.GetNetCode()),thickness)
    else:
        edge(node(track.GetStart(),track.GetLayer(),track.GetNetCode()),node(track.GetEnd(),track.GetLayer(),track.GetNetCode()),k.ToMM(track.GetLength()))
by_net=collections.defaultdict(list)
for n in list(graph):by_net[n[-1]].append(n)
for fp in b.GetFootprints():
    ref=fp.GetReference();pads={p.GetNumber():p for p in fp.Pads()}
    for num,pad in pads.items():
        pos=pad.GetPosition()
        for n in by_net[pad.GetNetCode()]:
            if pad.GetLayerSet().Contains(n[0]) and pad.HitTest(k.VECTOR2I(n[1],n[2])):
                edge((ref,num),n,math.hypot(n[1]-pos.x,n[2]-pos.y)/1e6)
    if ref.startswith('R') and not fp.IsDNP() and fp.GetValue() in ['0','0R']:
        p,q=pads['1'].GetPosition(),pads['2'].GetPosition()
        edge((ref,'1'),(ref,'2'),math.hypot(p.x-q.x,p.y-q.y)/1e6)
lengths={}
for sig,pin in [('DATA1N','21'),('DATA1P','20'),('CLKN','18'),('CLKP','17'),('DATA0N','24'),('DATA0P','23')]:
    start=next(key for key,value in expected.items() if key[0]=='U4' and value=='DSI_'+sig)
    end=('J302',pin);dist={start:0};heap=[(0,str(start),start)]
    while heap:
        cost,_,cur=heapq.heappop(heap)
        if cur==end:lengths[sig]=round(cost,4);break
        if cost>dist[cur]:continue
        for nxt,delta in graph[cur]:
            nd=cost+delta
            if nd<dist.get(nxt,float('inf')):
                dist[nxt]=nd;heapq.heappush(heap,(nd,str(nxt),nxt))
    else:raise RuntimeError('No MIPI copper path for '+sig)
delta={pair:round(abs(lengths[pair+'N']-lengths[pair+'P']),4) for pair in ['DATA0','DATA1','CLK']}
means=[(lengths[pair+'N']+lengths[pair+'P'])/2 for pair in delta]
spread=round(max(means)-min(means),4)
if max(delta.values())>.254 or spread>.762:errors.append(['MIPI geometric length limits exceeded',delta,spread])
mipi={'method':'U4 to J302 pad centres; fitted 0R bridges and board-thickness through vias. Excludes screen FPC and silicon delay.',
      'length_mm':lengths,'within_pair_mm':delta,'pair_mean_spread_mm':spread,'within_pair_limit_mm':.254,'pair_mean_spread_limit_mm':.762}
(a.output/'mipi-lengths.json').write_text(json.dumps(mipi,indent=2),encoding='utf-8')
erc_items=[v for sheet in erc.get('sheets',[]) for v in sheet.get('violations',[])]
summary={'engine':version,'physical_components':len(refs),'unique_numbered_pads':len(actual),'native_netlist_nodes':len(expected),
         'erc_violations':len(erc_items),'drc_violations':len(drc['violations']),'unconnected_items':len(drc['unconnected_items']),
         'duplicate_refs':duplicates,'connectivity_errors':errors,'drc_ignored_checks':drc.get('ignored_checks',[]),
         'mipi':mipi,'pcb_sha256':hashlib.sha256(pcb.read_bytes()).hexdigest(),'schematic_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(a.project.glob('*.kicad_sch'))}}
(a.output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({key:val for key,val in summary.items() if key not in ['drc_ignored_checks','schematic_sha256']},ensure_ascii=False,indent=2))
sys.exit(1 if erc_items or drc['violations'] or drc['unconnected_items'] or errors or duplicates else 0)
