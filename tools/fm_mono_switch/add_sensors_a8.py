"""A7 -> A8: DHT20 horizontal + VEML7700-TT on the host's separate I2C bus.

Run once with native KiCad Python. Existing FM routing is retained and locked.
Requires the reviewed datasheets and C1850416 model in build/fm-mono-switch.
"""
from pathlib import Path
import copy, json, re, shutil, sys, uuid
import pcbnew as k
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp, ops, build
from kicad_lib.verify import Seg, Pin, verify

ROOT = Path(__file__).resolve().parents[2]
P = ROOT/'display_board/hardware/fm-mono-switch'
W = ROOT/'build/fm-mono-switch'
Q, S = sexp.QStr.of, sexp.Sym
V = lambda x,y: k.VECTOR2I(k.FromMM(x), k.FromMM(y))
pt = lambda x,y: (round(x*1.27,3),round(y*1.27,3))
catalog = json.loads((P/'procurement.json').read_text(encoding='utf-8'))
assert catalog['revision'] == 'A7', 'Migration only accepts A7; never overwrite later work.'
for name in ['fm_mono_switch.kicad_sch','fm_mono_switch.kicad_pcb','design-intent.json','Mono.kicad_sym','procurement.json']:
    shutil.copyfile(P/name, W/('a7-'+name))
shutil.copyfile(W/'erc-final.json', W/'erc-a7-baseline.json')
for name in ['DHT20','VEML7700']:
    shutil.copyfile(W/(name+'.pdf'), P/'reference'/(name+'.pdf'))

# DHT20 lead order is the face-up front view in Aosong V1.3 (2024.09).
# Body is north of the pad row; bending never changes the 1..4 order.
fp = ['(footprint "DHT20_Horizontal" (version 20241229) (generator "pcbnew") (layer "F.Cu") (attr through_hole)',
      '(descr "Aosong DHT20 lying face-up; 12.6x16.1x5.8mm body; formed leads; verify first assembly")',
      '(property "Reference" "REF**" (at 0 -19 0) (layer "F.SilkS") (effects (font (size 0.8 0.8) (thickness 0.12))))',
      '(property "Value" "DHT20" (at 0 -9.55 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))']
for layer,width,bounds in [('F.Fab',.1,(-6.3,-17.6,6.3,-1.5)),('F.SilkS',.12,(-6.42,-17.72,6.42,-1.38)),('F.CrtYd',.05,(-6.55,-17.85,6.55,1.1))]:
    x1,y1,x2,y2=bounds
    fp.append(f'(fp_rect (start {x1} {y1}) (end {x2} {y2}) (stroke (width {width}) (type default)) (fill none) (layer "{layer}"))')
for n,x in enumerate([-3.81,-1.27,1.27,3.81],1):
    fp.append(f'(pad "{n}" thru_hole {"rect" if n==1 else "circle"} (at {x} 0) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))')
fp.append('(model "${KIPRJMOD}/Mono.3dshapes/DHT20-horizontal-envelope.wrl" (offset (xyz 0 0 0)) (scale (xyz 1 1 1)) (rotate (xyz 0 0 0)))')
fp.append(')')
(P/'Mono.pretty/DHT20_Horizontal.kicad_mod').write_text('\n'.join(fp)+'\n',encoding='utf-8')

# Explicitly labelled mechanical envelope, not a claimed manufacturer STEP model.
model_dir=P/'Mono.3dshapes'; model_dir.mkdir(exist_ok=True)
vrml=['#VRML V2.0 utf8','# Simplified DHT20 horizontal mechanical envelope; mm / 2.54.']
def box(center,size,color):
    c=' '.join(f'{x/2.54:.6f}' for x in center);s=' '.join(f'{x/2.54:.6f}' for x in size)
    vrml.append(f'Transform {{ translation {c} children [ Shape {{ appearance Appearance {{ material Material {{ diffuseColor {color} }} }} geometry Box {{ size {s} }} }} ] }}')
box((0,9.55,3.4),(12.6,16.1,5.8),'0.85 0.86 0.82')
for x in [-4.2,-1.4,1.4,4.2]:
    for y in [4.2,7,9.8,12.6,15.4]:box((x,y,6.305),(1.5,1.5,.01),'0.12 0.13 0.12')
for x in [-3.81,-1.27,1.27,3.81]:
    box((x,.75,3.75),(.5,1.5,.3),'0.65 0.65 0.65')
    box((x,0,1.175),(.5,.3,5.15),'0.65 0.65 0.65')
(model_dir/'DHT20-horizontal-envelope.wrl').write_text('\n'.join(vrml)+'\n')

# Imported VEML7700-TT land pattern: normalize exact 1.27mm pitch from Rev1.8 p10.
src=W/'sensor-libs/libs'
mod=sexp.load_file(str(src/'easyeda2kicad.pretty/SENSOR-SMD_EML7700-TT.kicad_mod'))
mod[1]=Q('VEML7700_TT')
for pad,x in zip(sorted(sexp.find_all(mod,'pad'),key=lambda a:str(a[1])),[-1.905,-.635,.635,1.905]):
    sexp.find(pad,'at')[1:]=[S(str(x)),S('0'),S('0')]
for node in list(mod):
    if not isinstance(node,list):continue
    layer=sexp.find(node,'layer')
    if layer and str(layer[1]) in ['F.CrtYd','Cmts.User']:mod.remove(node)
    elif layer and str(layer[1])=='F.SilkS':
        width=sexp.find(node,'width')
        if width:width[1]=S('.12')
for layer,width,bounds in [('F.CrtYd',.05,(-3.65,-2.8,3.65,1.05)),('F.Fab',.1,(-3.4,-2.54,3.4,.46))]:
    x1,y1,x2,y2=bounds
    for a,b in [((x1,y1),(x2,y1)),((x2,y1),(x2,y2)),((x2,y2),(x1,y2)),((x1,y2),(x1,y1))]:
        mod.append(sexp.parse(f'(fp_line (start {a[0]} {a[1]}) (end {b[0]} {b[1]}) (layer {layer}) (width {width}))'))
for m in sexp.find_all(mod,'model'):m[1]=Q('${KIPRJMOD}/Mono.3dshapes/VEML7700-TT.wrl')
shutil.copyfile(src/'easyeda2kicad.3dshapes/SENSOR-SMD_EML7700-TT.wrl',model_dir/'VEML7700-TT.wrl')
sexp.save_file(str(P/'Mono.pretty/VEML7700_TT.kicad_mod'),mod)

catalog['revision']='A8'
catalog['price_basis']='Public LCSC USD quantity tiers; A8 sensors checked 2026-09-24. Other prices retained. No fixed purchase quantity; mainland account quote and immediate stock must be reconfirmed.'
for g in catalog['groups']:
    if g['refs']==['C4']:
        g['refs']+=['C11','C12'];g['decision']+=' C11/C12为新增传感器就近100nF去耦，每套合计3颗。'
    if g.get('optional'):
        g['refs'].append('J5');g['decision']='排针默认不装，直接飞线；可选4个1×2P和1个1×5P，2.54mm。J5为新增GPIO7/8传感器总线，不与FM的GPIO2/3短接。'
for ref,mpn,maker,code,package,price,stock,decision in [
    ('U4','DHT20','Aosong / 奥松','C3012627','SIP-4 卧式弯脚',.749,1836,'每套1颗；2.54mm通孔弯脚卧装，感应孔朝上。1 VDD/2 SDA/3 GND/4 SCL；3.3V、I2C1地址0x38。依靠主板R98/R109上拉，不重复加外部上拉。感应孔不可涂胶/三防漆；焊后安装，预留空气流通。'),
    ('U5','VEML7700-TT','Vishay / 威世','C1850416','SMD-4 顶视型',.7747,4468,'每套1颗；指定-TT顶视包装、四个1.27mm间距外露焊端，非QFN。1 SCL/2 VDD/3 GND/4 SDA，3.3V、I2C1地址0x10。不可接FM的I2C0，地址相同；外壳留透光窗，避免屏幕漏光。')]:
    catalog['groups'].insert(5,dict(refs=[ref],mpn=mpn,value=mpn,manufacturer=maker,lcsc=code,package=package,unit_usd=price,tier=100,moq_multiple=1,stock_observed=stock,purchase_url=f'https://www.lcsc.com/product-detail/{code}.html',datasheet='${KIPRJMOD}/reference/'+('DHT20' if ref=='U4' else 'VEML7700')+'.pdf',decision=decision+' 公开库存/价格未锁定，支持1颗起购；100颗只是参考报价阶梯。'))
choices={r:g for g in catalog['groups'] for r in g['refs']}
sch=sexp.load_file(str(P/'fm_mono_switch.kicad_sch'))
lib=sexp.load_file(str(P/'Mono.kicad_sym'))
intent=json.loads((P/'design-intent.json').read_text(encoding='utf-8'))
defs={str(d[1]):d for d in sexp.find_all(lib,'symbol')}
def sensor_def(name,pins):
    d=sexp.parse(f'(symbol "{name}" (pin_names (offset 0.635)) (in_bom yes) (on_board yes) (property "Reference" "U" (at 0 12.7 0) (effects (font (size 1.27 1.27)))) (property "Value" "{name}" (at 0 10.16 0) (effects (font (size 1.27 1.27)))) (symbol "{name}_0_1" (rectangle (start -10.16 7.62) (end 10.16 -7.62) (stroke (width 0.254) (type default)) (fill (type background)))) (symbol "{name}_1_1"))')
    body=list(sexp.find_all(d,'symbol'))[-1]
    for n,label,x,y,a,typ in pins:
        body.append(sexp.parse(f'(pin {typ} line (at {x} {y} {a}) (length 5.08) (name "{label}" (effects (font (size 1.016 1.016)))) (number "{n}" (effects (font (size 1.016 1.016)))))'))
    lib.append(copy.deepcopy(d));defs[name]=d
sensor_def('DHT20',[(1,'VDD',0,12.7,270,'power_in'),(2,'SDA',-15.24,2.54,0,'bidirectional'),(3,'GND',0,-12.7,90,'power_in'),(4,'SCL',-15.24,-2.54,0,'bidirectional')])
sensor_def('VEML7700',[(2,'VDD',0,12.7,270,'power_in'),(4,'SDA',-15.24,2.54,0,'bidirectional'),(3,'GND',0,-12.7,90,'power_in'),(1,'SCL',-15.24,-2.54,0,'input')])
segs=[];pins=[];added={}
specs=[('U4','DHT20',(305,258),{1:'+3V3_IN',2:'SENSOR_SDA',3:'GND',4:'SENSOR_SCL'},'Mono:DHT20_Horizontal',(10,55.5),0),
       ('U5','VEML7700',(401,258),{1:'SENSOR_SCL',2:'+3V3_IN',3:'GND',4:'SENSOR_SDA'},'Mono:VEML7700_TT',(26,44),0),
       ('C11','C',(339,268),{1:'+3V3_IN',2:'GND'},'Capacitor_SMD:C_0603_1608Metric',(2,55.5),90),
       ('C12','C',(435,268),{1:'+3V3_IN',2:'GND'},'Capacitor_SMD:C_0603_1608Metric',(26,47),0),
       ('J5','J2',(266,242),{1:'SENSOR_SDA',2:'SENSOR_SCL'},'Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical',(37,40),0)]
for ref,kind,xy,nets,footprint,pos,angle in specs:
    d=copy.deepcopy(defs[kind]);d[1]=Q('Mono:'+kind);at=pt(*xy);c=choices[ref]
    value=c.get('value', 'SENS: GPIO7 SDA / GPIO8 SCL' if ref=='J5' else c['mpn'])
    ops.place(sch,d,at,0,ref,value,[str(n) for n in nets],project='fm_mono_switch')
    sym=next(s for s in sexp.find_all(sch,'symbol') if any(str(p[1])=='Reference' and str(p[2])==ref for p in sexp.find_all(s,'property')))
    for name,val in [('Footprint',footprint),('Datasheet',c.get('datasheet','')),('MPN',c['mpn']),('Manufacturer',c['manufacturer']),('LCSC',c['lcsc'])]:ops.set_prop(sch,ref,name,val)
    for prop in sexp.find_all(sym,'property'):
        nam=str(prop[1]);a=sexp.find(prop,'at')
        if nam in ['Reference','Value']:
            dx=3.81 if kind=='C' else (12.7 if ref in ['U4','U5'] else 0);dy=(-1.27 if nam=='Reference' else 1.27) if kind=='C' else (-17.78 if nam=='Reference' else -15.24)
            a[1:]=[S(str(round(at[0]+dx,3))),S(str(round(at[1]+dy,3))),S('0')]
            if kind=='C':sexp.find(prop,'effects').append([S('justify'),S('left')])
        else:
            eff=sexp.find(prop,'effects')
            if eff is not None and sexp.find(eff,'hide') is None:eff.append([S('hide'),S('yes')])
    for sub in sexp.find_all(d,'symbol'):
        for pin in sexp.find_all(sub,'pin'):
            n=str(sexp.find(pin,'number')[1]);u,v,a=map(float,sexp.atoms(sexp.find(pin,'at')))
            point=(round(at[0]+u,3),round(at[1]-v,3));net=nets[int(n)]
            dx,dy={0:(-7.62,0),180:(7.62,0),90:(0,7.62),270:(0,-7.62)}[int(a)]
            if net in ['SENSOR_SDA','SENSOR_SCL'] and dx<0:dx=-12.7
            end=(round(point[0]+dx,3),round(point[1]+dy,3))
            ops.add_wire(sch,point,end);ops.add_label(sch,net,end);segs.append(Seg(net,point,end));pins.append(Pin(net,point,ref+'.'+n))
    intent['parts'][ref]=dict(kind=kind,value=value,nets={str(n):v for n,v in nets.items()},footprint=footprint,pcb=list(pos),angle=angle,mpn=c['mpn'],manufacturer=c['manufacturer'],lcsc=c['lcsc'],source=c.get('datasheet',''))
    added[ref]=str(sexp.find(sym,'uuid')[1])
for text,x,y in [('ENVIRONMENT SENSORS: separate 3.3V host I2C1; never bridge to FM I2C0.',278,226),('J5.1 -> P4 J1.3 / GPIO7 SDA; J5.2 -> P4 J1.5 / GPIO8 SCL.',278,287),('Host R98/R109: 2.2k pull-ups. DHT20 0x38; VEML7700 0x10 (7-bit).',278,292),('DHT20 lies face-up; bend leads without swapping pins. Keep vents/window clear.',278,297)]:
    sch.append(sexp.parse(f'(text {json.dumps(text)} (at {pt(x,y)[0]} {pt(x,y)[1]} 0) (effects (font (size 1.27 1.27)) (justify left top)) (uuid "{uuid.uuid4()}"))'))
assert not verify(segs,pins,[],False)
intent['geometry']['pins']+=len(pins);intent['geometry']['segments']+=len(segs)
intent['sensor_geometry']={'violations':0,'pins':len(pins),'segments':len(segs)}
intent['board_size_mm']=[40,58]
intent['sensors']={'bus':'host I2C1 GPIO7/8 at J5, distinct from FM GPIO2/3','DHT20':{'address_7bit':56,'mount':'horizontal, sensing face up'},'VEML7700-TT':{'address_7bit':16,'mount':'top view'},'supply':'+3V3_IN','host_pullups':'R98/R109 2.2k to ESP_3V3','firmware_implemented':False}
sexp.save_file(str(P/'Mono.kicad_sym'),lib)
sexp.save_file(str(P/'fm_mono_switch.kicad_sch'),sch)

b=k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
for net in ['/SENSOR_SDA','/SENSOR_SCL']:b.Add(k.NETINFO_ITEM(b,net))
nn={n.GetNetname():n for n in b.GetNetsByNetcode().values()}
for ref,_,_,_,fpname,pos,angle in specs:
    libname,name=fpname.split(':');folder=P/'Mono.pretty' if libname=='Mono' else Path('C:/Program Files/KiCad/10.0/share/kicad/footprints')/(libname+'.pretty')
    f=k.FootprintLoad(str(folder),name);assert f,fpname
    f.SetFPID(k.LIB_ID(libname,name));f.SetReference(ref);f.SetValue(intent['parts'][ref]['value']);b.Add(f)
    f.SetPosition(V(pos[0]+50,pos[1]+50));f.SetOrientationDegrees(angle)
    path=k.KIID_PATH();path.push_back(k.KIID(intent['root_uuid']));path.push_back(k.KIID(added[ref]));f.SetPath(path);f.SetSheetname('/');f.SetSheetfile('fm_mono_switch.kicad_sch')
    f.Value().SetVisible(False);f.Reference().SetTextSize(V(.8,.8));f.Reference().SetTextThickness(k.FromMM(.12));f.Reference().SetTextAngle(k.EDA_ANGLE(0,k.DEGREES_T))
    for pad in f.Pads():
        if pad.GetNumber():
            net=intent['parts'][ref]['nets'][pad.GetNumber()];pad.SetNet(nn[net if net=='GND' else '/'+net])
    intent['parts'][ref]['pad_positions']={p.GetNumber():[round(k.ToMM(p.GetPosition().x)-50,4),round(k.ToMM(p.GetPosition().y)-50,4)] for p in f.Pads() if p.GetNumber()}
    print(ref,intent['parts'][ref]['pad_positions'])
    if ref=='C11':f.Reference().SetPosition(V(52,102.7))
    if ref=='C12':f.Reference().SetPosition(V(76,99))
    if ref=='J5':f.Reference().SetPosition(V(87,87))
for draw in b.GetDrawings():
    if draw.GetLayer()==k.Edge_Cuts:
        for get,setter in [(draw.GetStart,draw.SetStart),(draw.GetEnd,draw.SetEnd)]:
            p=get()
            if p.y==k.FromMM(86):setter(V(k.ToMM(p.x),108))
# Keep original planes intact in the FM region; extend into sensor area after routing.
for z in list(b.Zones()):b.Remove(z)
for t in b.GetTracks():t.SetLocked(True)
for s,x,y in [('SENS I2C1',31,36.7),('SDA7',34,40),('SCL8',34,42.54),('DHT20 FACE UP',10,57.3),('LIGHT WINDOW',26,51),('40x58 A8',30,56)]:
    a=k.PCB_TEXT(b);a.SetText(s);a.SetPosition(V(x+50,y+50));a.SetLayer(k.F_SilkS);a.SetTextSize(V(.8,.8));a.SetTextThickness(k.FromMM(.12));b.Add(a)
k.SaveBoard(str(P/'fm_mono_switch.kicad_pcb'),b)
(P/'procurement.json').write_text(json.dumps(catalog,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(P/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('A8 staged: 40x58mm, horizontal DHT20, top-view light sensor, separate I2C1.')
