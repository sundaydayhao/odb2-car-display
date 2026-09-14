"""A3 fixture datums. Finished NPTH dimensions, no paste, no electrical net."""
from pathlib import Path
import pcbnew as k

ROOT = Path(__file__).resolve().parent

def v(x, y):
    return k.VECTOR2I(round(x * 1e6), round(y * 1e6))

def generate():
    for name, w, h in [('Fixture_Round_D1.2mm', 1.2, 1.2),
                       ('Fixture_Slot_1.6x1.2mm', 1.6, 1.2)]:
        fp = k.FOOTPRINT(None)
        fp.SetReference('H'); fp.SetValue(name); fp.SetFPID(k.LIB_ID('Node', name))
        fp.SetAttributes(k.FP_EXCLUDE_FROM_BOM | k.FP_EXCLUDE_FROM_POS_FILES)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
        pad = k.PAD(fp); pad.SetAttribute(k.PAD_ATTRIB_NPTH); pad.SetNumber('')
        pad.SetShape(k.PAD_SHAPE_OVAL if w != h else k.PAD_SHAPE_CIRCLE)
        pad.SetDrillShape(k.PAD_DRILL_SHAPE_OBLONG if w != h else k.PAD_DRILL_SHAPE_CIRCLE)
        pad.SetSize(v(w, h)); pad.SetDrillSize(v(w, h))
        layers = k.LSET(k.LSET.AllCuMask()); layers.AddLayer(k.F_Mask); layers.AddLayer(k.B_Mask)
        assert not k.LSET.AllCuMask().Contains(k.F_Mask)
        pad.SetLayerSet(layers); fp.Add(pad)
        for layer in [k.F_CrtYd, k.B_CrtYd]:
            x, y = w / 2 + .25, h / 2 + .25
            for a, b in [((-x,-y),(x,-y)),((x,-y),(x,y)),((x,y),(-x,y)),((-x,y),(-x,-y))]:
                s = k.PCB_SHAPE(fp); s.SetShape(k.SHAPE_T_SEGMENT); s.SetLayer(layer)
                s.SetStart(v(*a)); s.SetEnd(v(*b)); s.SetWidth(k.FromMM(.05)); fp.Add(s)
        k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(ROOT / 'Node.pretty'), fp)

def add_board_features(board):
    """Reserve component-free top support areas and annotate the bottom access strip."""
    def rectangle(name,x0,y0,x1,y1,layer):
        for a,c in [((x0,y0),(x1,y0)),((x1,y0),(x1,y1)),((x1,y1),(x0,y1)),((x0,y1),(x0,y0))]:
            s=k.PCB_SHAPE(board);s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(v(*a));s.SetEnd(v(*c))
            s.SetLayer(layer);s.SetWidth(k.FromMM(.10));board.Add(s)
        text=k.PCB_TEXT(board);text.SetText(name);text.SetPosition(v((x0+x1)/2,y0-.6))
        text.SetLayer(layer);text.SetTextSize(v(.55,.55));text.SetTextThickness(k.FromMM(.08));board.Add(text)
    rectangle('TEST ACCESS: plug/case clearance required',114.2,113.9,132.2,116.4,k.Dwgs_User)
    for name,x in [('SUPPORT A',115.0),('SUPPORT B',132.5)]:
        rectangle(name,x-.6,114.4,x+.6,115.6,k.Cmts_User)
        z=k.ZONE(board);z.SetIsRuleArea(True);z.SetZoneName(name.replace(' ','_'))
        z.SetLayer(k.F_Cu);z.SetDoNotAllowPads(True);z.SetDoNotAllowTracks(False)
        z.SetDoNotAllowVias(True);z.SetDoNotAllowZoneFills(False)
        z.Outline().NewOutline()
        for px,py in [(x-.6,114.4),(x+.6,114.4),(x+.6,115.6),(x-.6,115.6)]:z.Outline().Append(v(px,py))
        board.Add(z)

if __name__ == '__main__':
    generate()
