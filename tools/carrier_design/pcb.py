"""Build a linked, unrouted 4-layer carrier placement study from the circuit source.

No autorouting is performed: incomplete release-hold circuits must not acquire
plausible-looking copper. Native DRC evidence is retained separately.
"""
from pathlib import Path
import copy,json,shutil,math,sys,subprocess,xml.etree.ElementTree as ET
import pcbnew as k
from cad import ROOT,OUT,LIBS,PROJECT,ROOT_ID,uid
from board_spec import d
print('PCB: circuit loaded',len(d.parts),flush=True)
d.write()
xml_path=ROOT/'build/carrier-design/netlist.xml'
subprocess.run([str(LIBS.parents[1]/'bin/kicad-cli.exe'),'sch','export','netlist','--format','kicadxml','--output',str(xml_path),str(OUT/(PROJECT+'.kicad_sch'))],check=True,capture_output=True)
xml=ET.parse(xml_path).getroot()
native_pads={(z.get('ref'),z.get('pin')):n.get('name') for n in xml.findall('./nets/net') for z in n.findall('node')}

FP=OUT/'Carrier.pretty';MD=OUT/'Carrier.3dshapes'
FORMAL=ROOT/'display_board/hardware/display-carrier'
FP.mkdir(parents=True,exist_ok=True);MD.mkdir(parents=True,exist_ok=True)
plugin=k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP)

def writefp(fp,name):
    fp.SetFPID(k.LIB_ID('Carrier',name));fp.SetValue(name)
    plugin.FootprintSave(str(FP.resolve()),fp)

def model(fp,path,offset=None):
    fp.Models().clear();m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+path.name
    if offset:m.m_Offset=k.VECTOR3D(*offset)
    fp.Add3DModel(m)

# Start from the manufacturer's NON-EP version. The C6 artifact's unnamed
# pad and the C5-only center pads must never enter the common carrier pattern.
imported=ROOT/'build/carrier-design/Imported.pretty'
core=k.FootprintLoad(str(imported.resolve()),'ESP32P4C5模组') if imported.exists() else k.FootprintLoad(str(FORMAL/'Carrier.pretty'),'OSPTEK_P4C5_P4C6_Compatible')
for p in core.Pads():
    assert p.GetNumber().isdigit() and 1<=int(p.GetNumber())<=88
    sz=p.GetSize();p.SetSize(k.VECTOR2I(sz.x,k.FromMM(.70)))
assert len(list(core.Pads()))==88
src=Path('C:/projects/osptek/esp32-p4c5-core-board/docs/P4模组3D模型.step')
if not src.exists():src=FORMAL/'Carrier.3dshapes/OSPTEK_P4C5.step'
shutil.copy2(src,MD/'OSPTEK_P4C5.step');model(core,MD/'OSPTEK_P4C5.step',(-12.5,-12.5,1.05))
writefp(core,'OSPTEK_P4C5_P4C6_Compatible')

imports=[('tps22990-part','TPS22990_DML10'),('tusb320-part','TUSB320_RWB12'),('modem-part','ML307A_GCLN_LGA94')]
for folder,name in imports:
    srcdir=ROOT/'build/carrier-design'/folder/'libs'
    if not srcdir.exists():
        fp=k.FootprintLoad(str(FORMAL/'Carrier.pretty'),name)
        source_model=FORMAL/'Carrier.3dshapes'/(name+'.step')
        if source_model.exists():shutil.copy2(source_model,MD/source_model.name)
        writefp(fp,name)
        continue
    path=next((srcdir/'easyeda2kicad.pretty').glob('*.kicad_mod'))
    fp=k.FootprintLoad(str(path.parent),path.stem)
    for m in fp.Models():
        stem=Path(m.m_Filename).stem
        original=srcdir/'easyeda2kicad.3dshapes'/(stem+'.step')
        if original.exists():
            shutil.copy2(original,MD/(name+'.step'));m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+name+'.step'
    writefp(fp,name)

# Manufacturer pad geometry is unchanged; only the projecting nose's silkscreen
# is shortened to stay on this board. Save an explicit project footprint so a
# future Update Footprints does not silently restore out-of-board silk.
usb=k.FootprintLoad(str(LIBS/'footprints/Connector_USB.pretty'),'USB_C_Receptacle_HRO_TYPE-C-31-M-12')
for graphic in usb.GraphicalItems():
    if graphic.GetLayer()!=k.F_SilkS or not isinstance(graphic,k.PCB_SHAPE):continue
    if graphic.GetShape()!=k.SHAPE_T_SEGMENT:continue
    a,z=graphic.GetStart(),graphic.GetEnd();limit=k.FromMM(3.3)
    if a.y>limit and z.y>limit:graphic.SetLayer(k.F_Fab)
    elif a.y>limit or z.y>limit:
        t=(limit-a.y)/(z.y-a.y);cut=k.VECTOR2I(round(a.x+t*(z.x-a.x)),limit)
        if a.y>limit:graphic.SetStart(cut)
        else:graphic.SetEnd(cut)
writefp(usb,'USB_C_HRO_TYPE_C_31_M_12_Edge')

(OUT/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "Carrier") (type "KiCad") (uri "${KIPRJMOD}/Carrier.pretty") (options "") (descr "R0 checked identity, see footprint release holds")))\n',encoding='utf-8')

def mm(x):return k.FromMM(x)
def vec(x,y):return k.VECTOR2I(mm(x),mm(y))
b=k.BOARD();b.SetCopperLayerCount(4)
print('PCB: libraries prepared',flush=True)
b.GetDesignSettings().SetBoardThickness(mm(1.6))
O=(50.,50.)

def pos(x,y):return vec(O[0]+x,O[1]+y)
def line(x1,y1,x2,y2,layer=k.Dwgs_User,width=.1):
    s=k.PCB_SHAPE();s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(pos(x1,y1));s.SetEnd(pos(x2,y2));s.SetLayer(layer);s.SetWidth(mm(width));b.Add(s)
def rect(x,y,w,h,layer=k.Dwgs_User):
    for a,z in [((x,y),(x+w,y)),((x+w,y),(x+w,y+h)),((x+w,y+h),(x,y+h)),((x,y+h),(x,y))]:line(*a,*z,layer)
def text(s,x,y,layer=k.Dwgs_User,size=1):
    t=k.PCB_TEXT(b);t.SetText(s);t.SetPosition(pos(x,y));t.SetLayer(layer);t.SetTextSize(vec(size,size));t.SetTextThickness(mm(.15));b.Add(t)

rect(0,0,145,85,k.Edge_Cuts)
text('R0 PLACEMENT STUDY - UNROUTED / NOT FOR FAB',72.5,89,size=1.8)
text('F = SCREEN SIDE   B = REAR COVER',72.5,-8,size=1.3)
# Explicit design reservations, not fabricated sockets / false connections.
for x,y,w,h,label in [(55.5,40,30,40,'LCD FPC HOLD\nF face / entry from bottom'),(87,60,20,18,'TOUCH HOLD\nF face / same entry'),(62,0,29,12,'CAMERA FPC HOLD\nF face / horizontal'),(96,61,19,17,'NS4814 / SPEAKER\nCIRCUIT HOLD')]:
    rect(x,y,w,h);text(label,x+w/2,y+h/2,size=.85)
rect(0,77,145,8);text('BASE / STAND ZONE - NO USER CONNECTOR',72.5,82,size=1)

netnames=sorted({n for p in d.parts for n in p['nets'].values() if n is not None}|set(native_pads.values()))
nets={}
for i,name in enumerate(netnames,1):
    n=k.NETINFO_ITEM(b,name,i);b.Add(n);nets[name]=n
occupied=[];audit=[]
def bb(fp):
    z=fp.GetBoundingBox(False,False)
    return (k.ToMM(z.GetX())-O[0],k.ToMM(z.GetY())-O[1],k.ToMM(z.GetRight())-O[0],k.ToMM(z.GetBottom())-O[1])
def overlap(a,z,gap=.35):return a[0]<z[2]+gap and a[2]>z[0]-gap and a[1]<z[3]+gap and a[3]>z[1]-gap

for i,(x,y) in enumerate([(4,4),(141,4),(4,81),(141,81)],1):
    fp=k.FootprintLoad(str(LIBS/'footprints/MountingHole.pretty'),'MountingHole_2.7mm_M2.5')
    fp.SetReference('H'+str(i));fp.SetValue('M2.5 NPTH');fp.SetPosition(pos(x,y));b.Add(fp)
    fp.SetAttributes(fp.GetAttributes()|k.FP_BOARD_ONLY|k.FP_EXCLUDE_FROM_BOM|k.FP_EXCLUDE_FROM_POS_FILES)
    fp.Reference().SetVisible(False);fp.Value().SetVisible(False)
    occupied.append((x-3.5,y-3.5,x+3.5,y+3.5))

loaded=[]
print('PCB: loading footprints',flush=True)
for p in d.parts:
    if not p['footprint']:continue
    lib,name=p['footprint'].split(':',1)
    folder=FP if lib=='Carrier' else LIBS/'footprints'/(lib+'.pretty')
    fp=k.FootprintLoad(str(folder),name)
    assert fp,(p['ref'],p['footprint'])
    fp.SetFPID(k.LIB_ID(lib,name));fp.SetReference(p['ref']);fp.SetValue(p['value'])
    fp.SetField('DesignStatus',p['status']);fp.GetField('DesignStatus').SetVisible(False)
    fp.SetField('Datasheet',p['source']);fp.GetField('Datasheet').SetVisible(False)
    if p['ref'].startswith('TP'):fp.SetAttributes(fp.GetAttributes()|k.FP_EXCLUDE_FROM_BOM|k.FP_EXCLUDE_FROM_POS_FILES)
    else:fp.SetAttributes(fp.GetAttributes() & ~(k.FP_EXCLUDE_FROM_BOM|k.FP_EXCLUDE_FROM_POS_FILES|k.FP_BOARD_ONLY))
    fp.SetSheetname('/'+d.sheets[p['sheet']]+'/');fp.SetSheetfile(p['sheet']+'.kicad_sch')
    path=k.KIID_PATH()
    for u in [ROOT_ID,uid('sheet:'+p['sheet']),uid('part:'+p['ref'])]:path.push_back(k.KIID(u))
    fp.SetPath(path)
    fp.SetPosition(pos(*p['pcb']))
    b.Add(fp)  # Flip consults the owning board's copper stack in KiCad 10.
    if p['side']=='B':fp.Flip(fp.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetOrientationDegrees(p['angle'])
    fp.SetDNP(p['dnp'])
    fp.Reference().SetVisible(False);fp.Value().SetVisible(False)
    actual={z.GetNumber() for z in fp.Pads() if z.GetNumber()}
    assert actual==set(p['nets']),('pad set mismatch',p['ref'],actual-set(p['nets']),set(p['nets'])-actual)
    for pad in fp.Pads():
        n=native_pads.get((p['ref'],pad.GetNumber()))
        expected=p['nets'].get(pad.GetNumber())
        if expected is not None:assert n==expected,(p['ref'],pad.GetNumber(),n,expected)
        if n:pad.SetNet(nets[n])
    loaded.append((p,fp))

# Keep geometry of major interfaces/modules, then stage local small parts without
# courtyard overlap. This is an initial placement, not a routed electrical layout.
fixed={'U100','U1000','J200','J300','J301','J500','SW101','SW102','SW310','J1000','J1010','J1011','J800','J810','L400','U400','U300','L300','U310','U410','L410','U1010','L1010','C1012','C1013','U800'}
overrides={'J200':(3.6,31,90),'SW310':(128,11,0),'J800':(8,66,90),'J810':(43,73,90),'J500':(135.5,56,270),'J1000':(137,29,270),
           'L400':(21,47,0),'U400':(31,47,0),'L410':(53,44,0),'U1010':(110,50,0),'L1010':(114,50,0),
           'L300':(40,15,0),'C1012':(116,43,90),'C1013':(123,43,90),'J1010':(94,12,0),'J1011':(101,17,0),'SW101':(101,3.5,180),'SW102':(112,3.5,180),'J300':(30,5.5,0)}
for p,fp in loaded:
    if p['ref'] in overrides:
        x,y,a=overrides[p['ref']];fp.SetPosition(pos(x,y));fp.SetOrientationDegrees(a)
for p,fp in loaded:
    if p['ref'] in fixed:
        z=bb(fp);occupied.append(z);audit.append({'ref':p['ref'],'bbox_mm':z,'fixed':True})

regions={
 '01_core':(53,13,96,60),'02_usb':(5,26,27,43),'03_usb_current':(5,7,27,26),
 '04_charger':(27,5,51,24),'05_system_switch':(36,25,55,39),'06_main_power':(9,36,55,60),
 '07_sd':(115,48,141,76),'08_modem':(93,17,131,50),'09_modem_power':(94,8,133,61),
 '10_sim_rf':(105,5,141,47),'11_codec':(5,59,32,77),'12_fm_audio':(30,57,66,77)
}
for p,fp in sorted(loaded,key=lambda q:-((bb(q[1])[2]-bb(q[1])[0])*(bb(q[1])[3]-bb(q[1])[1]))):
    if p['ref'] in fixed:continue
    area=regions[p['sheet']];x0,y0=p['pcb'];chosen=None
    candidates=[(x0,y0)]
    for radius in range(1,40):
        r=radius*.6
        candidates.extend([(x0+dx,y0+dy) for dx,dy in [(r,0),(-r,0),(0,r),(0,-r),(r,r),(-r,r),(r,-r),(-r,-r)]])
    candidates.extend((x*.6,y*.6) for y in range(math.ceil(area[1]/.6),math.floor(area[3]/.6)) for x in range(math.ceil(area[0]/.6),math.floor(area[2]/.6)))
    for x,y in candidates:
        fp.SetPosition(pos(x,y));z=bb(fp)
        if z[0]<area[0] or z[1]<area[1] or z[2]>area[2] or z[3]>area[3]:continue
        if any(overlap(z,q) for q in occupied):continue
        chosen=(x,y,z);break
    if chosen is None:raise RuntimeError(('No collision-free position',p['ref'],area))
    occupied.append(chosen[2]);audit.append({'ref':p['ref'],'bbox_mm':chosen[2],'fixed':False,'position_mm':chosen[:2]})

# Serialize every pad's actual net and identity; later compare to native SCH XML.
padmap={fp.GetReference():{'path':fp.GetPath().AsString(),'pads':{p.GetNumber():p.GetNetname() for p in fp.Pads() if p.GetNumber()}} for _,fp in loaded}
(OUT/'pcb-pad-audit.json').write_text(json.dumps(padmap,indent=2),encoding='utf-8')
(OUT/'placement-audit.json').write_text(json.dumps(audit,indent=2),encoding='utf-8')
filename=OUT/(PROJECT+'.kicad_pcb');k.SaveBoard(str(filename),b)

default={'bus_width':12,'clearance':.127,'diff_pair_gap':.15,'diff_pair_via_gap':.25,'diff_pair_width':.15,'line_style':0,'microvia_diameter':.3,'microvia_drill':.1,'name':'Default','pcb_color':'rgba(0, 0, 0, 0.000)','priority':2147483647,'schematic_color':'rgba(0, 0, 0, 0.000)','track_width':.2,'tuning_profile':'','via_diameter':.6,'via_drill':.3,'wire_width':6}
classes=[default]
for name,w,g in [('MIPI_100R_STACKUP_HOLD',.15,.15),('USB_90R_STACKUP_HOLD',.18,.15),('POWER_REQUIRES_POLYGON',1.5,.25),('RF_50R_STACKUP_HOLD',.3,.2)]:
    n=copy.deepcopy(default);n.update(name=name,track_width=w,diff_pair_width=w,diff_pair_gap=g);classes.append(n)
patterns=[]
for name,pats in [(classes[1]['name'],['DSI_*','CSI_*']),(classes[2]['name'],['MODEM_USB_P','MODEM_USB_N','UART_USB_P','UART_USB_N']),(classes[3]['name'],['BAT_CONNECTOR','BAT_PROTECTED','SYS_ALWAYS','SYS_SWITCHED','+5V','MODEM_3V8']),(classes[4]['name'],['LTE_RF','LTE_ANT','GNSS_RF','GNSS_ANT'])]:
    patterns.extend({'netclass':name,'pattern':p} for p in pats)
pro={'meta':{'filename':PROJECT+'.kicad_pro','version':1},'board':{'design_settings':{'rules':{'min_clearance':.127,'min_track_width':.127,'min_via_diameter':.5,'min_through_hole_diameter':.25,'min_copper_edge_clearance':.3}}},'schematic':{},'net_settings':{'classes':classes,'meta':{'version':5},'net_colors':None,'netclass_assignments':None,'netclass_patterns':patterns}}
(OUT/(PROJECT+'.kicad_pro')).write_text(json.dumps(pro,indent=2),encoding='utf-8')
print('Wrote',filename,'with',len(loaded),'linked circuit footprints; no tracks; 4 mounting holes.')
