"""Validate the native single-board design with the installed KiCad Python.

Example on Windows:
  & 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_hardware.py
Reports go to build/display-singleboard-verification, never into the CAD project.
"""
from pathlib import Path
import argparse,collections,csv,hashlib,heapq,json,math,re,subprocess,sys,xml.etree.ElementTree as ET
import pcbnew as k

repo=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--project',type=Path,default=repo/'display_board/hardware/display-singleboard')
ap.add_argument('--output',type=Path,default=repo/'build/display-singleboard-verification')
ap.add_argument('--cli',type=Path,default=Path(sys.executable).with_name('kicad-cli.exe' if sys.platform=='win32' else 'kicad-cli'))
a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
name='display_singleboard';sch=a.project/(name+'.kicad_sch');pcb=a.project/(name+'.kicad_pcb')
enclosure_path=a.project/'enclosure.json'
enclosure=json.loads(enclosure_path.read_text(encoding='utf-8')) if enclosure_path.exists() else {}
a7=enclosure.get('revision','').startswith('A7')
removed_otg_refs={'J18','D21','D22','R220','R221','R234','U9','C72','C73','C74','C75','D15','R145','R151','R152','TP28','R187'}
removed_otg_nets={'USB_DM','USB_DP','USB_DM_A','USB_DP_A','VBUS_EN','VBUS_OUT','N18248775','GND_EARTH'}
def run(*args):
    p=subprocess.run([str(a.cli),*map(str,args)],text=True,encoding='utf-8',errors='replace',capture_output=True)
    if p.returncode:raise RuntimeError(p.stdout+'\n'+p.stderr)
    return p.stdout.strip()
version=run('version')
run('sch','erc','--format','json','--output',a.output/'erc.json',sch)
run('pcb','drc','--refill-zones','--format','json','--output',a.output/'drc.json',pcb)
run('sch','export','netlist','--format','kicadxml','--output',a.output/'netlist.xml',sch)
erc=json.loads((a.output/'erc.json').read_text(encoding='utf-8'));drc=json.loads((a.output/'drc.json').read_text(encoding='utf-8'))
root=ET.parse(a.output/'netlist.xml').getroot()
# The high-level loader rebuilds connectivity and may propagate pad nets onto
# touching orphan tracks/vias. Compare with the raw plugin load so that this
# cannot turn an accidental intersection into an apparently clean check.
raw_board=k.PCB_IO_MGR.Load(k.PCB_IO_MGR.KICAD_SEXP,str(pcb))
raw_copper={t.m_Uuid.AsString():t.GetNetname() for t in raw_board.GetTracks()}
b=k.LoadBoard(str(pcb))
expected={};actual={};errors=[];refs=[]
for path in [pcb,*a.project.glob('*.kicad_sch')]:
    ids=collections.Counter(re.findall(r'\(uuid\s+"([0-9a-fA-F-]{36})"\)',path.read_text(encoding='utf-8-sig')))
    repeated=[uid for uid,count in ids.items() if count>1]
    if repeated:errors.append(['duplicate object UUIDs',path.name,repeated])
for kind,items in [('copper',raw_board.GetTracks()),('footprint',raw_board.GetFootprints())]:
    ids=collections.Counter(item.m_Uuid.AsString() for item in items)
    repeated=[uid for uid,count in ids.items() if count>1]
    if repeated:errors.append(['duplicate '+kind+' UUIDs',repeated])
copper_net_changes=[{'uuid':t.m_Uuid.AsString(),'file_net':raw_copper.get(t.m_Uuid.AsString()),'loaded_net':t.GetNetname()}
                    for t in b.GetTracks() if raw_copper.get(t.m_Uuid.AsString())!=t.GetNetname()]
if copper_net_changes:errors.append(['native loading changed copper net identities',copper_net_changes])
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
# These parts originally shared the Ethernet sheet but are essential to Wi-Fi / SD.
required.update({('Q8','1'):'SD_PWRN',('Q8','2'):'ESP_3V3',('Q8','3'):'SD1_VDD',
                 ('R229','1'):'GND',('R229','2'):'SD_PWRN',('R231','2'):'SD_PWRN',
                 ('J21','4'):'SD1_VDD',('C1','1'):'ESP_3V3',('C1','2'):'GND'})
# A5: HS host plus independent native USB Serial/JTAG, side power control.
required.update({('J18','1'):'VBUS_OUT',('J18','2'):'USB_DM_A',('J18','3'):'USB_DP_A',('J18','4'):'GND',
                 ('D21','1'):'USB_DP_A',('D21','2'):'GND',('D22','1'):'USB_DM_A',('D22','2'):'GND',
                 ('U9','1'):'VBUS_OUT',('U9','4'):'VBUS_EN',('U9','5'):'VCC_5V',
                 ('J22','1'):'VIN_5V',('J22','2'):'GND',('SW3','1'):'INPUT_5V',('SW3','2'):'SW_PWR_EN',('SW3','3'):'GND',
                 ('U401','1'):'INPUT_5V',('U401','2'):'INPUT_5V',('U401','3'):'SW_PWR_EN',('U401','4'):'INPUT_5V',
                 ('U401','5'):'GND',('U401','6'):'PWR_RISE_CT',('U401','7'):'VCC_5V',('U401','8'):'VCC_5V',('U401','9'):'GND',
                 ('J19','3'):'USB_DBG_5V',('J19','7'):'USB1P1_N',('J19','8'):'USB1P1_P',
                 ('J19','9'):'USB1P1_N',('J19','10'):'USB1P1_P',
                 ('R104','1'):'GPIO24',('R104','2'):'USB1P1_N',('R105','1'):'GPIO25',('R105','2'):'USB1P1_P',
                 ('Q4','2'):'INPUT_5V',('Q4','3'):'USB_DBG_5V',('Q7','2'):'INPUT_5V',('Q7','3'):'VIN_5V',
                 ('R234','1'):'USB_DP',('R234','2'):'GND',('U4','52'):'GPIO24',('U4','53'):'GPIO25',
                 ('R209','1'):'GPIO36_BOOTMODE',('R209','2'):'ESP_3V3',
                 ('R204','1'):'GPIO36',('R204','2'):'GPIO36_BOOTMODE'})
# Keep the P4X core supply connection and compensation while removing the
# external USB host branch; pin 54 is not a spare on this silicon revision.
required.update({('U4','54'):'N18792940',('R232','1'):'N18792940',('R232','2'):'ESP_VDD_HP',
                 ('C118','1'):'N18792940',('C118','2'):'GND',
                 ('R128','1'):'ESP_VDD_HP',('R128','2'):'FB_DCDC',
                 ('C53','1'):'ESP_VDD_HP',('C53','2'):'FB_DCDC'})
# 1S battery charger and 5 V boost power path.  Keep these pin/net
# assertions here so a future Update PCB from Schematic cannot silently
# disconnect the battery socket, charger, boost converter, or USB power OR.
required.update({
    ('J23','1'):'BAT_PACK',('J23','2'):'GND',('J23','3'):'BAT_NTC',
    ('F1','1'):'BAT_PACK',('F1','2'):'BAT_RAW',
    ('U402','1'):'CHG_SW',('U402','2'):'CHG_STAT',('U402','3'):'CHG_CV',
    ('U402','4'):'CHG_CC',('U402','5'):'BAT_RAW',('U402','6'):'BAT_NTC',
    ('U402','7'):'USB_DBG_5V',('U402','8'):'GND',('U402','9'):'GND',
    ('L5','1'):'CHG_SW',('L5','2'):'BAT_RAW',
    ('C420','1'):'BAT_RAW',('C420','2'):'GND',('R421','1'):'CHG_CC',('R421','2'):'GND',
    ('R422','1'):'CHG_CV',('R422','2'):'GND',('R423','1'):'BAT_NTC',('R423','2'):'USB_DBG_5V',
    ('R424','1'):'CHG_STAT',('R424','2'):'GND',
    ('U403','1'):'BAT_RAW',('U403','2'):'BAT_RAW',('U403','3'):'BOOST_FSW',
    ('U403','4'):'BOOST_SW',('U403','5'):'BOOST_SW',('U403','6'):'BOOST_SW',('U403','7'):'BOOST_SW',
    ('U403','8'):'BOOST_BOOT',('U403','9'):'BAT_RAW',('U403','10'):'BOOST_SS',
    ('U403','11'):'GND',('U403','12'):'GND',('U403','13'):'GND',
    ('U403','14'):'BAT_5V',('U403','15'):'BAT_5V',('U403','16'):'BAT_5V',
    ('U403','17'):'BOOST_FB',('U403','18'):'BOOST_COMP',('U403','19'):'BOOST_ILIM',
    ('U403','20'):'GND',('U403','21'):'GND',
    ('L6','1'):'BOOST_SW',('L6','2'):'BAT_5V',
    ('C430','1'):'BAT_RAW',('C430','2'):'GND',('C431','1'):'BAT_5V',('C431','2'):'GND',
    ('C432','1'):'BOOST_BOOT',('C432','2'):'BOOST_SW',('C433','1'):'BOOST_COMP',('C433','2'):'GND',
    ('C434','1'):'BOOST_SS',('C434','2'):'GND',('C435','1'):'BAT_RAW',('C435','2'):'GND',
    ('R430','1'):'BOOST_FSW',('R430','2'):'BOOST_SW',('R431','1'):'BOOST_ILIM',('R431','2'):'GND',
    ('R432','1'):'BAT_5V',('R432','2'):'BOOST_FB',('R433','1'):'BOOST_FB',('R433','2'):'GND',
    ('Q10','1'):'POWER_OR_GATE',('Q10','2'):'BAT_5V',('Q10','3'):'INPUT_5V',
    ('Q11','1'):'POWER_OR_GATE',('Q11','2'):'USB_DBG_5V',('Q11','3'):'INPUT_5V'})
for number,net in enumerate(['GND','VIN_5V','ESP_3V3','GPIO37','GPIO38','ESP_EN','GPIO35_BOOTMODE'],401):
    required[(f'TP{number}','1')]=net
if a7:
    required={key:value for key,value in required.items() if key[0] not in removed_otg_refs}
    required.update({('U4','49'):None,('U4','50'):None})
    for ref in sorted(removed_otg_refs & set(refs)):
        errors.append(['removed USB OTG component remains',ref])
    for net in sorted(removed_otg_nets & set(value for value in expected.values() if value)):
        errors.append(['removed USB OTG net remains in schematic',net])
    stale=[t.m_Uuid.AsString() for t in raw_board.GetTracks() if t.GetNetname() in removed_otg_nets]
    stale += [z.m_Uuid.AsString() for z in raw_board.Zones() if z.GetNetname() in removed_otg_nets]
    if stale:errors.append(['removed USB OTG copper remains',stale])
for key,value in required.items():
    if expected.get(key)!=value:errors.append(['integrated interface changed',key,expected.get(key),value])
for ref in ['J5','J301','J303','J305','J306','J307']:
    if ref in refs:errors.append(['obsolete inter-board / alternative connector',ref])
for ref in ['J17','U8','Y3','R87','R89','R91','R92','R114','R115','R117','R132','R133','R135']:
    if ref in refs:errors.append(['obsolete Ethernet component',ref])
if (a.project/'ethernet.kicad_sch').exists():errors.append(['obsolete Ethernet schematic sheet'])
for ref in ['J15','J20','R113','R131','Q2','Q3',
            'R178','R179','R180','R182','R185','R186',
            'D13','D14','D16','D20','D23','D24','R176','R177','C76']:
    if ref in refs:errors.append(['obsolete USB branch component',ref])
fpmap={f.GetReference():f for f in b.GetFootprints()}
enclosure_path=a.project/'enclosure.json'
enclosure=json.loads(enclosure_path.read_text(encoding='utf-8')) if enclosure_path.exists() else {}
if enclosure.get('revision','').startswith('A7'):
    # The mechanical redesign must retain the analog mic bias and active-low
    # recovery buttons.  A connector footprint swap is not a pin-number swap.
    enclosure_pins={('J7','1'):'MICP',('J7','2'):'N18300564',
                    ('R66','2'):'GND',
                    ('J16','1'):'N18423897',('J16','2'):'N18423915',
                    ('SW1','1'):'GND',('SW1','2'):'ESP_EN',('SW1','MP'):'GND',
                    ('SW2','1'):'GND',('SW2','2'):'GPIO35_BOOTMODE',('SW2','MP'):'GND'}
    for key,value in enclosure_pins.items():
        if expected.get(key)!=value:errors.append(['enclosure interface pin changed',key,expected.get(key),value])
    for ref in ('SW1','SW2'):
        if ref not in fpmap or fpmap[ref].GetValue()!='SKRTLBE010':
            errors.append(['side-actuated recovery switch required',ref])
    if 'J7' not in fpmap or fpmap['J7'].GetValue()!='SM02B-GHS-TB(LF)(SN)':
        errors.append(['wired microphone locking connector required','J7'])
    for ref in ('J1','J2'):
        if ref in fpmap and fpmap[ref].GetPosition().y <= (b.GetBoardEdgesBoundingBox().GetTop()+b.GetBoardEdgesBoundingBox().GetBottom())/2:
            errors.append(['internal header must be in lower half',ref])
    if 'U1' in fpmap and abs((fpmap['U1'].GetOrientationDegrees()+180)%360-180)>.01:
        errors.append(['C6 antenna must face upper edge','U1'])
    if 'J9' in fpmap:
        box9=fpmap['J9'].GetBoundingBox(False,False)
        if box9.GetWidth() <= box9.GetHeight():errors.append(['camera FPC must be horizontal','J9'])
    for ref in ('J302','J308'):
        if ref in fpmap and fpmap[ref].GetLayer()!=k.F_Cu:
            errors.append(['panel FPC must be accessible through rear service cover',ref])
    holes={ref for ref in refs if re.fullmatch(r'H\d+',ref)}
    if holes!={'H1','H2','H3','H4'}:errors.append(['four-corner mounting reference set',sorted(holes)])
for ref in (() if a7 else ('D21','D22')):
    fp=fpmap.get(ref)
    if fp is None or fp.IsDNP() or fp.GetValue()!='TPD1E05U06DPYR' or str(fp.GetFPID().GetLibItemName())!='TPD1E05U06_DPY0002A_TI':
        errors.append(['HS USB requires fitted TI ESD with matching DPY footprint',ref])
    elif len({f.m_Uuid.AsString() for f in fp.GetFields()})!=len(list(fp.GetFields())):
        errors.append(['duplicate footprint field UUID',ref])
if 'R209' in fpmap and (fpmap['R209'].IsDNP() or fpmap['R209'].GetValue() not in ['10K','10k','10kR']):
    errors.append(['GPIO36 download strap requires fitted 10k pull-up'])
lcd=fpmap['J302'];box=b.GetBoardEdgesBoundingBox()
profile_path=a.project/'panel-profile.json'
panel_profile=json.loads(profile_path.read_text(encoding='utf-8')) if profile_path.exists() else None
if panel_profile:
    # The selected front-view left touch tail is on the rear-view right.
    # Compare with the declared CAD pose; this does not qualify the FPC contacts.
    for key in ('lcd_connector','touch_connector'):
        spec=panel_profile[key];fp=fpmap[spec['reference']]
        side=k.B_Cu if spec['board_side']=='B' else k.F_Cu
        if fp.GetLayer()!=side:errors.append(['panel connector side mismatch',spec['reference']])
        pose=spec.get('cad_pose')
        if pose:
            if math.dist([k.ToMM(fp.GetPosition().x),k.ToMM(fp.GetPosition().y)],pose['position_mm'])>.01:
                errors.append(['panel connector position mismatch',spec['reference']])
            angle=(fp.GetOrientationDegrees()-pose['angle_deg']+180)%360-180
            if abs(angle)>.01:errors.append(['panel connector orientation mismatch',spec['reference']])
else:
    if lcd.GetLayer()!=k.B_Cu or abs(lcd.GetPosition().x-(box.GetLeft()+box.GetRight())/2)>k.FromMM(.1):
        errors.append(['LCD must face screen and be centred across board width'])
    if fpmap['J308'].GetPosition().x>box.GetLeft()+k.FromMM(10):errors.append(['touch connector left-edge access lost'])

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

# Geometric MIPI path length: connected copper, fitted series parts, and vias.
# This is not an impedance or propagation-delay simulation and excludes the FPC.
# Explicit CSI series bridges retain their fitted BOM value. They are geometric
# connections, not a claim that the resistor is zero ohms or a delay simulation.
csi_series_suffix={'R78':'DATA0N','R81':'DATA0P','R88':'DATA1N',
                   'R90':'DATA1P','R86':'CLKN','R82':'CLKP'}
csi_series_components=[]
graph=collections.defaultdict(list)
def edge(p,q,length):
    graph[p].append((q,length));graph[q].append((p,length))
def node(pt,layer,net):return (layer,pt.x,pt.y,net)
thickness=k.ToMM(b.GetDesignSettings().GetBoardThickness())
for track in b.GetTracks():
    if isinstance(track,k.PCB_VIA):
        # A plated through-via can join signals on the power layer as well.
        # Until the stackup is qualified, charge one nominal board thickness
        # for each transition; this is a conservative length proxy, not delay.
        layers=[layer for layer in (k.F_Cu,k.In1_Cu,k.In2_Cu,k.B_Cu) if track.IsOnLayer(layer)]
        for i,layer in enumerate(layers):
            for other in layers[i+1:]:
                edge(node(track.GetPosition(),layer,track.GetNetCode()),node(track.GetPosition(),other,track.GetNetCode()),thickness)
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
    csi_bridge=False
    if ref in csi_series_suffix:
        suffix=csi_series_suffix[ref]
        csi_bridge=all(num in pads for num in ('1','2')) and {
            pads['1'].GetNetname(),pads['2'].GetNetname()}=={'CSI_'+suffix,'CSI_A_'+suffix}
        if not csi_bridge:errors.append(['CSI series bridge net mismatch',ref,suffix])
        csi_series_components.append({'reference':ref,'actual_value':fp.GetValue(),
                                      'populated':not fp.IsDNP(),'pin_net_pair_valid':csi_bridge})
    if ref.startswith('R') and not fp.IsDNP() and (fp.GetValue() in ['0','0R'] or csi_bridge):
        p,q=pads['1'].GetPosition(),pads['2'].GetPosition()
        edge((ref,'1'),(ref,'2'),math.hypot(p.x-q.x,p.y-q.y)/1e6)
def routed_length(start,end):
    dist={start:0};heap=[(0,str(start),start)]
    while heap:
        cost,_,cur=heapq.heappop(heap)
        if cur==end:return round(cost,4)
        if cost>dist[cur]:continue
        for nxt,delta in graph[cur]:
            nd=cost+delta
            if nd<dist.get(nxt,float('inf')):
                dist[nxt]=nd;heapq.heappush(heap,(nd,str(nxt),nxt))
    return None

lengths={}
for sig,pin in [('DATA1N','21'),('DATA1P','20'),('CLKN','18'),('CLKP','17'),('DATA0N','24'),('DATA0P','23')]:
    start=next(key for key,value in expected.items() if key[0]=='U4' and value=='DSI_'+sig)
    length=routed_length(start,('J302',pin))
    if length is None:errors.append(['No MIPI copper path',sig])
    else:lengths[sig]=length
delta={pair:round(abs(lengths[pair+'N']-lengths[pair+'P']),4) for pair in ['DATA0','DATA1','CLK'] if pair+'N' in lengths and pair+'P' in lengths}
means=[(lengths[pair+'N']+lengths[pair+'P'])/2 for pair in delta]
spread=round(max(means)-min(means),4) if len(means)==3 else None
if any(value>.254 for value in delta.values()) or (spread is not None and spread>.762):errors.append(['MIPI geometric length limits exceeded',delta,spread])
mipi={'method':'U4 to J302 pad centres; fitted 0R bridges and board-thickness through vias. Excludes screen FPC and silicon delay.',
      'length_mm':lengths,'within_pair_mm':delta,'pair_mean_spread_mm':spread,'within_pair_limit_mm':.254,'pair_mean_spread_limit_mm':.762}
(a.output/'mipi-lengths.json').write_text(json.dumps(mipi,indent=2),encoding='utf-8')

# J9 moved with the display/touch interface layout. Check its complete camera
# paths against the same Espressif MIPI pair/skew guidance, not branch totals.
csi=None
if panel_profile:
    camera_lengths={}
    for sig,pin in [('DATA0N','2'),('DATA0P','3'),('DATA1N','5'),('DATA1P','6'),('CLKN','8'),('CLKP','9')]:
        start=next(key for key,value in expected.items() if key[0]=='U4' and value=='CSI_'+sig)
        length=routed_length(start,('J9',pin))
        if length is None:errors.append(['No CSI copper path',sig])
        else:camera_lengths[sig]=length
    camera_delta={pair:round(abs(camera_lengths[pair+'N']-camera_lengths[pair+'P']),4)
                  for pair in ['DATA0','DATA1','CLK'] if pair+'N' in camera_lengths and pair+'P' in camera_lengths}
    camera_means=[(camera_lengths[pair+'N']+camera_lengths[pair+'P'])/2 for pair in camera_delta]
    camera_spread=round(max(camera_means)-min(camera_means),4) if len(camera_means)==3 else None
    if any(value>.254 for value in camera_delta.values()) or (camera_spread is not None and camera_spread>.762):
        errors.append(['CSI geometric length limits exceeded',camera_delta,camera_spread])
    csi={'method':'U4 to J9 pad centres, explicitly mapped fitted series-resistor bridges and board-thickness F/B transitions; excludes camera FPC, component and silicon delay.',
         'series_components':sorted(csi_series_components,key=lambda item:item['reference']),
         'length_mm':camera_lengths,'within_pair_mm':camera_delta,'pair_mean_spread_mm':camera_spread,
         'within_pair_limit_mm':.254,'pair_mean_spread_limit_mm':.762,
         'source':'https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/pcb-layout-design-esp32p4.html#mipi'}
    (a.output/'csi-lengths.json').write_text(json.dumps(csi,indent=2),encoding='utf-8')

usb={'port_present':False,'removed_references':sorted(removed_otg_refs),'retained_debug_connector':'J19'} if a7 else None
erc_items=[v for sheet in erc.get('sheets',[]) for v in sheet.get('violations',[])]
summary={'engine':version,'physical_components':len(refs),'unique_numbered_pads':len(actual),'native_netlist_nodes':len(expected),
         'erc_violations':len(erc_items),'drc_violations':len(drc['violations']),'unconnected_items':len(drc['unconnected_items']),
         'duplicate_refs':duplicates,'connectivity_errors':errors,'drc_ignored_checks':drc.get('ignored_checks',[]),
         'mipi':mipi,'pcb_sha256':hashlib.sha256(pcb.read_bytes()).hexdigest(),'schematic_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(a.project.glob('*.kicad_sch'))}}
if panel_profile:summary.update(panel_qualification=panel_profile['qualification'],csi=csi)
if usb is not None:summary['usb_hs']=usb
(a.output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({key:val for key,val in summary.items() if key not in ['drc_ignored_checks','schematic_sha256']},ensure_ascii=False,indent=2))
sys.exit(1 if erc_items or drc['violations'] or drc['unconnected_items'] or errors or duplicates else 0)
