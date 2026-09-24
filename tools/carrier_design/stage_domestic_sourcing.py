"""Apply sourcing revision to an unrouted baseline; preserve unrelated PCB objects.

Output is scratch only. Native ERC/DRC, schematic parity, pin-map audit, model
resolution and rendered review are required before promoting it.
"""
import argparse, copy, json, math, shutil, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
import pcbnew as k
import cad
from cad import LIBS, ROOT_ID, uid, sexp
from board_spec import d
from domestic_sourcing_spec import apply
from domestic_sourcing_assets import generate


def stage(baseline,output):
    assert baseline.resolve()!=output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    shutil.copytree(baseline,output,dirs_exist_ok=True)
    generate(output)
    evidence=output.parent/'evidence';evidence.mkdir(exist_ok=True)
    bi=json.loads((baseline/'design-intent.json').read_text(encoding='utf-8'))
    # The source of the existing layout is the reviewed baseline, including its
    # later decoupling edits. Apply the new electrical delta once to that state.
    d.parts=copy.deepcopy(bi['parts']);d.sheets=copy.deepcopy(bi['sheets']);apply(d)
    b=k.LoadBoard(str(baseline/'display_carrier.kicad_pcb'))
    assert len(b.GetTracks())==0 and all(z.GetIsRuleArea() for z in b.Zones())
    old={f.GetReference():f for f in b.GetFootprints()}
    bparts={p['ref']:p for p in bi['parts']}
    changed={p['ref'] for p in d.parts if p['ref'] not in bparts or any(
        p.get(key)!=bparts[p['ref']].get(key) for key in ['kind','value','nets','footprint','source','status','xy'])}
    for p in d.parts:
        if p['ref'] in old:
            f=old[p['ref']];p.update(pcb=[k.ToMM(f.GetPosition().x)-50,k.ToMM(f.GetPosition().y)-50],
                side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
    cad.OUT=output;d.write()
    subprocess.run([str(LIBS.parents[1]/'bin/kicad-cli.exe'),'sch','export','netlist','--format','kicadxml',
        '--output',str(evidence/'netlist.xml'),str(output/'display_carrier.kicad_sch')],check=True,capture_output=True)
    xml=ET.parse(evidence/'netlist.xml').getroot()
    padnet={(z.get('ref'),z.get('pin')):n.get('name') for n in xml.findall('./nets/net') for z in n.findall('node')}
    nets={n.GetNetname():n for n in b.GetNetInfo().NetsByNetcode().values()}
    for name in sorted(set(padnet.values())):
        if name not in nets:
            n=k.NETINFO_ITEM(b,name);b.Add(n);nets[name]=n
    def vec(x,y):return k.VECTOR2I(k.FromMM(x),k.FromMM(y))
    def pos(x,y):return vec(x+50,y+50)
    fps={}
    for p in d.parts:
        if not p['footprint']:continue
        ref=p['ref'];f=old.get(ref)
        replacement=f is None or p['footprint']!=str(f.GetFPID())
        if replacement:
            if f is not None:b.Remove(f)
            lib,name=p['footprint'].split(':',1)
            folder=output/'Carrier.pretty' if lib=='Carrier' else LIBS/'footprints'/(lib+'.pretty')
            f=k.FootprintLoad(str(folder.resolve()),name);assert f,(ref,name)
            f.SetFPID(k.LIB_ID(lib,name));f.SetReference(ref)
            path=k.KIID_PATH()
            for u in [ROOT_ID,uid('sheet:'+p['sheet']),uid('part:'+ref)]:path.push_back(k.KIID(u))
            f.SetPath(path);f.SetPosition(pos(*p['pcb']));b.Add(f)
            assert p['side']=='F';f.SetOrientationDegrees(p['angle'])
        f.SetValue(p['value']);f.SetDNP(p['dnp'])
        f.SetSheetname('/'+d.sheets[p['sheet']]+'/');f.SetSheetfile(p['sheet']+'.kicad_sch')
        for key,val in [('Datasheet',p['source']),('DesignStatus',p['status'])]:
            f.SetField(key,val);f.GetField(key).SetVisible(False)
        f.Reference().SetVisible(False);f.Value().SetVisible(False)
        assert {z.GetNumber() for z in f.Pads() if z.GetNumber()}==set(p['nets']),(ref,'pad set')
        for z in f.Pads():
            if z.GetNumber():z.SetNet(nets[padnet[ref,z.GetNumber()]])
        fps[ref]=f
    # New side controls face the top housing edge. Their panel cutouts differ.
    for ref in ['SW101','SW102']:fps[ref].SetOrientationDegrees(180)
    fps['SW310'].SetPosition(pos(20,5.5))
    for ref,x,y in [('R1014',44.5,56),('L1010',31,50.5)]:
        fps[ref].SetPosition(pos(x,y))
    # The larger FM package needs a local placement rebuild, including its
    # crystal and bypass capacitors. Keep all non-FM packages fixed initially.
    movable={p['ref'] for p in d.parts if p['sheet']=='12_fm_audio' and p['footprint']}
    movable-= {'J810','U811','U812','U813','U814','R820'}
    codec_movable={p['ref'] for p in d.parts if p['sheet']=='11_codec' and p['footprint']}-{'U800','U801','C800','C801'}
    movable|=codec_movable
    for ref in ['C802','C803']:fps[ref].SetOrientationDegrees(0)
    fps['C805'].SetOrientationDegrees(270)
    for ref in ['C806','C807']:fps[ref].SetOrientationDegrees(90)
    for ref in ['C879','C881','C882']:fps[ref].SetOrientationDegrees(180)
    for ref in ['C823','C824']:fps[ref].SetOrientationDegrees(270)
    # Expand the codec footprint in place, only move adjacent passives if the
    # final native DRC demonstrates a collision.
    def bbox(f):
        z=f.GetBoundingBox(False,False)
        return tuple(k.ToMM(x)-50 for x in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()])
    def overlap(a,c,g=.12):return a[0]<c[2]+g and a[2]>c[0]-g and a[1]<c[3]+g and a[3]>c[1]-g
    occupied=[(r,bbox(f)) for r,f in {**old,**fps}.items() if r not in movable]
    # Reservations relevant to these local zones (all original rule areas and
    # outlines remain in the native board for the independent DRC).
    occupied += [('LCD_SLOT',(64.5,52.5,84.5,55.5))]
    anchors={'U815':(104,64),'Y810':(104,70),'U816':(109,69),
        'C814':(99,65.75),'C823':(99,63.5),'C824':(99,68.25),'C825':(102,72),'C826':(106,72),
        'R821':(111,69),'C827':(111,67),'C813':(109,60),'C822':(100,69),
        'C802':(126.75,64.75),'C803':(126.75,66.5),'C804':(119,63.5),
        'C805':(123,61),'C806':(116.5,66),'C807':(116.5,70),
        'C879':(119,65.25),'C880':(130,65.25),'C881':(119,67),'C882':(119,68.75)}
    priority=['U815','Y810','C814','C823','C824','C825','C826','U816','C827','U810','C813','C812','R821']
    priority+=['C802','C803','C804','C879','C805','C881','C882','C806','C807','C880']
    pending=[r for r in priority if r in movable]+sorted(movable-set(priority),key=lambda r:(-((bbox(fps[r])[2]-bbox(fps[r])[0])*(bbox(fps[r])[3]-bbox(fps[r])[1])),r))
    for ref in pending:
        f=fps[ref];x0,y0=anchors.get(ref,(k.ToMM(f.GetPosition().x)-50,k.ToMM(f.GetPosition().y)-50))
        region=(115.25,56,139,76) if ref in codec_movable else (89,56,116,77)
        x1,y1,x2,y2=region
        sites=[(x0,y0)]+sorted(((x/4,y/4) for x in range(int(4*x1),int(4*x2)) for y in range(int(4*y1),int(4*y2))),key=lambda xy:(xy[0]-x0)**2+(xy[1]-y0)**2)
        found=False
        for x,y in sites:
            f.SetPosition(pos(x,y));bb=bbox(f)
            if bb[0]<x1 or bb[1]<y1 or bb[2]>x2 or bb[3]>y2:continue
            if not any(overlap(bb,c) for _,c in occupied):found=True;break
        assert found,('no free local site',ref)
        occupied.append((ref,bbox(f)))
    moves={}
    for p in d.parts:
        if p['ref'] not in fps:continue
        f=fps[p['ref']];xy=[round(k.ToMM(f.GetPosition().x)-50,4),round(k.ToMM(f.GetPosition().y)-50,4)]
        if any(abs(a-b)>.0001 for a,b in zip(xy,p['pcb'])) or abs(f.GetOrientationDegrees()-p['angle'])>.001:
            moves[p['ref']]=dict(before=p['pcb'],after=xy)
        p.update(pcb=xy,side=f.GetLayerName()[0],angle=f.GetOrientationDegrees())
    b.GetTitleBlock().SetRevision('P - Domestic sourcing / N MIPI')
    b.GetTitleBlock().SetDate('2026-09-24')
    k.SaveBoard(str(output/'display_carrier.kicad_pcb'),b)
    tree=sexp.load_file(str(output/'display_carrier.kicad_pcb'))
    for node in sexp.find_all(tree,'footprint'):
        ref=next(sexp.atoms(q)[1] for q in sexp.find_all(node,'property') if sexp.atoms(q)[0]=='Reference')
        if ref in old:sexp.find(node,'uuid')[1]=old[ref].m_Uuid.AsString()
    sexp.save_file(str(output/'display_carrier.kicad_pcb'),tree)
    shutil.copy2(baseline/'display_carrier.kicad_pro',output/'display_carrier.kicad_pro')
    d.write()
    # Only regenerate affected sheets; preserve reviewed content on other sheets.
    affected={p['sheet'] for p in d.parts if p['ref'] in changed}|{'13_release_holds'}
    for path in output.glob('*.kicad_sch'):
        if path.stem!='display_carrier' and path.stem not in affected:shutil.copy2(baseline/path.name,path)
        else:
            text=path.read_text(encoding='utf-8').replace('(rev "R0 ENGINEERING")','(rev "R0P SOURCING")').replace('(rev "R0L1 J300 PH")','(rev "R0P SOURCING")')
            import re
            text=re.sub(r'\(date "[^"]+"\)','(date "2026-09-24")',text)
            path.write_text(text,encoding='utf-8')
    report=dict(revision='P',baseline='90df33e',changed_references=sorted(changed),local_moves=moves,
        routing='UNROUTED',enclosure_fit='NOT_VALIDATED',added_references=sorted(set(fps)-set(old)))
    (output/'SOURCING_LAYOUT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:value for key,value in report.items() if key!='local_moves'},indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args();stage(a.baseline.resolve(),a.output.resolve())
