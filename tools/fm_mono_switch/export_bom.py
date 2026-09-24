"""Export engineering and purchasing BOMs without rewriting the KiCad design."""
from pathlib import Path
import csv
import json

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'display_board/hardware/fm-mono-switch'


def export_boms(project=PROJECT):
    intent = json.loads((project/'design-intent.json').read_text(encoding='utf-8'))
    catalog = json.loads((project/'procurement.json').read_text(encoding='utf-8'))
    revision = catalog['revision']
    choices = {ref: group for group in catalog['groups'] for ref in group['refs']}
    refs = {ref for ref, part in intent['parts'].items()
            if part['footprint'] and not ref.startswith('H')}
    assert refs == set(choices)

    groups = {}
    for ref, a in intent['parts'].items():
        if ref not in choices:
            continue
        key = (a['value'], a['footprint'], a['mpn'], a['lcsc'], a['manufacturer'])
        groups.setdefault(key, []).append(ref)
    with (project/'BOM.csv').open('w', newline='', encoding='utf-8-sig') as f:
        w = csv.writer(f)
        w.writerow(['References','每套用量','Value','Footprint','MPN','LCSC candidate','Manufacturer','Assembly note'])
        for (value, footprint, mpn, code, maker), rr in groups.items():
            w.writerow([','.join(rr),len(rr),value,footprint,mpn,code,maker,choices[rr[0]]['decision']])

    # One board per BOM set; choose the actual set count when purchasing.
    with (project/f'BOM-LCSC-{revision}.csv').open('w', newline='', encoding='utf-8-sig') as f:
        w = csv.writer(f)
        w.writerow(['商品编号','型号','品牌','封装','每套用量','位号','备注'])
        for c in catalog['groups']:
            if c.get('optional'):
                continue
            rr = c['refs']
            package = c.get('package', {'U1':'VSSOP-10','U2':'MSOP-10','U3':'SOT-23-5','D1':'SOD-882','Y1':'DT-26'}.get(rr[0], '0603'))
            w.writerow([c['lcsc'],c['mpn'],c['manufacturer'],package,
                        len(rr),','.join(rr),c['decision']])

    with (project/'BOM-cost.csv').open('w', newline='', encoding='utf-8-sig') as f:
        w = csv.writer(f)
        w.writerow(['位号','每套用量','MPN','品牌','立创编号','参考单价USD','参考报价阶梯数量',
                    '最小购买倍数_公开LCSC','每套净用料小计USD','网页库存记录_未锁货','来源','评估'])
        for c in catalog['groups']:
            q = len(c['refs']); price = c['unit_usd']
            w.writerow([','.join(c['refs']),q,c['mpn'],c['manufacturer'],c['lcsc'],
                        price if price is not None else '',c['tier'],c['moq_multiple'],
                        round(q*price,6) if price is not None else '',c['stock_observed'],
                        c.get('purchase_url',f"https://www.lcsc.com/product-detail/{c['lcsc']}.html") if c['lcsc'] else '',c['decision']])
    print(f'Exported {revision} BOMs with per-board usage only; no fixed purchase quantity.')


if __name__ == '__main__':
    export_boms()
