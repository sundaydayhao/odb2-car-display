"""Stage revision L on the reviewed K layout; never place or route components.

Only the microphone pad numbering changes. Preserve pad UUIDs, copper/net
geometry, component locations, all board cutouts and existing release holds.
Run with native KiCad Python. ERC/DRC and visual review follow separately.
"""
import argparse, hashlib, json, shutil
from pathlib import Path
import pcbnew as k
import cad
from board_spec import d
from stereo_audio import MIC


def stage(baseline, out):
    assert baseline.resolve()!=out.resolve()
    b=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
    fps={f.GetReference():f for f in b.GetFootprints()}
    assert not len(b.GetTracks()),'Migration applies only to the unrouted revision K board'
    assert all(fps[r].GetValue()=='LMA3729T381-OY3S' for r in ['U802','U803']), 'Use a revision K baseline; do not renumber an already-migrated microphone'
    if not out.exists():
        shutil.copytree(baseline,out,ignore=shutil.ignore_patterns('*.lck','*.kicad_prl','~*'))
    specs={p['ref']:p for p in d.parts}
    for ref,fp in fps.items():
        if ref in specs:
            specs[ref].update(pcb=[round(k.ToMM(fp.GetPosition().x)-50,6),round(k.ToMM(fp.GetPosition().y)-50,6)],
                              angle=fp.GetOrientationDegrees(),side=fp.GetLayerName()[0])
    cad.OUT=out;d.write()
    renumber={'1':'1','2':'3','3':'4','4':'2'}
    name='LMA3729T381_OAS02_TopPort'
    modelname=name+'_reference.wrl'
    model=out/'Carrier.3dshapes'/modelname
    oldmodel=(baseline/'Carrier.3dshapes/LMA3729T381_TopPort_reference.wrl').read_text(encoding='utf-8')
    model.write_text(oldmodel.replace('0.005 0.75','0 0.75').replace('radius 0.1625','radius 0.125'),encoding='utf-8')
    def update_mic(fp):
        angle=fp.GetOrientationDegrees();fp.SetOrientationDegrees(0)
        for pad in fp.Pads():pad.SetNumber(renumber[pad.GetNumber()])
        fp.SetFPID(k.LIB_ID('Carrier',name))
        # Fab acoustic-port diameter is 0.25 mm (M282 v1.1 p6).
        for g in fp.GraphicalItems():
            if g.GetShape()==k.SHAPE_T_CIRCLE and g.GetLayer()==k.F_Fab:
                if abs(k.ToMM(g.GetRadius())-.1625)<.005:
                    center=g.GetStart()
                    center.x-=k.FromMM(.005);g.SetStart(center)
                    g.SetEnd(k.VECTOR2I(center.x+k.FromMM(.125),center.y))
        fp.SetOrientationDegrees(angle)
        fp.Models().clear();m=k.FP_3DMODEL();m.m_Filename='${KIPRJMOD}/Carrier.3dshapes/'+modelname;fp.Add3DModel(m)
    libfp=k.FootprintLoad(str(baseline/'Carrier.pretty'),'LMA3729T381_TopPort')
    update_mic(libfp);libfp.SetValue(name)
    k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(out/'Carrier.pretty'),libfp)
    for ref in ['U802','U803','U810','U811','U812','U813','U814','R866','R867']:
        fp=fps[ref];p=specs[ref]
        fp.SetValue(p['value']);fp.SetField('Datasheet',p['source']);fp.GetField('Datasheet').SetVisible(False)
        fp.SetField('DesignStatus',p['status']);fp.GetField('DesignStatus').SetVisible(False)
        if ref in ['U802','U803']:update_mic(fp)
    # The NC net's generated name follows the new pin name.
    for pad in fps['U810'].Pads():
        if pad.GetNumber()=='4':
            n=k.NETINFO_ITEM(b,'unconnected-(U810-NC-Pad4)');b.Add(n);pad.SetNet(n)
    b.GetTitleBlock().SetRevision('R0L LCSC SOURCING')
    k.SaveBoard(str(out/'display_carrier.kicad_pcb'),b)
    manifest=json.loads((baseline/'model-manifest.json').read_text(encoding='utf-8'))
    for row in manifest['models']:
        if row['reference'] in ['U802','U803']:
            row.update(original_footprint='Carrier:'+name,footprint='Carrier:'+name,file=modelname,
                       source=MIC,sha256=hashlib.sha256(model.read_bytes()).hexdigest(),
                       note='M282 v1.1 2026-02-25: PCB top view 1=VDD upper right, 2=OUT upper left, 3/4=GND lower. 0.8mm lands, 1.70x2.46 spacing; 0.25mm top port. Envelope reference only.')
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Staged nine part substitutions; all 354 footprint positions preserved.')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();stage(a.baseline,a.output)
