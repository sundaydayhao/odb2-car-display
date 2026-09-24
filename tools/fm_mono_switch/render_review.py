"""Native exports and visual/semantic review, using Windows-safe git paths."""
from pathlib import Path
import json, subprocess, sys
import pymupdf
from PIL import Image, ImageChops
sys.path.insert(0,'C:/projects/kicad-projects/tools/KiSkill-main')
from kicad_lib.cli import probe
from kicad_lib.diff import semantic_diff
ROOT=Path(__file__).resolve().parents[2];W=ROOT/'build/fm-mono-switch'
P=ROOT/'display_board/hardware/fm-mono-switch'; CLI='C:/Program Files/KiCad/10.0/bin/kicad-cli.exe'
base=W/'baseline.kicad_sch'
base.write_bytes(subprocess.check_output(['git','show','7ceb508:display_board/hardware/fm-mono-switch/fm_mono_switch.kicad_sch'],cwd=ROOT))
for tag,sch in [('base',base),('final',P/'fm_mono_switch.kicad_sch')]:
    pdf=W/('schematic-'+tag+'.pdf')
    subprocess.run([CLI,'sch','export','pdf','--output',str(pdf),str(sch)],check=True)
    d=pymupdf.open(pdf);page=d[0]
    page.get_pixmap(matrix=pymupdf.Matrix(3000/page.rect.width,3000/page.rect.width)).save(W/('schematic-'+tag+'.png'))
for side,layers in [('front','F.Cu,F.Silkscreen,Edge.Cuts'),('back','B.Cu,B.Silkscreen,Edge.Cuts')]:
    svg=W/(side+'-final.svg')
    subprocess.run([CLI,'pcb','export','svg','--output',str(svg),'--layers',layers,'--mode-single','--fit-page-to-board','--exclude-drawing-sheet',str(P/'fm_mono_switch.kicad_pcb')],check=True)
    doc=pymupdf.open(svg);page=doc[0]
    page.get_pixmap(matrix=pymupdf.Matrix(1500/page.rect.width,1500/page.rect.width)).save(W/(side+'-final.png'))
old_erc=json.loads((W/'erc-a4-baseline.json').read_text(encoding='utf-8'))
new_erc=json.loads((W/'erc-final.json').read_text(encoding='utf-8'))
assert not old_erc.get('violations',[]) and not new_erc.get('violations',[])
before=Image.open(W/'schematic-base.png').convert('RGB')
after=Image.open(W/'schematic-final.png').convert('RGB')
mask=ImageChops.difference(before,after).convert('L').point(lambda x: 255 if x else 0)
bbox=mask.getbbox()
composite=after.copy()
composite.paste(Image.blend(after,Image.new('RGB',after.size,'red'),0.65),(0,0),mask)
composite.save(W/'schematic-diff.png')
pixel={'changed_px':mask.histogram()[255],'bbox_px':bbox,
       'bbox_mm':[round(x*594/after.width,3) for x in bbox] if bbox else None,
       'composite':str(W/'schematic-diff.png'),'method':'RGB difference mask; no monochrome threshold'}
report={'baseline':'7ceb508','semantic':semantic_diff(probe(str(base)),probe(str(P/'fm_mono_switch.kicad_sch'))),
        'pixel':pixel,
        'erc_new':[],'erc_gone':[], 'erc_note':'Both native JSON reports contain zero violations.'}
(W/'review-diff.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
