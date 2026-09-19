"""Stage additive carrier edits without replacing existing PCB identities.

Existing pad nets, placement, copper and geometry must stay identical. Assembly
flags and sheet/status metadata may follow the regenerated schematic. This is
deliberately NOT a general-purpose update-PCB or rerouting tool.
"""
import argparse, copy, json, shutil
from pathlib import Path
from cad import sexp


def properties(node):
    return {sexp.atoms(p)[0]:p for p in sexp.find_all(node,'property')}


def reference(node):
    return sexp.atoms(properties(node)['Reference'])[1]


def stage(baseline, generated, output):
    assert baseline.resolve()!=output.resolve() and generated.resolve()!=output.resolve()
    old=sexp.load_file(str(baseline/'display_carrier.kicad_pcb'))
    new=sexp.load_file(str(generated/'display_carrier.kicad_pcb'))
    original={reference(f):f for f in sexp.find_all(old,'footprint')}
    fresh={reference(f):f for f in sexp.find_all(new,'footprint')}
    assert set(original)<=set(fresh), 'This tool cannot remove existing footprints'
    changes=[]
    for ref,f in original.items():
        g=fresh[ref]
        if ref.startswith('H'):continue
        assert sexp.atoms(sexp.find(f,'at'))==sexp.atoms(sexp.find(g,'at')),('placement changed',ref)
        assert sexp.atoms(sexp.find(f,'layer'))==sexp.atoms(sexp.find(g,'layer')),('side changed',ref)
        def pads(fp):
            return sorted((sexp.atoms(p)[0], sexp.atoms(sexp.find(p,'net') or ['net']))
                for p in sexp.find_all(fp,'pad'))
        assert pads(f)==pads(g),('existing pad net changed',ref)
        attr=sexp.find(f,'attr');updated=sexp.find(g,'attr') or [sexp.Sym('attr')]
        if attr is None:
            attr=[sexp.Sym('attr')]
            if len(updated)>1:f.append(attr)
        assert sorted(str(x) for x in attr[1:] if str(x)!='dnp')==sorted(str(x) for x in updated[1:] if str(x)!='dnp')
        if attr!=updated:changes.append(ref)
        attr[:]=copy.deepcopy(updated)
        # Sheet display name changes when the formerly optional modem is fitted.
        sexp.find(f,'sheetname')[:]=copy.deepcopy(sexp.find(g,'sheetname'))
        if 'DesignStatus' in properties(f):
            properties(f)['DesignStatus'][2]=copy.deepcopy(properties(g)['DesignStatus'][2])
    added=sorted(set(fresh)-set(original))
    old.extend(copy.deepcopy(fresh[ref]) for ref in added)
    replacements={'LCD FPC HOLD\nF face / entry from bottom':'LCD FPC ADJUSTMENT AREA\nJ1200 / cable fit unverified',
        'TOUCH HOLD\nF face / same entry':'TOUCH FPC ADJUSTMENT\nJ1210 / pinout unverified',
        'CAMERA FPC HOLD\nF face / horizontal':'CAMERA FPC\nJ1220 / support pending'}
    for g in sexp.find_all(old,'gr_text'):
        value=sexp.atoms(g)[0]
        if value in replacements:g[1]=sexp.QStr.of(replacements[value])
    for g in sexp.find_all(new,'gr_text'):
        if sexp.atoms(g)[0] in ['LCD / J1200','TOUCH / J1210','CAMERA / J1220']:old.append(copy.deepcopy(g))
    shutil.copytree(baseline,output,dirs_exist_ok=True)
    for pattern in ['*.kicad_sch','Carrier.kicad_sym','sym-lib-table','design-intent.json','pcb-pad-audit.json','placement-audit.json','model-manifest.json']:
        for p in generated.glob(pattern):shutil.copy2(p,output/p.name)
    for p in (generated/'Carrier.pretty').glob('XUNPU_FPC*.kicad_mod'):
        shutil.copy2(p,output/'Carrier.pretty'/p.name)
    for p in (generated/'Carrier.3dshapes').glob('FPC-SMD*.step'):
        shutil.copy2(p,output/'Carrier.3dshapes'/p.name)
    sexp.save_file(str(output/'display_carrier.kicad_pcb'),old)
    report={'baseline':str(baseline),'existing_footprints_preserved':len(original),
        'added_references':added,'assembly_flag_changes':changes,
        'existing_placement_and_pad_nets':'unchanged','existing_project_settings':'preserved'}
    (output.parent/'addition-audit.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['baseline','generated','output']:ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();stage(a.baseline,a.generated,a.output)
