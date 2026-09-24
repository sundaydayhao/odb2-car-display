"""Stage the revision-N FM bypass placement correction on a fresh scratch copy.

Use KiCad Python; run native DRC/parity and verify.py before promotion.
This changes C814 placement only. It does not add or route any circuit.
"""
import argparse
import json
import math
import shutil
import sys
from pathlib import Path

import pcbnew as k

sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp


def stage(baseline, output):
    assert not output.exists() and baseline.resolve() != output.resolve()
    assert not list(baseline.glob('*.lck')), 'Close KiCad before staging changes'
    board_path = baseline / 'display_carrier.kicad_pcb'
    board = k.LoadBoard(str(board_path))
    assert not len(board.GetTracks()), 'This correction targets the unrouted draft'
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    fp = fps['C814']

    def xy(f):
        return [round(k.ToMM(f.GetPosition().x) - 50, 6),
                round(k.ToMM(f.GetPosition().y) - 50, 6)]

    def pad(f, number):
        return next(p for p in f.Pads() if p.GetNumber() == number)

    def distance():
        a, b = pad(fp, '1').GetPosition(), pad(fps['U815'], '7').GetPosition()
        return round(k.ToMM(math.hypot(a.x-b.x, a.y-b.y)), 4)

    def box(f):
        q = f.GetBoundingBox(False, False)
        return [k.ToMM(v) for v in [q.GetX(), q.GetY(), q.GetRight(), q.GetBottom()]]

    assert xy(fp) == [103.0, 59.0] and fp.GetOrientationDegrees() == 180
    assert fp.GetValue() == '22n'
    assert pad(fp, '1').GetNetname() == 'FM_3V0'
    assert pad(fp, '2').GetNetname() == 'GND'
    assert pad(fps['U815'], '7').GetNetname() == 'FM_3V0'
    before = dict(xy_mm=xy(fp), angle=fp.GetOrientationDegrees(), side='F')
    old_distance = distance()
    fp.SetPosition(k.VECTOR2I(k.FromMM(149.5), k.FromMM(115.75)))
    fp.SetOrientationDegrees(270)
    a = box(fp)
    for ref, other in fps.items():
        if ref == 'C814' or other.GetLayer() != fp.GetLayer():
            continue
        b = box(other)
        assert not (a[0] < b[2]+.4 and a[2] > b[0]-.4 and
                    a[1] < b[3]+.4 and a[3] > b[1]-.4), ('body clearance', ref)
    assert distance() < 2.5
    after = dict(xy_mm=xy(fp), angle=fp.GetOrientationDegrees(), side='F')
    shutil.copytree(baseline, output, ignore=shutil.ignore_patterns(
        '.history', '*.lck', '*.kicad_prl', '~*', 'fp-info-cache'))
    k.SaveBoard(str(output / board_path.name), board)

    # Native serialization must preserve all objects outside this footprint.
    def split(path):
        root = sexp.load_file(path)
        footprints, other = {}, []
        for node in root[1:]:
            if isinstance(node, list) and node and node[0] == 'footprint':
                ref = next(p[2] for p in node if isinstance(p, list) and
                           len(p) > 2 and p[:2] == ['property', 'Reference'])
                footprints[ref] = node
            else:
                other.append(node)
        return footprints, other

    old, old_other = split(board_path)
    new, new_other = split(output / board_path.name)
    assert old_other == new_other
    assert old.keys() == new.keys()
    changed = sorted(r for r in old if old[r] != new[r])
    assert changed == ['C814'], changed
    for name in ('design-intent.json', 'SCREEN_BACK_LAYOUT.json', 'AUDIO_LAYOUT.json'):
        path = output / name
        data = json.loads(path.read_text(encoding='utf-8'))
        if name == 'design-intent.json':
            part = next(p for p in data['parts'] if p['ref'] == 'C814')
            part.update(pcb=after['xy_mm'], angle=after['angle'])
        else:
            data['positions']['C814'] = after
            data['fm_sensor_review'] = 'FM_SENSOR_REVIEW.json'
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    review = dict(date='2026-09-24', base_revision='N',
        status='PLACEMENT_STAGED_PENDING_NATIVE_CHECKS',
        changed_footprints=changed, before=before, after=after,
        C814_pad1_to_U815_pad7_center_distance_mm=dict(before=old_distance, after=distance()),
        distance_boundary='Straight-line pad-center distance, not routed length or loop inductance',
        all_other_footprint_trees_preserved=True, all_non_footprint_board_objects_preserved=True,
        schematic_changes=False, new_components=0,
        schematic_and_pcb_pin_check='PENDING', physical_validation='NOT_RUN',
        fabrication_release=False)
    (output / 'FM_SENSOR_REVIEW.json').write_text(
        json.dumps(review, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(review, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    stage(args.baseline, args.output)
