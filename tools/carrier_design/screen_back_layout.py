"""Revision K: F-side assembly, B-side screen, two actual flex pass-through slots.

Run with KiCad Python on a scratch copy of the unrouted revision-J project.
The coordinate view changes from screen-front to screen-back: X is mirrored.
Slot locations and the 4 mm flex reach allowance are provisional until a
physical stack-up is measured. THT tails still require insulating clearance.
"""
import argparse,copy,json,math,shutil
from pathlib import Path
import pcbnew as k
from cad import ROOT,OUT,PROJECT,sexp
from board_spec import d

FPCS={'J1200':(74.5,46.175,0),'J1210':(109,7.575,0),'J1220':(68.5,7,180)}
SLOTS=[dict(name='TOUCH_PASS_THROUGH',ref='J1210',center_mm=[109,15.5],size_mm=[10,3],radius_mm=1.5),
       dict(name='LCD_PASS_THROUGH',ref='J1200',center_mm=[74.5,54],size_mm=[20,3],radius_mm=1.5)]

def stage(baseline,out):
    assert baseline.resolve()!=out.resolve()
    assert not (baseline/'SCREEN_BACK_LAYOUT.json').exists(),'One-time migration: baseline must use the old face definition'
    original=k.LoadBoard(str(baseline/(PROJECT+'.kicad_pcb')))
    assert not len(original.GetTracks()),'Refusing to relocate a routed board'
    assert not any(not z.GetIsRuleArea() for z in original.Zones()),'Refusing to discard copper zones'
    shutil.copytree(baseline,out,dirs_exist_ok=True)
    tree=sexp.load_file(str(baseline/(PROJECT+'.kicad_pcb')))
    # Remove former planar flex lanes and obsolete face/view annotations.
    keep=[]
    for n in tree:
        tag=sexp.tag_of(n) if isinstance(n,list) else ''
        if tag.startswith('gr_') and sexp.find(n,'layer') is not None:
            if sexp.atoms(sexp.find(n,'layer'))[0]!='Edge.Cuts':continue
        if tag=='zone':
            name=sexp.find(n,'name')
            if name is None or 'SHT40' not in str(sexp.atoms(name)[0]):continue
        keep.append(n)
    temp=out.parent/'flat-input.kicad_pcb';sexp.save_file(str(temp),keep)
    b=k.LoadBoard(str(temp));assert not len(b.GetTracks())
    assert not any(not z.GetIsRuleArea() for z in b.Zones())
    fps={f.GetReference():f for f in b.GetFootprints()}
    def v(x,y):return k.VECTOR2I(k.FromMM(x+50),k.FromMM(y+50))
    def xy(f):return [round(k.ToMM(f.GetPosition().x)-50,4),round(k.ToMM(f.GetPosition().y)-50,4)]
    def state(f):return dict(xy_mm=xy(f),side=f.GetLayerName()[0],angle=round(f.GetOrientationDegrees(),4))
    before={r:state(f) for r,f in fps.items()}
    def identity(f):return (f.m_Uuid.AsString(),f.GetPath().AsString(),f.GetValue(),f.IsDNP(),
        sorted((p.m_Uuid.AsString(),p.GetNumber(),p.GetNetname()) for p in f.Pads()))
    ids={r:identity(f) for r,f in fps.items()}
    for f in fps.values():
        x,y=xy(f)
        if f.GetLayer()==k.B_Cu:f.Flip(v(72.5,0),k.FLIP_DIRECTION_LEFT_RIGHT)
        else:
            f.SetPosition(v(145-x,y))
            # Existing F-side assemblies remain real, unmirrored footprints.
            # Their handedness cannot be mirrored on the same copper face.
        assert f.GetLayer()==k.F_Cu
    for g in b.GetDrawings():g.Flip(v(72.5,0),k.FLIP_DIRECTION_LEFT_RIGHT)
    for z in b.Zones():z.Flip(v(72.5,0),k.FLIP_DIRECTION_LEFT_RIGHT)
    def move(r,x,y,angle=None):
        f=fps[r];f.SetPosition(v(x,y))
        if angle is not None:f.SetOrientationDegrees(angle)
    for r,xyz in FPCS.items():move(r,*xyz)
    # Move the core clear of the LCD connector. Shift charger/magnetics out of
    # the touch slot. Keep microphones 60 mm apart and all edge ports facing out.
    overrides={'U100':(69,28,0),'U300':(118,18,180),'L300':(110,22,180),
               'U1200':(88.5,50,0),'U1201':(93.5,53.5,0),'SW310':(20,11,180)}
    for r,xyz in overrides.items():move(r,*xyz)
    def box(f):
        z=f.GetBoundingBox(False,False)
        return tuple(k.ToMM(a)-50 for a in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()])
    def overlaps(a,c,g=.22):return a[0]<c[2]+g and a[2]>c[0]-g and a[1]<c[3]+g and a[3]>c[1]-g
    # Reserve full short F-side flex bends, and a thermal island for its sensor.
    obstacles=[('TOUCH_BEND',(103.5,12,114.5,18)),('LCD_BEND',(63.5,50.4,85.5,56.3))]
    island=(18,72,28,85)
    sensor_members={'U1301','C1301','C1302'}
    placed=[]
    def reasons(f,fixed=False):
        z=box(f);r=f.GetReference();bad=[]
        if not fixed and (z[0]<.6 or z[1]<.6 or z[2]>144.4 or z[3]>77):bad.append('edge')
        for name,c in obstacles+placed:
            if overlaps(z,c):bad.append(name)
        if r not in sensor_members and overlaps(z,island):bad.append('THERMAL_ISLAND')
        return bad
    def lock(f):placed.append((f.GetReference(),box(f)))
    fixed={r for r in fps if r.startswith(('J','SW','H'))}|{'U100','U1000','U1300','U1301','U802','U803','L400','U400','U300','L300','U310','U410','L410','U1010','L1010','U800','U815','U820','U821','U822','L820','U1200','U1201'}|sensor_members
    # Lock mechanical features first, then place other parts near their block.
    for r in sorted(fixed):
        assert not reasons(fps[r],True),(r,reasons(fps[r],True),box(fps[r]))
        lock(fps[r])
    partbyref={p['ref']:p for p in d.parts}
    # Generic bounds keep passives around their original functional area.
    # Regions are expressed in the new screen-back/F-side coordinate frame.
    regions={'01_core':(48,10,89,55),'02_usb':(118,21,144,42),'03_usb_current':(116,6,142,29),
        '04_charger':(100,8,132,30),'05_system_switch':(91,17,119,38),
        '06_main_power':(88,35,139,58),'07_sd':(1,40,30,72),
        '08_modem':(24,13,54,44),'09_modem_power':(23,37,61,58),'10_sim_rf':(1,16,55,45),
        '11_codec':(115,54,140,77),'12_fm_audio':(85,57,115,77),
        '14_lcd_fpc':(85.7,38,103,60),'15_touch_fpc':(85,8,104,20),
        '16_camera_fpc':(57,1,81,12),'17_cabin_sensors':(16,1,91,77),
        '18_dual_mic':(59,57,140,77),'19_stereo_pa':(28,54,62,77)}
    anchors={r:xy(f) for r,f in fps.items()}
    # Follow moved active parts with their essential local capacitors.
    for r in ['C300','C301','C302','C303','C304','C305','C306','C307']:
        if r in anchors:anchors[r]=[anchors[r][0]+6,anchors[r][1]+3]
    for r in ['C1200','C1201','C1202','C1203']:
        anchors[r]=[88.5 if r in ['C1200','C1201','C1202'] else 93.5,53 if r!='C1203' else 57]
    movable=[f for r,f in fps.items() if r not in fixed]
    movable.sort(key=lambda f:(not f.GetReference().startswith('U'),-(box(f)[2]-box(f)[0])*(box(f)[3]-box(f)[1])))
    relocations=[]
    for f in movable:
        r=f.GetReference();p=partbyref.get(r,{})
        if r.startswith('TP'):
            region=(8,77,137,83);anchor=xy(f)
        else:region=regions.get(p.get('sheet'),(.6,.6,144.4,77));anchor=anchors[r]
        x1,y1,x2,y2=region
        def allowed():
            z=box(f)
            return not reasons(f,r.startswith('TP')) and z[0]>=x1 and z[1]>=y1 and z[2]<=x2 and z[3]<=y2
        if not allowed():
            sites=sorted(((ix/2,iy/2) for ix in range(math.ceil(2*x1),math.floor(2*x2)+1)
                          for iy in range(math.ceil(2*y1),math.floor(2*y2)+1)),
                         key=lambda q:(q[0]-anchor[0])**2+(q[1]-anchor[1])**2)
            found=False
            for x,y in sites:
                f.SetPosition(v(x,y))
                if allowed():found=True;break
            assert found,('No position',r,region,anchor)
            relocations.append(r)
        lock(f)
    def line(a,c,layer=k.Edge_Cuts,width=.05):
        g=k.PCB_SHAPE(b);g.SetShape(k.SHAPE_T_SEGMENT);g.SetStart(v(*a));g.SetEnd(v(*c))
        g.SetLayer(layer);g.SetWidth(k.FromMM(width));b.Add(g)
    def area(name,bounds,layers,tracks=False,vias=False,pour=False,pads=False,footprints=False):
        z=k.ZONE(b);z.SetIsRuleArea(True);z.SetZoneName(name);ls=k.LSET()
        for l in layers:ls.AddLayer(l)
        z.SetLayerSet(ls);z.SetDoNotAllowTracks(tracks);z.SetDoNotAllowVias(vias)
        z.SetDoNotAllowZoneFills(pour);z.SetDoNotAllowPads(pads);z.SetDoNotAllowFootprints(footprints)
        x1,y1,x2,y2=bounds;points=k.VECTOR_VECTOR2I()
        for x,y in [(x1,y1),(x2,y1),(x2,y2),(x1,y2)]:points.push_back(v(x,y))
        z.AddPolygon(points);b.Add(z)
    for slot in SLOTS:
        x,y=slot['center_mm'];w,h=slot['size_mm'];r=h/2;points=[]
        # Closed routed capsule; 15-degree chords approximate the rounded ends.
        for cx,start in [(x+w/2-r,-90),(x-w/2+r,90)]:
            for i in range(13):
                a=math.radians(start+15*i);points.append((round(cx+r*math.cos(a),6),round(y+r*math.sin(a),6)))
        for a,c in zip(points,points[1:]+points[:1]):line(a,c)
        area(slot['name']+'_COPPER_CLEAR',(x-w/2-.3,y-h/2-.3,x+w/2+.3,y+h/2+.3),
             [k.F_Cu,k.In1_Cu,k.In2_Cu,k.B_Cu],True,True,True,True,True)
    for name,z in obstacles:area(name+'_COMPONENT_CLEAR',z,[k.F_Cu],footprints=True)
    area('SCREEN_SIDE_NO_COMPONENTS',(0,0,145,85),[k.B_Cu],footprints=True)
    def text(s,x,y,layer=k.F_SilkS,size=.8):
        t=k.PCB_TEXT(b);t.SetText(s);t.SetPosition(v(x,y));t.SetLayer(layer)
        t.SetTextSize(k.VECTOR2I(k.FromMM(size),k.FromMM(size)));t.SetTextThickness(k.FromMM(.12))
        if layer==k.B_SilkS:t.SetMirrored(True)
        b.Add(t)
    text('F = COMPONENTS / REAR HOUSING',72.5,-5,k.Dwgs_User,1)
    text('B = SCREEN / INSULATING SPACER REQUIRED',72.5,88,k.Dwgs_User,1)
    text('SLOT LOCATIONS PROVISIONAL: CONFIRM FLEX REACH / STACK',72.5,-8,k.Dwgs_User,1)
    # Tiny labels outside the reserved cable bends, checked by native DRC.
    text('LCD',74.5,42.8);text('TOUCH',109,3.5)
    text('ALS',87,1.5);text('RH/T',23,74.5)
    for r in fps:assert identity(fps[r])==ids[r],r
    for p in d.parts:
        if p['ref'] in fps:
            st=state(fps[p['ref']]);p.update(pcb=st['xy_mm'],side=st['side'],angle=st['angle'])
    d.write()
    for path in OUT.glob('*.kicad_sch'):shutil.copy2(path,out/path.name)
    shutil.copy2(OUT/'design-intent.json',out/'design-intent.json')
    k.SaveBoard(str(out/(PROJECT+'.kicad_pcb')),b)
    shutil.copy2(baseline/(PROJECT+'.kicad_pro'),out/(PROJECT+'.kicad_pro'))
    report=dict(revision='K',baseline_commit='6eb6734',status='UNROUTED_PROVISIONAL_FLEX_STACK',
        coordinate_view='F/component side looking toward screen back; +X right, +Y down',
        screen_side='B',component_side='F',screen_front_to_F_view_X='145 - old PCB X',
        pcb_mm=[145,85],pcb_screen_offset_mm=[9.93,2.5],
        measured_planar_tip_from_screen_top_mm={'J1210':7.5,'J1200':46.1},
        provisional_longitudinal_reach_allowance_mm=4,screen_gap_mm=None,
        fpc_positions_mm=FPCS,slots=SLOTS,
        positions={r:state(f) for r,f in sorted(fps.items())},
        moves=[dict(ref=r,before=before[r],after=state(f)) for r,f in sorted(fps.items()) if before[r]!=state(f)],
        collision_relocations=relocations,all_footprint_and_pad_UUIDs_paths_nets_values_DNP_preserved=True,
        back_side_component_bodies=0,
        through_hole_references=sorted(r for r,f in fps.items() if any(p.GetAttribute()==k.PAD_ATTRIB_PTH for p in f.Pads())),
        known_J300_nominal_tail_below_B_mm=2.1,
        clearance_boundary='B has exposed THT solder tails and locating pegs. No metal-to-PCB direct contact. Gap, maximum protrusion and flex bend require physical measurement.',
        required_fit_checks=['screen gap / insulation','THT tail and peg maximum protrusion','flex free length / bend radius / strain','slot edge protection','actual tail widths','touch X / pin1 / touch pinout','latch opening','mic acoustic ports and sensor vents'])
    (out/'SCREEN_BACK_LAYOUT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    mechanical=json.loads((out/'MECHANICAL_LAYOUT.json').read_text(encoding='utf-8'))
    mechanical.update(revision='K',status=report['status'],coordinate_view=report['coordinate_view'],
        component_side='F',screen_side='B',fpc_positions_mm=FPCS,flex_pass_through_slots=SLOTS,
        provisional_longitudinal_reach_allowance_mm=4,moves=report['moves'],required_fit_checks=report['required_fit_checks'])
    (out/'MECHANICAL_LAYOUT.json').write_text(json.dumps(mechanical,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ['positions','moves']},indent=2))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();stage(a.baseline.resolve(),a.output.resolve())
