"""Compare native KiCad netlist and routed PCB against the electrical intent."""
from pathlib import Path
import hashlib, json, sys, xml.etree.ElementTree as ET
import pcbnew as k
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch'; W=ROOT/'build/fm-mono-switch'
intent=json.loads((P/'design-intent.json').read_text())
expected={(ref,str(pin)):net for ref,a in intent['parts'].items() if a['footprint'] for pin,net in a['nets'].items()}
expected[('U3','4')]='unconnected-(U3-NC-Pad4)'
xml=ET.parse(W/'netlist-final.xml').getroot()
actual={}
for net in xml.find('nets'):
    for node in net.findall('node'):
        key=(node.attrib['ref'],node.attrib['pin'])
        assert key not in actual,('duplicate native node',key)
        actual[key]=net.attrib['name'].lstrip('/')
assert actual==expected,{'missing':set(expected.items())-set(actual.items()),'extra':set(actual.items())-set(expected.items())}
board=k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
pcb={};refs=[]
for fp in board.GetFootprints():
    ref=fp.GetReference();refs.append(ref)
    assert str(fp.GetFPID().GetLibNickname())+':'+str(fp.GetFPID().GetLibItemName())==intent['parts'][ref]['footprint'],ref
    for pad in fp.Pads():
        if not pad.GetNumber():continue
        key=(ref,pad.GetNumber());assert key not in pcb,key
        pcb[key]=pad.GetNetname().lstrip('/')
assert pcb==expected,{'PCB differences':set(pcb.items())^set(expected.items())}
assert len(refs)==len(set(refs))
assert set(refs)=={ref for ref,a in intent['parts'].items() if a['footprint']}
assert intent['uses_fm_module'] is False
assert intent['geometry']['violations']==0
for name in ['erc-final','drc-final']:
    report=json.loads((W/(name+'.json')).read_text(encoding='utf-8'))
    for field in ['violations','unconnected_items','schematic_parity']:
        assert not report.get(field,[]),(name,field,report.get(field))
result={'kicad':'10.0.5','board_mm':[40,36],'layers':2,'footprints':len(refs),
        'physical_pin_net_pairs':len(pcb),'netlist_matches_intent':True,'pcb_matches_intent':True,
        'erc_violations':0,'drc_violations':0,'unconnected_items':0,'schematic_parity_issues':0,
        'bench_tested':False,'sha256':{}}
for name in ['fm_mono_switch.kicad_sch','fm_mono_switch.kicad_pcb','fm_mono_switch.kicad_pro']:
    result['sha256'][name]=hashlib.sha256((P/name).read_bytes()).hexdigest()
(W/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
