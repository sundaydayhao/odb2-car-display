"""Check local 3D assets and matching PCB/library bindings with KiCad Python.

Run with the Python bundled with KiCad. This checks model coverage, provenance
hashes, and transforms. It does not certify dimensions or enclosure interference.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path

import pcbnew as k


def binding(model):
    return {
        'path': model.m_Filename,
        'offset_mm': [round(getattr(model.m_Offset, c), 5) for c in 'xyz'],
        'rotation_deg': [round(getattr(model.m_Rotation, c), 5) for c in 'xyz'],
        'scale': [round(getattr(model.m_Scale, c), 5) for c in 'xyz'],
    }


def main():
    repo = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', type=Path, default=repo / 'hardware/display-singleboard')
    ap.add_argument('--output', type=Path, default=repo / 'build/display-3d-verification.json')
    args = ap.parse_args()
    project = args.project.resolve()
    data = json.loads((project / 'model-manifest.json').read_text(encoding='utf-8'))
    rows = {r['footprint']: r for r in data['footprints']}
    board = k.LoadBoard(str(project / 'display_singleboard.kicad_pcb'))
    seen = collections.defaultdict(list)
    errors = []
    for fp in board.GetFootprints():
        name = str(fp.GetFPID().GetLibItemName())
        seen[name].append(fp)
        if name not in rows:
            errors.append([fp.GetReference(), 'footprint absent from model manifest'])
    files = set()
    for name, row in rows.items():
        if name not in seen:
            errors.append([name, 'stale manifest entry'])
            continue
        footprints = seen[name]
        if sorted(f.GetReference() for f in footprints) != sorted(row['refs']):
            errors.append([name, 'reference list changed; update manifest'])
        if sum(not f.IsDNP() for f in footprints) != row['populated']:
            errors.append([name, 'assembly population changed; update manifest'])
        lib_fp = k.FootprintLoad(str(project / 'Display.pretty'), name)
        if lib_fp is None:
            errors.append([name, 'local library footprint missing'])
            continue
        model = row['model']
        wanted = []
        if model:
            path = (project / 'Display.3dshapes' / model['file']).resolve()
            if path.parent != project / 'Display.3dshapes' or not path.is_file():
                errors.append([name, 'model missing or outside project'])
                continue
            files.add(path)
            if hashlib.sha256(path.read_bytes()).hexdigest() != model['sha256']:
                errors.append([name, 'model content differs from reviewed asset'])
            wanted = [{'path': '${KIPRJMOD}/Display.3dshapes/' + model['file'],
                       'offset_mm': model['offset_mm'], 'rotation_deg': model['rotation_deg'],
                       'scale': [1, 1, 1]}]
        elif not name.startswith(('TPC', 'HOLE')):
            errors.append([name, 'physical package has no model'])
        for fp in [lib_fp, *footprints]:
            actual = [binding(m) for m in fp.Models()]
            if actual != wanted:
                errors.append([fp.GetReference(), name, 'model binding/transform differs', actual, wanted])
    result = {
        'pcb_sha256': hashlib.sha256((project / 'display_singleboard.kicad_pcb').read_bytes()).hexdigest(),
        'footprints': sum(len(v) for v in seen.values()),
        'model_files': len(files),
        'with_body': sum(r['count'] for r in rows.values() if r['model']),
        'fitted_with_body': sum(r['populated'] for r in rows.values() if r['model']),
        'bare_copper_or_mounting_holes': sum(r['count'] for r in rows.values() if not r['model']),
        'errors': errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
