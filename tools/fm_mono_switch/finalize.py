"""Finalize component metadata without regenerating the routed PCB."""
from pathlib import Path
import csv, json, re, shutil, sys
sys.path.insert(0, 'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib import sexp, ops

ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch'
Q=sexp.QStr.of
intent=json.loads((P/'design-intent.json').read_text())
mpn={'U1':'SGM3718YUWQ10G/TR','U2':'RDA5807M','U3':'TLV75530PDBVR',
     'D1':'PESD5V0F1BL','Y1':'AB26T-32.768KHZ-12.5-B'}
source='${KIPRJMOD}/reference/RDA5807M.pdf'
description='500mA low-dropout regulator, fixed 3.0V output, SOT-23-5; IN/GND/EN/NC/OUT'
(P/'reference').mkdir(exist_ok=True)
shutil.copyfile(ROOT/'fm-module/RDA5807M datasheet_v1.pdf',P/'reference/RDA5807M.pdf')

def edit_properties(node):
    if not isinstance(node,list):return
    if sexp.tag_of(node)=='property':
        key,value=sexp.atoms(node)[:2]
        if key=='LCSC' and value not in ['C700220','C82537']:node[2]=Q('')
        if key=='Datasheet' and 'RDA5807M datasheet' in value:node[2]=Q(source)
        if key=='Description' and 'Fixed Output 1.2V' in value:node[2]=Q(description)
    for child in node:edit_properties(child)

for filename in ['fm_mono_switch.kicad_sch','fm_mono_switch.kicad_pcb','Mono.kicad_sym']:
    doc=sexp.load_file(str(P/filename));edit_properties(doc)
    if filename.endswith('.kicad_sch'):
        for ref,value in mpn.items():ops.set_prop(doc,ref,'MPN',value)
        for sym in sexp.find_all(doc,'symbol'):
            props={sexp.atoms(p)[0]:sexp.atoms(p)[1] for p in sexp.find_all(sym,'property')}
            if props.get('Reference') in ['H1','H2']:
                sexp.find(sym,'in_bom')[1]=sexp.Sym('no')
    sexp.save_file(str(P/filename),doc)

import pcbnew as k
board=k.LoadBoard(str(P/'fm_mono_switch.kicad_pcb'))
nc_name='unconnected-(U3-NC-Pad4)'
nc=next((n for n in board.GetNetsByNetcode().values() if n.GetNetname()==nc_name),None)
if nc is None:
    nc=k.NETINFO_ITEM(board,nc_name);board.Add(nc)
for fp in board.GetFootprints():
    ref=fp.GetReference()
    if ref in mpn:
        fp.SetField('MPN',mpn[ref]);fp.GetField('MPN').SetVisible(False)
    if ref=='U3':
        for pad in fp.Pads():
            if pad.GetNumber()=='4':pad.SetNet(nc)
board.BuildConnectivity();k.ZONE_FILLER(board).Fill(board.Zones())
k.SaveBoard(str(P/'fm_mono_switch.kicad_pcb'),board)

for ref,a in intent['parts'].items():
    if ref not in ['U1','U2']:a['lcsc']=''
    if ref=='U2':a['source']=source
    a['mpn']=mpn.get(ref,'')
intent['status']='A2 integrated RDA5807M prototype; CAD checks passed, bench validation required'
intent['prototype_fabrication_ready']=True
intent['manufacturing_release']=False
(P/'design-intent.json').write_text(json.dumps(intent,indent=2)+'\n')

groups={}
for ref,a in intent['parts'].items():
    if not a['footprint'] or ref.startswith('H'):continue
    key=(a['value'],a['footprint'],a['mpn'],a['lcsc'])
    groups.setdefault(key,[]).append(ref)
with (P/'BOM.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['References','Qty','Value','Footprint','MPN','LCSC candidate','Assembly note'])
    for (value,fp,part,lcsc),refs in groups.items():
        note=''
        if refs[0].startswith('J'):note='Optional headers; wires may be soldered directly'
        if refs[0]=='Y1':note='Form leads to 1.9mm pitch; verify crystal startup on first prototype'
        if refs[0]=='U1':note='UTQFN; reflow recommended; verify pin 1 in assembly preview'
        if refs[0]=='D1':note='Low-capacitance F1BL, not S1BL; bidirectional'
        w.writerow([','.join(refs),len(refs),value,fp,part,lcsc,note])

# Keep the rebuild source consistent; require explicit consent to overwrite routing.
g=ROOT/'tools/fm_mono_switch/generate.py'
s=g.read_text(encoding='utf-8')
s=re.sub(r",lcsc='(?!C700220'|C82537')[^']+'",'',s)
s=s.replace('../../../../fm-module/RDA5807M datasheet_v1.pdf',source)
if 'regen-placed-board' not in s:
    s=s.replace("defs = {}", "if (OUT/(NAME+'.kicad_pcb')).exists() and '--regen-placed-board' not in sys.argv:\n    raise SystemExit('Refusing to overwrite routed PCB. Use --regen-placed-board only for a fresh layout.')\n\ndefs = {}")
    s=s.replace("if sexp.atoms(p)[0]=='Value':p[2]=Q(name)", "if sexp.atoms(p)[0]=='Value':p[2]=Q(name)\n        if name=='LDO' and sexp.atoms(p)[0]=='Description':p[2]=Q("+repr(description)+")")
g.write_text(s,encoding='utf-8')
print('Metadata, grouped BOM and portable RDA reference updated; routing preserved.')
