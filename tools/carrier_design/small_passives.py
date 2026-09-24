"""Reviewed 0402 substitutions; values and circuit connections stay unchanged.

Standard library 0402 is the imperial 1.0 x 0.5 mm body, not metric 0402.
Do not infer that every 0603 part can use these lower power ratings.
"""
RES0402={'10k':'C25744','100k':'C25741','4.7k':'C25900','47k':'C25792',
         '22k':'C25768','22R':'C25092','100R':'C25076'}
CAP0402={'100n':'C1525','1n':'C1523','22n':'C1532',
         '100p':'C1546','22p':'C1555','33p':'C1562'}

def selected_code(p):
    ref,value,fp=p['ref'],p['value'],p['footprint']
    nominal=value.split()[0]
    if ref.startswith('R') and fp in ['Resistor_SMD:R_0603_1608Metric','Resistor_SMD:R_0402_1005Metric']:
        # QOD discharge can start at 3.3^2/100 = 109 mW; keep its 0603.
        if ref=='R501' or '0.1%' in value:return None
        return RES0402.get(nominal)
    if ref.startswith('C') and fp in ['Capacitor_SMD:C_0603_1608Metric','Capacitor_SMD:C_0402_1005Metric']:
        # C0G audio/RF filters must not become X7R. Bulk/LDO and boost
        # bootstrap/compensation capacitors remain as reviewed in revision P.
        if nominal=='1n' and 'C0G' in value:return None
        if nominal=='100n' and any(v in value for v in ['25V','50V','C0G']):return None
        return CAP0402.get(nominal)

def apply(parts):
    for p in parts:
        if selected_code(p):
            p['footprint']=p['footprint'].replace('_0603_1608Metric','_0402_1005Metric')
