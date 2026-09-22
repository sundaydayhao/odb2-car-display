"""Stage revision J on an unrouted carrier, preserving every object except L400.

Native KiCad Python; source supplier STEP is fetched separately into scratch.
Land dimensions from Cjiang FEXU drawing p4: A=6.50, B=2.00 GAP, C=6.30.
The supplier footprint rounds centers and its courtyard excludes the pads;
construct the reviewed geometry explicitly rather than copying those defects.
"""
import argparse, copy, hashlib, json, shutil
from pathlib import Path
import pcbnew as k
from cad import ROOT, OUT, PROJECT, sexp
from board_spec import d

NAME='L_Cjiang_FEXU0640_6.2x6.0mm'
MPN='FEXU0640S2R2MGS'
PDF='https://atta.szlcsc.com/upload/public/pdf/source/20260302/EED1CBF0FC8744AFBE91D5FB993027E7.pdf'

def make_library(out):
    fp=k.FOOTPRINT(None);fp.SetFPID(k.LIB_ID('Carrier',NAME))
    fp.SetReference('REF**');fp.SetValue(MPN);fp.SetAttributes(k.FP_SMD)
    fp.SetLibDescription('Cjiang FEXU0640S; 6.2x6.0x4.0 max; land A6.50/B2.00 gap/C6.30; LCSC C54140025')
    def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
    for n,x in [('1',-2.125),('2',2.125)]:
        p=k.PAD(fp);p.SetNumber(n);p.SetAttribute(k.PAD_ATTRIB_SMD)
        p.SetShape(k.PAD_SHAPE_RECT);p.SetPosition(v(x,0));p.SetSize(v(2.25,6.3))
        p.SetLayerSet(k.PAD.SMDMask());fp.Add(p)
    def line(a,b,layer,width):
        g=k.PCB_SHAPE(fp);g.SetShape(k.SHAPE_T_SEGMENT);g.SetStart(v(*a));g.SetEnd(v(*b))
        g.SetLayer(layer);g.SetWidth(k.FromMM(width));fp.Add(g)
    for layer,x,y in [(k.F_Fab,3.1,3.0),(k.F_CrtYd,3.5,3.45)]:
        pts=[(-x,-y),(x,-y),(x,y),(-x,y)]
        for a,b in zip(pts,pts[1:]+pts[:1]):line(a,b,layer,.05 if layer==k.F_CrtYd else .1)
    for y in [-3.45,3.45]:line((-3.1,y),(3.1,y),k.F_SilkS,.12)
    fp.Reference().SetPosition(v(0,-4.2));fp.Value().SetPosition(v(0,4.2))
    source=ROOT/'build/carrier-inductor/cjiang0640-part/libs/easyeda2kicad.3dshapes/IND-SMD_L6.2-W6.0_FEXU0640S.step'
    target=out/'Carrier.3dshapes'/f'{MPN}.step';shutil.copy2(source,target)
    m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+target.name
    # Supplier terminal underside is z=-0.10; seat it on the board surface.
    m.m_Offset.z=.1;fp.Add3DModel(m)
    k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(out/'Carrier.pretty'),fp)
    manifest=json.loads((out/'model-manifest.json').read_text(encoding='utf-8'))
    manifest['models']=[r for r in manifest['models'] if r['reference']!='L400']
    manifest['models'].append(dict(reference='L400',original_footprint='Carrier:'+NAME,footprint='Carrier:'+NAME,
        file=target.name,offset_mm=[0,0,.1],rotation_deg=[0,0,0],scale=[1,1,1],kind='supplier_package',
        source='https://www.lcsc.com/product-detail/C54140025.html',
        note='Supplier STEP, raw z=-0.10..3.90, seated height 4.0mm. Nominal envelope checked against Cjiang drawing; not precision enclosure approval.',
        sha256=hashlib.sha256(target.read_bytes()).hexdigest(),raw_model_bounds_mm=[-3.1,3.1,-3.01,3,-.1,3.9]))
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

def stage(baseline,out):
    assert baseline.resolve()!=out.resolve()
    shutil.copytree(baseline,out,dirs_exist_ok=True);make_library(out)
    old=sexp.load_file(str(baseline/(PROJECT+'.kicad_pcb')))
    def ref(f):return next(sexp.atoms(p)[1] for p in sexp.find_all(f,'property') if sexp.atoms(p)[0]=='Reference')
    before={ref(f):f for f in sexp.find_all(old,'footprint')}
    assert not list(sexp.find_all(old,'segment')),'Routed boards require a routed ECO workflow'
    for p in d.parts:
        if p['ref'] in before:
            f=before[p['ref']];xyz=list(map(float,sexp.atoms(sexp.find(f,'at'))))
            p.update(pcb=[xyz[0]-50,xyz[1]-50],angle=xyz[2] if len(xyz)>2 else 0,side=sexp.atoms(sexp.find(f,'layer'))[0][0])
    d.write()  # Built-in geometric verification before each generated sheet write.
    for name in ['06_main_power.kicad_sch',PROJECT+'.kicad_sch','design-intent.json']:
        shutil.copy2(OUT/name,out/name)
    # All other sheets must agree with the source but remain byte-identical.
    for path in baseline.glob('[0-9]*.kicad_sch'):
        if path.name!='06_main_power.kicad_sch':assert path.read_bytes()==(OUT/path.name).read_bytes(),path.name
    b=k.LoadBoard(str(baseline/(PROJECT+'.kicad_pcb')));assert not len(b.GetTracks())
    original=next(f for f in b.GetFootprints() if f.GetReference()=='L400')
    padnets={p.GetNumber():p.GetNet() for p in original.Pads()}
    f=k.FootprintLoad(str(out/'Carrier.pretty'),NAME);f.SetFPID(k.LIB_ID('Carrier',NAME))
    part=next(p for p in d.parts if p['ref']=='L400')
    f.SetReference('L400');f.SetValue(part['value']);f.SetPath(original.GetPath())
    f.SetSheetname(original.GetSheetname());f.SetSheetfile(original.GetSheetfile())
    for field,value in [('Datasheet',part['source']),('DesignStatus',part['status'])]:
        f.SetField(field,value);f.GetField(field).SetVisible(False)
    f.Reference().SetVisible(False);f.Value().SetVisible(False);b.Add(f)
    f.SetPosition(original.GetPosition())
    if original.GetLayer()==k.B_Cu:f.Flip(f.GetPosition(),k.FLIP_DIRECTION_LEFT_RIGHT)
    f.SetOrientationDegrees(original.GetOrientationDegrees())
    for pad in f.Pads():pad.SetNet(padnets[pad.GetNumber()])
    b.Remove(original)
    native=out.parent/'inductor-native.kicad_pcb';k.SaveBoard(str(native),b)
    replacement=next(f for f in sexp.find_all(sexp.load_file(str(native)),'footprint') if ref(f)=='L400')
    sexp.find(replacement,'uuid')[:]=copy.deepcopy(sexp.find(before['L400'],'uuid'))
    final=[replacement if isinstance(n,list) and sexp.tag_of(n)=='footprint' and ref(n)=='L400' else n for n in old]
    sexp.save_file(str(out/(PROJECT+'.kicad_pcb')),final)
    print('Staged L400 Cjiang FEXU0640S2R2MGS; native checks and render review required')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();stage(a.baseline.resolve(),a.output.resolve())
