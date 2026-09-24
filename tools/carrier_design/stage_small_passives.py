"""Apply the approved package shrink to a scratch copy, retaining existing tracks.

Refuse to replace any footprint already contacted by copper. This makes this
operation safe after unrelated MIPI routing, but not after passive routing.
"""
import argparse,collections,copy,json
from pathlib import Path
import pcbnew as k
from cad import LIBS,sexp
from small_passives import selected_code,apply

def stage(project,stock_path):
    project=project.resolve()
    assert 'build' in project.parts,'Scratch project required'
    intent=json.loads((project/'design-intent.json').read_text('utf8'))
    before={p['ref']:copy.deepcopy(p) for p in intent['parts']}
    apply(intent['parts'])
    changed={p['ref']:p for p in intent['parts'] if p['footprint']!=before[p['ref']]['footprint']}
    assert changed,'No eligible 0603 parts'
    stock=json.loads(stock_path.read_text('utf8'))
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    fps={f.GetReference():f for f in b.GetFootprints()}
    report=[];identities={}
    for ref,p in changed.items():
        code=selected_code(p);s=stock[code]
        assert s['encapStandard']=='0402' and s['productCycle']=='normal'
        assert s['stock_channel']=='lcsc_domestic_direct'
        old=fps[ref]
        for pad in old.Pads():
            assert not any(t.GetNetCode()==pad.GetNetCode() and (pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd())) for t in b.GetTracks()),ref
        lib,name=p['footprint'].split(':')
        f=k.FootprintLoad(str(LIBS/'footprints'/(lib+'.pretty')),name);assert f
        f.SetFPID(k.LIB_ID(lib,name));f.SetReference(ref);f.SetValue(p['value'])
        f.SetPosition(old.GetPosition());f.SetOrientationDegrees(old.GetOrientationDegrees())
        f.SetPath(old.GetPath());f.SetDNP(old.IsDNP());f.SetLocked(old.IsLocked())
        identities[ref]=(old.m_Uuid.AsString(),{z.GetNumber():z.m_Uuid.AsString() for z in old.Pads()})
        f.SetSheetname(old.GetSheetname());f.SetSheetfile(old.GetSheetfile())
        for field in old.GetFields():
            if field.GetName() not in ['Reference','Value']:
                f.SetField(field.GetName(),field.GetText());f.GetField(field.GetName()).SetVisible(field.IsVisible())
        f.Reference().SetVisible(old.Reference().IsVisible());f.Value().SetVisible(old.Value().IsVisible())
        pads={z.GetNumber():z for z in old.Pads()}
        for z in f.Pads():
            prior=pads[z.GetNumber()];z.SetNet(prior.GetNet())
        b.Remove(old);b.Add(f)
        report.append(dict(reference=ref,value=p['value'],before=before[ref]['footprint'],after=p['footprint'],LCSC=code,MPN=s['productModel']))
    # Preserve all schematic geometry, labels, UUIDs, fields and connections.
    for path in project.glob('*.kicad_sch'):
        tree=sexp.load_file(str(path));dirty=False
        for sym in sexp.find_all(tree,'symbol'):
            props={str(x[1]):x for x in sexp.find_all(sym,'property')}
            ref=str(props.get('Reference',['','',''])[2])
            if ref in changed:
                assert str(props['Footprint'][2])==before[ref]['footprint']
                props['Footprint'][2]=sexp.QStr.of(changed[ref]['footprint']);dirty=True
        if dirty:sexp.save_file(str(path),tree)
    k.SaveBoard(str(project/'display_carrier.kicad_pcb'),b)
    tree=sexp.load_file(str(project/'display_carrier.kicad_pcb'))
    for node in sexp.find_all(tree,'footprint'):
        ref=next(str(q[2]) for q in sexp.find_all(node,'property') if str(q[1])=='Reference')
        if ref in identities:
            fid,pids=identities[ref];sexp.find(node,'uuid')[1]=sexp.QStr.of(fid)
            for pad in sexp.find_all(node,'pad'):sexp.find(pad,'uuid')[1]=sexp.QStr.of(pids[str(pad[1])])
    sexp.save_file(str(project/'display_carrier.kicad_pcb'),tree)
    (project/'design-intent.json').write_text(json.dumps(intent,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    snap=json.loads((project/'LCSC_STOCK_SNAPSHOT.json').read_text('utf8'))
    for code in {r['LCSC'] for r in report}:snap['parts'][code]=stock[code]
    snap['latest_passive_observation_date']='2026-09-25 Asia/Shanghai'
    (project/'LCSC_STOCK_SNAPSHOT.json').write_text(json.dumps(snap,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    result=dict(revision='Q',changed_count=len(report),by_kind=dict(collections.Counter(r['reference'][0] for r in report)),
        package='0402 imperial / 1005 metric',changes=report,
        preserved='All component centers, orientation, references, pad UUIDs, nets and existing MIPI tracks',
        retained='Power inductors, bulk and LDO capacitors, bootstrap/loop caps, C0G 1nF, precision/NTC/zero-ohm parts and R501 QOD remain at reviewed sizes')
    (project/'PASSIVE_SHRINK.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='changes'},indent=2))

if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--project',type=Path,required=True);a.add_argument('--stock',type=Path,required=True)
    args=a.parse_args();stage(args.project,args.stock)
