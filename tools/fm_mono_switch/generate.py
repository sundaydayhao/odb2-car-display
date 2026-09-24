"""Build RDA5807M + SGM3718 mono receiver in KiCad 10 (native Python)."""
from pathlib import Path
import copy, csv, json, sys, uuid, subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'display_board/hardware/fm-mono-switch'
WORK = ROOT / 'build/fm-mono-switch'
LIB = Path('C:/Program Files/KiCad/10.0/share/kicad')
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp, build, ops
from kicad_lib.verify import Seg, Pin, verify
import pcbnew as k

NAME = 'fm_mono_switch'
NS = uuid.UUID('85d1a1cf-4909-4637-aa6a-e1ebaa855809')
uid = lambda s: str(uuid.uuid5(NS, s))
Q = sexp.QStr.of
S = sexp.Sym
mm = lambda x: round(x * 1.27, 3)
pt = lambda x, y: (mm(x), mm(y))
SOURCE = 'https://www.sg-micro.com/rect/assets/3d47e91a-f3bb-4f8e-9305-8e041848aeda/SGM3718.pdf'
OUT.mkdir(parents=True, exist_ok=True)
WORK.mkdir(parents=True, exist_ok=True)

if (OUT/(NAME+'.kicad_pcb')).exists() and '--regen-placed-board' not in sys.argv:
    raise SystemExit('Refusing to overwrite routed PCB. Use --regen-placed-board only for a fresh layout.')

defs = {}
for name, lib, item in [('R','Device','R'),('C','Device','C'),('J2','Connector_Generic','Conn_01x02'),
                      ('J5','Connector_Generic','Conn_01x05'),('GND','power','GND'),('PWR_FLAG','power','PWR_FLAG'),
                      ('Y','Device','Crystal'),('TVS','Device','D_TVS'),('FB','Device','FerriteBead'),
                      ('LDO','Regulator_Linear','TLV70012_SOT23-5'),
                      ('Mount','Mechanical','MountingHole')]:
    d = ops.extract_libdef(str(LIB/'symbols'/f'{lib}.kicad_sym'),item,f'Mono:{name}')
    for sub in sexp.find_all(d,'symbol'):
        sub[1]=Q(sexp.atoms(sub)[0].replace(item+'_',name+'_',1))
    for p in sexp.find_all(d,'property'):
        if sexp.atoms(p)[0]=='Value':p[2]=Q(name)
        if name=='LDO' and sexp.atoms(p)[0]=='Description':p[2]=Q('500mA low-dropout regulator, fixed 3.0V output, SOT-23-5; IN/GND/EN/NC/OUT')
    defs[name]=d

# Pin numbering is checked against manufacturer table on page 3.
u = ['(symbol "Mono:SGM3718" (pin_names (offset 0.635)) (in_bom yes) (on_board yes)',
     '(property "Reference" "U" (at 0 30.48 0) (effects (font (size 1.27 1.27))))',
     '(property "Value" "SGM3718" (at 0 27.94 0) (effects (font (size 1.27 1.27))))',
     '(symbol "SGM3718_0_1" (rectangle (start -20.32 25.4) (end 20.32 -25.4) (stroke (width 0.254) (type default)) (fill (type background))))',
     '(symbol "SGM3718_1_1"']
upins = [(1,'V+',0,26,270,'power_in'),(5,'NC1 / CODEC_P',-22,16,0,'passive'),
         (2,'NO1 / FM_MONO',-22,8,0,'passive'),(7,'NC2 / CODEC_N',-22,-8,0,'passive'),
         (10,'NO2 / FM_REF',-22,-16,0,'passive'),(3,'COM1 / AMP_P',22,12,180,'passive'),
         (9,'COM2 / AMP_N',22,-12,180,'passive'),(4,'IN1',-8,-26,90,'input'),
         (8,'IN2',0,-26,90,'input'),(6,'GND',8,-26,90,'power_in')]
for n,name,x,y,a,t in upins:
    length=6 if abs(x)==22 else 6
    u.append(f'(pin {t} line (at {mm(x)} {mm(y)} {a}) (length {mm(length)}) (name "{name}" (effects (font (size 1.016 1.016)))) (number "{n}" (effects (font (size 1.016 1.016)))))')
u.append('))');defs['SGM3718']=sexp.parse('\n'.join(u))

# RDA5807M MSOP-10, not the ten-pin RRD-102 module from the screenshots.
u=['(symbol "Mono:RDA5807M" (pin_names (offset 0.635)) (in_bom yes) (on_board yes)',
   '(property "Reference" "U" (at 0 30.48 0) (effects (font (size 1.27 1.27))))',
   '(property "Value" "RDA5807M" (at 0 27.94 0) (effects (font (size 1.27 1.27))))',
   '(symbol "RDA5807M_0_1" (rectangle (start -20.32 22.86) (end 20.32 -22.86) (stroke (width 0.254) (type default)) (fill (type background))))',
   '(symbol "RDA5807M_1_1"']
for n,name,x,y,a,typ in [(7,'VDD',0,24,270,'power_in'),(2,'FMIN',-20,12,0,'input'),
                       (4,'SCLK',-20,0,0,'input'),(5,'SDIO',-20,-10,0,'bidirectional'),
                       (10,'LOUT',20,12,180,'output'),(9,'ROUT',20,0,180,'output'),
                       (6,'RCLK',12,-24,90,'passive'),(1,'GND',-12,-24,90,'power_in'),
                       (3,'GND',-4,-24,90,'power_in'),(8,'GND',4,-24,90,'power_in')]:
    length=4 if abs(x)==20 else 6
    u.append(f'(pin {typ} line (at {mm(x)} {mm(y)} {a}) (length {mm(length)}) (name "{name}" (effects (font (size 1.016 1.016)))) (number "{n}" (effects (font (size 1.016 1.016)))))')
u.append('))');defs['RDA5807M']=sexp.parse('\n'.join(u))

doc=build.empty_sheet(uid('root'),'A2')
doc.insert(6,sexp.parse('(title_block (title "P4 v1.4 - RDA5807M + SGM3718 mono FM") (date "2026-09-24") (rev "A2 PROTOTYPE") (company "OBD Car Display") (comment 1 "Remove R83/R84. Retain original capacitors and amplifier."))'))
parts={}; pins={}; segs=[]; labels=[]; junctions=[]
def add(ref,kind,value,xy,nets,footprint='',pcb=None,rot=0,angle=0,lcsc='',source=''):
    at=pt(*xy); d=defs[kind]
    ops.place(doc,d,at,rot,ref,value,list(map(str,nets)),project=NAME)
    sym=list(sexp.find_all(doc,'symbol'))[-1]
    sexp.find(sym,'uuid')[1]=Q(uid(ref))
    for prop in sexp.find_all(sym,'property'):
        nam=sexp.atoms(prop)[0]
        if nam=='Footprint':prop[2]=Q(footprint)
        if nam=='Datasheet':prop[2]=Q(source)
    if lcsc: sym.append(build.prop('LCSC',lcsc,*at,hide=True))
    if kind in ['R','C']:
        refpos=(at[0],at[1]-3.175) if rot else (at[0]+3.81,at[1]-1.27)
        valpos=(at[0],at[1]+3.175) if rot else (at[0]+3.81,at[1]+1.27)
    elif kind in ['SGM3718','RDA5807M']:refpos=(at[0]+12.7,at[1]-35.56);valpos=(at[0]+12.7,at[1]-33.02)
    else:refpos=(at[0],at[1]-10.16);valpos=(at[0],at[1]-7.62)
    for prop in sexp.find_all(sym,'property'):
        nam=sexp.atoms(prop)[0]
        if nam in ['Reference','Value']:
            x,y=refpos if nam=='Reference' else valpos
            sexp.find(prop,'at')[:]=[S('at'),S(str(x)),S(str(y)),S('90' if rot in [90,270] else '0')]
            if kind in ['R','C'] and rot==0:
                sexp.find(prop,'effects').append([S('justify'),S('left')])
            if kind in ['GND','PWR_FLAG','Mount']:
                prop.append([S('hide'),S('yes')])
    geom={}
    for sub in sexp.find_all(d,'symbol'):
        for pin in sexp.find_all(sub,'pin'):
            n=sexp.atoms(sexp.find(pin,'number'))[0];px,py,pa=map(float,sexp.atoms(sexp.find(pin,'at')))
            if rot==0:dx,dy=px,-py
            elif rot==90:dx,dy=-py,-px
            elif rot==180:dx,dy=-px,py
            elif rot==270:dx,dy=py,px
            geom[n]=(round(at[0]+dx,3),round(at[1]+dy,3))
            if int(n) in nets:pins[(ref,n)]=(nets[int(n)],geom[n])
            else:
                assert ref=='U3' and n=='4'
                doc.append(sexp.parse(f'(no_connect (at {geom[n][0]} {geom[n][1]}) (uuid {uid("NC-U3-4")}))'))
    parts[ref]=dict(kind=kind,value=value,nets={str(n):v for n,v in nets.items()},footprint=footprint,pcb=pcb,angle=angle,lcsc=lcsc,source=source)
    return geom
def p(ref,n):return pins[(ref,str(n))][1]
def wire(net,*points):
    for a,b in zip(points,points[1:]):
        if a==b:continue
        assert a[0]==b[0] or a[1]==b[1],(net,a,b)
        segs.append(Seg(net,a,b));ops.add_wire(doc,a,b)
def label(net,pos):
    ops.add_label(doc,net,pos);labels.append((net,pos))
def junction(pos):
    ops.add_junction(doc,pos);junctions.append(pos)
def stub(ref,n,dx=6,dy=0):
    net,a=pins[(ref,str(n))];b=(round(a[0]+mm(dx),3),round(a[1]+mm(dy),3));wire(net,a,b);label(net,b);return b
def ground(net,pos,ref):
    # All ground symbols refer to the same GND net.
    assert net=='GND';add(ref,'GND','GND',(pos[0]/1.27,pos[1]/1.27),{1:'GND'})
def note(s,x,y,size=1.27):
    doc.append(sexp.parse(f'(text {json.dumps(s)} (at {mm(x)} {mm(y)} 0) (effects (font (size {size} {size})) (justify left top)) (uuid {uid(s)}))'))

RFP='Resistor_SMD:R_0603_1608Metric'; CFP='Capacitor_SMD:C_0603_1608Metric'
H2='Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical'
H3='Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical'
add('J1','J2','CODEC / C39,C40 side',(44,35),{1:'CODEC_P',2:'CODEC_N'},H2,(3,9))
add('J2','J2','AMP / C59,C58 side',(126,35),{1:'AMP_P',2:'AMP_N'},H2,(29,8))
add('J3','J2','ANT / GND',(345,35),{1:'ANT',2:'GND'},H2,(3,22))
add('J4','J5','P4: 3V3,GND,SEL,SDA,SCL',(245,35),{1:'+3V3_IN',2:'GND',3:'GPIO6_SEL',4:'GPIO2_SDA',5:'GPIO3_SCL'},
    'Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical',(20,3),angle=90)
for r in ['J1','J2','J3','J4']:
    for n in parts[r]['nets']:stub(r,n,-10)

# FM: independent AC coupling, resistor summing, DC return.
add('C1','C','1uF 16V X7R',(38,76),{1:'FM_L',2:'FM_L_AC'},CFP,(7,17),rot=90)
add('C2','C','1uF 16V X7R',(38,92),{1:'FM_R',2:'FM_R_AC'},CFP,(7,19),rot=90)
add('R1','R','10k 1%',(58,76),{1:'FM_L_AC',2:'FM_MONO'},RFP,(11,17),rot=90)
add('R2','R','10k 1%',(58,92),{1:'FM_R_AC',2:'FM_MONO'},RFP,(14,20),rot=90)
stub('C1',1,-10);stub('C2',1,-10)
wire('FM_L_AC',p('C1',2),p('R1',1));wire('FM_R_AC',p('C2',2),p('R2',1))
label('FM_L_AC',p('C1',2));label('FM_R_AC',p('C2',2))
a=pt(78,76);b=pt(78,92);c=pt(78,84);e=pt(90,84)
wire('FM_MONO',p('R1',2),a,c,b,p('R2',2));wire('FM_MONO',c,e);label('FM_MONO',e);junction(c)
add('R3','R','47k 1%',(90,99),{1:'FM_MONO',2:'GND'},RFP,(17,19))
wire('FM_MONO',e,p('R3',1));g=pt(90,108);wire('GND',p('R3',2),g);ground('GND',g,'#PWR01')
add('R4','R','4.3k 1%',(114,99),{1:'FM_REF',2:'GND'},RFP,(11,14))
stub('R4',1,0,-7);g=pt(114,108);wire('GND',p('R4',2),g);ground('GND',g,'#PWR02')

add('U1','SGM3718','SGM3718YUWQ10G/TR',(174,94),
    {1:'VCC_SW',2:'FM_MONO',3:'AMP_P',4:'SEL',5:'CODEC_P',6:'GND',7:'CODEC_N',8:'SEL',9:'AMP_N',10:'FM_REF'},
    'Mono:SGM3718_UTQFN10_1.8x1.4_P0.4',(16,12),lcsc='C700220',source=SOURCE)
for n in [5,2,7,10]:stub('U1',n,-8)
for n in [3,9]:stub('U1',n,8)
stub('U1',1,0,-6)
for n in [4,8,6]:stub('U1',n,0,6)

# Bleeders keep both sources and both amplifier-facing wires near zero DC.
for ref,net,x,y,pcb in [('R5','CODEC_P',235,80,(8,8)),('R6','CODEC_N',261,80,(8,11.5)),
                      ('R7','AMP_P',235,108,(23,10)),('R8','AMP_N',261,108,(23,14))]:
    add(ref,'R','100k 1%',(x,y),{1:net,2:'GND'},RFP,pcb)
    stub(ref,1,0,-5);g=pt(x,y+8);wire('GND',p(ref,2),g);ground('GND',g,'#PWR'+ref)

# One selector line controls both switches. Low is codec, high is FM.
add('R9','R','1k 1%',(40,151),{1:'GPIO6_SEL',2:'SEL'},RFP,(22,7.5),rot=90)
stub('R9',1,-8);a=pt(58,151);b=pt(77,151);c=pt(88,151)
wire('SEL',p('R9',2),a,b,c);label('SEL',c)
add('R10','R','100k 1%',(58,164),{1:'SEL',2:'GND'},RFP,(21,17))
add('C3','C','1nF C0G',(77,164),{1:'SEL',2:'GND'},CFP,(23,19))
for ref,x in [('R10',58),('C3',77)]:
    wire('SEL',pt(x,151),p(ref,1));junction(pt(x,151));g=pt(x,173);wire('GND',p(ref,2),g);ground('GND',g,'#PWR'+ref)

# Local 3.3V filtering. Do not power this board from 5V.
add('R11','R','10R 1%',(152,151),{1:'+3V3_IN',2:'VCC_SW'},RFP,(17,6),rot=90)
stub('R11',1,-8);a=pt(169,151);b=pt(188,151);c=pt(200,151)
wire('VCC_SW',p('R11',2),a,b,c);label('VCC_SW',c)
add('C4','C','100nF 16V X7R',(169,164),{1:'VCC_SW',2:'GND'},CFP,(16,14.6))
add('C5','C','1uF 16V X7R',(188,164),{1:'VCC_SW',2:'GND'},CFP,(20,14.6))
for ref,x in [('C4',169),('C5',188)]:
    wire('VCC_SW',pt(x,151),p(ref,1));junction(pt(x,151));g=pt(x,173);wire('GND',p(ref,2),g);ground('GND',g,'#PWR'+ref)
for ref,net,xy in [('#FLG1','+3V3_IN',(238,156)),('#FLG2','VCC_SW',(265,156)),('#FLG3','GND',(292,156))]:
    add(ref,'PWR_FLAG','PWR_FLAG',xy,{1:net});stub(ref,1,0,5)
for ref,xy,pcb in [('H1',(235,180),(3,3)),('H2',(265,180),(29,23))]:
    add(ref,'Mount','M2 NPTH',xy,{},'MountingHole:MountingHole_2.2mm_M2',pcb)

note('FM stereo to mono (never short L and R together)',18,58)
note('Two poles switch the two wires of ONE mono amplifier.',144,58)
note('SEL = 0: ES8311 / MP3 / Xiaozhi    SEL = 1: FM',144,132)
note('Default LOW + GPIO edge filtering',18,136)
note('3.3 V supply only; keep C4 close to U1 pin 1 / pin 6.',135,136)
note('INSTALLATION: remove P4 R83 and R84; solder four separate wires to their four pads.',18,192)
note('J1.1 -> R83 C39 side; J1.2 -> R84 C40 side. J2.1 -> R83 C59 side; J2.2 -> R84 C58 side.',18,197)
note('J4.1 -> P4 J1 pin 17 (3V3); J4.2 -> P4 J1 pin 20 (GND); J4.3 -> P4 J1 pin 15 (GPIO6).',18,202)
note('Mute/disable the board PA (GPIO53) before changing SEL. Enable PA for FM after settling.',18,207)
note('PROTO A2: direct RDA5807M chip, not a module. Hardware/audio/RF tests required.',18,212)

# Dedicated antenna input, no headphone-ground antenna sharing.
add('C6','C','100pF C0G',(363,54),{1:'ANT',2:'FMIN'},CFP,(6.3,24),rot=90)
stub('C6',1,-8);stub('C6',2,8)
add('D1','TVS','PESD5V0F1BL (0.4pF)',(328,64),{1:'ANT',2:'GND'},'Diode_SMD:D_SOD-882',(5.5,21),rot=270,angle=90,
    source='https://assets.nexperia.com/documents/data-sheet/PESD5V0F1BL.pdf')
stub('D1',1,0,-4);g=pt(328,74);wire('GND',p('D1',2),g);ground('GND',g,'#PWRD1')
add('U2','RDA5807M','RDA5807M',(390,97),
    {1:'GND',2:'FMIN',3:'GND',4:'I2C_SCLK',5:'I2C_SDIO',6:'RCLK',7:'FM_VDD',8:'GND',9:'FM_R',10:'FM_L'},
    'Package_SO:MSOP-10_3x3mm_P0.5mm',(11,24.5),lcsc='C82537',source='${KIPRJMOD}/reference/RDA5807M.pdf')
for n in [2,4,5]:stub('U2',n,-9)
for n in [9,10]:stub('U2',n,8)
stub('U2',7,0,-6)
for n in [1,3,8,6]:stub('U2',n,0,6)
add('Y1','Y','32.768kHz CL12.5pF',(427,140),{1:'RCLK',2:'GND'},
    'Crystal:Crystal_C26-LF_D2.1mm_L6.5mm_Horizontal',(13.7,28),angle=270)
stub('Y1',1,-7);g=pt(430,147);wire('GND',p('Y1',2),g);ground('GND',g,'#PWRY1')
note('Bare chip pinout: PDF p14; crystal-to-GND: p17. No external load caps.',320,153)
note('Dedicated wire antenna; C6 + D1 are not the headphone LC choke.',320,158)

for r,up,io,ext,x,y,pc in [('R12','R14','I2C_SDIO','GPIO2_SDA',350,191,(7,33.5)),
                           ('R13','R15','I2C_SCLK','GPIO3_SCL',404,191,(11,33.5))]:
    add(r,'R','100R 1%',(x,y),{1:ext,2:io},RFP,pc,rot=90)
    stub(r,1,-8);stub(r,2,10)
    add(up,'R','4.7k 1%',(x+10,y-14),{1:'FM_VDD',2:io},RFP,(pc[0],pc[1]-2.5))
    stub(up,1,0,-5);stub(up,2,0,5)
note('I2C pull-ups to FM_VDD (~3.0V); use open-drain on P4.',320,209)
note('J4.4 -> P4 J1.21 / GPIO2; J4.5 -> P4 J1.19 / GPIO3.',320,214)

note('FM supply: 3.3V -> 3.0V LDO -> ferrite bead; separate from switch supply.',18,232)
add('U3','LDO','TLV75530PDBVR',(88,266),{1:'+3V3_IN',2:'GND',3:'+3V3_IN',5:'LDO_3V0'},
    'Package_TO_SOT_SMD:SOT-23-5',(28,30),source='https://www.ti.com/lit/ds/symlink/tlv755p.pdf')
for n in [1,3]:stub('U3',n,-10)
stub('U3',5,12);g=pt(88,279);wire('GND',p('U3',2),g);ground('GND',g,'#PWRU3')
for ref,net,xy,pc,value in [('C7','+3V3_IN',(40,273),(31.5,27.5),'4.7uF 16V X7R'),
                         ('C8','LDO_3V0',(121,273),(31.5,32.5),'4.7uF 16V X7R'),
                         ('C9','FM_VDD',(193,279),(16,26.5),'22nF 16V X7R'),
                         ('C10','FM_VDD',(230,279),(19.5,27),'1uF 16V X7R')]:
    add(ref,'C',value,xy,{1:net,2:'GND'},CFP,pc)
    stub(ref,1,0,-7);g=pt(xy[0],xy[1]+10);wire('GND',p(ref,2),g);ground('GND',g,'#PWR'+ref)
add('FB1','FB','600R@100MHz / DCR<0.5R',(154,264),{1:'LDO_3V0',2:'FM_VDD'},RFP,(22.5,29),rot=90)
stub('FB1',1,-8);stub('FB1',2,8)
add('#FLG4','PWR_FLAG','PWR_FLAG',(270,264),{1:'FM_VDD'});stub('#FLG4',1,0,5)
note('Y1: watch crystal, 2 x 6 mm body. Keep RCLK trace short; verify startup with the selected crystal.',18,304)
note('J3.1 ANT: external FM wire antenna. J3.2 GND: shield/return; no vehicle phantom power.',18,310)

# Placement groups keep the RF input and crystal away from the host header.
placement={'J2':(37,8),'H2':(37,33),'U1':(25,15),
           'R12':(8,16),'R13':(13,16),'R14':(8,18.5),'R15':(13,18.5),
           'C1':(16.5,21.5),'C2':(16.5,23.7),'R1':(20.5,21.5),'R2':(20.5,23.7),
           'R3':(24.5,23.7),'R4':(20.5,18.5),'R5':(8,8),'R6':(8,11.5),
           'R7':(32,12),'R8':(32,16),'R9':(29,7),'R10':(28.5,21),'C3':(32,22),
           'R11':(20,7),'C4':(25,18),'C5':(28.5,18)}
for ref,xy in placement.items():parts[ref]['pcb']=xy

assert verify([Seg('A',pt(0,1),pt(2,1)),Seg('B',pt(1,0),pt(1,2))],[],[],False)
pinmodel=[Pin(net,xy,ref+'.'+n) for (ref,n),(net,xy) in pins.items()]
violations=verify(segs,pinmodel,junctions,False)
assert not violations,violations
for net,pos in labels:assert any(z.net==net and ((z.a==pos) or (z.b==pos)) for z in segs)
sexp.save_file(str(OUT/(NAME+'.kicad_sch')),doc)
libdoc=[S('kicad_symbol_lib'),[S('version'),S('20250114')],[S('generator'),Q('kicad_symbol_editor')]]
for key,d in defs.items():
    d=copy.deepcopy(d);d[1]=Q(key);libdoc.append(d)
sexp.save_file(str(OUT/'Mono.kicad_sym'),libdoc)
(OUT/'sym-lib-table').write_text('(sym_lib_table (lib (name "Mono") (type "KiCad") (uri "${KIPRJMOD}/Mono.kicad_sym") (options "") (descr "Datasheet checked mono FM selector")))\n')
(OUT/'fp-lib-table').write_text('(fp_lib_table (lib (name "Mono") (type "KiCad") (uri "${KIPRJMOD}/Mono.pretty") (options "") (descr "SGM3718 manufacturer recommended land pattern")))\n')
intent={'root_uuid':uid('root'),'parts':parts,'geometry':{'violations':0,'pins':len(pinmodel),'segments':len(segs)},
        'status':'A2 integrated RDA5807M prototype; design checks pending',
        'manufacturing_release':False,'board_size_final':True,'uses_fm_module':False,
        'truth_table':{'0':[['CODEC_P','AMP_P'],['CODEC_N','AMP_N']],'1':[['FM_MONO','AMP_P'],['FM_REF','AMP_N']]},
        'supply':'3.3V input; 3.0V FM LDO','board_size_mm':[40,36],'hardware_tested':False}
(OUT/'design-intent.json').write_text(json.dumps(intent,indent=2)+'\n')
print('Schematic geometry OK:',len(pinmodel),'pins',len(segs),'segments')

def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
# Native footprint: datasheet page 11; top view pin 1 at lower left,
# matching supplier model, rotated 180 degrees relative to functional pin picture.
fp=k.FOOTPRINT(None);fp.SetFPID(k.LIB_ID('Mono','SGM3718_UTQFN10_1.8x1.4_P0.4'));fp.SetReference('REF**');fp.SetValue('SGM3718');fp.SetAttributes(k.FP_SMD)
coords={1:(-.2,.5185,.225,.663),2:(.2,.5685,.225,.563),3:(.7685,.4,.563,.225),
        4:(.7685,0,.563,.225),5:(.7685,-.4,.563,.225),6:(.2,-.5685,.225,.563),
        7:(-.2,-.5685,.225,.563),8:(-.7685,-.4,.563,.225),9:(-.7685,0,.563,.225),10:(-.7685,.4,.563,.225)}
for n,(x,y,w,h) in coords.items():
    p0=k.PAD(fp);p0.SetNumber(str(n));p0.SetPosition(v(x,y));p0.SetSize(v(w,h));p0.SetAttribute(k.PAD_ATTRIB_SMD);p0.SetShape(k.PAD_SHAPE_ROUNDRECT);p0.SetRoundRectRadiusRatio(.15);p0.SetLayerSet(k.PAD.SMDMask());fp.Add(p0)
def line(parent,a,b,layer,width):
    z=k.PCB_SHAPE(parent);z.SetShape(k.SHAPE_T_SEGMENT);z.SetStart(v(*a));z.SetEnd(v(*b));z.SetLayer(layer);z.SetWidth(k.FromMM(width));parent.Add(z)
for layer,w,h in [(k.F_Fab,1.8,1.4),(k.F_CrtYd,2.6,2.2)]:
    pts=[(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]
    for a,b in zip(pts,pts[1:]+pts[:1]):line(fp,a,b,layer,.05)
dot=k.PCB_SHAPE(fp);dot.SetShape(k.SHAPE_T_CIRCLE);dot.SetStart(v(-.85,1.15));dot.SetEnd(v(-.75,1.15));dot.SetLayer(k.F_SilkS);dot.SetWidth(k.FromMM(.12));fp.Add(dot)
fp.Reference().SetPosition(v(0,-1.8));fp.Reference().SetTextSize(v(.8,.8));fp.Reference().SetTextThickness(k.FromMM(.12));fp.Value().SetVisible(False)
model=k.FP_3DMODEL();model.m_Filename='${KIPRJMOD}/libs/easyeda2kicad.3dshapes/UTQFN-10L_L1.8-W1.4-H0.8-P0.40-BL.step';fp.Add3DModel(model)
(OUT/'Mono.pretty').mkdir(exist_ok=True)
k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(OUT/'Mono.pretty'),fp)

board=k.BOARD();board.SetCopperLayerCount(2)
nets={}
for name in sorted({n for a in parts.values() for n in a['nets'].values()}):
    z=k.NETINFO_ITEM(board,name if name=='GND' else '/'+name);board.Add(z);nets[name]=z
for ref,a in parts.items():
    if not a['footprint']:continue
    lib,name=a['footprint'].split(':',1)
    folder=OUT/'Mono.pretty' if lib=='Mono' else LIB/'footprints'/(lib+'.pretty')
    f0=k.FootprintLoad(str(folder),name);assert f0,ref
    f0.SetFPID(k.LIB_ID(lib,name));f0.SetReference(ref);f0.SetValue(a['value']);board.Add(f0)
    f0.SetPosition(v(a['pcb'][0]+50,a['pcb'][1]+50));f0.SetOrientationDegrees(a['angle'])
    path=k.KIID_PATH();path.push_back(k.KIID(uid('root')));path.push_back(k.KIID(uid(ref)));f0.SetPath(path);f0.SetSheetname('/');f0.SetSheetfile(NAME+'.kicad_sch')
    f0.Value().SetVisible(False);f0.Reference().SetVisible(True);f0.Reference().SetTextSize(v(.8,.8));f0.Reference().SetTextThickness(k.FromMM(.12))
    
    if ref.startswith('H'):f0.Reference().SetVisible(False)
    for rn,rx,ry in [('C4',25,19.4),('C2',16.5,25),('C5',28.5,19.4),('J4',17.5,3),('Y1',9,29),('U2',10.5,21.7),
                     ('R11',17.2,7),('R9',32,7),('R10',26,21),('R2',20.5,25.2),('C9',16,28),('C10',19.5,28.5),
                     ('R14',8,20),('R15',13,20),('J3',2.2,17.5),('D1',5.5,18)]:
        if ref==rn:f0.Reference().SetPosition(v(50+rx,50+ry))
    f0.Reference().SetTextAngle(k.EDA_ANGLE(0,k.DEGREES_T))
    for pad in f0.Pads():
        if pad.GetNumber() in a['nets']:pad.SetNet(nets[a['nets'][pad.GetNumber()]])
    for key,val in [('LCSC',a['lcsc']),('Datasheet',a['source'])]:
        if val:f0.SetField(key,val);f0.GetField(key).SetVisible(False)
    a['pad_positions']={pad.GetNumber():[round(k.ToMM(pad.GetPosition().x)-50,4),round(k.ToMM(pad.GetPosition().y)-50,4)] for pad in f0.Pads()}
for a,b in [((50,50),(90,50)),((90,50),(90,86)),((90,86),(50,86)),((50,86),(50,50))]:line(board,a,b,k.Edge_Cuts,.05)
def text(s,x,y,layer=k.F_SilkS,size=.8):
    z=k.PCB_TEXT(board);z.SetText(s);z.SetPosition(v(50+x,50+y));z.SetTextSize(v(size,size));z.SetTextThickness(k.FromMM(.12));z.SetLayer(layer);board.Add(z)
text('MONO FM A2',10,2,size=1);text('3V3 ONLY',25,1,size=.8)
text('CODEC',5.5,4.8);text('CP',5.3,9);text('CN',5.3,11.54)
text('TO PA',37,3);text('AP',34.7,8);text('AN',34.7,10.54)
for s,x in [('3V3',20),('G',22.54),('SEL',25.08),('SDA',27.62),('SCL',30.16)]:text(s,x,5.1,size=.8)
text('ANT',3,19.5);text('GND',3,27)
text('0:CODEC 1:FM',27,35,size=.8)
settings=board.GetDesignSettings();settings.SetCopperLayerCount(2)
k.SaveBoard(str(OUT/(NAME+'.kicad_pcb')),board)
pro={'meta':{'filename':NAME+'.kicad_pro','version':1},'board':{'design_settings':{'rules':{
    'min_clearance':.127,'min_track_width':.15,'min_via_diameter':.6,'min_through_hole_diameter':.3,
    'min_hole_clearance':.25,'min_copper_edge_clearance':.3,'solder_mask_min_width':.075,'min_silk_clearance':.1},
    'defaults':{'solder_mask_clearance':.025,'solder_mask_min_width':.075}}},
    'net_settings':{'classes':[{'name':'Default','clearance':.127,'track_width':.2,'via_diameter':.6,'via_drill':.3,'microvia_diameter':.3,'microvia_drill':.1,'bus_width':12,'wire_width':6,'pcb_color':'rgba(0, 0, 0, 0.000)','schematic_color':'rgba(0, 0, 0, 0.000)'}], 'meta':{'version':4}}}
(OUT/(NAME+'.kicad_pro')).write_text(json.dumps(pro,indent=2)+'\n')
(OUT/'design-intent.json').write_text(json.dumps(intent,indent=2)+'\n')
with (OUT/'BOM.csv').open('w',newline='',encoding='utf-8-sig') as f0:
    w=csv.writer(f0);w.writerow(['Reference','Value','Footprint','LCSC','Note'])
    for ref,a in parts.items():
        if not a['footprint']:continue
        w.writerow([ref,a['value'],a['footprint'],a['lcsc'],'Header hand solder; or solder wires directly' if ref.startswith('J') else ''])
print('Integrated chip board placed: 40x36mm. Native ERC/DRC and routing required.')
