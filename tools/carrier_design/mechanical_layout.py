"""Apply the 2026-09-23 measured fold layout to a staged, UNROUTED board.

Retains every existing footprint UUID, pad/net and schematic path. Positions
are front/screen projection in mm from the PCB upper left; KiCad adds (50,50).
The touch X coordinate and assembly stack are provisional, not measurements.
"""
import argparse, json, math
from pathlib import Path
import pcbnew as k
from cad import sexp

PCB_SCREEN_OFFSET = (9.93, 2.5)
FPC_MOUTH_OFFSET = 3.525
FPC_INSERTION_NOMINAL = 2.1
TAIL_TOP = {'J1200':46.1, 'J1210':7.5}
FPC_POSITIONS = {
    ref:(70.5 if ref=='J1200' else 36.0,
         round(tip-PCB_SCREEN_OFFSET[1]+FPC_INSERTION_NOMINAL-FPC_MOUTH_OFFSET,3),0)
    for ref,tip in TAIL_TOP.items()
}
FPC_POSITIONS['J1220']=(76.5,7.0,180)


def reflow(project):
    filename=project/'display_carrier.kicad_pcb'
    settings_path=project/'display_carrier.kicad_pro'
    settings_bytes=settings_path.read_bytes()
    assert not (project/'MECHANICAL_LAYOUT.json').exists(), 'Run from a newly staged baseline; do not duplicate layout rule areas'
    # KiCad 10 Windows SWIG corrupts type wrappers after Remove(drawing).
    # Remove obsolete annotations with the lossless parser before native load.
    tree=sexp.load_file(str(filename))
    kept=[]
    for g in tree:
        tag=sexp.tag_of(g) if isinstance(g,list) else ''
        if tag=='gr_text' and any(t in sexp.atoms(g)[0] for t in ['LCD FPC','TOUCH FPC','LCD / J1200','TOUCH / J1210']):continue
        if tag=='gr_line' and sexp.atoms(sexp.find(g,'layer'))[0]=='Dwgs.User':
            pts=[list(map(float,sexp.atoms(sexp.find(g,p)))) for p in ['start','end']]
            xs={round(p[0]-50,3) for p in pts};ys={round(p[1]-50,3) for p in pts}
            if (xs<={55.5,85.5} and ys<={40.,80.}) or (xs<={87.,107.} and ys<={60.,78.}):continue
        kept.append(g)
    temp=project/'layout-input.kicad_pcb'
    sexp.save_file(str(temp),kept)
    board=k.LoadBoard(str(temp))
    assert not len(board.GetTracks()),'Refuse to move routed components'
    assert not any(not z.GetIsRuleArea() for z in board.Zones()),'Refuse to move a board with copper zones'
    fps={f.GetReference():f for f in board.GetFootprints()}
    def v(x,y):return k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
    def coord(f):return [round(k.ToMM(f.GetPosition().x)-50,3),round(k.ToMM(f.GetPosition().y)-50,3),round(f.GetOrientationDegrees(),3),f.GetLayerName()]
    before={r:coord(f) for r,f in fps.items()}
    identity={r:(f.m_Uuid.AsString(),f.GetPath().AsString(),sorted((p.GetNumber(),p.GetNetname()) for p in f.Pads())) for r,f in fps.items()}
    def move(ref,x,y,a=None,side=None):
        f=fps[ref]
        if side and f.GetLayerName()!=side:f.Flip(f.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
        f.SetPosition(v(x,y))
        if a is not None:f.SetOrientationDegrees(a)
    for ref,xyz in FPC_POSITIONS.items():move(ref,*xyz)
    # J300 THT tails would enter the new touch courtyard. Move it left and
    # put three displaced input-protection passives on the clear opposite face.
    move('J300',24,5.5)
    for ref in ['D212','C212','R216']:
        x,y,a,_=before[ref];move(ref,x,y,a,'F.Cu')
    move('R530',134,68,0,'F.Cu')
    move('R524',134,72,0,'F.Cu')
    # Touch damping/pullups/decoupling on F, beside the top connector. Keep the
    # entire long folded ribbon corridor x30..42 free of F-side components.
    for i,ref in enumerate(['R1210','R1211','R1212','R1213','R1214','R1215','C1210','C1211']):
        move(ref,46+(i%4)*3.5,11+(i//4)*4,0,'F.Cu')
    # LCD logic sits alongside J1200, F side; flex from its mouth down remains free.
    move('U1200',89,38,0,'F.Cu');move('U1201',97,38,0,'F.Cu')
    for i,ref in enumerate(['C1200','C1201','C1202','C1203','R1200','R1201','R1202','R1203','R1204','R1205','R1206','R1207']):
        move(ref,88+(i%4)*3.5,43+(i//4)*4,0,'F.Cu')
    def line(a,b,layer=k.Dwgs_User,width=.1):
        s=k.PCB_SHAPE();s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(v(*a));s.SetEnd(v(*b));s.SetLayer(layer);s.SetWidth(k.FromMM(width));board.Add(s)
    def rect(x0,y0,x1,y1,layer=k.Dwgs_User):
        p=[(x0,y0),(x1,y0),(x1,y1),(x0,y1)]
        for a,b in zip(p,p[1:]+p[:1]):line(a,b,layer)
    def text(value,x,y,layer=k.Dwgs_User,size=1):
        t=k.PCB_TEXT(board);t.SetText(value);t.SetPosition(v(x,y));t.SetLayer(layer)
        t.SetTextSize(k.VECTOR2I(k.FromMM(size),k.FromMM(size)));t.SetTextThickness(k.FromMM(.12))
        if layer==k.B_SilkS:t.SetMirrored(True)
        board.Add(t)
    def area(name,points,layers,tracks=False,vias=False,pour=False,footprints=False):
        z=k.ZONE(board);z.SetIsRuleArea(True);z.SetZoneName(name)
        ls=k.LSET()
        for layer in layers:ls.AddLayer(layer)
        z.SetLayerSet(ls);z.SetDoNotAllowTracks(tracks);z.SetDoNotAllowVias(vias)
        z.SetDoNotAllowZoneFills(pour);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(footprints)
        poly=k.VECTOR_VECTOR2I()
        for x,y in points:poly.push_back(v(x,y))
        z.AddPolygon(poly)
        board.Add(z)
    rect(30,8,42,85);text('TOUCH FLEX / F-side clear',36,49,size=.75)
    rect(59,46.3,82,85);text('LCD FLEX / F-side clear',70.5,72,size=.8)
    area('TOUCH_FLEX_COMPONENT_CLEARANCE',[(30,8),(42,8),(42,85),(30,85)],[k.F_Cu],footprints=True)
    area('LCD_FLEX_COMPONENT_CLEARANCE',[(59,46.3),(82,46.3),(82,85),(59,85)],[k.F_Cu],footprints=True)
    # All-layer no-pour around the RH/T island; routing will use four narrow
    # traces through its neck. No broad ground/power plane may bridge the slots.
    area('SHT40_THERMAL_ISLAND_NO_POUR',[(117,72),(127,72),(127,85),(117,85)],
         [k.F_Cu,k.In1_Cu,k.In2_Cu,k.B_Cu],pour=True)
    # Two real 1 mm routed isolation slots with rounded ends. Leave top and
    # bottom mechanical bridges; their residual heat flow requires enclosure QA.
    for x in [117.5,126.5]:
        pts=[]
        for cy,start in [(73,180),(83,0)]:
            for j in range(13):
                a=math.radians(start+j*15)
                pts.append((round(x+.5*math.cos(a),6),round(cy+.5*math.sin(a),6)))
        for a,b in zip(pts,pts[1:]+pts[:1]):line(a,b,k.Edge_Cuts,.05)
    text('LCD / J1200',70.5,38,k.F_SilkS,.9)
    text('TOUCH / J1210',36,9,k.F_SilkS,.8)
    text('ALS WINDOW',58,1.6,k.B_SilkS,.8)
    text('RH/T VENT',122,74.8,k.B_SilkS,.8)
    text('SHT40: no coating / vented pocket',121.5,88,size=.8)
    for ref in ['J1200','J1210']:
        x,y,_=FPC_POSITIONS[ref]
        text(f'{ref}: tip Y={TAIL_TOP[ref]} from screen top',x,y-6,size=.75)
    text('PCB screen offset: X=9.93, Y=2.50 mm; touch X estimated',72.5,-11,size=1)
    after={r:coord(f) for r,f in fps.items()}
    assert identity=={r:(f.m_Uuid.AsString(),f.GetPath().AsString(),sorted((p.GetNumber(),p.GetNetname()) for p in f.Pads())) for r,f in fps.items()}
    k.SaveBoard(str(filename),board)
    # Loading an intermediate filename makes pcbnew attach default project
    # settings. Never let SaveBoard replace the user's netclasses/DRC settings.
    settings_path.write_bytes(settings_bytes)
    temp.unlink()
    intent_path=project/'design-intent.json'
    intent=json.loads(intent_path.read_text(encoding='utf-8'))
    for p in intent['parts']:
        if p['ref'] in after:
            x,y,a,side=after[p['ref']];p.update(pcb=[x,y],angle=a,side=side[0])
    intent_path.write_text(json.dumps(intent,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    report={'date':'2026-09-23','status':'MEASURED_Y_PROVISIONAL_X_AND_STACKUP',
        'screen_nominal_mm':[164.86,99.96],'pcb_mm':[145,85],
        'pcb_offset_from_screen_top_left_mm':PCB_SCREEN_OFFSET,
        'tail_tip_from_screen_top_mm':TAIL_TOP,'superseded_measurements_mm':[1.2,105.3],
        'connector_mouth_from_footprint_origin_mm':FPC_MOUTH_OFFSET,
        'connector_nominal_insertion_mm':FPC_INSERTION_NOMINAL,
        'fpc_positions_mm':FPC_POSITIONS,'touch_x_basis':'Photo estimate: back X~119 mm, front X~46 mm; NOT a caliper measurement',
        'existing_uuid_paths_pad_nets_preserved':True,
        'moves':[{ 'ref':r,'before':before[r],'after':after[r]} for r in sorted(fps) if before[r]!=after[r]],
        'required_fit_checks':['touch X and total glass outline','tail plane height and full insertion','pin-1 mapping and touch pinout','FPC tolerance and latch clearance','vent/window and cabin thermal response']}
    (project/'MECHANICAL_LAYOUT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True)
    reflow(p.parse_args().project.resolve())
