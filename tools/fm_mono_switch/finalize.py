"""Apply the reviewed sourcing choices without regenerating PCB geometry or routing.

Run with KiCad's native Python. procurement.json is the sourcing source of truth.
"""
from pathlib import Path
import json, sys
from export_bom import export_boms
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp, ops
import pcbnew as k

ROOT = Path(__file__).resolve().parents[2]
P = ROOT / 'display_board/hardware/fm-mono-switch'
intent = json.loads((P/'design-intent.json').read_text(encoding='utf-8'))
catalog = json.loads((P/'procurement.json').read_text(encoding='utf-8'))
revision = catalog['revision']
choices = {ref: group for group in catalog['groups'] for ref in group['refs']}
refs = {ref for ref, p in intent['parts'].items() if p['footprint'] and not ref.startswith('H')}
assert refs == set(choices), (refs - choices.keys(), choices.keys() - refs)
sch = sexp.load_file(str(P/'fm_mono_switch.kicad_sch'))
board = k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}

for ref, c in choices.items():
    a = intent['parts'][ref]
    a.update(mpn=c['mpn'], lcsc=c['lcsc'], manufacturer=c['manufacturer'])
    if 'value' in c: a['value'] = c['value']
    if 'datasheet' in c: a['source'] = c['datasheet']
    props = {'Value': a['value'], 'MPN': c['mpn'], 'LCSC': c['lcsc'],
             'Manufacturer': c['manufacturer'], 'Datasheet': a['source']}
    if ref == 'U3':
        props['Description'] = 'ME6211C30M5G-N: 3.0V LDO, SOT-23-5; 1 VIN, 2 GND, 3 CE active high, 4 NC, 5 VOUT'
    if ref == 'U1':
        props['Description'] = 'RS2117HYN MSOP-10: dual SPDT negative-audio switch; 1 V+, 2 NO1, 3 COM1, 4 IN1, 5 NC1, 6 GND, 7 NC2, 8 IN2, 9 COM2, 10 NO2'
    for name, value in props.items():
        ops.set_prop(sch, ref, name, value)
        if name == 'Value': fps[ref].SetValue(value)
        else:
            fps[ref].SetField(name, value)
            fps[ref].GetField(name).SetVisible(False)
    # Newly added schematic procurement fields must not clutter the drawing.
    for sym in sexp.find_all(sch, 'symbol'):
        ps = {sexp.atoms(p)[0]: p for p in sexp.find_all(sym, 'property')}
        if ps.get('Reference') and sexp.atoms(ps['Reference'])[1] == ref:
            if ref == 'D1':
                x, y = map(float, sexp.atoms(sexp.find(sym, 'at'))[:2])
                for name, dy in [('Reference', -10.16), ('Value', -7.62)]:
                    pos = sexp.find(ps[name], 'at')
                    pos[1], pos[2] = sexp.Sym(str(round(x-15.24,3))), sexp.Sym(str(round(y+dy,3)))
            for name in ['MPN', 'LCSC', 'Manufacturer', 'Datasheet', 'Description']:
                if name in ps:
                    effects = sexp.find(ps[name], 'effects')
                    if effects is not None and sexp.find(effects, 'hide') is None:
                        effects.append([sexp.Sym('hide'), sexp.Sym('yes')])

# Only printed revision text changes. All pins, pads, wires, tracks and zones stay intact.
for node in sexp.find_all(sch, 'text'):
    if str(node[1]).startswith('PROTO A'):
        node[1] = sexp.QStr.of('PROTO '+revision+':'+str(node[1]).split(':',1)[1])
title = sexp.find(sch, 'title_block')
sexp.find(title, 'rev')[1] = sexp.QStr.of(revision+' PROTOTYPE')
for draw in board.GetDrawings():
    if isinstance(draw, k.PCB_TEXT) and draw.GetText().startswith('MONO FM A'): draw.SetText('MONO FM '+revision)
sexp.save_file(str(P/'fm_mono_switch.kicad_sch'), sch)
k.SaveBoard(str(P/'fm_mono_switch.kicad_pcb'), board)
intent['status'] = revision+' cost/sourcing prototype; bench qualification and final supplier availability required'
intent['manufacturing_release'] = False
(P/'design-intent.json').write_text(json.dumps(intent, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

export_boms(P)
print(f'Applied {revision} metadata.')
print('No circuit connections, pad positions, tracks or zones changed.')
