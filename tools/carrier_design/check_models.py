"""Audit native PCB model coverage and compare model-only edits to a baseline."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys
import pcbnew as k

sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp


def normalized_tree(path, mapping):
    """Preserve every native CAD node other than model bindings and library IDs."""
    def clean(node):
        if isinstance(node,list):
            if node and str(node[0]) == 'model':
                return None
            return [z for x in node if (z:=clean(x)) is not None]
        return mapping.get(str(node),str(node))
    tree=clean(sexp.load_file(str(path)))
    # KiCad may reorder root items when saving; internal geometry order matters.
    return sorted((json.dumps(x,ensure_ascii=False) for x in tree[1:]))


def audit(project, model_root, baseline=None):
    manifest=json.loads((project/'model-manifest.json').read_text(encoding='utf-8'))
    mapping={r['footprint']:r['original_footprint'] for r in manifest['models']}
    board=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    rows=[]; failures=[];physical=bare=0
    expected={r['reference']:r for r in manifest['models']}
    for f in board.GetFootprints():
        ref=f.GetReference();intentional=ref.startswith('TP') or ref in {'H1','H2','H3','H4'}
        if intentional:bare+=1
        else:physical+=1
        models=[]
        for m in f.Models():
            p=Path(m.m_Filename.replace('${KIPRJMOD}',str(project)).replace('${KICAD10_3DMODEL_DIR}',str(model_root)))
            models.append({'path':m.m_Filename,'exists':p.is_file(),'shown':m.m_Show})
            if not p.is_file():failures.append(ref+': missing '+str(p))
            if not m.m_Show and not intentional:failures.append(ref+': hidden model')
        if not intentional and not models:failures.append(ref+': no model')
        if ref in expected:
            r=expected[ref]; local=project/'Carrier.3dshapes'/r['file']
            if hashlib.sha256(local.read_bytes()).hexdigest()!=r['sha256']:failures.append(ref+': SHA mismatch')
            lib=k.FootprintLoad(str(project/'Carrier.pretty'),r['footprint'].split(':',1)[1])
            for target in [f,lib]:
                mm=list(target.Models())
                if len(mm)!=1:failures.append(ref+': wrong binding count');continue
                m=mm[0]
                if m.m_Filename!='${KIPRJMOD}/Carrier.3dshapes/'+r['file']:failures.append(ref+': incorrect URI')
                for vector,key in [(m.m_Offset,'offset_mm'),(m.m_Rotation,'rotation_deg'),(m.m_Scale,'scale')]:
                    if any(abs(a-b)>1e-7 for a,b in zip([vector.x,vector.y,vector.z],r[key])):failures.append(ref+': '+key+' mismatch')
        rows.append({'reference':ref,'dnp':f.IsDNP(),'intentional_no_model':intentional,'models':models})
    preserved=[]
    if baseline:
        for path in [project/'display_carrier.kicad_pcb',*sorted(project.glob('*.kicad_sch'))]:
            before=normalized_tree(baseline/path.name,mapping)
            after=normalized_tree(path,mapping)
            if before!=after:
                failures.append(path.name+': non-model change')
                import difflib
                print('\n'.join(list(difflib.unified_diff(before,after))[:8])[:3000])
            else:preserved.append(path.name)
    summary={'status':'PASS' if not failures else 'FAIL','footprints':len(rows),'physical_components':physical,
      'physical_components_with_resolvable_models':sum(not r['intentional_no_model'] and r['models'] and all(m['exists'] for m in r['models']) for r in rows),
      'intentional_bare_features':bare,'dnp_footprints':sum(r['dnp'] for r in rows),
      'repaired_references':sorted(expected),'reference_models':[r['reference'] for r in manifest['models'] if r['kind']=='datasheet_reference'],
      'unchanged_cad_geometry_and_electrical_files':preserved,'failures':failures,'inventory':rows}
    return summary


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--baseline',type=Path)
    ap.add_argument('--model-root',type=Path,default=Path('C:/Program Files/KiCad/10.0/share/kicad/3dmodels'))
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();report=audit(args.project.resolve(),args.model_root,args.baseline.resolve() if args.baseline else None)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='inventory'},ensure_ascii=False,indent=2))
    raise SystemExit(bool(report['failures']))
