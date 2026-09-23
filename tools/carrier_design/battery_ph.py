"""Change J300 to J301's JST PH header on the unrouted revision-L carrier.

The footprint and two pad UUIDs remain stable. Pin1 remains BAT_CONNECTOR,
pin2 remains GND. Current capacity is reduced: 2A with AWG24, not 10A.
"""
import argparse,json,shutil
from pathlib import Path
import pcbnew as k
import cad
from board_spec import d


def stage(baseline,out):
    assert baseline.resolve()!=out.resolve()
    assert not out.exists(),'Stage into a fresh scratch directory'
    shutil.copytree(baseline,out,ignore=shutil.ignore_patterns('*.lck','*.kicad_prl','~*','fp-info-cache'))
    b=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'));assert not len(b.GetTracks())
    fps={f.GetReference():f for f in b.GetFootprints()};old=fps['J300'];mate=fps['J301']
    assert 'JST_VH_B2P-VH' in old.GetFPIDAsString(),'Use the pre-PH revision-L baseline'
    assert mate.GetFPIDAsString()=='Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical'
    for p in d.parts:
        if p['ref'] in fps:
            fp=fps[p['ref']];p.update(pcb=[round(k.ToMM(fp.GetPosition().x)-50,6),round(k.ToMM(fp.GetPosition().y)-50,6)],side=fp.GetLayerName()[0],angle=fp.GetOrientationDegrees())
    cad.OUT=out;d.write()
    specs={p['ref']:p for p in d.parts}
    name=mate.GetFPID().GetLibItemName()
    fp=k.FootprintLoad(str(cad.LIBS/'footprints/Connector_JST.pretty'),name);assert fp
    fp.SetFPID(mate.GetFPID());fp.SetReference('J300');fp.SetValue(specs['J300']['value'])
    # KiCad 10 exposes UUIDs as read-only. Restore these identities after saving.
    uuid_map={fp.m_Uuid.AsString():old.m_Uuid.AsString()}
    fp.SetPath(old.GetPath());fp.SetSheetname(old.GetSheetname());fp.SetSheetfile(old.GetSheetfile())
    fp.SetPosition(old.GetPosition());fp.SetOrientationDegrees(old.GetOrientationDegrees());assert old.GetLayer()==k.F_Cu
    for prop in ['Datasheet','DesignStatus']:
        fp.SetField(prop,specs['J300']['source' if prop=='Datasheet' else 'status']);fp.GetField(prop).SetVisible(False)
    fp.Reference().SetVisible(False);fp.Value().SetVisible(False);fp.SetDNP(old.IsDNP())
    oldpads={p.GetNumber():p for p in old.Pads()}
    for p in fp.Pads():
        uuid_map[p.m_Uuid.AsString()]=oldpads[p.GetNumber()].m_Uuid.AsString()
        p.SetNet(oldpads[p.GetNumber()].GetNet())
    b.Remove(old);b.Add(fp)
    fuse=fps['F300'];fuse.SetValue(specs['F300']['value']);fuse.SetField('DesignStatus',specs['F300']['status']);fuse.GetField('DesignStatus').SetVisible(False)
    b.GetTitleBlock().SetRevision('R0L1 J300 PH');k.SaveBoard(str(out/'display_carrier.kicad_pcb'),b)
    path=out/'display_carrier.kicad_pcb';content=path.read_text(encoding='utf-8')
    for fresh,stable in uuid_map.items():
        assert content.count(fresh)==1 and stable not in content
        content=content.replace(fresh,stable)
    path.write_text(content,encoding='utf-8')
    manifest=json.loads((out/'model-manifest.json').read_text(encoding='utf-8'))
    manifest['models']=[r for r in manifest['models'] if r['reference']!='J300']
    (out/'model-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('J300 changed to J301 footprint/model; pin1 BAT_CONNECTOR, pin2 GND; F300 selection reopened.')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();stage(a.baseline,a.output)
