"""Apply the reviewed sourcing choices without regenerating PCB geometry or routing.

Run with KiCad's native Python. procurement.json is the sourcing source of truth.
"""
from pathlib import Path
import csv, json, sys
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

groups = {}
for ref, a in intent['parts'].items():
    if ref not in choices: continue
    key = (a['value'], a['footprint'], a['mpn'], a['lcsc'], a['manufacturer'])
    groups.setdefault(key, []).append(ref)
with (P/'BOM.csv').open('w', newline='', encoding='utf-8-sig') as f:
    w = csv.writer(f)
    w.writerow(['References','Qty','Value','Footprint','MPN','LCSC candidate','Manufacturer','Assembly note'])
    for (value, footprint, mpn, code, maker), rr in groups.items():
        w.writerow([','.join(rr),len(rr),value,footprint,mpn,code,maker,choices[rr[0]]['decision']])

# Explicit quantities for 100 boards. Import with BOM multiplier = 1, not 100.
with (P/f'BOM-LCSC-100boards-{revision}.csv').open('w', newline='', encoding='utf-8-sig') as f:
    w = csv.writer(f)
    w.writerow(['商品编号','型号','品牌','封装','数量','位号','备注'])
    for c in catalog['groups']:
        if c.get('optional'): continue
        rr = c['refs']; a = intent['parts'][rr[0]]
        package = {'U1':'MSOP-10','U2':'MSOP-10','U3':'SOT-23-5','D1':'SOD-882','Y1':'DT-26'}.get(rr[0], '0603')
        w.writerow([c['lcsc'], c['mpn'],c['manufacturer'],package,
                    100*len(rr),','.join(rr),c['decision']])

with (P/'BOM-cost-100.csv').open('w', newline='', encoding='utf-8-sig') as f:
    w = csv.writer(f)
    w.writerow(['位号','每板数量','100板净用量','MPN','品牌','立创编号','参考单价USD','适用阶梯数量',
                '最小购买倍数_公开LCSC','100板净用料小计USD','网页库存记录_未锁货','来源','评估'])
    for c in catalog['groups']:
        q = len(c['refs']); price = c['unit_usd']
        w.writerow([','.join(c['refs']),q,q*100,c['mpn'],c['manufacturer'],c['lcsc'],
                    price if price is not None else '',c['tier'],c['moq_multiple'],
                    round(q*100*price,4) if price is not None else '',c['stock_observed'],
                    c.get('purchase_url',f"https://www.lcsc.com/product-detail/{c['lcsc']}.html") if c['lcsc'] else '',c['decision']])
subtotal = sum(len(c['refs'])*100*c['unit_usd'] for c in catalog['groups'] if c['unit_usd'] is not None)
print(f'Applied {revision} metadata. Priced 100-board subtotal excluding U2/headers: USD {subtotal:.2f}.')
print('No circuit connections, pad positions, tracks or zones changed.')
