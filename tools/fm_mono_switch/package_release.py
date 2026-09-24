"""Export the verified prototype PCB and package a portable KiCad project."""
from pathlib import Path
import csv, hashlib, json, shutil, subprocess, sys, zipfile

ROOT=Path(__file__).resolve().parents[2];P=ROOT/'display_board/hardware/fm-mono-switch'
W=ROOT/'build/fm-mono-switch';OUT=P/'output';FAB=OUT/'fabrication';G=FAB/'gerbers'
G.mkdir(parents=True,exist_ok=True)
CLI='C:/Program Files/KiCad/10.0/bin/kicad-cli.exe';PCB=P/'fm_mono_switch.kicad_pcb'
verification=json.loads((W/'verification.json').read_text())
for name,digest in verification['sha256'].items():
    assert hashlib.sha256((P/name).read_bytes()).hexdigest()==digest,('Changed after verification',name)
def run(*args):
    subprocess.run([CLI,*map(str,args)],check=True)
run('pcb','export','gerbers','--output',str(G)+'/',
    '--layers','F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,F.Paste,B.Paste,Edge.Cuts',
    '--subtract-soldermask',PCB)
run('pcb','export','drill','--output',str(G)+'/', '--format','excellon',
    '--excellon-units','mm','--excellon-separate-th',PCB)
run('pcb','export','pos','--output',FAB/'cpl-kicad.csv','--format','csv','--units','mm',
    '--side','both','--smd-only','--exclude-dnp',PCB)

# Native coordinates and raw angles are retained for assembly-provider review.
with (FAB/'cpl-kicad.csv').open(newline='',encoding='utf-8-sig') as f:positions=list(csv.DictReader(f))
assert len(positions)==33,len(positions)
smd_refs={r['Ref'] for r in positions}
with (FAB/'cpl-jlc-candidate.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Designator','Mid X','Mid Y','Layer','Rotation'])
    for r in positions:w.writerow([r['Ref'],r['PosX']+'mm',r['PosY']+'mm','Top' if r['Side']=='top' else 'Bottom',r['Rot']])
shutil.copyfile(P/'BOM.csv',FAB/'BOM-engineering.csv')
with (P/'BOM.csv').open(newline='',encoding='utf-8-sig') as f:rows=list(csv.DictReader(f))
with (FAB/'bom-jlc-candidate.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Comment','Designator','Footprint','LCSC Part #'])
    for r in rows:
        if not set(r['References'].replace(' ','').split(',')) <= smd_refs:continue
        w.writerow([r['Value'],r['References'],r['Footprint'].split(':')[-1],r['LCSC candidate']])
(FAB/'ASSEMBLY-README.txt').write_text(
    'A8 prototype, 40 x 58 mm, 2 layers, nominal 1.6 mm FR4, 1 oz copper.\n'
    'Gerbers use absolute coordinates, matching the native placement export.\n'
    'CPL: 33 top-side SMD components. U4 DHT20, Y1 crystal and optional headers are hand-soldered.\n'
    'U4 DHT20: bend the leads and install HORIZONTALLY, sensing face UP. Body 12.6 x 16.1 x 5.8 mm, ~0.5 mm standoff. Verify the first formed part; do not bend at the plastic seal or cover the sensing holes.\n'
    'U5 is VEML7700-TT top view; never substitute -TR side view. Provide a clear optical window.\n'
    'Engineering BOM is authoritative. Candidate JLC BOM contains reviewed supplier codes; domestic stock/MOQ must be reconfirmed. RDA5807M is on backorder in the user quote.\n'
    'Assembly service must match exact parts, pin 1 and rotation before ordering.\n'
    'TS5A22364DGSR is VSSOP-10 with exposed leads; use A8 PCB and check pin 1 in the assembly preview.\n'
    'R5/R6 are 1k series current limiters. A8 is not pin-compatible with the A5 switch.\n'
    'D1: Nexperia PESD5V0F1BL,315 / C45961; SOD882 / DFN1006-2. U3 and Y1 unchanged from A6.\n'
    'Y1: form leads to 1.9 mm hole pitch. No hardware validation has been performed.\n',encoding='utf-8')
for name in ['erc-final.json','drc-final.json','verification.json','review-diff.json','a8-geometry-comparison.json']:
    shutil.copyfile(W/name,OUT/name)
for name in ['front-final.png','back-final.png']:
    shutil.copyfile(W/name,OUT/name)
assert len(list(G.glob('*.g*')))==10, list(G.iterdir())  # 9 layers plus gbrjob
assert len(list(G.glob('*.drl')))==2
subprocess.run([sys.executable,str(ROOT/'tools/fm_mono_switch/verify_gerbers.py')],check=True)
with zipfile.ZipFile(OUT/'fm-mono-switch-A8-Gerber.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(G.iterdir()):z.write(p,p.name)
with zipfile.ZipFile(OUT/'fm-mono-switch-A8-project.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(P.rglob('*')):
        if not p.is_file() or p.suffix in ['.zip','.kicad_prl'] or p.name=='.gitignore':continue
        if any(q.startswith('.') for q in p.relative_to(P).parts):continue
        if p.parent == P and p.name not in {'fm_mono_switch.kicad_sch','fm_mono_switch.kicad_pcb','fm_mono_switch.kicad_pro','Mono.kicad_sym','sym-lib-table','fp-lib-table','design-intent.json','procurement.json','BOM.csv','BOM-cost.csv','BOM-LCSC-A8.csv','README.md','SOURCES.md','VERIFICATION.md','PROCUREMENT.md','SENSORS.md'}:continue
        if 'archive' in p.relative_to(P).parts:continue
        if p.name in ['a4-geometry-comparison.json','a5-geometry-comparison.json','a6-geometry-comparison.json','a7-geometry-comparison.json']:continue
        z.write(p,'fm-mono-switch-A8/'+p.relative_to(P).as_posix())
    for p in sorted((ROOT/'tools/fm_mono_switch').glob('*.py')):
        z.write(p,'repository-tools/fm_mono_switch/'+p.name)
print('Exported 9 Gerber layers, 2 drill files and 33 SMD placements.')
print(OUT/'fm-mono-switch-A8-project.zip')
print(OUT/'fm-mono-switch-A8-Gerber.zip')
