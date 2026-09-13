"""Generate the review schematic and staged PCB using KiCad 10 + KiSkill.

The OBD mating standard does NOT define PCB solder-tail geometry. J1 now uses
an explicitly assumed straight-tail footprint integrated with the electronics.
Actual plug fit and vehicle qualification remain physical validation gates.
"""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import uuid
from functools import lru_cache

parser = argparse.ArgumentParser()
parser.add_argument('--engine', default=os.environ.get('KISKILL_PATH'))
parser.add_argument('--libraries', default=os.environ.get('KICAD_LIB_ROOT', r'C:\Program Files\KiCad\10.0\share\kicad'))
args = parser.parse_args()
if not args.engine:
    parser.error('Supply --engine path/to/KiSkill-main or KISKILL_PATH')
sys.path.insert(0, args.engine)
from kicad_lib import sexp, build, ops, geom
from kicad_lib.verify import Seg, Pin, verify
import pcbnew

ROOT = Path(__file__).resolve().parent
LIB = Path(args.libraries)
NAME = 'obd_ble_node'
NS = uuid.UUID('bedc3b8b-3a3d-47f1-ac34-aad9f24a8b8a')
def uid(name): return str(uuid.uuid5(NS, name))
def parse(s): return sexp.parse(s)
def tag(n): return sexp.tag_of(n)
def atoms(n): return sexp.atoms(n)
def find(n, t): return sexp.find(n, t)
def allof(n, t): return sexp.find_all(n, t)
def q(s): return sexp.QStr.of(str(s))
def num(s): return sexp.Sym(f'{s:g}')
def snap(x, y): return geom.snap(x), geom.snap(y)

@lru_cache(None)
def library(lib_id):
    group, name = lib_id.split(':')
    tree = sexp.load_file(str(LIB / 'symbols' / (group + '.kicad_sym')))
    by_name = {atoms(n)[0]: n for n in allof(tree, 'symbol')}
    def flatten(name):
        src = copy.deepcopy(by_name[name])
        ext = find(src, 'extends')
        if ext:
            base = flatten(atoms(ext)[0]); base_name = atoms(base)[0]
            for n in allof(base, 'symbol'):
                n[1] = q(atoms(n)[0].replace(base_name + '_', name + '_', 1))
            props = {atoms(p)[0]: p for p in allof(src, 'property')}
            base[:] = [n for n in base if not isinstance(n, list) or tag(n) != 'property' or atoms(n)[0] not in props]
            base.extend(props.values()); base[1] = q(name); src = base
        return src
    result = flatten(name); result[1] = q(lib_id)
    return result

custom = {}
def ic(name, left, right, width=15.24):
    """Pins listed top to bottom, tuple(number, name, electrical_type)."""
    count = max(len(left), len(right)); half = geom.snap((count + 1) * 1.905)
    body = f'(symbol "Node:{name}" (pin_names (offset 0.635)) (in_bom yes) (on_board yes) (property "Reference" "U" (at 0 {half+3.81} 0) (effects (font (size 1.27 1.27)))) (property "Value" "{name}" (at 0 {-half-3.81} 0) (effects (font (size 1.27 1.27)))) (symbol "{name}_0_1" (rectangle (start {-width} {half}) (end {width} {-half}) (stroke (width 0.254) (type default)) (fill (type background)))) (symbol "{name}_1_1" '
    for side, entries in [(-1,left),(1,right)]:
        for index, (number, label, kind) in enumerate(entries):
            yy = half - 3.81 * (index + 1)
            body += f'(pin {kind} line (at {side*(width+5.08):g} {yy:g} {0 if side<0 else 180}) (length 5.08) (name "{label}" (effects (font (size 1.016 1.016)))) (number "{number}" (effects (font (size 1.016 1.016))))) '
    result = parse(body + '))'); custom[name] = result; return result

C3 = ic('ESP32-C3FH4X', [
    ('1','LNA_IN','bidirectional'),('2','VDD3P3_1','power_in'),('3','VDD3P3_2','power_in'),
    ('11','VDD3P3_RTC','power_in'),('17','VDD3P3_CPU','power_in'),('31','VDDA_1','power_in'),('32','VDDA_2','power_in'),
    ('18','VDD_SPI','power_out'),('7','CHIP_EN','input'),('29','XTAL_N','passive'),('30','XTAL_P','passive'),
    ('33','EP_GND','power_in'),('19','NC19','no_connect'),('20','NC20','no_connect'),('21','NC21','no_connect'),
    ('22','NC22','no_connect'),('23','NC23','no_connect'),('24','NC24','no_connect')], [
    ('4','GPIO0 / ADC1_CH0','bidirectional'),('5','GPIO1 / CAN_TX','bidirectional'),('6','GPIO2 / STRAP','bidirectional'),
    ('8','GPIO3 / CAN_RX','bidirectional'),('9','GPIO4 / TX_INHIBIT','bidirectional'),('10','GPIO5 / CAN_STB','bidirectional'),
    ('12','GPIO6 / LED','bidirectional'),('13','GPIO7','bidirectional'),('14','GPIO8 / STRAP','bidirectional'),
    ('15','GPIO9 / BOOT','bidirectional'),('16','GPIO10','bidirectional'),('25','GPIO18 / USB_D-','bidirectional'),
    ('26','GPIO19 / USB_D+','bidirectional'),('27','GPIO20 / U0RXD','input'),('28','GPIO21 / U0TXD','output')], width=24.13)
BUCK = ic('LMR51610YDBVR', [('5','VIN','power_in'),('4','EN','input'),('2','GND','power_in')],
    [('1','CB','passive'),('6','SW','power_out'),('3','FB','input')])
CAN = ic('TCAN1044AVDRBRQ1', [('1','TXD','input'),('4','RXD','output'),('8','STB','input'),('5','VIO','power_in'),('3','VCC','power_in')],
    [('7','CANH','bidirectional'),('6','CANL','bidirectional'),('2','GND','power_in'),('9','EP','power_in')])
GATE = ic('SN74LVC1G32DCKR', [('1','A','input'),('2','B','input'),('3','GND','power_in')], [('4','Y','output'),('5','VCC','power_in')],width=10.16)
ESD = ic('PESD2CANFD24V-T', [('1','CANL','passive'),('2','CANH','passive')], [('3','GND','passive')],width=10.16)
OBD = ic('OBDII_LOGICAL_ONLY', [(str(n),f'OBD_{n}','passive') for n in range(1,9)],
    [(str(n),f'OBD_{n}','passive') for n in range(9,17)],width=10.16)

parts = []
def part(ref, value, symbol, footprint, nets, sch, pcb=None, rot=0, side='F', angle=0, mpn='', note='', dnp=False):
    symbol = library(symbol) if isinstance(symbol,str) else symbol
    parts.append(dict(ref=ref,value=value,lib_id=atoms(symbol)[0],symbol=symbol,footprint=footprint,
        nets={str(k):v for k,v in nets.items()},sch=snap(*sch),pcb=pcb,rot=rot,side=side,angle=angle,
        mpn=mpn or value,note=note,dnp=dnp))
RFP='Resistor_SMD:R_0402_1005Metric'
CFP='Capacitor_SMD:C_0402_1005Metric'
def r(ref,value,a,b,sch,pcb,**kw): part(ref,value,'Device:R',RFP,{1:a,2:b},sch,pcb,rot=90,**kw)
def c(ref,value,a,b,sch,pcb,footprint=CFP,**kw): part(ref,value,'Device:C',footprint,{1:a,2:b},sch,pcb,**kw)

# Standard pin functions, with the user-requested straight-tail trial geometry.
part('J1','OBD-II 16P / STRAIGHT TAIL ASSUMPTION',OBD,'Node:OBD_StraightTail_16P_4x8p6_Assumed',{4:'GND',5:'GND',6:'CAN_H',14:'CAN_L',16:'VBAT'},(57.15,73.66),(119.25,108.3),note='4.00 mm pitch / 8.60 mm rows; 2.40 x 1.20 mm plated slots are trial values. Verify actual solder tails and case.')
part('F1','500mA 63V','Device:Fuse','Fuse:Fuse_1206_3216Metric',{1:'VBAT',2:'VBAT_FUSED'},(133.35,45.72),(102.6,103),rot=90,mpn='0466.500NR',note='Validate fuse I2t / TVS coordination')
part('D1','SS110 100V','Device:D_Schottky','Diode_SMD:D_SMA',{1:'VIN_PROT',2:'VBAT_FUSED'},(185.42,45.72),(107.5,103),mpn='SS110',note='Series reverse-polarity protection')
part('D2','SMBJ18A','Device:D_TVS','Diode_SMD:D_SMB',{1:'VIN_PROT',2:'GND'},(137.16,78.74),(107.6,108),rot=90,mpn='SMBJ18A',note='12 V passenger cars; pulse qualification required')
c('C1','2.2u 50V','VIN_PROT','GND',(180.34,78.74),(112.1,102.2),footprint='Capacitor_SMD:C_0805_2012Metric',mpn='GRM21BR71H225KA12L')
c('C2','100n 50V','VIN_PROT','GND',(218.44,78.74),(112.4,104.2),mpn='GRM155R71H104KE14D')
part('U1','LMR51610YDBVR',BUCK,'Package_TO_SOT_SMD:SOT-23-6',{1:'BOOT_SW',2:'GND',3:'FB5',4:'VIN_PROT',5:'VIN_PROT',6:'SW'},(279.4,58.42),(115.1,103.2))
c('C3','100n 16V','BOOT_SW','SW',(340.36,40.64),(116.5,105.5),angle=90)
part('L1','10uH >=3A Isat','Device:L','Inductor_SMD:L_Coilcraft_XAL4030-XXX',{1:'SW',2:'+5V'},(393.7,45.72),(120,103),rot=90,mpn='XGL4030-103MEC',note='XGL4030 datasheet verified: 0.98 x 3.4 mm lands, 2.37 mm center pitch; identical to KiCad XAL4030 land pattern')
c('C4','10u 25V','+5V','GND',(355.6,83.82),(123.8,101.8),footprint='Capacitor_SMD:C_0805_2012Metric',mpn='GRM21BR61E106KA73L')
c('C5','10u 25V','+5V','GND',(391.16,83.82),(123.8,104.3),footprint='Capacitor_SMD:C_0805_2012Metric',mpn='GRM21BR61E106KA73L')
r('R1','84.5k 1%','+5V','FB5',(270.51,99.06),(114.5,106.7),side='B')
r('R2','16.2k 1%','FB5','GND',(318.77,99.06),(117,106.7),side='B')
part('U2','TLV75533PDBVR','Regulator_Linear:TLV75533PDBV','Package_TO_SOT_SMD:SOT-23-5',{1:'+5V',2:'GND',3:'+5V',5:'+3V3'},(470,55),(128.1,103))
c('C6','2.2u 10V','+5V','GND',(443.23,101.6),(125.3,106.4),side='B')
c('C7','10u 10V','+3V3','GND',(492.76,101.6),(129.8,105.5),footprint='Capacitor_SMD:C_0603_1608Metric')

# Physical CAN inhibit: CAN_TX OR TX_INHIBIT -> TXD. Pullups force recessive/sleep at reset.
part('U3','TCAN1044AVDRBRQ1',CAN,'Package_SON:VSON-8-1EP_3x3mm_P0.65mm_EP1.65x2.4mm',
 {1:'TXD_GATE',2:'GND',3:'+5V',4:'CAN_RX',5:'+3V3',6:'CAN_L',7:'CAN_H',8:'CAN_STB',9:'GND'},(196.85,156.21),(113,112.2))
part('U4','SN74LVC1G32DCKR',GATE,'Package_TO_SOT_SMD:SOT-353_SC-70-5',{1:'CAN_TX',2:'TX_INHIBIT',3:'GND',4:'TXD_GATE',5:'+3V3'},(77.47,154.94),(118,111.7))
part('D3','PESD2CANFD24V-T',ESD,'Package_TO_SOT_SMD:SOT-23',{1:'CAN_L',2:'CAN_H',3:'GND'},(308.61,153.67),(107.3,113.8))
r('R3','10k','+3V3','TX_INHIBIT',(58.42,201.93),(119,109.5),side='B')
r('R4','10k','+3V3','CAN_STB',(118.11,201.93),(114,109),side='B')
c('C8','100n','+5V','GND',(175.26,201.93),(110.9,109.2))
c('C9','100n','+3V3','GND',(219.71,201.93),(115,114.8))
c('C10','100n','+3V3','GND',(264.16,201.93),(118.3,114),side='B')

# ESP32-C3FH4X pin map verified against datasheet v2.4: 19..24 are NC.
part('U5','ESP32-C3FH4X',C3,'Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.7x3.7mm',
 {1:'RF_SOC',2:'VDD_RF',3:'VDD_RF',4:'VBAT_ADC',5:'CAN_TX',6:'STRAP2',7:'EN',8:'CAN_RX',9:'TX_INHIBIT',10:'CAN_STB',11:'+3V3',12:'LED',14:'STRAP8',15:'BOOT',17:'+3V3',18:'VDD_SPI',25:'USB_DM',26:'USB_DP',27:'U0RX',28:'U0TX',29:'XTAL_N',30:'XTAL_P',31:'+3V3',32:'+3V3',33:'GND'},(438.15,199.39),(127.5,111.5))
part('FB1','600R@100M 500mA','Device:FerriteBead','Inductor_SMD:L_0402_1005Metric',{1:'+3V3',2:'VDD_RF'},(358.14,265.43),(123.1,110.5),rot=90,mpn='BLM15AG601SN1D',note='Verify RF supply impedance and transient droop')
c('C11','10u 10V','VDD_RF','GND',(309.88,296),(122.5,113),footprint='Capacitor_SMD:C_0603_1608Metric')
c('C12','100n','VDD_RF','GND',(354.33,296),(124.2,110.6))
c('C13','100n','+3V3','GND',(398.78,296),(127,114.7))
c('C14','100n','+3V3','GND',(443.23,296),(130.7,113),angle=90)
c('C15','100n','+3V3','GND',(487.68,296),(126.5,108.4))
c('C16','1u','VDD_SPI','GND',(532.13,296),(130.7,111.7),angle=90)
r('R5','10k','+3V3','EN',(58.42,257.81),(124.4,114.5),side='B')
c('C17','1u','EN','GND',(103,257.81),(124.3,112.7),side='B')
r('R6','10k','+3V3','STRAP2',(160.02,257.81),(126,112.8),side='B')
r('R7','10k','+3V3','STRAP8',(215.9,257.81),(128,114),side='B')
r('R8','10k','+3V3','BOOT',(58.42,290.83),(130.2,114.5),side='B')
part('Y1','40MHz CL=10pF','Device:Crystal_GND24','Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm',
 {1:'XTAL_P',2:'GND',3:'XTAL_N',4:'GND'},(173.99,299.72),(133.3,106),mpn='ABM8G-40.000MHZ-10-D2X-T',
 note='40 MHz fundamental, CL 10 pF; 16 pF loads assume 2 pF stray. Verify total frequency error after assembly; 1.4 x 1.2 mm lands at 2.2 x 1.7 mm pitch match manufacturer drawing')
c('C18','16p C0G','XTAL_P','GND',(125.73,325.12),(131.1,107.9))
c('C19','16p C0G','XTAL_N','GND',(219.71,325.12),(134.7,107.9))

# RF matching starts from Espressif's guideline range; must be tuned on assembled board.
c('C20','1.5p C0G','RF_SOC','GND',(353.06,350.52),(123.9,108),footprint='Capacitor_SMD:C_0201_0603Metric')
part('L2','2.4nH','Device:L','Inductor_SMD:L_0201_0603Metric',{1:'RF_SOC',2:'RF50'},(401.32,341.63),(123,107.2),rot=90,mpn='LQP03TG2N4B02D')
c('C21','1.5p C0G','RF50','GND',(447.04,350.52),(121.9,107.6),footprint='Capacitor_SMD:C_0201_0603Metric')
part('J2','U.FL / external FPC antenna','Connector:Conn_Coaxial','Connector_Coaxial:U.FL_Hirose_U.FL-R-SMT-1_Vertical',{1:'RF50',2:'GND'},(523.24,347.98),(121.2,107.1),angle=180,mpn='U.FL-R-SMT-1(10)',note='Coax/FPC antenna located in plastic case away from OBD metal')

# ADC samples the protected supply: reported voltage excludes D1/fuse drop.
r('R9','1M 1%','VIN_PROT','VBAT_ADC',(59.69,353.06),(127.4,109.3),side='B')
r('R10','100k 1%','VBAT_ADC','GND',(118.11,353.06),(125.6,109.3),side='B')
c('C22','100n','VBAT_ADC','GND',(175.26,358.14),(124,109.3),side='B')
r('R11','1k','LED','LED_A',(237.49,350.52),(134,114),side='B')
part('D4','GREEN','Device:LED','LED_SMD:LED_0402_1005Metric',{1:'GND',2:'LED_A'},(275.59,382.27),(136.7,113.7))
r('R12','499R','U0TX','UART_TX',(52.07,387.35),(133.5,111.4),side='B')
# Separate pogo lands, not a USB connector or USB 5V supply input.
for i, net in enumerate(['GND','+3V3','EN','BOOT','U0RX','UART_TX','USB_DM','USB_DP']):
    part('TP'+str(i+1),net,'Connector:TestPoint','TestPoint:TestPoint_Pad_D1.0mm',{1:net},(326.39+31.75*(i%4),377.19+15.24*(i//4)),
         (101.5+2*i,115.2),side='B',note='Bench fixture pad')

# Short, unambiguous label stubs keep the sheet legible at functional-block scale.
# Placement revisions are collected here to keep electrical definitions readable.
positions={
 'F1':(103,102),'D1':(104.5,103.5),'D2':(104.5,106.5),'D3':(104.5,112.5),
 'U1':(113.8,103.4),'C1':(113.8,106.7),'C2':(110.9,106.7),'C3':(110.6,102.4),
 'L1':(120,103.4),'C4':(124.1,101.5),'C5':(124.1,104.1),'U2':(128,102.8),'C7':(132,102.8),
 'U3':(112.8,112.3),'U4':(118,112.3),'C8':(109.45,112.5),'C9':(115.8,114.3),
 'C12':(122.9,110.8),'FB1':(120.5,111.6),'C11':(122.1,113.8),
 'C13':(126.7,115.5),'C14':(132,114.3),'C15':(127.2,107.6),'C16':(132,112.8),
 'Y1':(133,106),'C18':(131.8,108.6),'C19':(134.2,108.6),
 'C20':(124,108.9),'L2':(122.8,108),'C21':(122.4,109.5),'J2':(119.3,108.4),
 'C22':(123.8,109.3),'R10':(126.1,109.3),'R9':(128.4,109.3),
 'R5':(123.8,114.6),'C17':(123.8,112.5),'R6':(126.1,112.5),'R7':(128.4,114.6),'R8':(130.7,114.6),
 'R1':(114,104),'R2':(114,106),'C6':(128,102.8)
}
angles={'C2':0,'C3':0,'C9':90,'C12':180,'C14':0,'C16':0,'C20':90,'L2':180,'C21':90}
for p in parts:
    if p['ref'] in positions:p['pcb']=positions[p['ref']]
    if p['ref'] in angles:p['angle']=angles[p['ref']]
    if p['ref']=='D1':p['side']='B'

# Integrated placement is maintained separately from schematic coordinates.
integrated=json.loads((ROOT/'placement.json').read_text(encoding='utf-8'))
assert set(integrated)=={p['ref'] for p in parts}
for p in parts:
    x,y,angle,side=integrated[p['ref']]
    p.update(pcb=[100+x,100+y],angle=angle,side=side)
doc=build.empty_sheet(uid('root'),'A2'); segments=[]; pins=[]; expected={}
doc.insert(7,parse('(title_block (title "OBD CAN / BLE Node") (date "2026-09-14") (rev "A1 - INTEGRATED PROTOTYPE"))'))
for p in parts:
    definition=p['symbol']; libpins=list(sexp.walk(definition,'pin'))
    names=[atoms(find(n,'number'))[0] for n in libpins]
    assert set(p['nets']).issubset(names), (p['ref'],set(p['nets'])-set(names))
    node=build.symbol(NAME,uid('root'),p['lib_id'],p['sch'],p['rot'],p['ref'],p['value'],names)
    if p['ref'].startswith('TP'):
        find(node,'in_bom')[1]=sexp.Sym('no'); find(node,'in_pos_files')[1]=sexp.Sym('no')
    find(node,'uuid')[1]=q(uid(p['ref']))
    if p['dnp']: find(node,'dnp')[1]=sexp.Sym('yes')
    ops.ensure_lib(doc,definition); ops._insert_body(doc,node)
    ops.set_prop(doc,p['ref'],'Footprint',p['footprint'])
    ops.set_prop(doc,p['ref'],'MPN',p['mpn']); ops.set_prop(doc,p['ref'],'DesignNote',p['note'])
    # IC names go above the rectangle, small components use compact offset text.
    rects=list(sexp.walk(definition,'rectangle'))
    isic=p['lib_id'].startswith('Node:')
    if isic:
        height=abs(float(atoms(find(rects[0],'start'))[1]))
        for field, yy in [('Reference',p['sch'][1]-height-6.35),('Value',p['sch'][1]-height-3.81)]:
            for prop in allof(node,'property'):
                if atoms(prop)[0]==field: find(prop,'at')[2]=num(yy)
    elif len(libpins)<=2:
        world=[geom.pin_world(p['sch'],p['rot'],tuple(map(float,atoms(find(n,'at'))[:2]))) for n in libpins]
        vertical=len(world)==1 or world[0][0]==world[1][0]
        for prop in allof(node,'property'):
            if atoms(prop)[0] in ['Reference','Value']:
                dy=(-1.27 if atoms(prop)[0]=='Reference' else 1.27) if vertical else (-5.08 if atoms(prop)[0]=='Reference' else 5.08)
                at=find(prop,'at'); at[1]=num(p['sch'][0]+(6.35 if vertical else 0)); at[2]=num(p['sch'][1]+dy)
                find(find(prop,'effects'),'font')[1]=parse('(size 1.016 1.016)')
    if p['ref'] in ['U2','Y1','J2']:
        for prop in allof(node,'property'):
            if atoms(prop)[0]=='Value':
                at=find(prop,'at'); at[1]=num(p['sch'][0]); at[2]=num(p['sch'][1]-10.16)
    for pin in libpins:
        number=atoms(find(pin,'number'))[0]; at=atoms(find(pin,'at')); u,v,a=map(float,at)
        pos=geom.pin_world(p['sch'],p['rot'],(u,v)); net=p['nets'].get(number)
        if net is None:
            ops._insert_body(doc,parse(f'(no_connect (at {pos[0]} {pos[1]}) (uuid "{uid(p["ref"]+"NC"+number)}"))'))
            continue
        # A library pin points inward; stub heads outward.
        dx,dy=geom.rot_xy(-math.cos(math.radians(a))*5.08,-math.sin(math.radians(a))*5.08,p['rot'])
        end=geom.pt(pos[0]+dx,pos[1]+dy)
        pins.append(Pin(net,pos,p['ref']+'.'+number))
        expected.setdefault(net,[]).append([p['ref'],number])
        if Seg(net,pos,end) in segments: continue # stacked crystal case pins
        segments.append(Seg(net,pos,end))
        ops.add_wire(doc,pos,end)
        label=build.label(net,end)
        if dx<0: find(find(label,'effects'),'justify')[1]=sexp.Sym('right')
        if dy: find(label,'at')[3]=num(90)
        ops._insert_body(doc,label)

# Power flags mark sources crossing passive components, not regulated power outputs.
flag=library('power:PWR_FLAG')
for i,net in enumerate(['VBAT','GND','VIN_PROT','+5V','VDD_RF']):
    at=snap(30+i*35,118)
    node=build.symbol(NAME,uid('root'),'power:PWR_FLAG',at,0,'#FLG0'+str(i+1),'PWR_FLAG',['1'],ref_hidden=True)
    for prop in allof(node,'property'):
        if atoms(prop)[0]=='Value': prop.append(parse('(hide yes)'))
    ops.ensure_lib(doc,flag); ops._insert_body(doc,node)
    end=geom.pt(at[0],at[1]+5.08); ops.add_wire(doc,at,end); ops.add_label(doc,net,end)
    segments.append(Seg(net,at,end)); pins.append(Pin(net,at,'#FLG0'+str(i+1)+'.1'))

def text(msg,x,y,size=1.524):
    ops._insert_body(doc,parse(f'(text {sexp.dumps(q(msg))} (at {x:g} {y:g} 0) (effects (font (size {size:g} {size:g})) (justify left top)) (uuid "{uid(msg)}"))'))
text('REV A / 38.5 x 16.6 mm / CLASSIC CAN + BLE / ENGINEERING DRAFT',22.86,17.78,2.54)
text('J1: straight-tail 4.00 x 8.60 mm grid ASSUMED\nPlated slot 2.40 x 1.20 mm: verify actual plug fit.',20.32,28,1.27)
text('12 V INPUT / REVERSE PROTECTION / 65 V BUCK / 5 V -> 3.3 V',120.65,25.4,1.778)
text('No 120 ohm termination on vehicle stub. TX inhibit HIGH physically forces recessive.',25.4,133.35)
text('RESET / STRAPS / 40 MHz',25.4,233.68,1.778)
text('C3FH4X: integrated flash, pins 19..24 NC. VDD_SPI is a supply output.',339.09,136,1.27)
text('RF / 50 ohm reference plane / match values require VNA tuning in final case',311.15,321.31)
text('ADC senses VIN_PROT; fuse and diode drop are not compensated.',25.4,335.28,1.27)
text('USB pads: DATA ONLY. Fixture power requires vehicle disconnection.',310,404,1.016)

errors=verify(segments,pins,[],merge_power=False)
assert not errors,'\n'.join(errors)
assert verify([Seg('A',(0,0),(5.08,0)),Seg('B',(2.54,-2.54),(2.54,2.54))],[],[],False), 'Mutation detector failed'
ROOT.joinpath('build').mkdir(exist_ok=True)
# The same schematic J1 is adopted by PCB, with electrical nets on physical pads.
assert parts[0]['ref']=='J1' and parts[0]['footprint'].startswith('Node:OBD_')
sexp.save_file(str(ROOT/(NAME+'.kicad_sch')),doc)
project=ROOT/(NAME+'.kicad_pro')
project_data=json.loads(project.read_text()) if project.exists() else {'meta':{'filename':project.name,'version':1}}
project_data.setdefault('board',{}).setdefault('design_settings',{}).setdefault('rules',{}).update({'min_clearance':0.15,'min_track_width':0.15,'min_copper_edge_clearance':0.25,'min_text_height':0.6,'min_text_thickness':0.1,'min_through_hole_diameter':0.2,'min_via_diameter':0.45})
project_data['net_settings']={'classes':[{'name':'Default','clearance':0.15,'track_width':0.15,'via_diameter':0.45,'via_drill':0.2,'pcb_color':'rgba(0, 0, 0, 0.000)','schematic_color':'rgba(0, 0, 0, 0.000)'}],'meta':{'version':3}}
project.write_text(json.dumps(project_data,indent=2))
(ROOT/(NAME+'.kicad_dru')).write_text('(version 1)\n(rule "Core copper clearance" (constraint clearance (min 0.15)))\n')
libtree=parse('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor"))')
for name,definition in custom.items():
    definition=copy.deepcopy(definition); definition[1]=q(name); libtree.append(definition)
sexp.save_file(str(ROOT/'Node.kicad_sym'),libtree)
(ROOT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "Node") (type "KiCad") (uri "${KIPRJMOD}/Node.kicad_sym") (options "") (descr "Verified pin maps; J1 mechanical interface pending")))\n')
serial=[{k:v for k,v in p.items() if k!='symbol'} for p in parts]
(ROOT/'design.json').write_text(json.dumps({'board_mm':[38.5,16.6],'layers':4,'mechanical_status':'Integrated straight-tail geometry; actual plug fit unverified','parts':serial,'expected_nets':expected},indent=2),encoding='utf-8')
(ROOT/'build/geometry.json').write_text(json.dumps({'violations':errors,'mutation_detected':True,'pins':len(pins),'segments':len(segments)},indent=2))
print(f'Schematic generated: {len(parts)} components including netted J1; geometry verified.')
