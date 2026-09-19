"""Independent draft audits. Passing these checks is NOT fabrication approval.

Run with KiCad Python after pcb.py and native ERC/DRC exports. The audit compares
the native schematic XML against actual PCB pads, including no-connect nets.
"""
from pathlib import Path
import collections, hashlib, itertools, json, math, xml.etree.ElementTree as ET
import pcbnew as k

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/carrier-design'
OUT=BUILD/'project'
intent=json.loads((OUT/'design-intent.json').read_text(encoding='utf-8'))
parts=intent['parts'];refs=[p['ref'] for p in parts]
assert len(refs)==len(set(refs)) and all('?' not in r for r in refs)
tree=ET.parse(BUILD/'netlist.xml').getroot()
sch={(z.get('ref'),z.get('pin')):n.get('name') for n in tree.findall('./nets/net') for z in n.findall('node')}
board=k.LoadBoard(str(OUT/'display_carrier.kicad_pcb'))
fps={f.GetReference():f for f in board.GetFootprints()}
expected={p['ref'] for p in parts if p['footprint']}
assert expected==set(fps)-{'H1','H2','H3','H4'}
for p in parts:
    if not p['footprint']:continue
    fp=fps[p['ref']]
    assert {pad.GetNumber() for pad in fp.Pads() if pad.GetNumber()}==set(p['nets'])
    for pad in fp.Pads():
        pin=pad.GetNumber()
        if not pin:continue
        n=pad.GetNetname();assert n==sch[(p['ref'],pin)],(p['ref'],pin,n,sch.get((p['ref'],pin)))
        target=p['nets'][pin]
        assert (n==target) if target is not None else n.startswith('unconnected-')
    assert fp.GetPath().AsString().startswith('/'+intent['root_uuid']+'/')

erc=json.loads((BUILD/'erc-final.json').read_text(encoding='utf-8'))
er=[v for s in erc['sheets'] for v in s['violations']]
assert all(v['severity']=='warning' and v['type']=='isolated_pin_label' for v in er)
pending=sorted(v['items'][0]['description'].split("'")[1] for v in er)
hold_nets=sorted(['LCD_RESET','LCD_STBYB','TOUCH_INT','TOUCH_RESET','DSI_D1_P','DSI_D1_N',
 'DSI_CLK_N','DSI_CLK_P','DSI_D0_P','DSI_D0_N','CSI_D0_N','CSI_D0_P','CSI_CLK_P','CSI_CLK_N',
 'CSI_D1_N','CSI_D1_P','CAMERA_RESET','BACKLIGHT_PWM','LCD_VDD_EN','LCD_BIAS_EN','CAM_POWER_EN','AMP_EN'])
assert pending==hold_nets,(pending,hold_nets)
drc=json.loads((BUILD/'drc-final.json').read_text(encoding='utf-8'))
assert not drc['schematic_parity'],drc['schematic_parity']
assert not drc['violations'],drc['violations']
assert len(board.GetTracks())==0 and len(drc['unconnected_items'])>0

# Compute TS thresholds with resistor and NTC R25/B tolerances. Uses the IC's
# specified threshold extremes, not the temperature labels for its default NTC.
def threshold(q,r1,r2,r25,beta):
    parallel=q*r1/(1-q)
    rntc=1/(1/parallel-1/r2)
    return 1/(1/298.15+math.log(rntc/r25)/beta)-273.15
temps={}
for name,limits in [('cold_inhibit',(0.7275,0.7375)),('cold_derate',(0.6775,0.6875)),
                    ('warm_derate',(0.4425,0.4525)),('hot_inhibit',(0.33875,0.34875))]:
    results=[threshold(q,9090*(1+a),187000*(1+b),10000*(1+c),3950*(1+dt))
             for q,a,b,c,dt in itertools.product(limits,(-.001,.001),(-.001,.001),(-.01,.01),(-.01,.01))]
    temps[name]=[round(min(results),3),round(max(results),3)]
assert temps['cold_inhibit'][0]>0 and temps['hot_inhibit'][1]<45
legacy=68.1;high=1/(1/68.1+1/33.2)
current={'legacy_tps2553_max_mA':round(22980/(legacy*.99)**.94,2),
         'qualified_typec_tps2553_max_mA':round(22980/(high*.99)**.94,2)}

report={'status':'DRAFT_ONLY_NOT_FAB_APPROVED','kicad':k.GetBuildVersion(),
 'schematic_symbols':len(parts),'circuit_footprints':len(expected),'mounting_holes':4,
 'schematic_sheets_including_root':len(intent['sheets'])+1,
 'geometric_violations':sum(v['violations'] for v in intent['geometric_verifier'].values()),
 'duplicate_references':0,'pcb_pad_net_mismatches':0,'erc_errors':0,'erc_warnings':len(er),
 'pending_interface_nets':pending,'pcb_physical_violations':len(drc['violations']),
 'schematic_parity_violations':len(drc['schematic_parity']),
 'pcb_unconnected_items':len(drc['unconnected_items']),'tracks':len(board.GetTracks()),
 'zones':len(board.Zones()),'ts_temperature_threshold_ranges_C':temps,
 'ts_open_ratio':round(187000/(187000+9090),6),'usb_current_limit_calculation':current,
 'notes':['0 physical DRC violations applies to an UNROUTED placement only.',
          'Native default ignored checks are retained, not counted as passing.',
          'NTC sensor thermal contact, aging, and IC comparator dynamics are not included in this calculation.'],
 'native_ignored_drc_checks':drc.get('ignored_checks',[]),
 'hash_normalization':'CRLF converted to LF before SHA-256',
 'design_file_sha256':{p.name:hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest() for p in sorted(OUT.glob('*.kicad_*')) if p.suffix in ['.kicad_sch','.kicad_pcb','.kicad_pro']}}
(BUILD/'verification-summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ['design_file_sha256','native_ignored_drc_checks','pending_interface_nets']},indent=2,ensure_ascii=False))
