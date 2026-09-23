"""Revision M: enclosure-mounted screen, bottom-edge flexes, no FPC board slots.

One-time migration from unrouted L1. Keeps electrical and pad identities.
The 3 mm nominal screen gap and connector setbacks remain assembly assumptions.
"""
import argparse,json,math,shutil,sys
from pathlib import Path
import pcbnew as k
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp

FPCS={'J1200':(74.5,54,0),'J1210':(109,15.5,0),'J1220':(68.5,7,180)}
# Footprint-free areas allow copper routing below the insulating flex.
AREAS={
 'TOUCH_FLEX_COMPONENT_CLEAR':(104.5,19.9,113.5,85),
 'LCD_FLEX_COMPONENT_CLEAR':(63.5,58.2,85.5,85),
 'TOUCH_PAD_ESCAPE':(104,7.75,114,12.75),
 'LCD_PAD_ESCAPE':(63.5,41.8,83.5,51.25),
}

def stage(baseline,out):
    assert baseline.resolve()!=out.resolve() and not out.exists()
    board=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
    assert not len(board.GetTracks()) and not any(not z.GetIsRuleArea() for z in board.Zones())
    info=json.loads((baseline/'SCREEN_BACK_LAYOUT.json').read_text(encoding='utf-8'))
    assert len(info['slots'])==2,'Requires pre-M slot layout'
    intent=json.loads((baseline/'design-intent.json').read_text(encoding='utf-8'))
    parts={p['ref']:p for p in intent['parts']};fps={f.GetReference():f for f in board.GetFootprints()}
    def vec(x,y):return k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
    def pos(f):return [round(k.ToMM(f.GetPosition().x)-50,6),round(k.ToMM(f.GetPosition().y)-50,6)]
    def state(f):return dict(xy_mm=pos(f),side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
    def identity(f):return [f.m_Uuid.AsString(),f.GetPath().AsString(),f.GetValue(),f.GetFPIDAsString(),f.IsDNP(),
        sorted((p.m_Uuid.AsString(),p.GetNumber(),p.GetNetname(),p.GetSize().x,p.GetSize().y,p.GetDrillSize().x,p.GetDrillSize().y) for p in f.Pads())]
    before={r:state(f) for r,f in fps.items()};identities={r:identity(f) for r,f in fps.items()}
    removed_edges=[]
    # Only the 52 segments belonging to the two recorded FPC slots are removed.
    for g in list(board.GetDrawings()):
        if g.GetLayer()!=k.Edge_Cuts:continue
        bb=g.GetBoundingBox();cx=k.ToMM(bb.GetCenter().x)-50;cy=k.ToMM(bb.GetCenter().y)-50
        if any(abs(cx-s['center_mm'][0])<=s['size_mm'][0]/2+.06 and abs(cy-s['center_mm'][1])<=s['size_mm'][1]/2+.06 for s in info['slots']):
            removed_edges.append(g.m_Uuid.AsString())
    assert len(removed_edges)==52,len(removed_edges)
    obsolete={'TOUCH_PASS_THROUGH_COPPER_CLEAR','LCD_PASS_THROUGH_COPPER_CLEAR','TOUCH_BEND_COMPONENT_CLEAR','LCD_BEND_COMPONENT_CLEAR'}
    tree=sexp.load_file(str(baseline/'display_carrier.kicad_pcb'));kept=[]
    for node in tree:
        if isinstance(node,list):
            uid=sexp.find(node,'uuid');name=sexp.find(node,'name')
            if uid is not None and str(sexp.atoms(uid)[0]) in removed_edges:continue
            if sexp.tag_of(node)=='zone' and name is not None and str(sexp.atoms(name)[0]) in obsolete:continue
        kept.append(node)
    temp=out.parent/'enclosure-input.kicad_pcb';temp.parent.mkdir(parents=True,exist_ok=True);sexp.save_file(str(temp),kept)
    shutil.copyfile(baseline/'display_carrier.kicad_pro',temp.with_suffix('.kicad_pro'))
    board=k.LoadBoard(str(temp));fps={f.GetReference():f for f in board.GetFootprints()}
    for g in list(board.GetDrawings()):
        if isinstance(g,k.PCB_TEXT):
            if g.GetText().startswith('SLOT LOCATIONS'):g.SetText('SCREEN/PCB ON ENCLOSURE; NOMINAL GAP 3mm; FLEX OVER LOWER EDGE')
            if g.GetText()=='LCD':g.SetPosition(vec(74.5,50.5))
            if g.GetText()=='TOUCH':g.SetPosition(vec(109,11.5))
    def move(r,x,y,angle=None):
        f=fps[r];f.SetPosition(vec(x,y))
        if angle is not None:f.SetOrientationDegrees(angle)
    for r,xyz in FPCS.items():move(r,*xyz)
    active={
      'U300':(96,20,180),'L300':(95,29,180),'U310':(97,36,180),
      'R305':(88,42.5,180),'R306':(88,45,180),
      'U400':(124,47,180),'L400':(134,47,180),'U410':(97,49,180),'L410':(91.5,49,180),
      'C410':(101.25,45.5,180),'C411':(91.5,54,180),'C412':(95.5,54,90),
      'C413':(91.5,56.5,180),'R410':(101.3,50.5,180),'R411':(100.5,53,180),
      'U1200':(58,48,0),'U1201':(58,54,0),
      'R1206':(85,45,90),'R1207':(85,49,90),
      'R819':(87.25,52.5,90),'R820':(87,56.5,90),
      'U810':(100,56.5,180),'U811':(90,59.5,180),'U812':(95,59.5,180),
      'U813':(90,63.5,180),'U814':(95,63.5,180),'U815':(98.5,68.5,180),'Y810':(99,73,180),
      'J810':(91,81,180),
      'U802':(120.5,73,180),'U803':(60.5,73,180),
    }
    for r,xyz in active.items():move(r,*xyz)
    for r,p in parts.items():
        if p['sheet']=='14_lcd_fpc' and r.startswith(('R','C')):fps[r].SetOrientationDegrees(90)
    # Group-specific regions keep power bypasses, feedback and audio local.
    regions={
      '04_charger':(83,7,103.1,33.3),'05_system_switch':(83,30,103.1,46),
      '14_lcd_fpc':(50.8,42,63.1,59.8),'15_touch_fpc':(90.3,7,103.1,18),
      '12_fm_audio':(85.9,53.5,104.1,84),'19_stereo_pa':(28,54,63.1,80),
      '18_dual_mic':(116,58,140,78),
    }
    anchors={r:pos(f) for r,f in fps.items()}
    anchors.update({
      'C300':[94,16],'C301':[98,16],'C302':[100,22.5],'C303':[91,24],'C304':[90,29],'C305':[100,29],
      'C306':[98.5,24.5],'F300':[127,4],'R300':[100,18.5],'R301':[90,18],'R302':[90,20.5],
      'C310':[91,33.5],'C311':[93,36],'C312':[101,36],'C313':[95,40],
      'C400':[123.5,41.5],'C401':[123.5,43.5],'C402':[128,44],'C403':[119,48],
      'C404':[119,50],'C405':[134,41.5],'C406':[134,52.5],'C407':[140,52.5],
      'C408':[125,52.5],'C409':[140,42],'R400':[130,52.5],'R401':[119,52],
      'R402':[119,43],'R403':[119,45],'R404':[119,54],
      'C410':[101,46],'C411':[91.5,54],'C412':[95.5,54],'C413':[94,56.5],'R410':[101,50.5],'R411':[101,52.5],
      'C1200':[54.5,46],'C1201':[61,49],'C1202':[61,51],'C1203':[61,54],
      'R1200':[54,43],'R1201':[54,48.5],'R1202':[54,51],'R1203':[58,44],
      'R1204':[54,54],'R1205':[54,56],'R1206':[57.5,58],'R1207':[61,58],
      'C1210':[101,16],'C1211':[101,14],'R1210':[101,8],'R1211':[97,8],'R1212':[97,10.5],'R1213':[97,13],
      'R1214':[93,10.5],'R1215':[93,13],
      'C812':[100,53.8],'C813':[100,59.5],'C814':[100,65],'C818':[92,57],'C819':[100,61.5],
      'C820':[95,57],'C821':[96,71],'C822':[100,75.5],'R811':[100,78],
      'R818':[96,75],'R820':[100,77.5],
      'C844':[60.5,69],'R841':[60.5,66.5],'C849':[60.5,77],
      'C840':[120.5,69],'R840':[120.5,66.5],'C848':[116.8,73],
    })
    def box(f):
        z=f.GetBoundingBox(False,False)
        return tuple(k.ToMM(t)-50 for t in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()])
    def overlap(a,b,g=.6):return a[0]<b[2]+g and a[2]>b[0]-g and a[1]<b[3]+g and a[3]>b[1]-g
    placed=[]
    def clear(r,fixed=False):
        z=box(fps[r]);bad=[]
        for name,bounds in AREAS.items():
            if overlap(z,bounds,0):bad.append(name)
        if r not in {'U1301','C1301','C1302'} and overlap(z,(18,72,28,85),.3):bad.append('SHT40_ISLAND')
        for name,bounds in placed:
            if overlap(z,bounds,.6):bad.append(name)
        if not fixed and (z[0]<.6 or z[1]<.6 or z[2]>144.4 or z[3]>84.4):bad.append('BOARD_EDGE')
        return bad
    target_sheets=set(regions)|{'06_main_power'}
    movable={r for r in fps if parts.get(r,{}).get('sheet') in target_sheets and not r.startswith(('J','SW')) and r not in active}
    movable|={r for r in fps if r.startswith('TP')}
    # Preserve unrelated footprints exactly; only relocated groups use larger gaps.
    fixed=set(fps)-movable
    for r in sorted(fixed-active.keys()):
        assert not any(overlap(box(fps[r]),a,0) for a in AREAS.values()),(r,'fixed in flex/escape lane')
        placed.append((r,box(fps[r])))
    for r in active:
        assert not clear(r,True),(r,clear(r,True))
        placed.append((r,box(fps[r])))
    def region(r):
        if r.startswith('TP'):return (.8,78,144,84.3)
        if r=='F300':return (124.2,1.2,137,7)
        sheet=parts[r]['sheet']
        if sheet=='06_main_power':return (87.8,45,103.1,57) if r in ['C410','C411','C412','C413','R410','R411'] else (115.2,40.8,143.7,57.8)
        if r in ['C844','R841','C849']:return (58.3,65,63.1,80)
        return regions[sheet]
    priority={s:i for i,s in enumerate(['15_touch_fpc','06_main_power','04_charger','05_system_switch','14_lcd_fpc','12_fm_audio','18_dual_mic','19_stereo_pa'])}
    ordered=sorted(movable,key=lambda r:(r.startswith('TP'),priority.get(parts.get(r,{}).get('sheet'),99),-((box(fps[r])[2]-box(fps[r])[0])*(box(fps[r])[3]-box(fps[r])[1])),r))
    for r in ordered:
        x1,y1,x2,y2=region(r);ax,ay=anchors[r]
        angle=fps[r].GetOrientationDegrees();angles=[angle,angle+90] if r.startswith(('R','C')) else [angle]
        def valid():
            z=box(fps[r]);return z[0]>=x1 and z[1]>=y1 and z[2]<=x2 and z[3]<=y2 and not clear(r)
        # Try the functional anchor first, then nearby half-mm sites.
        move(r,ax,ay)
        if not valid():
            sites=sorted(((ix/2,iy/2) for ix in range(math.ceil(x1*2),math.floor(x2*2)+1) for iy in range(math.ceil(y1*2),math.floor(y2*2)+1)),key=lambda q:(q[0]-ax)**2+(q[1]-ay)**2)
            found=False
            for x,y in sites:
                for a in angles:
                    move(r,x,y,a)
                    if valid():found=True;break
                if found:break
            else:
                (out.parent/'placement-debug.json').write_text(json.dumps({'ref':r,'region':region(r),'placed':placed,'moving_box':box(fps[r])},indent=2),encoding='utf-8')
                raise AssertionError(('No site',r,region(r),anchors[r]))
        placed.append((r,box(fps[r])))
    for name,(x1,y1,x2,y2) in AREAS.items():
        z=k.ZONE(board);z.SetIsRuleArea(True);z.SetZoneName(name);z.SetLayer(k.F_Cu)
        z.SetDoNotAllowFootprints(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowTracks(False);z.SetDoNotAllowVias(False);z.SetDoNotAllowZoneFills(False)
        points=k.VECTOR_VECTOR2I()
        for x,y in [(x1,y1),(x2,y1),(x2,y2),(x1,y2)]:points.push_back(vec(x,y))
        z.AddPolygon(points);board.Add(z)
    assert all(identity(f)==identities[r] for r,f in fps.items())
    shutil.copytree(baseline,out,ignore=shutil.ignore_patterns('*.lck','*.kicad_prl','~*','fp-info-cache'))
    board.GetTitleBlock().SetRevision('R0M ENCLOSURE FPC');k.SaveBoard(str(out/'display_carrier.kicad_pcb'),board)
    shutil.copyfile(baseline/'display_carrier.kicad_pro',out/'display_carrier.kicad_pro')
    for p in intent['parts']:
        if p['ref'] in fps:
            st=state(fps[p['ref']]);p.update(pcb=st['xy_mm'],side=st['side'],angle=st['angle'])
    (out/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    import cad
    from board_spec import d
    staged={p['ref']:p for p in intent['parts']}
    for p in d.parts:
        for key in ['pcb','side','angle']:p[key]=staged[p['ref']][key]
    cad.OUT=out;d.write()
    moves=[dict(ref=r,before=before[r],after=state(f)) for r,f in sorted(fps.items()) if before[r]!=state(f)]
    report=dict(revision='M',baseline_commit='7117720',status='UNROUTED_ENCLOSURE_FLEX_FIT_UNVERIFIED',
      coordinate_view=info['coordinate_view'],component_side='F',screen_side='B',pcb_mm=[145,85],pcb_screen_offset_mm=[9.93,2.5],
      mounting='PCB and screen independently attached to enclosure',screen_gap_mm=3,screen_gap_source='user nominal, 2026-09-24',
      measured_planar_tip_from_screen_top_mm=info['measured_planar_tip_from_screen_top_mm'],
      fpc_positions_mm=FPCS,slots=[],removed_slot_edge_segments=len(removed_edges),
      cable_route='Both tails originate at screen lower edge; wrap outside PCB lower edge onto F, then enter upward-facing tail ends into downward-facing socket mouths.',
      component_clear_areas_mm=AREAS,positions={r:state(f) for r,f in sorted(fps.items())},moves=moves,
      all_footprint_and_pad_UUIDs_paths_nets_values_DNP_preserved=True,back_side_component_bodies=0,
      known_J300_nominal_tail_below_B_mm=1.8,
      required_fit_checks=['actual 3mm gap / solder maximum / insulation','free flex length and minimum bend radius','bottom-edge loop and strain relief','touch X / pin1 / pinout','latch opening'],
      routing_status='No tracks added; copper allowed under insulated flex, functional groups and pad escape areas reserved. Actual differential impedance, return paths and escape routing pending.')
    (out/'SCREEN_BACK_LAYOUT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'moves':len(moves),'refs':[m['ref'] for m in moves],'FPCs':FPCS,'removed_edges':len(removed_edges)},indent=2))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();stage(a.baseline,a.output)
