"""Stage the revision-I SIM8066 replacement on an unrouted carrier.

Run with native KiCad Python. Only J1000, C1030/R1033 values and the new
R1034 change; all other board objects are preserved through AST replacement.
Supplier files are fetched into scratch; reviewed library assets live locally.
"""
import argparse,copy,hashlib,json,shutil,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
import pcbnew as k
from cad import ROOT,OUT,LIBS,PROJECT,ROOT_ID,sexp,uid
from board_spec import d

NAME='nanoSIM_GCT_SIM8066_PushPush'
VALUE='GCT SIM8066-6-1-14-01-A'

def make_library(out):
    source=ROOT/'build/carrier-sim/sim8066-part/libs'
    path=next((source/'easyeda2kicad.pretty').glob('*.kicad_mod'))
    fp=k.FootprintLoad(str(path.parent),path.stem)
    def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
    def rect(x1,y1,x2,y2,layer):
        pts=[(x1,y1),(x2,y1),(x2,y2),(x1,y2)]
        for a,b in zip(pts,pts[1:]+pts[:1]):
            g=k.PCB_SHAPE(fp);g.SetShape(k.SHAPE_T_SEGMENT);g.SetStart(v(*a));g.SetEnd(v(*b));g.SetLayer(layer)
            g.SetWidth(k.FromMM(.05 if layer==k.F_CrtYd else .1));fp.Add(g)
    # The downloaded holes are wrongly plated; manufacturer pegs need NPTH.
    signal_x={'3':-4.83,'7':-3.56,'2':-2.29,'6':-1.02,'1':.25,'5':1.52,'CD':2.47}
    for pad in fp.Pads():
        n=pad.GetNumber()
        if n in signal_x:
            assert abs(k.ToMM(pad.GetPosition().x)-signal_x[n])<.001
            assert abs(k.ToMM(pad.GetPosition().y)+6.74)<.001
            # A1 manufacturer drawing uses 0.80 x 1.14mm solder areas.
            pad.SetSize(v(.8,1.14))
        if n.startswith('SH'):pad.SetNumber('SH')
        if n=='CD':pad.SetNumber('SW')
        if not n:
            pad.SetAttribute(k.PAD_ATTRIB_NPTH);pad.SetLayerSet(k.PAD.UnplatedHoleMask())
            pad.SetDrillSize(v(.75,.75));pad.SetSize(v(.75,.75))
    assert {p.GetNumber() for p in fp.Pads()}=={'1','2','3','5','6','7','SW','SH',''}
    # The supplier's courtyard excludes solder tabs. Preserve its outline on Fab.
    for g in fp.GraphicalItems():
        if g.GetLayer()==k.F_CrtYd:g.SetLayer(k.F_Fab)
    rect(-7.1,-7.6,7.1,8.05,k.F_CrtYd)
    rect(-6.145,-6.23,6.145,7.49,k.F_Fab)  # 12.29 x 13.72 body envelope
    # Conservative component-side copper exclusion covers all contact windows.
    zone=k.ZONE(fp);zone.SetLayer(k.F_Cu);zone.SetIsRuleArea(True)
    zone.SetDoNotAllowTracks(True);zone.SetDoNotAllowVias(True);zone.SetDoNotAllowZoneFills(True)
    zone.SetDoNotAllowPads(False);zone.SetDoNotAllowFootprints(False)
    poly=zone.Outline();poly.NewOutline()
    for x,y in [(-5.9,-5.6),(5.9,-5.6),(5.9,7.6),(-5.9,7.6)]:poly.Append(k.FromMM(x),k.FromMM(y))
    fp.Add(zone)
    fp.SetFPID(k.LIB_ID('Carrier',NAME));fp.SetReference('REF**');fp.SetValue(VALUE)
    model_name='SIM8066-6-1-14-01-A.step'
    target=out/'Carrier.3dshapes'/model_name
    shutil.copy2(next((source/'easyeda2kicad.3dshapes').glob('*.step')),target)
    fp.Models().clear();m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+model_name;fp.Add3DModel(m)
    k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(out/'Carrier.pretty'),fp)
    manifest=json.loads((out/'model-manifest.json').read_text(encoding='utf-8'))
    manifest['models']=[r for r in manifest['models'] if r['reference']!='J1000']
    manifest['models'].append(dict(reference='J1000',original_footprint='Carrier:'+NAME,footprint='Carrier:'+NAME,
        file=model_name,offset_mm=[0,0,0],rotation_deg=[0,0,0],scale=[1,1,1],kind='supplier_package',
        source='https://www.lcsc.com/product-detail/C3032925.html',
        note='Push-push SIM8066. Supplier STEP, not precision enclosure approval. Land pattern checked against GCT A1; pegs corrected to NPTH; 4 shell tabs share SH/GND.',
        sha256=hashlib.sha256(target.read_bytes()).hexdigest(),raw_model_bounds_mm=[-6.545,6.545,-7.29,7.29,-.4,1.42]))
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

def stage(baseline,out):
    assert baseline.resolve()!=out.resolve()
    out.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(baseline,out,dirs_exist_ok=True)
    make_library(out)
    old=sexp.load_file(str(baseline/(PROJECT+'.kicad_pcb')))
    def ref(f):return next(sexp.atoms(p)[1] for p in sexp.find_all(f,'property') if sexp.atoms(p)[0]=='Reference')
    before={ref(f):f for f in sexp.find_all(old,'footprint')}
    assert not list(sexp.find_all(old,'segment')),'Use a routed ECO workflow for routed boards'
    modified={'J1000','C1030','R1033'}
    for p in d.parts:
        if p['ref'] in before:
            f=before[p['ref']];xyz=list(map(float,sexp.atoms(sexp.find(f,'at'))))
            p.update(pcb=[xyz[0]-50,xyz[1]-50],angle=xyz[2] if len(xyz)>2 else 0,side=sexp.atoms(sexp.find(f,'layer'))[0][0])
        if p['ref']=='J1000':p.update(pcb=[136.8,29],side='B',angle=270)
        if p['ref']=='R1034':p.update(pcb=[127.2,33],side='B',angle=90)
    d.write()
    for pattern in ['*.kicad_sch','Carrier.kicad_sym','design-intent.json']:
        for p in OUT.glob(pattern):shutil.copy2(p,out/p.name)
    reports=out.parent
    subprocess.run([str(LIBS.parents[1]/'bin/kicad-cli.exe'),'sch','export','netlist','--format','kicadxml',
        '--output',str(reports/'netlist.xml'),str(out/(PROJECT+'.kicad_sch'))],check=True,capture_output=True)
    xml=ET.parse(reports/'netlist.xml').getroot()
    padnet={(z.get('ref'),z.get('pin')):n.get('name') for n in xml.findall('./nets/net') for z in n.findall('node')}
    kept=[n for n in old if not (isinstance(n,list) and sexp.tag_of(n)=='footprint' and ref(n) in modified)]
    temp=reports/'sim-input.kicad_pcb';sexp.save_file(str(temp),kept)
    b=k.LoadBoard(str(temp));assert not len(b.GetTracks())
    assert not any(not z.GetIsRuleArea() for z in b.Zones())
    nets={n.GetNetname():n for n in b.GetNetInfo().NetsByNetcode().values()}
    for name in set(padnet.values()):
        if name not in nets:n=k.NETINFO_ITEM(b,name);b.Add(n);nets[name]=n
    for p in d.parts:
        if p['ref'] not in modified|{'R1034'}:continue
        lib,name=p['footprint'].split(':',1)
        folder=out/'Carrier.pretty' if lib=='Carrier' else LIBS/'footprints'/(lib+'.pretty')
        f=k.FootprintLoad(str(folder),name);assert f
        f.SetFPID(k.LIB_ID(lib,name));f.SetReference(p['ref']);f.SetValue(p['value'])
        f.SetSheetname('/'+d.sheets[p['sheet']]+'/');f.SetSheetfile(p['sheet']+'.kicad_sch')
        path=k.KIID_PATH()
        for value in [ROOT_ID,uid('sheet:'+p['sheet']),uid('part:'+p['ref'])]:path.push_back(k.KIID(value))
        f.SetPath(path)
        for field,value in [('Datasheet',p['source']),('DesignStatus',p['status'])]:f.SetField(field,value);f.GetField(field).SetVisible(False)
        f.Reference().SetVisible(False);f.Value().SetVisible(False);f.SetDNP(p['dnp']);b.Add(f)
        f.SetPosition(k.VECTOR2I(*[k.FromMM(x+50) for x in p['pcb']]))
        if p['side']=='B':f.Flip(f.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
        f.SetOrientationDegrees(p['angle'])
        for pad in f.Pads():
            if pad.GetNumber():pad.SetNet(nets[padnet[(p['ref'],pad.GetNumber())]])
    native=reports/'sim-native.kicad_pcb';k.SaveBoard(str(native),b)
    fresh={ref(f):f for f in sexp.find_all(sexp.load_file(str(native)),'footprint')}
    final=[]
    for node in old:
        if isinstance(node,list) and sexp.tag_of(node)=='footprint' and ref(node) in modified:
            name=ref(node);node=copy.deepcopy(fresh[name]);sexp.find(node,'uuid')[:]=copy.deepcopy(sexp.find(before[name],'uuid'))
        final.append(node)
    final.append(fresh['R1034']);sexp.save_file(str(out/(PROJECT+'.kicad_pcb')),final)
    print('Staged push-push SIM8066; 350 footprints plus 4 mounting holes; native checks required')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();stage(args.baseline.resolve(),args.output.resolve())
