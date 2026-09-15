"""Generate an editable three-piece enclosure FIT PROTOTYPE with measured checks.

Run with a Python environment providing CadQuery 2.6.1.
All unmeasured values remain labelled in the profile and verification report.
PCB absolute XY is retained; Z points from display toward rear cover.
"""
import argparse, hashlib, json, math, os, sys
from pathlib import Path
import cadquery as cq

HERE=Path(__file__).resolve().parent

def box(x,y,z,w,h,d):
    return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).translate((x,y,z)).val()

def rounded(x,y,z,w,h,d,r=2):
    return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).edges('|Z').fillet(r).translate((x,y,z)).val()

def cylinder(x,y,z,r,h):
    return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z)).val()

def union(shapes):
    s=shapes[0]
    for q in shapes[1:]: s=s.fuse(q)
    return s.clean()

def bb(s):
    b=s.BoundingBox()
    return [round(v,4) for v in (b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax)]

def check_files(path):
    data=path.read_bytes()
    if path.suffix=='.step':
        assert data.startswith(b'ISO-10303-21;') and data.rstrip().endswith(b'END-ISO-10303-21;')
    else: assert len(data)>84

def build(p,out):
    front=p['front']; screen=p['screen']; pcb=p['pcb']; car=p['carrier']
    fz=pcb['b_z']+pcb['thickness']; join=front['join_z']; wall=car['wall']; rearz=car['rear_cover_inner_z']
    fx,fy=front['center_xy']; fw,fh=front['outer_xy']; sx,sy=screen['center_xy']; sw,sh=screen['lcm_xy']
    sz=screen['front_z']; screenback=sz+screen['thickness_assumed']; sidegap=screen['pocket_side_clearance']
    ux,uy=car['upper_center_xy']; uw,uh=car['upper_outer_xy']; lx,ly=car['lower_pod_center_xy']; lw,lh=car['lower_pod_outer_xy']
    # Independent screen frame; its display aperture is explicitly an assumption.
    frame=rounded(fx,fy,0,fw,fh,join,front['corner_radius'])
    pocket=box(sx,sy,front['lip_thickness'],sw+2*sidegap,sh+2*sidegap,join+2)
    window=box(sx,sy,-1,*front['window_xy_assumed'],join+2)
    frame=frame.cut(pocket).cut(window)
    # Front join bosses remain entirely outside the measured LCD rectangle.
    screw_metal=[]
    for x,y in car['panel_join_screws']:
        boss=cylinder(x,y,3,2.8,join-3)
        frame=frame.fuse(boss).cut(cylinder(x,y,join-5,car['case_insert_pilot']/2,6))
        screw_metal.append((f'front_insert_{x}_{y}',cylinder(x,y,join-5,car['case_insert_pilot']/2,5)))
    # Mic capsule is mounted in the plastic header above the LCD, on a grommet.
    mic=p['audio']['microphone']; mx,my=mic['center_xy']
    frame=frame.cut(cylinder(mx,my,-1,mic['acoustic_hole_diameter']/2,join+2))
    micseat=join+3
    frame=frame.fuse(rounded(mx,my,join,16,9,4.2,2))
    frame=frame.cut(cylinder(mx,my,join-.2,mic['grommet_pocket_diameter']/2,3.3))
    frame=frame.cut(cylinder(mx,my,micseat,mic['carrier_pocket_diameter']/2,2))
    miccap=rounded(mx,my,join+4.2,16,9,1.2,2)
    # Open the back of the wire solder pads, and route the twisted pair out of
    # the lower edge without loading the microphone's hidden SMT solder joints.
    wirecut=box(mx,my+3.5,micseat,5,4,4)
    frame=frame.cut(wirecut);miccap=miccap.cut(wirecut)
    for x in (mx-6,mx+6):
        frame=frame.cut(cylinder(x,my,join+.8,.8,4))
        miccap=miccap.cut(cylinder(x,my,join+4,1.1,2))
        screw_metal.append((f'mic_cap_screw_{x}',cylinder(x,my,join+1,1,4.4)))
    # Rear carrier: compact upper electronics tub plus a narrower speaker/base pod.
    audio_pocket=p['carrier']['audio_harness_pocket']
    po=audio_pocket['outer_xy'];pi=audio_pocket['inner_xy']
    outer2d=lambda z,h: union([rounded(ux,uy,z,uw,uh,h,3),rounded(lx,ly,z,lw,lh,h,3),rounded((po[0]+po[2])/2,(po[1]+po[3])/2,z,po[2]-po[0],po[3]-po[1],h,1.5)])
    inner2d=lambda z,h: union([rounded(ux,uy,z,uw-2*wall,uh-2*wall,h,2),rounded(lx,ly-1,z,lw-2*wall,lh,h,2),rounded((pi[0]+pi[2])/2,(pi[1]+pi[3])/2,z,pi[2]-pi[0],pi[3]-pi[1],h,.5)])
    flange=rounded(fx,fy,join,fw,fh,car['flange_thickness'],front['corner_radius'])
    carrier=flange.fuse(outer2d(join,rearz-join)).cut(inner2d(join-1,rearz-join+2))
    # The microphone/grommet and strain-relief tabs are carried by the front
    # frame; their passage through the carrier flange must remain unobstructed.
    carrier=carrier.cut(box(mx,my,join-1,18,10,8))
    # The LCD frame lip engages a shallow ledge; four clips only locate the seam.
    clip_solids=[]
    for x,y,sgn in [(fx-fw/2+3,fy-10,-1),(fx+fw/2-3,fy-10,1),(fx-30,fy+fh/2-3,0),(fx+30,fy+fh/2-3,0)]:
        if sgn:
            tongue=box(x,y,join-3,1.2,7,3.2)
            tip=box(x+sgn*.45,y,join-1,.8,6,1)
            receiver=box(x+sgn*.1,y,join-3.2,2.1,7.6,3.4)
        else:
            tongue=box(x,y,join-3,7,1.2,3.2)
            tip=box(x,y+.45,join-1,6,.8,1)
            receiver=box(x,y+.1,join-3.2,7.6,2.1,3.4)
        frame=frame.cut(receiver)
        carrier=carrier.fuse(tongue.fuse(tip))
        clip_solids.append(tongue)
    for x,y in car['panel_join_screws']:
        carrier=carrier.cut(cylinder(x,y,join-1,car['case_screw_clearance']/2,car['flange_thickness']+2))
    # PCB fixes from the open rear. B face seats on pillars; F remains accessible.
    for x,y in pcb['holes']:
        post=cylinder(x,y,join,pcb['boss_outer_diameter']/2,pcb['b_z']-join)
        # Corner ribs join each pillar to a nearby sidewall while avoiding board interior.
        edge_x=ux-uw/2+wall/2 if x<ux else ux+uw/2-wall/2
        rib=box((x+edge_x)/2,y,join,abs(x-edge_x)+1,2.5,pcb['b_z']-join)
        carrier=carrier.fuse(post).fuse(rib).cut(cylinder(x,y,join-1,pcb['boss_pilot_diameter']/2,pcb['b_z']-join+2))
    # Rear cover and its screw bosses are independent of PCB four-hole mounting.
    cover=outer2d(rearz,car['rear_cover_thickness'])
    for x,y in car['rear_cover_screws']:
        boss=cylinder(x,y,rearz-6,2.8,6)
        carrier=carrier.fuse(boss).cut(cylinder(x,y,rearz-6.2,pcb['boss_pilot_diameter']/2,6.5))
        cover=cover.cut(cylinder(x,y,rearz-1,car['case_screw_clearance']/2,4))
        screw_metal.append((f'cover_screw_{x}_{y}',cylinder(x,y,rearz-5,1.25,7)))
    # Side apertures include nominal overmold access; labels remain in documentation.
    apertures=[]
    for pt in p['ports']:
        cz=pt.get('center_z_absolute',fz+pt.get('center_z_offset_from_f',0)); along=pt['center_along']; w,h=pt['window_wh']
        if pt['wall'] in ['left','right']:
            px=ux+(-uw/2 if pt['wall']=='left' else uw/2)
            # Extend the left port tunnel through the closed audio harness pocket;
            # its continuous separating wall remains behind the USB cable aperture.
            depth=22 if pt['wall']=='left' else wall+6
            cut=rounded(px-5 if pt['wall']=='left' else px,along,cz-h/2,depth,w,h,.5)
        else:
            py=uy-uh/2
            cut=rounded(along,py,cz-h/2,w,wall+6,h,.5)
        carrier=carrier.cut(cut)
        apertures.append({**pt,'absolute_center_z':cz,'cut_bbox':bb(cut)})
    # Guided power cap. Only the large power switch gets a finger-operated cap;
    # the 0.2 mm travel RST/BOOT switches use recessed service holes.
    power=next(q for q in p['ports'] if q['ref']=='SW3')
    px=power['center_along']; py=uy-uh/2; pz=fz+power['center_z_offset_from_f']
    powercap=box(px,py-1.5,pz-3,12,3.2,6).fuse(box(px,py+1.4,pz-3,5.5,3,6)).fuse(box(px,py+2.8,pz-3,16.7,1,6))
    powercap=powercap.cut(box(px,py+1.3,pz-2.15,4.3,5.2,4.3)).clean()
    for xx in [-10.1,10.1]:
        carrier=carrier.fuse(box(px+xx,py+1.8,pz-3.3,1.2,3.2,6.6))
    # Supplied switch STEP depicts its left detent; place the cap on that detent.
    powercap=powercap.translate((-1.1,0,0))
    # Separate sealed rear cup and front duct for the 40 x 20 mm speaker.
    # Supplier drawing p6 requires a conservative 8 mm total envelope.
    sp=p['audio']['speaker']; ax,ay=sp['center_xy']; aw,ah=sp['body_xy_assumed']; border=sp['gasket_border']; sf=sp['front_z']
    cup=rounded(ax,ay,sf+1,45,25,rearz-sf-1,2)
    cup=cup.cut(rounded(ax,ay,sf+1-.1,41,21,rearz-sf+1,1))
    cover=cover.fuse(cup)
    # A 1 mm shoulder supports the steel rim, never the diaphragm/basket.
    shoulder=box(ax,ay,sf+1,41,21,1).cut(rounded(ax,ay,sf+.9,36.6,16.6,1.2,4))
    cover=cover.fuse(shoulder)
    # The removable insert adjusts the net rear chamber after subtracting the
    # provisional basket/magnet model. Actual displacement needs a sample.
    vi=sp['volume_insert_front_z_mm']
    volumeinsert=rounded(ax,ay,vi,40.6,20.6,rearz-vi-.15,.8)
    # A sealed front baffle/duct faces the lower LEFT side toward the driver.
    ductouter=union([rounded(ax,ay,12.8,50,30,6.5,2),box(4,ay,12.8,12,20,6.5)])
    ductinner=union([rounded(ax,ay,14,36,16,6,4),box(4,ay,14,40,16,4.8)])
    baffle=ductouter.cut(ductinner)
    # Flared side outlet is in the pod, never in the removable rear cover.
    outlet=box(-1,ay,12.4,7,21,7.3)
    carrier=carrier.cut(outlet)
    # Four fixing posts and screws clamp a full perimeter gasket at Z19.3..20.
    for xx in [-22.5,22.5]:
        for yy in [-12.5,12.5]:
            post=cylinder(ax+xx,ay+yy,19.3,2.4,rearz-19.3)
            cover=cover.fuse(post).cut(cylinder(ax+xx,ay+yy,18.5,.8,8))
            baffle=baffle.cut(cylinder(ax+xx,ay+yy,12.5,1.1,8))
    # Silicone-sealed cable feedthrough in the cup's left upper wall; grommet
    # and wire dress are explicit assembly operations, not assumed airtight CAD.
    wirebore=cq.Workplane('YZ').circle(.9).extrude(8).translate((ax-26,ay-7,sf+5)).val()
    cover=cover.cut(wirebore)
    bars=[]
    # Base force path goes into the lower pod and sidewalls, not the PCB.
    base=p['base']; bz=base['boss_center_z']; by=base['bottom_y']; bw=base['reinforcement_width']
    baseboss=box(lx,by-4,bz-5,bw,8,10)
    for x in base['hole_center_x']:
        drill=cq.Workplane('XZ').circle(base['pilot_diameter']/2).extrude(12).translate((x,by+1,bz)).val()
        baseboss=baseboss.cut(drill)
    carrier=carrier.fuse(baseboss)
    # Local screw-post relief stays away from the two base load-fastener axes.
    for xx in [-22.5,22.5]:
        for yy in [-12.5,12.5]:
            carrier=carrier.cut(cylinder(ax+xx,ay+yy,19.2,2.7,rearz-19.2))
    # Geometric reference objects. No imported PCB routing or vendor body detail is claimed.
    panel=box(sx,sy,sz,sw,sh,screen['thickness_assumed'])
    x0,y0,x1,y1=pcb['outline']; board=box((x0+x1)/2,(y0+y1)/2,pcb['b_z'],x1-x0,y1-y0,pcb['thickness'])
    for x,y in pcb['holes']: board=board.cut(cylinder(x,y,pcb['b_z']-1,pcb['hole_diameter']/2,4))
    ant=p['rf']['antenna_xy']; antenna=box((ant[0]+ant[2])/2,(ant[1]+ant[3])/2,fz+p['rf']['plane_z_offset_from_f'],ant[2]-ant[0],ant[3]-ant[1],.01)
    speaker=box(ax,ay,sf,aw,ah,1).fuse(rounded(ax,ay,sf+1,36,16,4.5,5)).fuse(cylinder(ax,ay,sf+1,14.43/2,7))
    # Body detail is a drawing-derived nominal model; corner screw-hole spacing
    # is unspecified, so the enclosure uses perimeter clamping instead.
    micbody=cylinder(mx,my,micseat-mic['capsule_depth_assumed'],mic['capsule_diameter_assumed']/2,mic['capsule_depth_assumed'])
    for i in range(6):
        a=i*math.pi/3
        micbody=micbody.cut(cylinder(mx+math.cos(a),my+math.sin(a),micseat-mic['capsule_depth_assumed']-.01,.125,.21))
    micpcb=cylinder(mx,my,micseat,mic['carrier_diameter']/2,mic['carrier_thickness'])
    for dx in [-2.8,2.8]:micpcb=micpcb.cut(cylinder(mx+dx,my+.6,micseat-.1,.5,1))
    # LCD flex length: two quarter circles around the lower board edge.
    flex=p['fpc']['lcd']; radius=flex['bend_radius']; cy=flex['mouth_xy'][1]; rooty=flex['tail_root_xy'][1]
    contactz=fz+flex['connector_contact_z_offset_from_f']; rootz=screenback+flex['tail_root_z_offset_from_screen_back']; dz=contactz-rootz
    edge_exit=y1+flex['board_edge_clearance']; turn_y=edge_exit+2*radius
    path_centerline=(rooty-cy)+dz+(math.pi-4)*radius
    required=path_centerline+flex['engagement_allowance']+flex['assembly_slack']
    flexinfo={'status':'UNCONFIRMED','root_xy_assumed':flex['tail_root_xy'],'root_z_assumed':rootz,'mouth_xy_candidate':flex['mouth_xy'],'contact_z_assumed':contactz,'bend_radius_assumed':radius,'centerline_estimate_mm':round(path_centerline,4),'engagement_mm':flex['engagement_allowance'],'assembly_slack_mm':flex['assembly_slack'],'required_usable_length_estimate_mm':round(required,4),'seller_reference_length_mm':flex['reference_length'],'reference_margin_mm':round(flex['reference_length']-required,4),'actual_usable_length_mm':flex['actual_usable_length'],'limitation':'LCD root, usable tail length, bend radius and contact face unconfirmed; this is NOT a fit pass.'}
    assert dz>=2*radius and rooty>turn_y and edge_exit>cy, 'LCD bend geometry cannot be constructed from parameters'
    # Thin 2D ribbon profile in YZ, swept along its 15.5 mm width; renderer/reference only.
    xf=flex['mouth_xy'][0]; thick=flex['flex_thickness_assumed']; width=flex['flex_width']
    path=[(rooty,rootz),(turn_y,rootz)]
    # First arc toward +Z, then second toward -Y.
    c1=(turn_y,rootz+radius)
    for i in range(1,13):
        a=-math.pi/2-i*math.pi/24;path.append((c1[0]+radius*math.cos(a),c1[1]+radius*math.sin(a)))
    path.append((edge_exit+radius,contactz-radius))
    c2=(edge_exit,contactz-radius)
    for i in range(1,13):
        a=i*math.pi/24;path.append((c2[0]+radius*math.cos(a),c2[1]+radius*math.sin(a)))
    path.append((cy,contactz))
    poly=path+[(y,z+thick) for y,z in reversed(path)]
    ribbon=cq.Workplane('YZ').polyline(poly).close().extrude(width).translate((xf-width/2,0,0)).val()
    # Validity, actual solid separation and conservative RF envelope checks.
    parts={'front_frame':frame.clean(),'pcb_carrier':carrier.clean(),'rear_service_cover':cover.clean(),'power_slide_cap':powercap,'microphone_retainer':miccap.clean(),'speaker_baffle_duct':baffle.clean(),'speaker_volume_insert':volumeinsert.clean()}
    for i,part in enumerate(bars): parts[f'speaker_retainer_{i+1}']=part
    report={'revision':p['revision'],'parameters_sha256':hashlib.sha256(json.dumps(p,sort_keys=True).encode()).hexdigest(),'units':'mm','output_coordinate_transform':'STEP/STL right-handed axes: X = PCB X, Y = -PCB Y, Z = display-to-rear; reported layout bounds retain PCB XY','parts':{},'part_pair_collisions':[],'rf':{},'lcd_fpc':flexinfo,'touch_fpc':{'status':'UNCONFIRMED','reason':'Tail root and usable length are not supplied'},'apertures':apertures,'unqualified':p['unqualified'],'limits':['Uses schematic/placement envelopes, not a final routed PCB STEP assembly','All shell dimensions are prototype assumptions, not manufacturing qualification','Mic/speaker and port mating details must be checked on samples','Clips only locate; screws retain the assembly; plastic strength/load/thermal tests not performed']}
    for name,shape in parts.items():
        assert shape.isValid(),f'{name}: invalid OCCT solid'
        assert len(shape.Solids())==1,f'{name}: expected a connected printable body, got {len(shape.Solids())}'
        report['parts'][name]={'valid':True,'solids':len(shape.Solids()),'bbox':bb(shape),'volume_mm3':round(shape.Volume(),3)}
        cq.exporters.export(shape.mirror('XZ'),str(out/f'{name}.step'));check_files(out/f'{name}.step')
        cq.exporters.export(shape.mirror('XZ'),str(out/f'{name}.stl'),tolerance=.08,angularTolerance=.15);check_files(out/f'{name}.stl')
    for n1,n2 in [('front_frame','pcb_carrier'),('front_frame','rear_service_cover'),('pcb_carrier','rear_service_cover'),('rear_service_cover','speaker_baffle_duct'),('rear_service_cover','speaker_volume_insert'),('speaker_baffle_duct','pcb_carrier'),('power_slide_cap','pcb_carrier'),('microphone_retainer','front_frame'),('microphone_retainer','pcb_carrier')]:
        v=parts[n1].intersect(parts[n2]).Volume()
        if v>1e-4:report['part_pair_collisions'].append({'parts':[n1,n2],'intersection_volume_mm3':round(v,5)})
    screengap=antenna.distance(panel)
    metal=[('screen_metal_envelope',panel)]+screw_metal+[('speaker_envelope_conservative_metal',speaker)]
    distances=[{'object':n,'distance_mm':round(antenna.distance(s),4)} for n,s in metal]
    xy_gap=max(0,sy-sh/2-ant[3]); vertical_needed=math.sqrt(max(0,p['rf']['metal_clearance_target']**2-xy_gap**2))
    report['rf']={'target_mm':p['rf']['metal_clearance_target'],'screen_distance_mm':round(screengap,4),'minimum_modelled_metal_distance_mm':min(d['distance_mm'] for d in distances),'modelled_distances':distances,'geometry_meets_target_for_assumed_dimensions':all(d['distance_mm']>=p['rf']['metal_clearance_target'] for d in distances),'maximum_screen_thickness_at_other_nominals_mm':round(fz-sz-vertical_needed,4),'qualification':'UNCONFIRMED: repeat with actual panel metal, PCB placement, screws, brackets and harnesses; no RF performance test'}
    report['pcb_screen_clearance_mm']=round(board.distance(panel),4)
    report['panel_frame_intersection_mm3']=round(panel.intersect(frame).Volume(),6)
    report['pcb_carrier_intersection_mm3']=round(board.intersect(carrier).Volume(),6)
    report['fpc_frame_intersection_mm3']=round(ribbon.intersect(frame).Volume(),6)
    report['fpc_carrier_intersection_mm3']=round(ribbon.intersect(carrier).Volume(),6)
    rearair=rounded(ax,ay,sf+1,41,21,rearz-sf-1,1).cut(speaker).cut(shoulder).cut(volumeinsert)
    report['speaker']={'part':sp['part'],'status':sp['status'],'conservative_dimensions_mm':[40,20,8],'front_z_mm':sf,'nominal_model_net_rear_chamber_cc':round(rearair.Volume()/1000,4),'volume_insert_front_z_mm':30.25,'volume_limit':'Derived from provisional basket and magnet model; measure actual displaced volume and tune insert before acoustic approval','sound_outlet':'Left pod wall, Y11/Z12.4..19.7; front sealed duct, rear solid cover','body_intersections_mm3':{n:round(speaker.intersect(s).Volume(),6) for n,s in parts.items()},'conflicts':sp['conflicts']}
    report['power_cap']={'nominal_travel_mm':2.2,'shown_detent':'left, matching the actual switch STEP','shaft_center_z_offset_from_f_mm':3.3,'stroke_end_intersections_mm3':[round(powercap.translate((dx,0,0)).intersect(carrier).Volume(),6) for dx in [0,2.2]],'status':'Prototype socket and hard stops; check actual stem, travel and fit before use'}
    report['microphone_carrier']={'diameter_mm':mic['carrier_diameter'],'thickness_mm':mic['carrier_thickness'],'pcb_front_z':micseat,'mic_acoustic_face_z':micseat-mic['capsule_depth_assumed'],'retainer_inner_z':join+4.2,'board_intersections_mm3':{n:round(micpcb.intersect(s).Volume(),6) for n,s in parts.items()},'capsule_intersections_mm3':{n:round(micbody.intersect(s).Volume(),6) for n,s in parts.items()},'fixing':'Two M2 x 4 screws and compliant insulating pads; wire exit faces +PCB Y; compression and threads need prototype calibration'}
    # Complete assembly and deliberately separated exploded assembly.
    reference={'LCD_envelope_ASSUMED':panel,'PCB_envelope_CANDIDATE':board,'C6_antenna_KEEP_METAL_CLEAR':antenna,'LCD_flex_UNCONFIRMED':ribbon,'wired_speaker_ASSUMED':speaker,'wired_microphone_ASSUMED':micbody,'microphone_carrier_PCB':micpcb}
    # The four edge cushions only preload the panel metal perimeter, not its
    # active display area; location/thickness remain dependent on the real module.
    for i,(dx,dy) in enumerate([(-sw/2+4,-sh/2+4),(sw/2-4,-sh/2+4),(-sw/2+4,sh/2-4),(sw/2-4,sh/2-4)]):
        reference[f'panel_edge_foam_{i+1}_ASSUMED']=box(sx+dx,sy+dy,screenback,6,6,join-screenback)
    palette={'front_frame':(.62,.66,.7),'pcb_carrier':(.78,.81,.84),'rear_service_cover':(.58,.63,.68),'power_slide_cap':(.20,.28,.36),'LCD_envelope_ASSUMED':(.07,.12,.18),'PCB_envelope_CANDIDATE':(.08,.38,.20),'C6_antenna_KEEP_METAL_CLEAR':(.85,.25,.65),'LCD_flex_UNCONFIRMED':(.78,.43,.07),'wired_speaker_ASSUMED':(.1,.1,.11),'wired_microphone_ASSUMED':(.7,.7,.68)}
    for exploded in (False,True):
        ass=cq.Assembly(name='A7_fit_prototype_exploded' if exploded else 'A7_fit_prototype')
        for name,shape in {**parts,**reference}.items():
            dz=0
            if exploded:
                dz=35 if name=='rear_service_cover' or name.startswith('speaker_') or name=='wired_speaker_ASSUMED' else (12 if name=='PCB_envelope_CANDIDATE' else (-20 if name in ['front_frame','LCD_envelope_ASSUMED','wired_microphone_ASSUMED','microphone_carrier_PCB','microphone_retainer'] else 0))
            ass.add(shape.mirror('XZ').translate((0,0,dz)),name=name,color=cq.Color(*palette.get(name,(.55,.56,.57))))
        name='assembly_exploded.step' if exploded else 'assembly.step'
        ass.export(str(out/name));check_files(out/name)
    (out/'mechanical-check.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
    # Meshes for an independent VTK inspection render.
    render=[]
    for name,shape in {**parts,**reference}.items():
        path=out/f'_view_{name}.stl';cq.exporters.export(shape.mirror('XZ'),str(path),tolerance=.12,angularTolerance=.2)
        render.append({'name':name,'path':str(path),'color':palette.get(name,(.55,.56,.57))})
        if name in reference:cq.exporters.export(shape.mirror('XZ'),str(out/f'reference-{name}.step'))
    (out/'render-parts.json').write_text(json.dumps(render,indent=2),encoding='utf8')
    print(json.dumps({'parts':report['parts'],'rf':report['rf'],'lcd_fpc':flexinfo,'collisions':report['part_pair_collisions'],'panel_frame_intersection_mm3':report['panel_frame_intersection_mm3'],'pcb_carrier_intersection_mm3':report['pcb_carrier_intersection_mm3'],'fpc_carrier_intersection_mm3':report['fpc_carrier_intersection_mm3']},ensure_ascii=False),flush=True)
    assert not report['part_pair_collisions'],'Assembly solids interfere; see report'
    assert report['panel_frame_intersection_mm3']<1e-4,'Screen collides with frame'
    assert report['pcb_carrier_intersection_mm3']<1e-4,'PCB collides with carrier'
    assert report['fpc_carrier_intersection_mm3']<1e-4,'Reference ribbon collides with carrier'
    assert max(report['speaker']['body_intersections_mm3'].values())<1e-4,'Speaker body interferes'
    assert max(report['power_cap']['stroke_end_intersections_mm3'])<1e-4,'Power cap collides at a stroke end'
    assert max(report['microphone_carrier']['board_intersections_mm3'].values())<1e-4,'Mic board interferes'
    assert max(report['microphone_carrier']['capsule_intersections_mm3'].values())<1e-4,'Mic capsule interferes'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--parameters',type=Path,default=HERE/'case-parameters.json');ap.add_argument('--output',type=Path,default=HERE/'models');ap.add_argument('--windows-fast-exit',action='store_true');a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    build(json.loads(a.parameters.read_text(encoding='utf8')),a.output)
    return a.windows_fast_exit

if __name__=='__main__':
    fast=main()
    if fast and sys.platform=='win32':sys.stdout.flush();sys.stderr.flush();os._exit(0)
