"""Export the verified prototype PCB and package a portable KiCad project."""
from pathlib import Path
import csv, hashlib, json, shutil, subprocess, zipfile

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
assert len(positions)==30,len(positions)
with (FAB/'cpl-jlc-candidate.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Designator','Mid X','Mid Y','Layer','Rotation'])
    for r in positions:w.writerow([r['Ref'],r['PosX']+'mm',r['PosY']+'mm','Top' if r['Side']=='top' else 'Bottom',r['Rot']])
shutil.copyfile(P/'BOM.csv',FAB/'BOM-engineering.csv')
with (P/'BOM.csv').open(newline='',encoding='utf-8-sig') as f:rows=list(csv.DictReader(f))
with (FAB/'bom-jlc-candidate.csv').open('w',newline='',encoding='utf-8-sig') as f:
    w=csv.writer(f);w.writerow(['Comment','Designator','Footprint','LCSC Part #'])
    for r in rows:
        if r['References'].startswith(('J','Y')):continue
        w.writerow([r['Value'],r['References'],r['Footprint'].split(':')[-1],r['LCSC candidate']])
(FAB/'ASSEMBLY-README.txt').write_text(
    'A2 prototype, 40 x 36 mm, 2 layers, nominal 1.6 mm FR4, 1 oz copper.\n'
    'Gerbers use absolute coordinates, matching the native placement export.\n'
    'CPL: 30 top-side SMD components. Headers and Y1 crystal are hand-soldered.\n'
    'Engineering BOM is authoritative. Candidate JLC BOM has intentionally blank supplier codes.\n'
    'Assembly service must match exact parts, pin 1 and rotation before ordering.\n'
    'SGM3718 is UTQFN-10; use included custom footprint and assembly preview.\n'
    'Y1: form leads to 1.9 mm hole pitch. No hardware validation has been performed.\n',encoding='utf-8')
for name in ['erc-final.json','drc-final.json','verification.json','review-diff.json']:
    shutil.copyfile(W/name,OUT/name)
for name in ['front-final.png','back-final.png']:
    shutil.copyfile(W/name,OUT/name)
assert len(list(G.glob('*.g*')))==10, list(G.iterdir())  # 9 layers plus gbrjob
assert len(list(G.glob('*.drl')))==2
with zipfile.ZipFile(OUT/'fm-mono-switch-A2-Gerber.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(G.iterdir()):z.write(p,p.name)
with zipfile.ZipFile(OUT/'fm-mono-switch-A2-project.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(P.rglob('*')):
        if not p.is_file() or p.suffix in ['.zip','.kicad_prl'] or p.name=='.gitignore':continue
        if any(q.startswith('.') for q in p.relative_to(P).parts):continue
        z.write(p,'fm-mono-switch-A2/'+p.relative_to(P).as_posix())
    for p in sorted((ROOT/'tools/fm_mono_switch').glob('*.py')):
        z.write(p,'repository-tools/fm_mono_switch/'+p.name)
print('Exported 9 Gerber layers, 2 drill files and 30 SMD placements.')
print(OUT/'fm-mono-switch-A2-project.zip')
print(OUT/'fm-mono-switch-A2-Gerber.zip')
