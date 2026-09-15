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
pins=(root/'../../main/board_pins.h').resolve().read_text()
# GPIO -> QFN pad map is independent of the generated net assignments.
gpio_to_pad={0:'4',1:'5',3:'8',4:'9',5:'10',6:'12',9:'15'}
mapping={'BATTERY_ADC':'VBAT_ADC','CAN_TX':'CAN_TX','CAN_RX':'CAN_RX','TX_INHIBIT':'TX_INHIBIT','CAN_STANDBY':'CAN_STB','LED':'LED','BOOT':'BOOT'}
for key,net in mapping.items():
    gpio=int(re.search(r'#define NODE_PIN_'+key+r'\s+(\d+)',pins).group(1))
    assert soc['nets'][gpio_to_pad[gpio]]==net,(key,net)
import pcbnew as k
import math
board=k.LoadBoard(str(root/'obd_ble_node.kicad_pcb'))
footprints={f.GetReference():f for f in board.GetFootprints()}
assert set(footprints)=={p['ref'] for p in design['parts']}
sequence=['TP1','TP2','TP9','TP7','TP8','TP3','TP4','TP5','TP6']
expected_tp_nets=['GND','+3V3','+5V','USB_DM','USB_DP','EN','BOOT','U0RX','UART_TX']
for index,(ref,net) in enumerate(zip(sequence,expected_tp_nets)):
    fp=footprints[ref];pads=list(fp.Pads());assert len(pads)==1
    p=pads[0]
    assert fp.GetLayer()==k.B_Cu and p.GetNetname()=='/'+net
    assert abs(k.ToMM(p.GetPosition().x)-(115.2+index*2))<.00001 and k.ToMM(p.GetPosition().y)==115.0
    assert k.ToMM(p.GetSize().x)==k.ToMM(p.GetSize().y)==1.0
    assert p.IsOnLayer(k.B_Mask) and not p.IsOnLayer(k.B_Paste) and not p.IsOnLayer(k.F_Cu)
    # A drill inside a probe land can catch the needle; ordinary net DRC allows it.
    for via in (t for t in board.GetTracks() if isinstance(t,k.PCB_VIA)):
        gap=math.hypot(via.GetPosition().x-p.GetPosition().x,via.GetPosition().y-p.GetPosition().y)
        assert gap>=p.GetSize().x/2+via.GetDrillValue()/2+k.FromMM(.1),('Drill too close to test land',ref)
for ref,position,drill in [('H1',(102.0,115.0),(1.2,1.2)),('H2',(134.3,115.0),(1.6,1.2))]:
    fp=footprints[ref];pads=list(fp.Pads());assert len(pads)==1
    p=pads[0]
    assert p.GetAttribute()==k.PAD_ATTRIB_NPTH and not p.GetNetname()
    assert (k.ToMM(p.GetPosition().x),k.ToMM(p.GetPosition().y))==position
    assert (k.ToMM(p.GetDrillSize().x),k.ToMM(p.GetDrillSize().y))==drill
supports={z.GetZoneName():z for z in board.Zones() if z.GetIsRuleArea() and z.GetZoneName().startswith('SUPPORT_')}
assert set(supports)=={'SUPPORT_A','SUPPORT_B'}
assert all(z.GetDoNotAllowPads() and z.GetDoNotAllowVias() and z.IsOnLayer(k.F_Cu) for z in supports.values())
assert 'J2' not in footprints and 'AE1' in footprints,'External antenna connector must not return'
ant=footprints['AE1']
assert ant.GetValue()=='AANI-CH-0133-Q'
assert ant.GetFPID().GetLibItemName()=='AANI-CH-0133-Q_Corner'
for p in ant.Pads():
    if p.GetNumber()=='1':assert p.GetNetname()=='/ANT_FEED'
    elif p.GetNumber()=='2':assert p.GetNetname().startswith('unconnected-'), 'Radiator must float, never GND'
assert footprints['C23'].GetAttributes() & k.FP_DNP,'Antenna X1 should initially be DNP'
areas=[z for z in board.Zones() if z.GetIsRuleArea() and z.GetZoneName()=='ANTENNA_CLEARANCE']
assert len(areas)==1
area=areas[0];bb=area.GetBoundingBox()
assert k.ToMM(bb.GetWidth())>=3.25 and k.ToMM(bb.GetHeight())>=5.25
assert area.GetDoNotAllowZoneFills() and area.GetDoNotAllowVias()
for layer in [k.F_Cu,k.In1_Cu,k.In2_Cu,k.B_Cu]:assert area.IsOnLayer(layer)
assert {z.GetZoneName() for z in board.Zones() if z.GetIsRuleArea()} >= {'ANTENNA_NO_INNER_TRACKS','ANTENNA_TOP_TRACK_GUARD'}
# Conservative geometric guard also covers the small reserved feed corridor.
def touches_antenna(item):
    box=item.GetBoundingBox()
    return box.GetRight()>135250000 and box.GetLeft()<138500000 and box.GetBottom()>100000000 and box.GetTop()<105250000
for f in board.GetFootprints():
    if f.GetReference()!='AE1':assert not any(touches_antenna(p) for p in f.Pads()),('Foreign pad in antenna clearance',f.GetReference())
for t in board.GetTracks():
    if touches_antenna(t):assert not isinstance(t,k.PCB_VIA) and t.GetLayer()==k.F_Cu and t.GetNetname()=='/ANT_FEED','Foreign copper in antenna clearance'
for t in board.GetTracks():
    if t.GetNetname() in ['/RF_SOC','/RF50','/ANT_FEED']:
        assert not isinstance(t,k.PCB_VIA) and t.GetLayer()==k.F_Cu,'RF must remain on F.Cu without signal vias'
assert next(p for p in design['parts'] if p['ref']=='L3')['nets']=={'1':'RF50','2':'ANT_FEED'}
assert next(p for p in design['parts'] if p['ref']=='L4')['nets']=={'1':'RF50','2':'GND'}
j1={int(p.GetNumber()):p for p in footprints['J1'].Pads()}
assert set(j1)==set(range(1,17))
for n,p in j1.items():
    expected=(105.25+4*((n-1)%8),104 if n<=8 else 112.6)
    actual=(k.ToMM(p.GetPosition().x),k.ToMM(p.GetPosition().y))
    assert all(abs(a-b)<.00001 for a,b in zip(actual,expected)),(n,actual)
    assert p.GetAttribute()==k.PAD_ATTRIB_PTH and p.GetDrillShape()==k.PAD_DRILL_SHAPE_OBLONG
    assert (k.ToMM(p.GetSize().x),k.ToMM(p.GetSize().y))==(3.2,2.0)
    assert (k.ToMM(p.GetDrillSize().x),k.ToMM(p.GetDrillSize().y))==(2.4,1.2)
    assert p.IsOnLayer(k.F_Mask) and p.IsOnLayer(k.B_Mask)
    assert not p.IsOnLayer(k.F_Paste) and not p.IsOnLayer(k.B_Paste)
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
    'test_pad_row':'9 bottom pads, 2 mm pitch, +5 V sense included',
    'fixture_datums':'H1 round 1.2 NPTH; H2 horizontal 1.6 x 1.2 NPTH; 32.3 mm centre spacing',
    'connector_solder_lands':'16 plated slots, 0.4 mm nominal annular ring, front/back mask openings verified',
    'existing_connector_fit':'UNVERIFIED: supplier solder-tail/body dimensions unavailable',
    'tracks':len(tracks),'electrical_layout_gate':'PASS','production_ready':False,
    'antenna':'AANI-CH-0133-Q-T; all-layer clearance and separate pi match verified',
    'blocker':'Verify actual plug fit, antenna/RF, CAN hardware, power transients and parking current on assembled prototypes.'}
(out/'verification.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
if args.release:
    print('PRODUCTION RELEASE BLOCKED: physical fit and measured hardware qualification pending.',file=sys.stderr);sys.exit(2)
