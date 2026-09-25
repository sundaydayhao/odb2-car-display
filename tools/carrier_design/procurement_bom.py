"""Generate a purchasing review, not a fabrication/assembly release.

Counts fitted references from checked design intent for exactly one board.
No production multiplier, spare allowance or supplier order rounding is used.
An unknown stock quantity is never treated as zero or as available.
"""
import argparse, collections, csv, hashlib, json
from pathlib import Path

# Matches are by electrical value AND footprint; exceptional references below
# carry explicit engineering holds. Do not silently relax dielectric/tolerance.
RES={
    '0R':'C21189','22R':'C23345','100R':'C22775','300R':'C23025','1k':'C21190',
    '4.7k':'C23162','10k':'C25804','22k':'C31850','33.2k':'C23003','47k':'C25819',
    '56.2k':'C23080','68.1k':'C25976','100k':'C25803','178k':'C22823','324k':'C667203',
    '453k':'C273765','680k':'C25822','887k':'C403386',
}
ICS={
    'CH343P':'C2846043','TXU0202DCUR':'C5186957','USBLC6-2SC6':'C7519',
    'TUSB320LAIRWBR':'C132554','AP2112K-3.3TRG1':'C51118','TPS2553DBVR':'C55266',
    'BQ25896RTWR':'C181475','TPS22990DMLR':'C962990','TPS61088RHLR':'C87357',
    'TLV62569DBVR':'C141836','TPS22918DBVR':'C131941','TLV62585DRLR':'C485117',
    'ES8389':'C22380673','ME6211C30M5G-N':'C403651','SN74LVC1G3157DCKR':'C38663',
    'LMA3729T381-OAS02':'C54320678','NS4150B':'C189961','OPT3001DNPR':'C90462',
    'AP2112K-1.8TRG1':'C176944','TXU0102DCUR':'C5217871',
}
SPECIAL={
    'J200':'C165948','J300':'C131337','J301':'C131337','J810':'C131337',
    'J820':'C131337','J821':'C131337','J500':'C114218','J1000':'C3032925',
    'J1010':'C598199','J1011':'C598199','J1200':'C2856833','J1210':'C2856827',
    'J1220':'C2856831','L400':'C54140025','Y810':'C276424','D810':'C552574',
    'SW310':'C273395','R301':'C861611','R302':'C1854557',
}
# Revision P overrides after package and electrical review.
ICS.pop('ES8389')
ICS.update({'ES8388':'C365736','FSV4730-D60-GU':'C19191931','MCP120T-270I/TT':'C145625',
            'SHT40-AD1F-R2':'C7461846','BAT54H,115':'C426769'})
SPECIAL.update({'SW310':'C609825','SW101':'C52178082','SW102':'C52178082',
    'R109':'C49352955','R1010':'C137701','R1014':'C17901','L300':'C7461343',
    'L410':'C408335','L1010':'C408332','L820':'C408332','C1012':'C116743',
    'C1013':'C116743','Y810':'C22386778'})
HOLDS={'F300':'PH 2A 电池端子：按线材、电池短路电流和温度降额定型；额定电流及分断能力未闭合，不能仅按现货选料。'}

def choose(p):
    ref,value,fp=p['ref'],p['value'],p['footprint']
    from cost_sourcing_spec import selected_code as cost_code
    if cost_code(ref):return cost_code(ref),'SELECTED',p['status']
    if '_0402_1005Metric' in fp:
        from small_passives import selected_code
        code=selected_code(p)
        assert code,(ref,value,fp)
        return code,'SELECTED','0402/1005：小信号电阻 62.5mW；100nF 为 16V X7R，其余电容 50V，保持原介质要求。'
    if ref in ['U100','U1000']:return '', 'USER_FIXED', '用户指定鱼鹰光电核心板、ML307 模组，单独采购。'
    if ref in HOLDS:return '', 'HOLD', HOLDS[ref]
    if value=='DNP RF tune':return '', 'DNP', '待实测匹配后定值；本批不采购。'
    if ref in SPECIAL:return SPECIAL[ref], 'SELECTED', '与 J301 同款 PH 2Pin；额定 2A/AWG24。电池总电流及保险丝配合尚未验证。' if ref=='J300' else ''
    if value in ICS:return ICS[value], 'SELECTED', ''
    if value=='MMBT3904':return 'C20526','SELECTED','长电 MMBT3904，SOT-23。'
    if value=='2N7002':return 'C8545','SELECTED','长电 2N7002，SOT-23；按现有低电流用途，非功率开关替代。'
    if ref.startswith('R') and fp=='Resistor_SMD:R_0603_1608Metric':
        assert '0.1%' not in value, (ref,value)
        code=RES.get(value.split()[0])
        if code:return code,'SELECTED','324k 使用 0.1% 精度，其余按要求匹配或优于原公差。' if ref=='R402' else ''
    if ref.startswith('C'):
        size=next((s for s in ['0603','0805','1206','1210'] if 'C_'+s+'_' in fp),None)
        nominal=value.split()[0]
        cmap={('0603','100n'):'C14663',('0603','1u'):'C29936',('0603','4.7u'):'C19666',
              ('0603','2.2u'):'C23630',('0603','10u'):'C19702',('0603','10n'):'C57112',
              ('0603','47n'):'C1622',('0603','22n'):'C21122',('0603','100p'):'C14858',
              ('0603','22p'):'C1653',('0603','33p'):'C1663',('0603','1n'):'C1588',('0603','470n'):'C1623',
              ('0805','10u'):'C15850',('0805','22u'):'C45783',('1206','22u'):'C12891',
              ('1206','10u'):'C13585',('1210','47u'):'C84494'}
        code=cmap.get((size,nominal))
        if 'C0G' in value and nominal=='1n':code='C507408'
        if code:
            note='额定电压不低于原要求；电源 MLCC 的直流偏压有效容量、环路和温升仍属整机验证。'
            if ref=='C1044':note+=' RF DC block 的高频损耗/自谐振仍需实测确认。'
            return code,'SELECTED',note
    raise AssertionError(('Unclassified physical component',ref,value,fp))


def generate(project, intent_path):
    intent=json.loads(intent_path.read_text(encoding='utf-8'))
    snapshot=json.loads((project/'LCSC_STOCK_SNAPSHOT.json').read_text(encoding='utf-8'))
    cat=snapshot['parts'];rows=[];groups=collections.defaultdict(list)
    for p in intent['parts']:
        if not p['footprint'] or p['ref'].startswith('TP'):continue
        code,status,note=choose(p);r=cat.get(code,{})
        if code:
            assert r['productCycle']=='normal',(p['ref'],code,r['productCycle'])
            assert r['stockNumber'] is not None,(p['ref'],code)
        row=dict(Reference=p['ref'],CircuitValue=p['value'],Footprint=p['footprint'],
                 Fitted=0 if p['dnp'] else 1,LCSC=code,Manufacturer=r.get('brandNameEn',''),
                 MPN=r.get('productModel',p['value'] if status=='USER_FIXED' else ''),SelectionStatus=status,Note=note,URL=r.get('url',''))
        rows.append(row);groups[code or 'HOLD:'+p['ref']].append(row)
    assert len(rows)==338 and sum(r['Fitted'] for r in rows)==331
    grouped=[]
    for key,rr in groups.items():
        code=rr[0]['LCSC'];part=cat.get(code,{})
        perboard=sum(r['Fitted'] for r in rr)
        stock=part.get('stockNumber');status=rr[0]['SelectionStatus']
        if code and perboard:
            status='STOCK_OK' if stock>=perboard else 'SHORTAGE'
            if part.get('stock_channel')=='partner':status='PARTNER_CONFIRM'
        elif not perboard:status='DNP'
        grouped.append(dict(LCSC=code,MPN=rr[0]['MPN'],Manufacturer=rr[0]['Manufacturer'],
            CircuitValues=' | '.join(dict.fromkeys(r['CircuitValue'] for r in rr)),
            References=' '.join(r['Reference'] for r in rr if r['Fitted']),
            DNP_References=' '.join(r['Reference'] for r in rr if not r['Fitted']),
            PerBoard=perboard,Package=part.get('encapStandard',''),
            ObservedStock=stock if stock is not None else '',SupplyStatus=status,
            StockChannel=part.get('stock_channel','') if code else '',
            ObservedAtUTC=part.get('retrieved_at_utc',''),URL=part.get('url',''),
            Note=' | '.join(dict.fromkeys(r['Note'] for r in rr if r['Note']))))
    def write_csv(name,data):
        with (project/name).open('w',encoding='utf-8-sig',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    # A single quantity column and exact product codes prevent BOM auto-match
    # from interpreting U100 or DNP capacitor references as catalogue codes.
    selected=[r for r in grouped if r['LCSC'] and r['PerBoard']]
    upload=[dict(商品编号=r['LCSC'],型号=r['MPN'],品牌=r['Manufacturer'],封装=r['Package'],位号=r['References'],数量=r['PerBoard']) for r in selected]
    write_csv('PROCUREMENT_REFERENCES.csv',rows)
    write_csv('PROCUREMENT_PER_BOARD.csv',upload)
    excluded=[r for r in rows if not r['Fitted'] or not r['LCSC']]
    (project/'PROCUREMENT_REVIEW.json').write_text(json.dumps(dict(groups=grouped,excluded=excluded),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    counts=collections.Counter()
    for r in rows:
        if r['Fitted']:counts['selected' if r['LCSC'] else r['SelectionStatus'].lower()]+=1
    report=dict(revision='R',boards=1,physical_positions=len(rows),fitted_per_board=sum(r['Fitted'] for r in rows),
        per_board_sourcing=dict(counts),unique_selected_lcsc_codes=len({r['LCSC'] for r in rows if r['LCSC']}),
        supply_groups=dict(collections.Counter(r['SupplyStatus'] for r in grouped)),
        policy='Exactly one board. Aggregate fitted references by exact LCSC code; no spares, MOQ rounding or batch multiplier. Auto-match CSV excludes DNP, USER_FIXED and HOLD. Stock is an unreserved snapshot.',
        hash_normalization='CRLF converted to LF before SHA-256',
        design_intent_sha256=hashlib.sha256(intent_path.read_bytes().replace(b'\r\n',b'\n')).hexdigest(),
        snapshot_sha256=hashlib.sha256((project/'LCSC_STOCK_SNAPSHOT.json').read_bytes().replace(b'\r\n',b'\n')).hexdigest(),
        status='SOURCING_REVIEW_NOT_RELEASED')
    if (project/'SCREEN_BACK_LAYOUT.json').is_file():
        report['layout_revision']=json.loads((project/'SCREEN_BACK_LAYOUT.json').read_text(encoding='utf-8'))['revision']
    (project/'PROCUREMENT_AUDIT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--intent',type=Path,required=True);a=ap.parse_args();generate(a.project,a.intent)
