"""AANI-CH-0133-Q ceramic antenna, datasheet 2025-03-07 pp. 3/4.

Pad 2 is a floating printed radiator, NOT ground. The L-shaped copper extension
is part of that pad; only the rectangular solder land has paste/mask openings.
The footprint is oriented for the board's upper-right corner.
"""
from pathlib import Path
import json
import pcbnew as k
ROOT=Path(__file__).resolve().parent
NAME='AANI-CH-0133-Q_Corner'
def v(x,y):return k.VECTOR2I(round(x*1e6),round(y*1e6))
def layers(*ids):
    result=k.LSET()
    for n in ids:result.AddLayer(n)
    return result

def generate():
    fp=k.FOOTPRINT(None);fp.SetReference('AE1');fp.SetValue('AANI-CH-0133-Q')
    fp.SetFPID(k.LIB_ID('Node',NAME));fp.SetAttributes(k.FP_SMD)
    fp.SetLibDescription('Abracon ceramic 1.6x0.8x0.4; 5x3 mm all-layer clearance; floating pad 2 radiator per datasheet p4')
    for number,x in [('1',-.795),('2',.795)]:
        p=k.PAD(fp);p.SetNumber(number);p.SetAttribute(k.PAD_ATTRIB_SMD)
        p.SetPosition(v(x,0));p.SetSize(v(.5,1));p.SetShape(k.PAD_SHAPE_RECT)
        p.SetLayerSet(layers(k.F_Cu,k.F_Paste,k.F_Mask));fp.Add(p)
    p=k.PAD(fp);p.SetNumber('2');p.SetAttribute(k.PAD_ATTRIB_SMD)
    p.SetPosition(v(.795,0));p.SetSize(v(.5,1));p.SetShape(k.PAD_SHAPE_CUSTOM)
    p.SetAnchorPadShape(k.F_Cu,k.PAD_SHAPE_RECT);p.SetLayerSet(layers(k.F_Cu))
    for x0,y0,x1,y1 in [(-.05,.49,.25,3.01),(-1.25,2.01,.25,3.01)]:
        poly=k.VECTOR_VECTOR2I()
        for x,y in [(x0,y0),(x1,y0),(x1,y1),(x0,y1)]:poly.push_back(v(x,y))
        p.AddPrimitivePoly(k.F_Cu,poly,0,True)
    fp.Add(p)
    for layer,x0,y0,x1,y1,w in [(k.F_Fab,-.8,-.4,.8,.4,.1),(k.F_CrtYd,-1.295,-.75,1.295,.75,.05)]:
        for a,b in [((x0,y0),(x1,y0)),((x1,y0),(x1,y1)),((x1,y1),(x0,y1)),((x0,y1),(x0,y0))]:
            s=k.PCB_SHAPE(fp);s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(v(*a));s.SetEnd(v(*b));s.SetWidth(k.FromMM(w));s.SetLayer(layer);fp.Add(s)
    fp.Reference().SetVisible(False);fp.Value().SetVisible(False)
    # Dimensionally representative local model; not a supplier STEP model.
    model=k.FP_3DMODEL();model.m_Filename='${KIPRJMOD}/Node.3dshapes/AANI-CH-0133-Q.wrl';fp.Models().push_back(model)
    k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(ROOT/'Node.pretty'),fp)

def clearance(board):
    """No plane or via underneath antenna; only its own feed may enter on F.Cu."""
    z=k.ZONE(board);z.SetIsRuleArea(True);z.SetZoneName('ANTENNA_CLEARANCE')
    z.SetLayerSet(k.LSET.AllCuMask());z.SetDoNotAllowZoneFills(True);z.SetDoNotAllowVias(True)
    z.SetDoNotAllowTracks(False);z.SetDoNotAllowPads(False)
    poly=z.Outline();poly.NewOutline()
    for x,y in [(135.25,100),(138.5,100),(138.5,105.25),(135.25,105.25)]:poly.Append(v(x,y))
    board.Add(z)
    # Native board rule areas also work in headless DRC. Reserve a narrow
    # corridor for the reviewed top-side antenna feed, forbid all inner tracks.
    inner=k.ZONE(board);inner.SetIsRuleArea(True);inner.SetZoneName('ANTENNA_NO_INNER_TRACKS')
    inner.SetLayerSet(layers(k.In1_Cu,k.In2_Cu,k.B_Cu));inner.SetDoNotAllowTracks(True);inner.SetDoNotAllowPads(True)
    inner.Outline().Append(poly);board.Add(inner)
    front=k.ZONE(board);front.SetIsRuleArea(True);front.SetZoneName('ANTENNA_TOP_TRACK_GUARD')
    front.SetLayer(k.F_Cu);front.SetDoNotAllowTracks(True);front.SetDoNotAllowPads(False)
    outline=k.SHAPE_POLY_SET(poly)
    seed=json.loads((ROOT/'ceramic_rf_routes.json').read_text())
    for segment in seed['segments']:
        if segment['net']!='ANT_FEED':continue
        t=k.PCB_TRACK(board);t.SetStart(v(*segment['start']));t.SetEnd(v(*segment['end']));t.SetWidth(k.FromMM(segment['width']));t.SetLayer(k.F_Cu)
        corridor=k.SHAPE_POLY_SET();t.TransformShapeToPolygon(corridor,k.F_Cu,30000,1000,k.ERROR_OUTSIDE)
        outline.BooleanSubtract(corridor)
    front.Outline().Append(outline);board.Add(front)
if __name__=='__main__':generate()
