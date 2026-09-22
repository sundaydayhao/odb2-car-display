"""Build the datasheet-reviewed OPT3001 land pattern in a scratch library.

Run with KiCad Python. TI DNP0006A drawing 4221434/C, January 2018:
six 0.50 x 0.25 lands, 1.90 row spacing, 0.65 pitch, 0.65 x 1.35 EP.
The small VRML body is an explicitly dimensioned reference, not supplier CAD.
"""
import argparse, hashlib, json, shutil
from pathlib import Path
import pcbnew as k
from cad import LIBS, ROOT


def create(out):
    source = ROOT / 'display_board/hardware/display-carrier'
    shutil.copytree(source, out, dirs_exist_ok=True)
    fp = k.FOOTPRINT(None)
    name = 'OPT3001_DNP0006A'
    fp.SetFPID(k.LIB_ID('Carrier', name))
    fp.SetReference('REF**'); fp.SetValue('OPT3001DNPR'); fp.SetAttributes(k.FP_SMD)
    fp.SetLibDescription('TI OPT3001 DNP0006A; land pattern 4221434/C Jan2018; EP=7 to GND. Body model is a dimensional reference.')
    def v(x,y):return k.VECTOR2I(k.FromMM(x), k.FromMM(y))
    for n,x,y,w,h in [(1,-.95,-.65,.5,.25),(2,-.95,0,.5,.25),(3,-.95,.65,.5,.25),
                      (4,.95,.65,.5,.25),(5,.95,0,.5,.25),(6,.95,-.65,.5,.25),(7,0,0,.65,1.35)]:
        p=k.PAD(fp);p.SetNumber(str(n));p.SetAttribute(k.PAD_ATTRIB_SMD)
        p.SetShape(k.PAD_SHAPE_OVAL if n!=7 else k.PAD_SHAPE_RECT)
        p.SetPosition(v(x,y));p.SetSize(v(w,h));p.SetLayerSet(k.PAD.SMDMask());fp.Add(p)
    for layer,x,y in [(k.F_Fab,1,1),(k.F_CrtYd,1.45,1.25)]:
        pts=[(-x,-y),(x,-y),(x,y),(-x,y)]
        for a,b in zip(pts,pts[1:]+pts[:1]):
            s=k.PCB_SHAPE();s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(v(*a));s.SetEnd(v(*b))
            s.SetLayer(layer);s.SetWidth(k.FromMM(.05 if layer==k.F_CrtYd else .1));fp.Add(s)
    mark=k.PCB_SHAPE();mark.SetShape(k.SHAPE_T_SEGMENT);mark.SetStart(v(-1,-.5));mark.SetEnd(v(-.5,-1));mark.SetLayer(k.F_Fab);mark.SetWidth(k.FromMM(.1));fp.Add(mark)
    # KiCad's VRML unit is 0.1 inch; scale the 2 x 2 x 0.65 mm body explicitly.
    modelpath=out/'Carrier.3dshapes/OPT3001_DNP_reference.wrl'
    modelpath.write_text('#VRML V2.0 utf8\n# Dimension reference only; TI body 2x2x0.65 mm\n'
        'Transform { scale 0.3937007874 0.3937007874 0.3937007874 children [\n'
        'Transform { translation 0 0 0.325 children [ Shape { appearance Appearance { material Material { diffuseColor 0.25 0.20 0.15 transparency 0.12 } } geometry Box { size 2 2 0.65 } } ] }\n'
        '] }\n',encoding='utf-8')
    m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+modelpath.name;fp.Add3DModel(m)
    plugin=k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP)
    plugin.FootprintSave(str(out/'Carrier.pretty'),fp)
    manifest=json.loads((out/'model-manifest.json').read_text(encoding='utf-8'))
    manifest['models']=[r for r in manifest['models'] if r['reference'] not in {'U1300','U1301'}]
    manifest['models'].append(dict(reference='U1300',original_footprint='Carrier:'+name,footprint='Carrier:'+name,
        file=modelpath.name,offset_mm=[0,0,0],rotation_deg=[0,0,0],scale=[1,1,1],kind='datasheet_reference',
        source='https://www.ti.com/lit/ds/symlink/opt3001.pdf',
        note='Body envelope only, 2x2x0.65mm. Not manufacturer STEP or optical/mechanical sign-off.',
        sha256=hashlib.sha256(modelpath.read_bytes()).hexdigest()))
    sht=k.FootprintLoad(str(LIBS/'footprints/Sensor_Humidity.pretty'),
        'Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad')
    sht.SetFPID(k.LIB_ID('Carrier','SHT40_DFN4_NoCentralPad'))
    sht.SetValue('SHT40-AD1B-R2');sht.Models().clear()
    sht_model=out/'Carrier.3dshapes/SHT40_DFN4_reference.wrl'
    sht_model.write_text('#VRML V2.0 utf8\n# SHT40 standard body reference, no membrane, no manufacturer CAD\n'
        'Transform { scale 0.3937007874 0.3937007874 0.3937007874 children [\n'
        'Transform { translation 0 0 0.25 children [ Shape { appearance Appearance { material Material { diffuseColor 0.1 0.1 0.1 } } geometry Box { size 1.5 1.5 0.5 } } ] }\n'
        '] }\n',encoding='utf-8')
    sm=k.FP_3DMODEL();sm.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+sht_model.name;sht.Add3DModel(sm)
    plugin.FootprintSave(str(out/'Carrier.pretty'),sht)
    manifest['models'].append(dict(reference='U1301',original_footprint='Carrier:SHT40_DFN4_NoCentralPad',
        footprint='Carrier:SHT40_DFN4_NoCentralPad',file=sht_model.name,offset_mm=[0,0,0],
        rotation_deg=[0,0,0],scale=[1,1,1],kind='datasheet_reference',
        source='https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf',
        note='Official KiCad pad geometry, no center copper; nominal body 1.5x1.5x0.5 mm reference only.',
        sha256=hashlib.sha256(sht_model.read_bytes()).hexdigest()))
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    create(p.parse_args().output.resolve())
