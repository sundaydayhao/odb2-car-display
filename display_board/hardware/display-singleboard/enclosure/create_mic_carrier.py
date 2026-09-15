"""Create the 8 mm microphone carrier. Run with KiCad's Python/pcbnew."""
from pathlib import Path
import argparse,json,math,shutil,sys,uuid
import pcbnew
ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,default=Path(__file__).parent/'microphone-carrier');ap.add_argument('--model-source',type=Path,required=True);ap.add_argument('--kiskill-root',type=Path,required=True);a=ap.parse_args()
sys.path.insert(0,str(a.kiskill_root));from kicad_lib.verify import Seg,Pin,verify
P=a.output;P.mkdir(parents=True,exist_ok=True);(P/'Microphone.pretty').mkdir(exist_ok=True);(P/'Microphone.3dshapes').mkdir(exist_ok=True)
modeldest=P/'Microphone.3dshapes/B4013AM422-042_nominal.step'
if a.model_source.resolve()!=modeldest.resolve():shutil.copy2(a.model_source,modeldest)
def uid(s):return str(uuid.uuid5(uuid.NAMESPACE_URL,'a7-mic-carrier:'+s))
def fx(n):return f'{n:.6f}'.rstrip('0').rstrip('.')
mic='''(footprint "B4013AM422_042_LAND_OFFICIAL" (version 20260206) (generator pcbnew) (layer "F.Cu")
 (property "Reference" "MIC1" (at 0 -2.5) (layer "F.Fab") (effects(font(size .7 .7)(thickness .12))))
 (property "Value" "B4013AM422-042" (at 0 2.5) (layer "F.Fab") (effects(font(size .6 .6)(thickness .1))))
 (attr smd)
 (fp_circle(center 0 0)(end 2 0)(stroke(width .1)(type default))(fill no)(layer "F.Fab"))
 (fp_circle(center 0 0)(end 2.1 0)(stroke(width .05)(type default))(fill no)(layer "F.CrtYd"))
 (pad "1" smd circle(at 0 0)(size .9 .9)(layers "F.Cu" "F.Mask" "F.Paste"))
 (pad "2" smd custom(at 0 1.125)(size .2 .2)(layers "F.Cu" "F.Mask")
  (options(clearance outline)(anchor circle))
  (primitives
   (gr_circle(center 0 -1.125)(end 1.125 -1.125)(width .45)(fill no))
   (gr_circle(center 0 -1.125)(end 1.8 -1.125)(width .2)(fill no))
   (gr_line(start 0 0)(end 0 .675)(width .2))
   (gr_line(start 0 -2.25)(end 0 -2.925)(width .2))
   (gr_line(start 1.125 -1.125)(end 1.8 -1.125)(width .2))
   (gr_line(start -1.125 -1.125)(end -1.8 -1.125)(width .2))))
'''
# Exact annular sector boundaries: three 0.8 mm-wide radial paste gaps.
for ri,ro in [(.9,1.35),(1.7,1.9)]:
 for k in range(3):
  low=math.radians(-90+120*k);high=low+math.radians(120)
  pts=[]
  for r,t0,t1 in [(ro,low+math.asin(.4/ro),high-math.asin(.4/ro)),(ri,high-math.asin(.4/ri),low+math.asin(.4/ri))]:
   for j in range(25):
    t=t0+(t1-t0)*j/24;pts.append(f'(xy {fx(r*math.cos(t))} {fx(r*math.sin(t))})')
  mic+='(fp_poly(pts '+''.join(pts)+')(stroke(width 0)(type default))(fill yes)(layer "F.Paste"))\n'
mic+='(model "${KIPRJMOD}/Microphone.3dshapes/B4013AM422-042_nominal.step"(offset(xyz 0 0 0))(scale(xyz 1 1 1))(rotate(xyz 0 0 0))))'
(P/'Microphone.pretty/B4013AM422_042_LAND_OFFICIAL.kicad_mod').write_text(mic)
wire='''(footprint "WirePair_28AWG" (version 20260206)(generator pcbnew)(layer "F.Cu")
 (property "Reference" "J1"(at 0 0)(layer "F.Fab")(effects(font(size .7 .7)(thickness .1))))
 (property "Value" "2_WIRE_TO_MAIN_J7"(at 0 1)(layer "F.Fab")(effects(font(size .7 .7)(thickness .1))))
 (attr through_hole)
 (pad "1" thru_hole circle(at -1.2 2.5)(size 1.2 1.2)(drill .6)(layers "*.Cu" "*.Mask"))
 (pad "2" thru_hole circle(at 1.2 2.5)(size 1.2 1.2)(drill .6)(layers "*.Cu" "*.Mask")))'''
(P/'Microphone.pretty/WirePair_28AWG.kicad_mod').write_text(wire)
(P/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "Microphone") (type "KiCad") (uri "${KIPRJMOD}/Microphone.pretty") (options "") (descr "A7 wired microphone daughterboard")))\n')

libmic='''(symbol "B4013AM422-042" (pin_names(offset 0)) (in_bom yes)(on_board yes)
 (property "Reference" "MIC"(at 0 6.35 0)(effects(font(size 1.27 1.27))))
 (property "Value" "B4013AM422-042"(at 0 -6.35 0)(effects(font(size 1.27 1.27))))
 (property "Footprint" "Microphone:B4013AM422_042_LAND_OFFICIAL"(at 0 0 0)(effects(font(size 1.27 1.27))(hide yes)))
 (symbol "B4013AM422-042_0_1"(circle(center 0 0)(radius 3.81)(stroke(width 0)(type default))(fill(type background)))
  (text "+"(at 0 1.27 0)(effects(font(size 1.27 1.27))))(text "-"(at 0 -1.27 0)(effects(font(size 1.27 1.27)))))
 (symbol "B4013AM422-042_1_1"
  (pin passive line(at 5.08 2.54 180)(length 2.54)(name "Output+"(effects(font(size 1.0 1.0))))(number "1"(effects(font(size 1 1)))))
  (pin passive line(at 5.08 -2.54 180)(length 2.54)(name "Return-"(effects(font(size 1.0 1.0))))(number "2"(effects(font(size 1 1)))))))'''
libj='''(symbol "WirePair" (pin_names(offset 0)) (in_bom yes)(on_board yes)
 (property "Reference" "J"(at 0 6.35 0)(effects(font(size 1.27 1.27))))
 (property "Value" "WirePair"(at 0 -6.35 0)(effects(font(size 1.27 1.27))))
 (property "Footprint" "Microphone:WirePair_28AWG"(at 0 0 0)(effects(font(size 1.27 1.27))(hide yes)))
 (symbol "WirePair_0_1"(rectangle(start -2.54 3.81)(end 2.54 -3.81)(stroke(width 0)(type default))(fill(type background))))
 (symbol "WirePair_1_1"
  (pin passive line(at -5.08 2.54 0)(length 2.54)(name "RED"(effects(font(size 1 1))))(number "1"(effects(font(size 1 1)))))
  (pin passive line(at -5.08 -2.54 0)(length 2.54)(name "BLACK"(effects(font(size 1 1))))(number "2"(effects(font(size 1 1)))))))'''
(P/'Microphone.kicad_sym').write_text('(kicad_symbol_lib(version 20241209)(generator "kicad_symbol_editor")\n'+libmic+'\n'+libj+')')
(P/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "Microphone") (type "KiCad") (uri "${KIPRJMOD}/Microphone.kicad_sym") (options "") (descr "Goertek two-wire microphone and wire solder pads")))')
root=uid('schematic');refs={'MIC1':uid('MIC1'),'J1':uid('J1')}
segs=[Seg('MICP',(81.28,48.26),(106.68,48.26)),Seg('MIC_RETURN',(81.28,53.34),(106.68,53.34))]
pins=[Pin(n,p,f'{r}.{i}') for n,y,i in [('MICP',48.26,1),('MIC_RETURN',53.34,2)] for r,p in [('MIC1',(81.28,y)),('J1',(106.68,y))]]
errors=verify(segs,pins,[]);assert not errors,errors
sch=f'(kicad_sch(version 20250114)(generator "eeschema")(uuid "{root}")(paper "A5")(lib_symbols\n'+libmic.replace('(symbol "B4013AM422-042"','(symbol "Microphone:B4013AM422-042"',1)+'\n'+libj.replace('(symbol "WirePair"','(symbol "Microphone:WirePair"',1)+')\n'
for ref,val,x,y,fp in [('MIC1','B4013AM422-042',76.2,50.8,'B4013AM422_042_LAND_OFFICIAL'),('J1','WirePair',111.76,50.8,'WirePair_28AWG')]:
 sch+=f'''(symbol(lib_id "Microphone:{val}")(at {x} {y} 0)(unit 1)(in_bom yes)(on_board yes)(dnp no)(uuid "{refs[ref]}")
 (property "Reference" "{ref}"(at {x} {y-7.62} 0)(effects(font(size 1.27 1.27))))
 (property "Value" "{val}"(at {x} {y+7.62} 0)(effects(font(size 1.27 1.27))))
 (property "Footprint" "Microphone:{fp}"(at {x} {y} 0)(effects(font(size 1.27 1.27))(hide yes)))
 (pin "1"(uuid "{uid(ref+'.1')}"))(pin "2"(uuid "{uid(ref+'.2')}"))
 (instances(project "microphone_carrier"(path "/{root}"(reference "{ref}")(unit 1)))))\n'''
for s in segs:
 sch+=f'(wire(pts(xy {s.a[0]} {s.a[1]})(xy {s.b[0]} {s.b[1]}))(stroke(width 0)(type default))(uuid "{uid(s.net)}"))\n'
 sch+=f'(label "{s.net}"(at 93.98 {s.a[1]} 0)(effects(font(size 1 1))(justify left bottom))(uuid "{uid(s.net+"label")}"))\n'
for i,t in enumerate(['8 mm carrier / 0.8 mm PCB; acoustic face on F side','J1.1 RED -> main J7.1 MICP (biased signal)','J1.2 BLACK -> main J7.2 dedicated return','Do not connect to direct 3V3 or chassis ground','Center via MUST be resin filled + copper capped','Goertek original land and 0.10 mm stencil; no washing']):
 sch+=f'(text "{t}"(at 76.2 {73.66+i*5.08} 0)(effects(font(size 1.27 1.27))(justify left))(uuid "{uid(t)}"))\n'
sch+=f'(sheet_instances(path "/"(page "1")))(embedded_fonts no))'
(P/'microphone_carrier.kicad_sch').write_text(sch)

b=pcbnew.BOARD();b.GetDesignSettings().SetBoardThickness(pcbnew.FromMM(.8))
nets={}
for i,n in enumerate(['/MICP','/MIC_RETURN'],1):nets[i]=pcbnew.NETINFO_ITEM(b,n,i);b.Add(nets[i])
for ref,fp in [('MIC1','B4013AM422_042_LAND_OFFICIAL'),('J1','WirePair_28AWG')]:
 f=pcbnew.FootprintLoad(str(P/'Microphone.pretty'),fp);f.SetFPID(pcbnew.LIB_ID('Microphone',fp));f.SetReference(ref);f.SetValue('B4013AM422-042' if ref=='MIC1' else 'WirePair');f.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(10),pcbnew.FromMM(10)));f.SetPath(pcbnew.KIID_PATH('/'+root+'/'+refs[ref]));b.Add(f)
 for pad in f.Pads():pad.SetNet(nets[int(pad.GetNumber())])
for i,x in enumerate([7.2,12.8],1):
 f=pcbnew.FOOTPRINT(b);f.SetReference('H'+str(i));f.SetValue('Wire strain relief 1mm');f.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x),pcbnew.FromMM(10.6)));f.SetAttributes(pcbnew.FP_BOARD_ONLY|pcbnew.FP_EXCLUDE_FROM_POS_FILES|pcbnew.FP_EXCLUDE_FROM_BOM);f.Reference().SetVisible(False);f.Value().SetVisible(False);b.Add(f)
 p=pcbnew.PAD(f);p.SetAttribute(pcbnew.PAD_ATTRIB_NPTH);p.SetShape(pcbnew.PAD_SHAPE_CIRCLE);p.SetSize(pcbnew.VECTOR2I(pcbnew.FromMM(1),pcbnew.FromMM(1)));p.SetDrillSize(pcbnew.VECTOR2I(pcbnew.FromMM(1),pcbnew.FromMM(1)));p.SetLayerSet(pcbnew.LSET.AllCuMask());p.SetPosition(f.GetPosition());f.Add(p)
sh=pcbnew.PCB_SHAPE();sh.SetShape(pcbnew.SHAPE_T_CIRCLE);sh.SetCenter(pcbnew.VECTOR2I(pcbnew.FromMM(10),pcbnew.FromMM(10)));sh.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(14),pcbnew.FromMM(10)));sh.SetWidth(pcbnew.FromMM(.05));sh.SetLayer(pcbnew.Edge_Cuts);b.Add(sh)
v=pcbnew.PCB_VIA(b);v.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(10),pcbnew.FromMM(10)));v.SetWidth(pcbnew.FromMM(.5));v.SetDrill(pcbnew.FromMM(.2));v.SetViaType(pcbnew.VIATYPE_THROUGH);v.SetLayerPair(pcbnew.F_Cu,pcbnew.B_Cu);v.SetNet(nets[1]);b.Add(v)
for net,layer,coords in [(1,pcbnew.B_Cu,[(10,10),(8.8,11.2),(8.8,12.5)]),(2,pcbnew.F_Cu,[(10,11.125),(11.2,12.325),(11.2,12.5)])]:
 for s,e in zip(coords,coords[1:]):
  t=pcbnew.PCB_TRACK(b);t.SetStart(pcbnew.VECTOR2I(*[pcbnew.FromMM(q) for q in s]));t.SetEnd(pcbnew.VECTOR2I(*[pcbnew.FromMM(q) for q in e]));t.SetWidth(pcbnew.FromMM(.2));t.SetLayer(layer);t.SetNet(nets[net]);b.Add(t)
for tx,x,y in [('+',8.8,11.2),('-',11.2,11.2),('MIC',10,7.4)]:
 t=pcbnew.PCB_TEXT(b);t.SetText(tx);t.SetTextPos(pcbnew.VECTOR2I(pcbnew.FromMM(x),pcbnew.FromMM(y)));t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(.8),pcbnew.FromMM(.8)));t.SetTextThickness(pcbnew.FromMM(.12));t.SetLayer(pcbnew.B_SilkS);t.SetMirrored(True);b.Add(t)
pcbnew.SaveBoard(str(P/'microphone_carrier.kicad_pcb'),b)
pro={'meta':{'filename':'microphone_carrier.kicad_pro','version':1},'board':{'design_settings':{'rules':{'min_clearance':.15,'min_copper_edge_clearance':.25,'min_through_hole_diameter':.2,'min_via_annular_width':.1},'defaults':{}}},'text_variables':{'MIC_PROCESS':'CENTER VIA RESIN FILLED AND COPPER CAPPED / 0.10 MM STENCIL'}}
(P/'microphone_carrier.kicad_pro').write_text(json.dumps(pro,indent=2))
(P/'geometry-check.json').write_text(json.dumps({'schematic_geometry_errors':errors,'mic_copper_land_mm':{'center_diameter':.9,'inner_ring_inner':1.8,'inner_ring_outer':2.7,'outer_ring_inner':3.4,'outer_ring_outer':3.8,'four_bridges_width':.2},'paste':{'thickness_mm':.1,'radial_gaps':3,'gap_mm':.8},'pcb':{'diameter_mm':8,'thickness_mm':.8,'center_via_drill_mm':.2,'required_process':'Resin filled and copper capped, planarized to prevent paste loss/wicking under MIC1'}} ,indent=2))
print(P)
