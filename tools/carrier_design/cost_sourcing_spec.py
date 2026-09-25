"""Design-file purchasing changes from the 2026-09-25 domestic quotation.

Keep this delta after the earlier sourcing/passive revisions. No pin/net changes.
Physical assembly, power transients and thermal qualification remain open.
"""
POLYMER_PDF='https://atta.szlcsc.com/upload/public/pdf/source/20231121/628C7D57E65046C1B359A0CE9A870900.pdf'
CHANGES={
 'U1301':dict(lcsc='C2909890',value='SHT40-AD1B-R2',footprint='Carrier:SHT40_DFN4_NoCentralPad',
   status='Bare SHT40; same I2C 0x44 and pins; protect exposed sensor from liquids, cleaning and coating'),
 'U300':dict(lcsc='C2876371',value='BQ25896RTWT',
   status='Same BQ25896 RTW die/pinout; small-reel variant chosen for domestic direct stock; limited buffer'),
 'J1010':dict(lcsc='C88374',value='U.FL-R-SMT-1(80)',source='https://www.hirose.com/product/download/?distributor=all&lang=en&series=U.FL&type=catalogue',
   status='Same U.FL-R-SMT-1 receptacle and footprint; (80) is 10000/reel packaging'),
 'J1011':dict(lcsc='C88374',value='U.FL-R-SMT-1(80)',source='https://www.hirose.com/product/download/?distributor=all&lang=en&series=U.FL&type=catalogue',
   status='Same U.FL-R-SMT-1 receptacle and footprint; (80) is 10000/reel packaging'),
}
for ref in ['C405','C406']:
 CHANGES[ref]=dict(lcsc='C7432785',value='22u 25V X5R 10%',source='https://item.szlcsc.com/8438267.html',
   status='HGC1206R5226K250NSPJ; 22uF 25V X5R +/-10%, same 1206; SYS input bypass, DC-bias/ripple validation remains required')
for ref in ['C1012','C1013']:
 CHANGES[ref]=dict(lcsc='C19192479',value='220u 6.3V polymer 15mR',footprint='Carrier:KNSCHA_177EC0013_7343',source=POLYMER_PDF,
   status='177EC0013; polarized 220uF +/-20%, ESR 15mOhm, 6.3V on 3.8V rail; 2.2mm max height; burst/startup/ripple qualification required')

def apply(parts):
 for p in parts:
  if p['ref'] in CHANGES:
   p.update({k:v for k,v in CHANGES[p['ref']].items() if k!='lcsc'})

def selected_code(ref):
 return CHANGES.get(ref,{}).get('lcsc')
