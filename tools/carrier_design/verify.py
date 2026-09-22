"""Independent draft audits. Passing these checks is NOT fabrication approval.

Run with KiCad Python after pcb.py and native ERC/DRC exports. The audit compares
the native schematic XML against actual PCB pads, including no-connect nets.
"""
from pathlib import Path
import argparse, collections, hashlib, itertools, json, math, xml.etree.ElementTree as ET
import pcbnew as k

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/carrier-design'
OUT=BUILD/'project'
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--project',type=Path,default=OUT)
ap.add_argument('--evidence',type=Path,default=BUILD)
args=ap.parse_args();OUT=args.project;BUILD=args.evidence
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
    if (OUT/'MECHANICAL_LAYOUT.json').exists():
        assert fp.GetLayerName()==p['side']+'.Cu'
        assert all(abs(a-b)<.01 for a,b in zip([k.ToMM(fp.GetPosition().x)-50,k.ToMM(fp.GetPosition().y)-50],p['pcb']))

erc=json.loads((BUILD/'erc-final.json').read_text(encoding='utf-8'))
er=[v for s in erc['sheets'] for v in s['violations']]
assert all(v['severity']=='warning' and v['type']=='isolated_pin_label' for v in er)
pending=sorted(v['items'][0]['description'].split("'")[1] for v in er)
hold_nets=sorted(['CAMERA_RESET','BACKLIGHT_PWM','LCD_BIAS_EN','CAM_POWER_EN',
 'LCD_VGH','LCD_VGL','LCD_AVDD_9V8','LCD_VCOM','CAM_MCLK_1V8','CAM_DOVDD_1V8',
 'CAM_RESET_1V8','CAM_SDA_1V8','CAM_SCL_1V8'])
if 'U820' not in fps:hold_nets=sorted(hold_nets+['AMP_EN'])
assert pending==hold_nets,(pending,hold_nets)
drc=json.loads((BUILD/'drc-final.json').read_text(encoding='utf-8'))
assert not drc['schematic_parity'],drc['schematic_parity']
assert not drc['violations'],drc['violations']
assert len(board.GetTracks())==0 and len(drc['unconnected_items'])>0
connectivity=board.GetConnectivity();connectivity.Build(board)
unrouted=connectivity.GetUnconnectedCount(False)
assert unrouted>=len(drc['unconnected_items'])
assert {r for r,f in fps.items() if f.IsDNP()}=={'R105','R107','R304','C1040','C1041','C1042','C1043'}
fpc_expected=[('J1200',(70.5,57.5),0),('J1210',(96,67),0),('J1220',(76.5,7),180)]
if (OUT/'MECHANICAL_LAYOUT.json').exists():
    # These expected coordinates are derived independently from the measured
    # tip datum: y = tip - PCB top offset + insertion - footprint mouth offset.
    fpc_expected=[('J1200',(70.5,46.1-2.5+2.1-3.525),0),
                  ('J1210',(36,7.5-2.5+2.1-3.525),0),('J1220',(76.5,7),180)]
for ref,xy,angle in fpc_expected:
    fp=fps[ref]
    assert fp.GetLayer()==k.F_Cu and not fp.IsDNP()
    assert abs(fp.GetOrientationDegrees()-angle)<.01
    assert abs(k.ToMM(fp.GetPosition().x)-50-xy[0])<.01
    assert abs(k.ToMM(fp.GetPosition().y)-50-xy[1])<.01
# LCD PDF p6 and OSPTEK camera PDF p3: independently guard differential polarity.
for ref,mapping in {
 'J1200':{'17':'DSI_CLK_P','18':'DSI_CLK_N','20':'DSI_D1_P','21':'DSI_D1_N','23':'DSI_D0_P','24':'DSI_D0_N'},
 'J1220':{'3':'CSI_D1_N','4':'CSI_D1_P','6':'CSI_CLK_N','7':'CSI_CLK_P','9':'CSI_D0_N','10':'CSI_D0_P'}
}.items():
    for pin,net in mapping.items():assert sch[(ref,pin)]==net
if 'U1300' in fps:
    for ref,mapping in {
      'U1300':{'1':'+3V3','2':'+3V3','3':'GND','4':'CABIN_I2C_SCL','6':'CABIN_I2C_SDA','7':'GND'},
      'U1301':{'1':'CABIN_I2C_SDA','2':'CABIN_I2C_SCL','3':'+3V3','4':'GND'},
      'R1300':{'1':'SYSTEM_I2C_SDA','2':'CABIN_I2C_SDA'},
      'R1301':{'1':'SYSTEM_I2C_SCL','2':'CABIN_I2C_SCL'}
    }.items():
        for pin,net in mapping.items():assert sch[(ref,pin)]==net,(ref,pin)
    assert sch[('U1300','5')].startswith('unconnected-')
    for net,expected_nodes in {
       'CABIN_I2C_SDA':{('U1300','6'),('U1301','1'),('R1300','2')},
       'CABIN_I2C_SCL':{('U1300','4'),('U1301','2'),('R1301','2')}
    }.items():assert {rp for rp,n in sch.items() if n==net}==expected_nodes
    assert all(not fps[r].IsDNP() for r in ['U1300','U1301','R1300','R1301','C1300','C1301','C1302'])
sim_checks=None
if 'SIM8066' in fps['J1000'].GetValue():
    for pin,net in {'1':'SIM0_VCC','2':'SIM_RST_SOCKET','3':'SIM_CLK_SOCKET','5':'GND',
                    '7':'SIM_DATA_SOCKET','SH':'GND','SW':'SIM0_DET'}.items():assert sch[('J1000',pin)]==net
    assert sch[('J1000','6')].startswith('unconnected-')
    assert sch[('R1033','1')]=='MODEM_1V8' and sch[('R1033','2')]=='SIM0_DET'
    assert sch[('R1034','1')]=='SIM0_VCC' and sch[('R1034','2')]=='SIM_DATA_SOCKET'
    assert fps['R1033'].GetValue()==fps['R1034'].GetValue()=='10k'
    assert fps['C1030'].GetValue()=='1u 10V'
    f=fps['J1000'];pads=list(f.Pads());holes=[p for p in pads if not p.GetNumber()]
    assert len(holes)==2 and all(p.GetAttribute()==k.PAD_ATTRIB_NPTH and abs(k.ToMM(p.GetDrillSize().x)-.75)<.001 for p in holes)
    assert len([p for p in pads if p.GetNumber()=='SH'])==4
    assert f.GetLayer()==k.B_Cu and abs(f.GetOrientationDegrees()%360-270)<.001
    assert len(list(f.Zones()))==1
    # In this bottom-side rotation, shell-front tabs must be nearer +X than signal tails.
    assert max(p.GetPosition().x for p in pads if p.GetNumber()=='SH')>max(p.GetPosition().x for p in pads if p.GetNumber()=='1')
    sim_checks={'socket':'GCT SIM8066-6-1-14-01-A','mechanism':'push-push','entry':'right edge in screen-front projection',
        'detect_polarity':'HIGH=card present; normally closed CD-to-GND when empty',
        'ML307A_default_polarity_match':True,'NPTH_peg_holes_mm':[.75,.75],
        'four_shell_tabs_grounded':True,'component_side_contact_copper_keepout':True,
        'manufacturing_and_enclosure_mating':'NOT_VALIDATED'}
inductor_checks=None
if 'FEXU0640' in fps['L400'].GetValue():
    fp=fps['L400']
    assert str(fp.GetFPID().GetLibItemName())=='L_Cjiang_FEXU0640_6.2x6.0mm'
    assert fp.GetLayer()==k.B_Cu and not fp.IsDNP()
    assert abs(fp.GetOrientationDegrees())<.001
    for pad in fp.Pads():
        assert all(abs(a-b)<.001 for a,b in zip([k.ToMM(pad.GetSize().x),k.ToMM(pad.GetSize().y)],[2.25,6.3]))
        assert abs(abs(k.ToMM(pad.GetPosition().x-fp.GetPosition().x))-2.125)<.001
        assert pad.GetPosition().y==fp.GetPosition().y
    assert sch[('L400','1')]=='SYS_SWITCHED' and sch[('L400','2')]=='BOOST_SW'
    assert fps['R402'].GetValue()=='324k 1%' and fps['R403'].GetValue()=='100k 1%'
    vo=1.204*(1+178/56.2);low_l=2.2e-6*.8*.7;cases=[]
    for vin in [2.7,3.0,3.6,4.2,4.4]:
        freq=(vin/vo)/(324e3*23e-12/4+89e-9)
        avg=vo*4/(.85*vin);ripple=vin*(1-vin/vo)/(freq*low_l)
        rms=math.sqrt(avg**2+ripple**2/12)
        cases.append(dict(vin_V=vin,fsw_nominal_kHz=round(freq/1000,3),average_A=round(avg,3),
                          peak_A=round(avg+ripple/2,3),rms_A=round(rms,3),copper_loss_25C_W=round(rms*rms*.007,3)))
    inductor_checks=dict(part='FEXU0640S2R2MGS',LCSC='C54140025',body_mm=[6.2,6.0,4.0],
        land_pattern_mm=dict(overall=6.5,gap=2.0,height=6.3),DCR_max_ohm=.007,
        manufacturer_current_columns=dict(Irms_typ_A=16,Irms_column_labeled_Max_A=14,Isat_typ_A=18,Isat_column_labeled_Max_A=15),
        current_rating_conditions='25C ambient; Irms at 40C rise; Isat at 30 percent L drop; Max column is not a guaranteed minimum',
        estimate_assumptions=dict(output_A=4,efficiency=.85,L_H=low_l,frequency_tolerance_included=False),
        estimates=cases,qualification='NOT_RUN: hot Isat, loop, AC/core losses, transients and enclosure; 2.7V/4A lacks minimum current-limit margin')

audio_checks=None
if 'U820' in fps:
    # Independent pin map from S31-Korvo-1 p4 / ES8389 p6 and RDA5807M p14.
    for ref,mapping in {
        'U800':{'1':'CODEC_AD0','2':'SYSTEM_I2C_SDA','3':'SYSTEM_I2C_SCL','4':'CODEC_TDMIN',
            '5':'AUDIO_3V3','6':'AUDIO_3V3','7':'GND','8':'CODEC_BCLK','9':'CODEC_MCLK',
            '10':'CODEC_LRCK','11':'CODEC_DOUT','12':'DAC_R_P','13':'DAC_R_N','14':'DAC_VREF',
            '15':'DAC_L_N','16':'DAC_L_P','17':'AUDIO_3V3','18':'GND','19':'CODEC_VMID',
            '20':'ADC_VREF','21':'MIC_R_N','22':'MIC_R_P','23':'MIC_L_N','24':'MIC_L_P','25':'GND'},
        'U815':{'1':'GND','2':'FM_ANT_IN','3':'GND','4':'FM_SCL','5':'FM_SDA',
                 '6':'FM_RCLK','7':'FM_3V0','8':'GND','9':'FM_R','10':'FM_L'},
        'R805':{'1':'CODEC_AD0','2':'GND'},'R806':{'1':'AUDIO_3V3','2':'CODEC_DOUT'},
        'R803':{'1':'I2S_DOUT','2':'CODEC_TDMIN'},'R800':{'1':'I2S_MCLK','2':'CODEC_MCLK'},
        'U810':{'1':'AUDIO_3V3','2':'GND','3':'FM_POWER_EN','5':'FM_3V0'},
        'U822':{'1':'GND','2':'PA_BUCK_SW','3':'+5V','4':'+5V','5':'PA_BUCK_FB'},
    }.items():
        for pin,net in mapping.items():assert sch[(ref,pin)]==net,(ref,pin)
    for i,ch in enumerate(['L','R']):
        for ref,mapping in {
            'U'+str(802+i):{'1':'MIC_'+ch+'_3V3','2':'GND','3':'GND','4':'MIC_'+ch+'_OUT'},
            'U'+str(820+i):{'1':'AMP_EN','2':'PA_'+ch+'_BYPASS','3':'PA_'+ch+'_INP','4':'PA_'+ch+'_INN',
                 '5':'SPK_'+ch+'_N','6':'PA_4V8','7':'GND','8':'SPK_'+ch+'_P'},
            'J'+str(820+i):{'1':'SPK_'+ch+'_P','2':'SPK_'+ch+'_N'},
            'C'+str(841+4*i):{'1':'MIC_'+ch+'_OUT','2':'MIC_'+ch+'_P'},
            'C'+str(842+4*i):{'1':'GND','2':'MIC_'+ch+'_N'},
        }.items():
            for pin,net in mapping.items():assert sch[(ref,pin)]==net,(ref,pin)
        for j,pol in enumerate(['P','N']):
            ref='U'+str(811+2*i+j)
            mapping={'1':'FM_'+ch+'_BIASED' if pol=='P' else 'AUDIO_MID',
                     '3':'DAC_'+ch+'_'+pol+'_AC','4':'AMP_'+ch+'_'+pol+'_AC',
                     '2':'GND','5':'AUDIO_3V3','6':'AUDIO_SELECT_LOCAL'}
            for pin,net in mapping.items():assert sch[(ref,pin)]==net,(ref,pin)
    assert 'FM_SUM' not in sch.values() and 'J800' not in fps
    assert all(not fps[r].IsDNP() for r in ['U800','U802','U803','U815','U820','U821','J820','J821'])
    assert fps['U802'].GetLayer()==fps['U803'].GetLayer()==k.B_Cu
    assert abs(k.ToMM(fps['U803'].GetPosition().x-fps['U802'].GetPosition().x)-60)<.001
    assert fps['U803'].GetPosition().y==fps['U802'].GetPosition().y
    # TI full-temperature FB range is +/-2%; divider resistors are 0.1%.
    pa_range=[vref*(1+698000*(1+rtol)/(100000*(1-rtol))) for vref,rtol in [(.588,-.001),(.612,.001)]]
    assert max(pa_range)<5.0 and min(pa_range)>3.0
    audio_checks={'ES8389_7bit_address':'0x12','RDA5807M_7bit_address':'0x10',
        'stereo_mic_dac_and_BTL_pinmaps':'PASS','S31_TDM_clock_data_pin_swap':'PASS',
        'fm_separate_left_right':'PASS','onboard_mics':2,'speaker_connectors':2,
        'microphone_center_spacing_mm':60,'PA_nominal_V':4.788,
        'PA_static_tolerance_range_V':[round(x,4) for x in pa_range],
        'amplifier_nominal_gain_V_V':2.4,'firmware_and_physical_audio_validation':'NOT_RUN'}
# A passive connector with duplicate power pins does not necessarily cause an
# isolated-label warning. Report these open supply boundaries independently.
power_holds=['LCD_LEDA','LCD_LEDK','LCD_AVDD_9V8','LCD_VGH','LCD_VGL','LCD_VCOM',
             'CAM_AVDD_2V8','CAM_DOVDD_1V8']
for net in power_holds:
    members={ref for (ref,pin),name in sch.items() if name==net}
    assert members<= {'J1200','J1220'} and members,(net,members)

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
 'pcb_unconnected_items':unrouted,
 'drc_reported_unconnected_items':len(drc['unconnected_items']),
 'unconnected_count_source':'native CONNECTIVITY_DATA.GetUnconnectedCount(False); CLI report list is truncated',
 'open_peripheral_power_boundaries':power_holds,
 'fitted_physical_components':sum(not f.IsDNP() and not r.startswith(('TP','H')) for r,f in fps.items()),
 'dnp_exceptions':['R105','R107','R304','C1040','C1041','C1042','C1043'],
 'tracks':len(board.GetTracks()),
 'zones':sum(not z.GetIsRuleArea() for z in board.Zones()),
 'rule_areas':sum(z.GetIsRuleArea() for z in board.Zones()),'ts_temperature_threshold_ranges_C':temps,
 'ts_open_ratio':round(187000/(187000+9090),6),'usb_current_limit_calculation':current,
 'notes':['0 physical DRC violations applies to an UNROUTED placement only.',
          'Native default ignored checks are retained, not counted as passing.',
          'NTC sensor thermal contact, aging, and IC comparator dynamics are not included in this calculation.'],
 'native_ignored_drc_checks':drc.get('ignored_checks',[]),
 'hash_normalization':'CRLF converted to LF before SHA-256',
 'design_file_sha256':{p.name:hashlib.sha256(p.read_bytes().replace(b'\r\n',b'\n')).hexdigest() for p in sorted(OUT.glob('*.kicad_*')) if p.suffix in ['.kicad_sch','.kicad_pcb','.kicad_pro']}}
if audio_checks:report['stereo_audio']=audio_checks
if sim_checks:report['push_push_sim']=sim_checks
if inductor_checks:report['compact_inductor']=inductor_checks
(BUILD/'verification-summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ['design_file_sha256','native_ignored_drc_checks','pending_interface_nets']},indent=2,ensure_ascii=False))
