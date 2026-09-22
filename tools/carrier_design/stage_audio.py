"""Replace only the unrouted audio blocks, preserving all other PCB objects.

Run after audio_assets.py. Produces scratch CAD; native ERC/DRC, independent
pad/net checks and visual review are required before promoting it.
"""
import argparse,copy,json,shutil,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
import pcbnew as k
import cad
from cad import ROOT,OUT,LIBS,PROJECT,ROOT_ID,uid,sexp
from board_spec import d
from models import stage_library

AUDIO={'11_codec','12_fm_audio','18_dual_mic','19_stereo_pa'}


def stage(baseline,library,output):
    assert baseline.resolve()!=output.resolve()
    old=sexp.load_file(str(baseline/'display_carrier.kicad_pcb'))
    def reference(f):return next(sexp.atoms(x)[1] for x in sexp.find_all(f,'property') if sexp.atoms(x)[0]=='Reference')
    all_old={reference(f):f for f in sexp.find_all(old,'footprint')}
    def is_audio(f):
        sheet=sexp.find(f,'sheetfile')
        return sheet is not None and Path(sexp.atoms(sheet)[0]).stem in AUDIO
    changed={r for r,f in all_old.items() if is_audio(f)}
    unchanged=set(all_old)-changed
    for p in d.parts:
        if p['ref'] in unchanged:
            f=all_old[p['ref']];xyz=list(map(float,sexp.atoms(sexp.find(f,'at'))))
            p.update(pcb=[xyz[0]-50,xyz[1]-50],angle=xyz[2] if len(xyz)>2 else 0,
                     side=sexp.atoms(sexp.find(f,'layer'))[0][0])
    d.write()
    shutil.copytree(baseline,output,dirs_exist_ok=True)
    for pattern in ['*.kicad_sch','Carrier.kicad_sym','sym-lib-table','design-intent.json']:
        for path in OUT.glob(pattern):shutil.copy2(path,output/path.name)
    stage_library(output,library)
    reportdir=output.parent
    subprocess.run([str(LIBS.parents[1]/'bin/kicad-cli.exe'),'sch','export','netlist','--format','kicadxml',
        '--output',str(reportdir/'netlist.xml'),str(output/(PROJECT+'.kicad_sch'))],check=True,capture_output=True)
    xml=ET.parse(reportdir/'netlist.xml').getroot()
    padnet={(z.get('ref'),z.get('pin')):n.get('name') for n in xml.findall('./nets/net') for z in n.findall('node')}
    # Strip only explicitly replaced audio footprints and obsolete PA reservation.
    kept=[]
    for node in old:
        tag=sexp.tag_of(node) if isinstance(node,list) else ''
        if tag=='footprint' and reference(node) in changed:continue
        if tag=='gr_text' and 'NS4814 / SPEAKER' in sexp.atoms(node)[0]:continue
        if tag=='gr_line' and sexp.atoms(sexp.find(node,'layer'))[0]=='Dwgs.User':
            pts=[list(map(float,sexp.atoms(sexp.find(node,p)))) for p in ['start','end']]
            if {round(p[0]-50,3) for p in pts}<={96.,115.} and {round(p[1]-50,3) for p in pts}<={61.,78.}:continue
        kept.append(node)
    temp=reportdir/'audio-input.kicad_pcb';sexp.save_file(str(temp),kept)
    b=k.LoadBoard(str(temp));assert not len(b.GetTracks())
    assert not any(not z.GetIsRuleArea() for z in b.Zones())
    def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
    def pos(x,y):return v(x+50,y+50)
    def bbox(f):
        z=f.GetBoundingBox(False,False)
        return tuple(k.ToMM(x)-50 for x in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()])
    def overlap(a,c,gap=.25):return a[0]<c[2]+gap and a[2]>c[0]-gap and a[1]<c[3]+gap and a[3]>c[1]-gap
    occupied={k.F_Cu:[],k.B_Cu:[]}
    for f in b.GetFootprints():
        z=bbox(f);occupied[f.GetLayer()].append((f.GetReference(),z))
        if any(p.GetDrillSize().x>0 for p in f.Pads()):occupied[k.B_Cu if f.GetLayer()==k.F_Cu else k.F_Cu].append((f.GetReference(),z))
    for z in [(30,8,42,85),(59,46.3,82,85)]:occupied[k.F_Cu].append(('FLEX',z))
    for layer in occupied:occupied[layer].append(('THERMAL',(117,72,127,85)))
    nets={n.GetNetname():n for n in b.GetNetInfo().NetsByNetcode().values()}
    for name in sorted(set(padnet.values())):
        if name not in nets:
            n=k.NETINFO_ITEM(b,name);b.Add(n);nets[name]=n
    # Major audio interfaces and two identical top-port microphones are fixed.
    fixed={'U800':(22,65,'B',0),'U801':(11,61,'B',0),
           'U802':(20,73,'B',0),'U803':(80,73,'B',0),
           'U815':(41,66,'B',0),'U810':(35,62,'B',0),
           'U811':(49,63,'B',0),'U812':(53,63,'B',0),'U813':(49,67,'B',0),'U814':(53,67,'B',0),
           'J810':(52,74,'B',0),'Y810':(41,70,'B',0),
           'U820':(92,67,'F',0),'U821':(107,67,'F',0),'U822':(101,59,'F',0),'L820':(106,59,'F',0),
           'J820':(94,73,'F',0),'J821':(109,73,'F',0)}
    audio=[p for p in d.parts if p['sheet'] in AUDIO and p['footprint']]
    loaded={}
    for p in audio:
        lib,name=p['footprint'].split(':',1)
        folder=output/'Carrier.pretty' if lib=='Carrier' else LIBS/'footprints'/(lib+'.pretty')
        f=k.FootprintLoad(str(folder),name);assert f,(p['ref'],folder,name)
        f.SetFPID(k.LIB_ID(lib,name));f.SetReference(p['ref']);f.SetValue(p['value'])
        f.SetSheetname('/'+d.sheets[p['sheet']]+'/');f.SetSheetfile(p['sheet']+'.kicad_sch')
        path=k.KIID_PATH()
        for value in [ROOT_ID,uid('sheet:'+p['sheet']),uid('part:'+p['ref'])]:path.push_back(k.KIID(value))
        f.SetPath(path)
        for field,val in [('Datasheet',p['source']),('DesignStatus',p['status'])]:
            f.SetField(field,val);f.GetField(field).SetVisible(False)
        f.Reference().SetVisible(False);f.Value().SetVisible(False);f.SetDNP(p['dnp'])
        b.Add(f)
        x,y,side,angle=fixed.get(p['ref'],(*p['pcb'],'F' if p['sheet']=='19_stereo_pa' else 'B',0))
        f.SetPosition(pos(x,y))
        if side=='B':f.Flip(f.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
        f.SetOrientationDegrees(angle)
        for pad in f.Pads():pad.SetNet(nets[padnet[(p['ref'],pad.GetNumber())]])
        loaded[p['ref']]=(p,f)
    def accept(f):
        z=bbox(f);side=f.GetLayer();both=any(p.GetDrillSize().x>0 for p in f.Pads())
        if z[0]<.6 or z[1]<.6 or z[2]>144.4 or z[3]>76.8:return False
        return not any(overlap(z,c) for layer in (list(occupied) if both else [side]) for _,c in occupied[layer])
    def lock(f):
        z=bbox(f);occupied[f.GetLayer()].append((f.GetReference(),z))
        if any(p.GetDrillSize().x>0 for p in f.Pads()):occupied[k.B_Cu if f.GetLayer()==k.F_Cu else k.F_Cu].append((f.GetReference(),z))
    for ref in fixed:
        p,f=loaded[ref]
        assert accept(f),(ref,bbox(f),[(r,c) for layer in occupied for r,c in occupied[layer] if overlap(bbox(f),c)])
        lock(f)
    # Keep decoupling/filter components in their functional zone, nearest their
    # associated IC before seeking other free grid sites; do not move other blocks.
    regions={'11_codec':(5,56,29,76),'12_fm_audio':(30,58,60,76),
             '18_dual_mic':(6,57,86,76),'19_stereo_pa':(85,54,116,76)}
    anchors={
        'C800':(10,58),'C801':(10,65),'C802':(23,61),'C803':(19,62),'C804':(19,65),
        'C805':(25,63),'C806':(25,66),'C807':(25,69),
        'C813':(35,66),'C814':(43,63),'C812':(35,59),'D810':(56,72),'C822':(46,70),
        'R840':(17,70),'C840':(20,69),'C848':(23,73),
        'R841':(80,67),'C844':(80,69),'C849':(83,73),
        'U822':(101,59),'C876':(98,59),'C877':(110,57),'C878':(110,60),
        'R866':(101,55),'R867':(104,55)}
    for i in range(2):
        for n in range(7):anchors['C'+str(860+8*i+n)]=(92+15*i,63+(n%3)*4)
        for n in range(3):anchors['R'+str(860+3*i+n)]=(88+15*i,65+n*3)
    candidates=[]
    for p,f in loaded.values():
        if p['ref'] in fixed:continue
        x0,y0=anchors.get(p['ref'],(22,66) if p['sheet']=='11_codec' else (48,70) if p['sheet']=='12_fm_audio' else (21,67) if p['sheet']=='18_dual_mic' else (98,66))
        candidates.append((p,f,x0,y0))
    candidates.sort(key=lambda a:-((bbox(a[1])[2]-bbox(a[1])[0])*(bbox(a[1])[3]-bbox(a[1])[1])))
    for p,f,x0,y0 in candidates:
        x1,y1,x2,y2=regions[p['sheet']]
        # ADC coupling caps stay near the codec; only local mic supply parts sit at right mic.
        if p['sheet']=='18_dual_mic' and p['ref'] not in ['R841','C844','C849']:x2=29
        if p['sheet']=='18_dual_mic' and p['ref'] in ['R841','C844','C849']:x1,x2=75,86
        sites=[(x0,y0)]+sorted(((x/2,y/2) for x in range(int(2*x1),int(2*x2)) for y in range(int(2*y1),int(2*y2))),key=lambda xy:(xy[0]-x0)**2+(xy[1]-y0)**2)
        found=False
        for x,y in sites:
            f.SetPosition(pos(x,y));z=bbox(f)
            if z[0]<x1 or z[2]>x2 or z[1]<y1 or z[3]>y2:continue
            if accept(f):found=True;break
        if not found:raise RuntimeError(('No free audio position',p['ref'],p['sheet']))
        lock(f)
    for p,f in loaded.values():
        p.update(pcb=[round(k.ToMM(f.GetPosition().x)-50,3),round(k.ToMM(f.GetPosition().y)-50,3)],
                 side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
    # Save native footprint changes, then transplant only the new audio nodes
    # back into the lossless baseline tree. Other footprint ordering is retained.
    native=reportdir/'audio-native.kicad_pcb';k.SaveBoard(str(native),b)
    generated=sexp.load_file(str(native));fresh={reference(f):f for f in sexp.find_all(generated,'footprint')}
    result=[]
    for node in kept:
        if isinstance(node,list) and sexp.tag_of(node)=='footprint':
            ref=reference(node);assert node==all_old[ref]
        result.append(node)
    for ref in sorted(loaded):
        if ref in all_old:sexp.find(fresh[ref],'uuid')[:]=copy.deepcopy(sexp.find(all_old[ref],'uuid'))
        result.append(fresh[ref])
    sexp.save_file(str(output/(PROJECT+'.kicad_pcb')),result)
    # Preserve user design rules; native SaveBoard may create default sidecar files.
    shutil.copy2(baseline/(PROJECT+'.kicad_pro'),output/(PROJECT+'.kicad_pro'))
    d.write()
    for path in OUT.glob('*.kicad_sch'):shutil.copy2(path,output/path.name)
    shutil.copy2(OUT/'design-intent.json',output/'design-intent.json')
    newrefs=set(loaded)
    report={'baseline_commit':'88c088a','existing_non_audio_footprints_unchanged':len(unchanged),
        'removed_references':sorted(changed-newrefs),'added_references':sorted(newrefs-set(all_old)),
        'replaced_audio_references':sorted(changed & newrefs),
        'positions':{p['ref']:{'xy_mm':p['pcb'],'side':p['side'],'angle':p['angle']} for p in audio},
        'microphone_center_spacing_mm':60,'microphone_side':'B','microphone_port_offset_local_mm':[.005,-.75],
        'original_screen_flex_and_thermal_slot_geometry':'preserved',
        'status':'UNROUTED_REQUIRES_NATIVE_CHECKS'}
    (output/'AUDIO_LAYOUT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:val for key,val in report.items() if key!='positions'},indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['baseline','library','output']:ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();stage(a.baseline,a.library,a.output)
