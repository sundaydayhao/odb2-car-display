"""Review gate for current KiCad artifacts; --release fails until board is complete."""
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
# Confirm the only omitted part is the mechanically unresolved connector.
parity=drc['schematic_parity']
assert len(parity)==1 and parity[0]['type']=='missing_footprint',parity
assert 'J1' in json.dumps(parity),parity
design=json.loads((root/'design.json').read_text(encoding='utf-8'))
soc=next(p for p in design['parts'] if p['ref']=='U5')
pins=(root/'../../firmware/obd_ble_node/main/board_pins.h').resolve().read_text()
# GPIO -> QFN pad map is independent of the generated net assignments.
gpio_to_pad={0:'4',1:'5',3:'8',4:'9',5:'10',6:'12',9:'15'}
mapping={'BATTERY_ADC':'VBAT_ADC','CAN_TX':'CAN_TX','CAN_RX':'CAN_RX','TX_INHIBIT':'TX_INHIBIT','CAN_STANDBY':'CAN_STB','LED':'LED','BOOT':'BOOT'}
for key,net in mapping.items():
    gpio=int(re.search(r'#define NODE_PIN_'+key+r'\s+(\d+)',pins).group(1))
    assert soc['nets'][gpio_to_pad[gpio]]==net,(key,net)
result={'erc_violations':len(e),'pcb_placement_violations':len(drc['violations']),
    'unconnected_items':len(drc['unconnected_items']),'schematic_parity_issues':len(parity),
    'firmware_pinmap':'PASS','fabrication_ready':False,
    'blocker':'Actual J1 OBD solder-tail drawing and case clearances are required before final placement/routing.'}
(out/'verification.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
if args.release:
    print('RELEASE BLOCKED: J1 unresolved, board unrouted.',file=sys.stderr);sys.exit(2)
