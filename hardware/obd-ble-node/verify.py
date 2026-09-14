"""Electrical/layout gate; production release still requires measured hardware."""
import argparse,json,re,subprocess,sys
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--release',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parent;out=root/'build';out.mkdir(exist_ok=True)
def cli(*args):subprocess.run(['kicad-cli',*map(str,args)],check=True)
cli('sch','erc','--format','json','--severity-all','--output',out/'erc.json',root/'obd_ble_node.kicad_sch')
cli('pcb','drc','--format','json','--severity-all','--schematic-parity','--output',out/'drc-parity.json',root/'obd_ble_node.kicad_pcb')
erc=json.loads((out/'erc.json').read_text(encoding='utf-8'))
drc=json.loads((out/'drc-parity.json').read_text(encoding='utf-8'))
e=[v for sheet in erc['sheets'] for v in sheet['violations']]
assert not e,e
assert not drc['violations'],drc['violations']
# Every schematic part, including the physical J1, must be present and connected.
parity=drc['schematic_parity']
assert not parity,parity
assert not drc['unconnected_items'],drc['unconnected_items']
design=json.loads((root/'design.json').read_text(encoding='utf-8'))
soc=next(p for p in design['parts'] if p['ref']=='U5')
pins=(root/'../../firmware/obd_ble_node/main/board_pins.h').resolve().read_text()
# GPIO -> QFN pad map is independent of the generated net assignments.
gpio_to_pad={0:'4',1:'5',3:'8',4:'9',5:'10',6:'12',9:'15'}
mapping={'BATTERY_ADC':'VBAT_ADC','CAN_TX':'CAN_TX','CAN_RX':'CAN_RX','TX_INHIBIT':'TX_INHIBIT','CAN_STANDBY':'CAN_STB','LED':'LED','BOOT':'BOOT'}
for key,net in mapping.items():
    gpio=int(re.search(r'#define NODE_PIN_'+key+r'\s+(\d+)',pins).group(1))
    assert soc['nets'][gpio_to_pad[gpio]]==net,(key,net)
import pcbnew as k
board=k.LoadBoard(str(root/'obd_ble_node.kicad_pcb'))
footprints={f.GetReference():f for f in board.GetFootprints()}
assert set(footprints)=={p['ref'] for p in design['parts']}
j1={int(p.GetNumber()):p for p in footprints['J1'].Pads()}
assert set(j1)==set(range(1,17))
for n,p in j1.items():
    expected=(105.25+4*((n-1)%8),104 if n<=8 else 112.6)
    actual=(k.ToMM(p.GetPosition().x),k.ToMM(p.GetPosition().y))
    assert all(abs(a-b)<.00001 for a,b in zip(actual,expected)),(n,actual)
for n,net in {4:'GND',5:'GND',6:'CAN_H',14:'CAN_L',16:'VBAT'}.items():
    assert j1[n].GetNetname()=='/'+net,(n,j1[n].GetNetname())
# Native bounding boxes include the 0.05 mm drawing stroke; dimensions are
# measured from the closed Edge.Cuts centreline, as the fabricator routes it.
edges=[d for d in board.GetDrawings() if d.GetLayer()==k.Edge_Cuts]
assert len(edges)==4
points=[(k.ToMM(p.x),k.ToMM(p.y)) for e in edges for p in [e.GetStart(),e.GetEnd()]]
assert abs(max(x for x,y in points)-min(x for x,y in points)-38.5)<.001
assert abs(max(y for x,y in points)-min(y for x,y in points)-16.6)<.001
for p in set(points):assert points.count(p)==2,('open outline',p)
tracks=[t for t in board.GetTracks() if not isinstance(t,k.PCB_VIA)]
assert tracks,'No routed electronics'
assert not [t for t in tracks if t.GetLayer()==k.In1_Cu and t.GetNetname()!='/GND'],'Ground reference layer was used for signal routing'
assert any(z.GetLayer()==k.In1_Cu and z.GetNetname()=='/GND' for z in board.Zones())
result={'erc_violations':len(e),'pcb_placement_violations':len(drc['violations']),
    'unconnected_items':len(drc['unconnected_items']),'schematic_parity_issues':len(parity),
    'firmware_pinmap':'PASS','components':len(footprints),'obd_physical_pads':len(j1),
    'tracks':len(tracks),'electrical_layout_gate':'PASS','production_ready':False,
    'blocker':'Verify actual plug fit, antenna/RF, CAN hardware, power transients and parking current on assembled prototypes.'}
(out/'verification.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
if args.release:
    print('PRODUCTION RELEASE BLOCKED: physical fit and measured hardware qualification pending.',file=sys.stderr);sys.exit(2)
