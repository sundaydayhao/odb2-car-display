"""Generate a mechanical trial ONLY, assuming coaxial, straight OBD solder tails.

SAE J1962 defines the mating grid, not a universal PCB footprint. No electronics
are copied into this trial board. Keep the parent project's J1 release gate.
"""
from pathlib import Path
import json, math
import pcbnew as k

ROOT=Path(__file__).resolve().parent
NAME='OBD_J1962_StraightTail_TRIAL_4x8p6'
board=k.BOARD()
def v(x,y):return k.VECTOR2I(round(x*1_000_000),round(y*1_000_000))
def segment(parent,a,b,layer=k.Dwgs_User,width=.1):
    item=k.PCB_SHAPE(parent);item.SetShape(k.SHAPE_T_SEGMENT);item.SetStart(v(*a));item.SetEnd(v(*b));item.SetLayer(layer);item.SetWidth(k.FromMM(width));parent.Add(item)
def note(parent,text,x,y,size=.8,layer=k.Dwgs_User):
    item=k.PCB_TEXT(parent);item.SetText(text);item.SetPosition(v(x,y));item.SetTextSize(v(size,size));item.SetTextThickness(k.FromMM(.12));item.SetLayer(layer);parent.Add(item)

points=[(n,105.25+((n-1)%8)*4,104 if n<=8 else 112.6) for n in range(1,17)]
# Pre-save geometry gate: dimensions are defined in mm, numbering is unique.
assert {n for n,x,y in points}==set(range(1,17))
for n,x,y in points:
    assert 100+.25+1.6 < x < 138.5-.25-1.6
    assert 100+.25+1 < y < 116.6-.25-1
assert math.isclose(points[7][1]-points[0][1],28)
assert math.isclose(points[8][2]-points[0][2],8.6)

for a,b in [((100,100),(138.5,100)),((138.5,100),(138.5,116.6)),((138.5,116.6),(100,116.6)),((100,116.6),(100,100))]:segment(board,a,b,k.Edge_Cuts,.05)
fp=k.FOOTPRINT(board);fp.SetReference('J1');fp.SetValue('STRAIGHT TAIL TRIAL - VERIFY ACTUAL PLUG');fp.SetFPID(k.LIB_ID('OBD_Trial',NAME));fp.SetAttributes(k.FP_THROUGH_HOLE)
fp.Reference().SetVisible(False);fp.Value().SetVisible(False);board.Add(fp)
layers=k.LSET.AllCuMask();layers.AddLayer(k.F_Mask);layers.AddLayer(k.B_Mask)
for n,x,y in points:
    pad=k.PAD(fp);pad.SetNumber(str(n));pad.SetAttribute(k.PAD_ATTRIB_PTH)
    pad.SetShape(k.PAD_SHAPE_RECT if n==1 else k.PAD_SHAPE_OVAL);pad.SetSize(v(3.2,2))
    pad.SetDrillShape(k.PAD_DRILL_SHAPE_OBLONG);pad.SetDrillSize(v(2.4,1.2));pad.SetLayerSet(layers)
    pad.SetPosition(v(x-119.25,y-108.3));fp.Add(pad)
for a,b in [((-16,-5.6),(16,-5.6)),((16,-5.6),(16,5.6)),((16,5.6),(-16,5.6)),((-16,5.6),(-16,-5.6))]:segment(fp,a,b,k.F_CrtYd,.05)
library=ROOT/'OBD_Trial.pretty';library.mkdir(exist_ok=True)
k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(library),fp)
assert (library/(NAME+'.kicad_mod')).exists()
(ROOT/'fp-lib-table').write_text('(fp_lib_table\n (lib (name "OBD_Trial")(type "KiCad")(uri "${KIPRJMOD}/OBD_Trial.pretty")(options "")(descr "Straight-tail mechanical trial, not validated for MINI ELM327"))\n)\n')
fp.SetPosition(v(119.25,108.3))
note(board,'38.5 x 16.6 mm / STRAIGHT-TAIL MECHANICAL TRIAL',119.25,96.5,1)
note(board,'F.Cu: plug REAR view / wide edge UP',119.25,98.3,.7)
for n,x,y in points:note(board,str(n),x,101.7 if n<=8 else 114.9,.8,k.F_SilkS)
note(board,'TRIAL ONLY / NO ELECTRONICS',119.25,107.6,.9,k.F_SilkS)
note(board,'PITCH 4.00 / ROWS 8.60',119.25,109.2,.8,k.F_SilkS)
note(board,'16 plated slots 2.40 x 1.20 mm: ENGINEERING TRIAL VALUE',119.25,119,.8)
note(board,'Assumes solder tails follow mating grid. Actual MINI plug NOT verified.',119.25,120.5,.75)
note(board,'4=chassis  5=signal GND  6=CAN_H  14=CAN_L  16=VBAT',119.25,122,.75)
assert len(list(fp.Pads()))==16
k.SaveBoard(str(ROOT/'obd_straight_pin_trial.kicad_pcb'),board,True)
loaded=k.LoadBoard(str(ROOT/'obd_straight_pin_trial.kicad_pcb'))
actual={int(p.GetNumber()):(k.ToMM(p.GetPosition().x),k.ToMM(p.GetPosition().y)) for f in loaded.GetFootprints() for p in f.Pads()}
assert all(math.isclose(actual[n][0],x) and math.isclose(actual[n][1],y) for n,x,y in points)
out=ROOT/'build';out.mkdir(exist_ok=True)
(out/'geometry.json').write_text(json.dumps({'pads':16,'board_mm':[38.5,16.6],'pitch_mm':4,'row_spacing_mm':8.6,'serialized_coordinates':'PASS','actual_plug_verified':False,'collector_electronics_present':False},indent=2))
print('Geometry and serialized pin coordinates PASS; actual plug fit UNVERIFIED.')
