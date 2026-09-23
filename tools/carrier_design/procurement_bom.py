"""Generate a purchasing review, not a fabrication/assembly release.

Counts fitted references from checked design intent, groups identical LCSC
codes before adding 20% spares, then rounds once to the observed order step.
An unknown stock quantity is never treated as zero or as available.
"""
import argparse, collections, csv, hashlib, json, math
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
HOLDS={
    'SW101':'侧按键 PTS645Vx39-2LFS 尚无可核实的立创现货替代；替换需核对定位孔、按压高度和侧向行程。',
    'SW102':'同 SW101，不能把通用 6x6 按键当作已验证的直替。',
    'R109':'NTC 10k/B3950 缺完整订货号；R25/B 公差及封装需确认。电池包 NTC 本身不在此载板 BOM 内。',
    'F300':'J300 改为 PH 2A 后撤销原 8A 占位；按端子、线材和电池包短路电流重选保险丝，额定电流/分断能力/温度降额待闭合。',
    'L300':'1uH/Isat>=8A 尚需核对热态饱和、RMS/DCR 与 SWPA6045S 焊盘，不能仅按电感值买料。',
    'L410':'2.2uH/Isat>=3.5A 尚需核对热态饱和、RMS/DCR 与 SWPA4020S 焊盘。',
    'L1010':'1uH/Isat>=4.5A 尚需核对热态饱和、RMS/DCR 与 SWPA4020S 焊盘。',
    'L820':'同 L1010；功放电源需负载瞬态验证。',
    'C1012':'220uF/6.3V 低 ESR 储能待选聚合物及纹波/降额验证；普通钽不能直接替代。',
    'C1013':'同 C1012；需保证两只在模组脉冲负载下的有效储能。',
    'U815':'保留用户指定 RDA5807M；立创旧商品页仅有历史零库存信息，当前可采购库存/交期未确认。',
    'U1301':'现用 SHT40-AD1B-R2 的立创当前现货未确认；AD1F-R2/C7461846 国内有货，但膜版高度/焊盘/装配尚未完成复核，未自动换料。',
    'R1010':'536k 1%/0603 尚未找到已核实现货料号；不直接改用相近阻值，以免改变 ML307 供电。',
    'R1014':'100R/0.25W/1206 精确采购号待闭合；不可用本表 0603 100R 替代。',
}
for _r in ['D211','D212','D310','D311']:
    HOLDS[_r]='BAT54H/C426769 为 SOD-123F，现板为 SOD-123；焊盘兼容复核未完成，暂不作为已验证替代。'


def choose(p):
    ref,value,fp=p['ref'],p['value'],p['footprint']
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
              ('0603','22p'):'C1653',('0603','33p'):'C1663',('0603','1n'):'C1588',
              ('0805','10u'):'C15850',('0805','22u'):'C45783',('1206','22u'):'C12891',
              ('1206','10u'):'C13585',('1210','47u'):'C2932478'}
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
    assert len(rows)==327 and sum(r['Fitted'] for r in rows)==320
    grouped=[]
    for key,rr in groups.items():
        code=rr[0]['LCSC'];part=cat.get(code,{})
        perboard=sum(r['Fitted'] for r in rr);needed=perboard*120
        step=part.get('split') or 1;minimum=part.get('minBuyNumber') or 1
        order=math.ceil(max(needed,minimum)/step)*step if needed else 0
        stock=part.get('stockNumber');status=rr[0]['SelectionStatus']
        if code and needed:
            status='STOCK_OK' if stock>=order else 'SHORTAGE'
            if part.get('stock_channel')=='partner':status='PARTNER_CONFIRM'
            elif stock>=order and stock<3*order:status='LOW_BUFFER'
        elif not needed:status='DNP'
        grouped.append(dict(LCSC=code,MPN=rr[0]['MPN'],Manufacturer=rr[0]['Manufacturer'],
            CircuitValues=' | '.join(dict.fromkeys(r['CircuitValue'] for r in rr)),
            References=' '.join(r['Reference'] for r in rr if r['Fitted']),
            DNP_References=' '.join(r['Reference'] for r in rr if not r['Fitted']),
            PerBoard=perboard,Boards=100,BaseQty=perboard*100,SparePercent=20,
            QtyWithSpares=needed,OrderStep=step if code else '',ProposedQty=order,
            ObservedStock=stock if stock is not None else '',SupplyStatus=status,
            StockChannel=part.get('stock_channel','') if code else '',
            ObservedAtUTC=part.get('retrieved_at_utc',''),URL=part.get('url',''),
            Note=' | '.join(dict.fromkeys(r['Note'] for r in rr if r['Note']))))
    def write_csv(name,data):
        with (project/name).open('w',encoding='utf-8-sig',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    write_csv('PROCUREMENT_REFERENCES.csv',rows);write_csv('PROCUREMENT_100PCS.csv',grouped)
    counts=collections.Counter()
    for r in rows:
        if r['Fitted']:counts['selected' if r['LCSC'] else r['SelectionStatus'].lower()]+=1
    report=dict(revision='L1',boards=100,spare_percent=20,physical_positions=327,fitted_per_board=320,
        per_board_sourcing=dict(counts),unique_selected_lcsc_codes=len({r['LCSC'] for r in rows if r['LCSC']}),
        supply_groups=dict(collections.Counter(r['SupplyStatus'] for r in grouped)),
        policy='Aggregate fitted count by LCSC, add 20%, ceil to observed MOQ/order step. DNP count is zero. Stock is an unreserved snapshot, not a supply guarantee.',
        design_intent_sha256=hashlib.sha256(intent_path.read_bytes()).hexdigest(),
        snapshot_sha256=hashlib.sha256((project/'LCSC_STOCK_SNAPSHOT.json').read_bytes()).hexdigest(),
        status='SOURCING_REVIEW_NOT_RELEASED')
    (project/'PROCUREMENT_AUDIT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--project',type=Path,required=True)
    ap.add_argument('--intent',type=Path,required=True);a=ap.parse_args();generate(a.project,a.intent)
