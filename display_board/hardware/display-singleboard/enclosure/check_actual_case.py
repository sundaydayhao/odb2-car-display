"""Check actual KiCad-exported bodies against the prototype enclosure.

The PCB and enclosure STEP files both use right-handed native +Y up. Planning
JSON retains PCB +Y down, which is reflected only while creating its case shapes.
"""
import argparse,json,os,sys,hashlib
from pathlib import Path
import cadquery as cq

ROOT=Path(__file__).resolve().parent
def box(x,y,z,w,h,d):return cq.Workplane('XY').box(w,h,d,centered=(True,True,False)).translate((x,y,z)).val()
def bb(s):
    b=s.BoundingBox();return [getattr(b,k) for k in ['xmin','ymin','zmin','xmax','ymax','zmax']]
def disjoint(a,b):return any(a[i]>b[i+3]+1e-7 or b[i]>a[i+3]+1e-7 for i in range(3))
def volume(a,b):
    if disjoint(bb(a),bb(b)):return 0.0
    return round(a.intersect(b).Volume(),6)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--directory',type=Path,default=ROOT/'models');a=ap.parse_args();out=a.directory
    p=json.loads((ROOT/'case-parameters.json').read_text());fz=p['pcb']['b_z']+p['pcb']['thickness']
    # KiCad STEP contains dielectric 0..1.51; top model mounting datum includes
    # copper/mask and is 1.595 mm. This is independently visible at the J302 base.
    datum=1.595;shift=fz-datum
    def read(name):return cq.importers.importStep(str(out/name)).val()
    def actual(ref):return read(f'actual-{ref}.step').translate((0,0,shift))
    case={name:read(name+'.step') for name in ['front_frame','pcb_carrier','rear_service_cover','power_slide_cap','speaker_baffle_duct','speaker_volume_insert','microphone_retainer']}
    x,y=p['screen']['center_xy'];w,h=p['screen']['lcm_xy'];panel=box(x,-y,p['screen']['front_z'],w,h,p['screen']['thickness_assumed'])
    refs=['J19','J21','J22','SW3','SW1','SW2','J1','J2','J302','J308','J9','J7','J16','U1']
    components={ref:actual(ref) for ref in refs}
    report={'status':'Actual exported body geometry; vendor/mating models retain their original qualification limits','input_step_sha256':{path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in [out/f'actual-{ref}.step' for ref in ['pcb','bare-pcb']+refs]},'parameters_sha256':hashlib.sha256((ROOT/'case-parameters.json').read_bytes()).hexdigest(),'coordinate_transform':{'native_step_model_f_datum_z_mm':datum,'case_f_datum_z_mm':fz,'z_translation_mm':shift,'y_axis':'Native STEP +Y up retained, no reflection of actual PCB; enclosure STEP uses X=PCB X,Y=-PCB Y'},'components':{},'mating_envelopes':{},'errors':[],'limits':['STEP bodies use the project models, including documented nominal/substitute models; this is not supplier tolerance validation','Actual panel glass/metal, flex, plugs, speaker and microphone carrier still need samples','J1/J2 mating socket dimensions are representative service reservations; no specific mating part has been qualified']}
    provenance=json.loads((out/'actual-export-provenance.json').read_text())
    report['native_export_provenance']=provenance
    report['source_pcb_sha256']=provenance['pcb_sha256']
    assert all(provenance['outputs_sha256'].get(k)==v for k,v in report['input_step_sha256'].items()),'STEP changed after captured native export'
    for ref,s in components.items():
        col={name:volume(s,t) for name,t in case.items()}
        # Power shaft intentionally enters the empty socket, so even this overlap
        # is checked; no body intersections are silently accepted.
        col={name:v for name,v in col.items() if v>1e-4}
        v=volume(s,panel)
        if v>1e-4:col['screen_metal_envelope']=v
        report['components'][ref]={'bbox_mm':[round(v,4) for v in bb(s)],'valid':s.isValid(),'screen_distance_mm':round(s.distance(panel),4),'intersections_mm3':col}
        if col:report['errors'].append({'reference':ref,'intersections_mm3':col})
        cq.exporters.export(s,str(out/f'case-aligned-{ref}.step'))
    # Representative cable overmolds at the assembled case; insertion clearance
    # outside the shell is tested geometrically, with all assumptions listed.
    mating={
      'J19_USB_C_overmold':box(-44.1,-38.5,fz+1.6-3.5,22,13,7),
      'J22_5V_harness':box(-44,-65,fz+3.8-4.5,22,14,9),
      'J21_card_and_tool':box(96.1,-44.0824,18.8,16,12.2,1.0),
      'J1_mated_IDC_assumed':box(2,-23.5,fz+2.54,52,8,8.46),
      'J2_mated_socket_assumed':box(50,-17,fz+2.54,8.5,8,8.46),
      'J7_GH_mic_plug_assumed':box(-27.5,-57.5,fz+.4,6,4.5,3.2),
      'J16_speaker_plug_assumed':box(-34,-49.4822,fz+.4,8,6,5),
      'J16_speaker_wire_bend_assumed':box(-41,-49.4822,fz+1.0,6,3.5,4.5),
    }
    mating={name:s.mirror('XZ') for name,s in mating.items()}
    for name,s in mating.items():
        col={ref:volume(s,t) for ref,t in case.items()};col={ref:v for ref,v in col.items() if v>1e-4}
        report['mating_envelopes'][name]={'bbox_mm':[round(v,4) for v in bb(s)],'intersections_mm3':col,'qualification':'Representative envelope only; actual mating part not selected'}
        if col:report['errors'].append({'mating':name,'intersections_mm3':col})
        cq.exporters.export(s,str(out/f'check-{name}.step'))
    # A driver approaching the four PCB screws while the rear cover is removed.
    for i,(x,y) in enumerate(p['pcb']['holes']):
        tool=cq.Workplane('XY').circle(4).extrude(30).translate((x,-y,fz+.2)).val()
        col={ref:volume(tool,t) for ref,t in components.items()};col={ref:v for ref,v in col.items() if v>1e-4}
        col['carrier']=volume(tool,case['pcb_carrier']);col={ref:v for ref,v in col.items() if v>1e-4}
        report.setdefault('pcb_tool_access',[]).append({'hole':i+1,'tool_diameter_mm':8,'intersections_mm3':col})
        if col:report['errors'].append({'pcb_tool_hole':i+1,'intersections_mm3':col})
    # Whole real PCB, not just its empty board outline, against all three shells.
    full=actual('pcb');nativebare=actual('bare-pcb')
    whole={name:volume(full,shape) for name,shape in case.items()}
    report['all_actual_pcb_intersections_mm3']={name:v for name,v in whole.items() if v>1e-4}
    if report['all_actual_pcb_intersections_mm3']:report['errors'].append({'all_actual_pcb_intersections_mm3':report['all_actual_pcb_intersections_mm3']})
    cq.exporters.export(full,str(out/'case-aligned-actual-pcb.step'))
    native_parts=[]
    for s in full.Solids():
        q=bb(s)
        is_board=abs((q[3]-q[0])-120)<.01 and abs((q[4]-q[1])-74)<.01 and abs((q[5]-q[2])-1.51)<.02
        if not is_board:native_parts.append(s)
    rest=cq.Compound.makeCompound(native_parts)
    cq.exporters.export(nativebare,str(out/'_view_PCB_REAL_dielectric.stl'),tolerance=.12,angularTolerance=.2)
    cq.exporters.export(rest,str(out/'_view_PCB_REAL_components.stl'),tolerance=.12,angularTolerance=.2)
    render=json.loads((out/'render-parts.json').read_text());render=[q for q in render if q['name']!='PCB_envelope_CANDIDATE']
    render.extend([{'name':'PCB_REAL_dielectric','path':str(out/'_view_PCB_REAL_dielectric.stl'),'color':[.08,.38,.20]},{'name':'PCB_REAL_components','path':str(out/'_view_PCB_REAL_components.stl'),'color':[.55,.58,.60]}])
    (out/'render-parts-real.json').write_text(json.dumps(render,indent=2))
    # Named real-board assembly, keeping each case piece independent.
    references={}
    for path in out.glob('reference-*.step'):
        name=path.stem.removeprefix('reference-')
        if name!='PCB_envelope_CANDIDATE':references[name]=read(path.name)
    micpath=ROOT/'microphone-carrier/microphone_carrier.step'
    if micpath.exists():
        mx,my=p['audio']['microphone']['center_xy'];micfz=p['front']['join_z']+3
        micreal=cq.importers.importStep(str(micpath)).val().rotate((0,0,0),(0,1,0),180).translate((mx+10,-my+10,micfz+.795))
        miccol={name:volume(micreal,s) for name,s in case.items()};miccol={name:v for name,v in miccol.items() if v>1e-4}
        report['microphone_actual_carrier']={'source_sha256':hashlib.sha256(micpath.read_bytes()).hexdigest(),'rotation_y_deg':180,'f_plane_case_z_mm':micfz,'intersections_mm3':miccol,'limits':'Native 0.8 mm PCB top-model datum .795 mm; nominal microphone body derived from original drawing, not supplier tolerance model'}
        if miccol:report['errors'].append({'microphone_actual_carrier':miccol})
        references.pop('wired_microphone_ASSUMED',None);references.pop('microphone_carrier_PCB',None)
        references['microphone_actual_carrier']=micreal
        cq.exporters.export(micreal,str(out/'_view_microphone_actual_carrier.stl'),tolerance=.03,angularTolerance=.1)
        render=[q for q in render if q['name'] not in ['wired_microphone_ASSUMED','microphone_carrier_PCB']]
        render.append({'name':'microphone_actual_carrier','path':str(out/'_view_microphone_actual_carrier.stl'),'color':[.32,.43,.37]})
        (out/'render-parts-real.json').write_text(json.dumps(render,indent=2))
    for exploded in [False,True]:
        ass=cq.Assembly(name='A7_actual_PCB_fit_prototype_exploded' if exploded else 'A7_actual_PCB_fit_prototype')
        for name,s in case.items():
            dz=(35 if name=='rear_service_cover' or name.startswith('speaker_') else (-20 if name in ['front_frame','microphone_retainer'] else 0)) if exploded else 0
            ass.add(s.translate((0,0,dz)),name=name,color=cq.Color(.65,.69,.73))
        ass.add(nativebare.translate((0,0,12 if exploded else 0)),name='PCB_dielectric',color=cq.Color(.08,.38,.20));ass.add(rest.translate((0,0,12 if exploded else 0)),name='PCB_actual_components',color=cq.Color(.55,.58,.60))
        for name,s in references.items():
            dz=(35 if name=='wired_speaker_ASSUMED' else (-20 if name in ['LCD_envelope_ASSUMED','wired_microphone_ASSUMED','microphone_carrier_PCB','microphone_actual_carrier'] or name.startswith('panel_edge_foam') else 0)) if exploded else 0
            color=(.08,.12,.18) if name=='LCD_envelope_ASSUMED' else ((.78,.43,.07) if name=='LCD_flex_UNCONFIRMED' else (.6,.6,.6))
            ass.add(s.translate((0,0,dz)),name=name,color=cq.Color(*color))
        ass.export(str(out/('assembly_actual_pcb_exploded.step' if exploded else 'assembly_actual_pcb.step')))
    (out/'actual-component-check.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
    print(json.dumps({'errors':report['errors'],'parts_checked':len(components),'mating_envelopes_checked':len(mating),'actual_pcb_solids':len(full.Solids())},ensure_ascii=False),flush=True)
    assert not report['errors'],'Actual body or mating clearance check failed; see actual-component-check.json'

if __name__=='__main__':main();sys.stdout.flush();sys.stderr.flush();os._exit(0)
