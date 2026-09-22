"""Stage checked ES8389 and top-port MEMS footprints/models in scratch."""
import argparse,hashlib,json,shutil
from pathlib import Path
import pcbnew as k
from cad import ROOT


def create(out):
    source=ROOT/'display_board/hardware/display-carrier'
    assert source.resolve()!=out.resolve(), 'Stage audio assets into scratch, not the formal project'
    shutil.copytree(source,out,dirs_exist_ok=True)
    manifest=json.loads((source/'model-manifest.json').read_text(encoding='utf-8'))
    manifest['models']=[r for r in manifest['models'] if r['reference'] not in ['U800','U802','U803','J800','J810']]
    fpdir=out/'Carrier.pretty';models=out/'Carrier.3dshapes'
    plugin=k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP)
    def v(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
    def rectangle(fp,w,h,layer):
        pts=[(-w/2,-h/2),(w/2,-h/2),(w/2,h/2),(-w/2,h/2)]
        for a,b in zip(pts,pts[1:]+pts[:1]):
            g=k.PCB_SHAPE(fp);g.SetShape(k.SHAPE_T_SEGMENT);g.SetStart(v(*a));g.SetEnd(v(*b))
            g.SetLayer(layer);g.SetWidth(k.FromMM(.05 if layer==k.F_CrtYd else .1));fp.Add(g)
    def bind(fp,name,refs,modelpath,kind,src,note):
        fp.SetFPID(k.LIB_ID('Carrier',name));fp.SetReference('REF**');fp.SetValue(name)
        fp.Models().clear();m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+modelpath.name;fp.Add3DModel(m)
        plugin.FootprintSave(str(fpdir),fp)
        for ref in refs:manifest['models'].append(dict(reference=ref,original_footprint='Carrier:'+name,
            footprint='Carrier:'+name,file=modelpath.name,offset_mm=[0,0,0],rotation_deg=[0,0,0],scale=[1,1,1],
            kind=kind,source=src,note=note,sha256=hashlib.sha256(modelpath.read_bytes()).hexdigest()))
    imported=ROOT/'build/carrier-audio/es8389-part/libs'
    path=next((imported/'easyeda2kicad.pretty').glob('*.kicad_mod'))
    fp=k.FootprintLoad(str(path.parent),path.stem)
    assert len(list(fp.Pads()))==25
    pads={p.GetNumber():p for p in fp.Pads()}
    assert abs(k.ToMM(pads['25'].GetSize().x)-1.7)<.001
    assert all(abs(abs(k.ToMM(pads[str(n)].GetPosition().x))-1.43)<.001 for n in range(1,7))
    assert all(abs(abs(k.ToMM(pads[str(n)].GetPosition().y)-k.ToMM(pads[str(n+1)].GetPosition().y))-.35)<.011 for n in range(1,6))
    # Supplier courtyard includes duplicate/zero-length segments. Keep those
    # construction marks on Fab and provide one closed, valid courtyard.
    for g in fp.GraphicalItems():
        if g.GetLayer()==k.F_CrtYd:g.SetLayer(k.F_Fab)
    rectangle(fp,4,4,k.F_CrtYd);rectangle(fp,3,3,k.F_Fab)
    # Reduce EP paste area; final window segmentation requires assembly review.
    pads['25'].SetLocalSolderPasteMarginRatio(-.15)
    dest=models/'ES8389_QFN24.step';shutil.copy2(next((imported/'easyeda2kicad.3dshapes').glob('*.step')),dest)
    bind(fp,'ES8389_QFN24_3x3_P0.35',['U800'],dest,'supplier',
         'https://www.lcsc.com/product-detail/C22380673.html',
         'Supplier land geometry checked: QFN24 3x3, 0.35 pitch, EP1.7; pad25 GND. Added courtyard.')
    # Manufacturer M141 v1.1 p6: viewed from PCB top, VDD is top-right.
    fp=k.FOOTPRINT(None);fp.SetAttributes(k.FP_SMD)
    for n,x,y in [(1,.85,-1.23),(2,.85,1.23),(3,-.85,1.23),(4,-.85,-1.23)]:
        p=k.PAD(fp);p.SetNumber(str(n));p.SetPosition(v(x,y));p.SetSize(v(.8,.8))
        p.SetAttribute(k.PAD_ATTRIB_SMD);p.SetShape(k.PAD_SHAPE_CIRCLE);p.SetLayerSet(k.PAD.SMDMask());fp.Add(p)
    rectangle(fp,2.95,3.76,k.F_Fab);rectangle(fp,3.6,4.4,k.F_CrtYd)
    g=k.PCB_SHAPE(fp);g.SetShape(k.SHAPE_T_CIRCLE);g.SetStart(v(.005,-.75));g.SetEnd(v(.1675,-.75))
    g.SetLayer(k.F_Fab);g.SetWidth(k.FromMM(.05));fp.Add(g)
    dest=models/'LMA3729T381_TopPort_reference.wrl'
    dest.write_text('#VRML V2.0 utf8\n# 2.95x3.76x1.00mm top-port body reference, not vendor CAD\n'
        'Transform { scale 0.3937007874 0.3937007874 0.3937007874 children [\n'
        'Transform { translation 0 0 0.5 children [ Shape { appearance Appearance { material Material { diffuseColor 0.65 0.65 0.62 } } geometry Box { size 2.95 3.76 1.0 } } ] }\n'
        'Transform { translation 0.005 0.75 1.001 rotation 1 0 0 1.5707963268 children [ Shape { appearance Appearance { material Material { diffuseColor 0.03 0.03 0.03 } } geometry Cylinder { radius 0.1625 height 0.002 } } ] }\n'
        '] }\n',encoding='utf-8')
    bind(fp,'LMA3729T381_TopPort',['U802','U803'],dest,'datasheet_reference',
         'https://atta.szlcsc.com/upload/public/pdf/source/20240604/C1447F1D03265B3E9B89676302ED84F3.pdf',
         'Manufacturer circular 0.8mm lands, 1.70x2.46 pitch; top port offset (+0.005,-0.75). Envelope model only.')
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True)
    create(ap.parse_args().output.resolve())
