"""Local, electrically usable OBD straight-tail footprint; mechanical fit assumed.

No housing rests on the PCB: per-contact courtyards describe the soldered metal
tails on BOTH faces. Electronics can occupy the region between the two rows.
"""
from pathlib import Path
import pcbnew as k

ROOT=Path(__file__).resolve().parent
NAME='OBD_StraightTail_16P_4x8p6_Assumed'
def v(x,y):return k.VECTOR2I(round(x*1_000_000),round(y*1_000_000))
def generate():
    fp=k.FOOTPRINT(None);fp.SetReference('J1');fp.SetValue('OBD straight tails: FIT CHECK')
    fp.SetFPID(k.LIB_ID('Node',NAME));fp.SetAttributes(k.FP_THROUGH_HOLE)
    fp.Reference().SetVisible(False);fp.Value().SetVisible(False)
    layers=k.LSET.AllCuMask();layers.AddLayer(k.F_Mask);layers.AddLayer(k.B_Mask)
    for n in range(1,17):
        x=-14+4*((n-1)%8);y=-4.3 if n<=8 else 4.3
        pad=k.PAD(fp);pad.SetNumber(str(n));pad.SetAttribute(k.PAD_ATTRIB_PTH)
        pad.SetShape(k.PAD_SHAPE_RECT if n==1 else k.PAD_SHAPE_OVAL)
        pad.SetSize(v(3.2,2));pad.SetDrillShape(k.PAD_DRILL_SHAPE_OBLONG)
        pad.SetDrillSize(v(2.4,1.2));pad.SetLayerSet(layers);pad.SetPosition(v(x,y));fp.Add(pad)
        for layer in [k.F_CrtYd,k.B_CrtYd]:
            # Metal blades extend through the slots; there is no plastic body
            # on this plane. Courtyard is copper + 0.10 mm for each solder land.
            for a,b in [((x-1.7,y-1.1),(x+1.7,y-1.1)),((x+1.7,y-1.1),(x+1.7,y+1.1)),((x+1.7,y+1.1),(x-1.7,y+1.1)),((x-1.7,y+1.1),(x-1.7,y-1.1))]:
                line=k.PCB_SHAPE(fp);line.SetShape(k.SHAPE_T_SEGMENT);line.SetLayer(layer)
                line.SetStart(v(*a));line.SetEnd(v(*b));line.SetWidth(k.FromMM(.05));fp.Add(line)
    assert {p.GetNumber() for p in fp.Pads()}==set(map(str,range(1,17)))
    lib=ROOT/'Node.pretty';lib.mkdir(exist_ok=True)
    k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(lib),fp)
    (ROOT/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "Node") (type "KiCad") (uri "${KIPRJMOD}/Node.pretty") (options "") (descr "Integrated straight-tail prototype, verify actual plug")))\n')
if __name__=='__main__':generate()
