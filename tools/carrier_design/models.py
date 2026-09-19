"""Keep checked project model bindings when rebuilding the carrier library."""
import hashlib
import json
from pathlib import Path
import shutil

PROJECT = Path(__file__).resolve().parents[2] / 'display_board/hardware/display-carrier'


def manifest(source=PROJECT):
    return json.loads((source / 'model-manifest.json').read_text(encoding='utf-8'))


def remap_footprints(parts):
    mapping = {r['original_footprint']: r['footprint'] for r in manifest()['models']}
    for part in parts:
        part['footprint'] = mapping.get(part['footprint'], part['footprint'])


def stage_library(destination, source=PROJECT):
    """Copy immutable checked footprints/assets after the original imports.

    Sources are the reviewed local library, not unverified supplier footprints.
    This also replaces imports whose SWIG model records were formerly modified
    by assignment to iterator copies and never written back to the footprint.
    """
    destination = Path(destination)
    if source.resolve() != destination.resolve():
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'model-manifest.json', destination / 'model-manifest.json')
    for row in manifest(source)['models']:
        name = row['footprint'].split(':', 1)[1]
        for folder, filename in [('Carrier.pretty', name + '.kicad_mod'),
                                 ('Carrier.3dshapes', row['file'])]:
            src = source / folder / filename
            dst = destination / folder / filename
            assert src.is_file(), src
            if folder.endswith('3dshapes'):
                assert hashlib.sha256(src.read_bytes()).hexdigest() == row['sha256'], src
            dst.parent.mkdir(parents=True, exist_ok=True)
            if src.resolve() != dst.resolve():
                shutil.copy2(src, dst)
