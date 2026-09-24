"""Parse exported CAM files independently and render the actual top copper."""
from pathlib import Path
import json, sys
import pymupdf
ROOT=Path(__file__).resolve().parents[2];W=ROOT/'build/fm-mono-switch'
P=ROOT/'display_board/hardware/fm-mono-switch/output'
sys.path.insert(0,str(W/'runtime/gerber-python'))
from gerbonara import GerberFile, ExcellonFile
from gerbonara.utils import MM
G=P/'fabrication/gerbers'
report={}
for f in sorted(G.iterdir()):
    if f.suffix=='.gbrjob':continue
    layer=ExcellonFile.open(f) if f.suffix=='.drl' else GerberFile.open(f)
    report[f.name]={'objects':len(layer.objects),'bounds_mm':layer.bounding_box(MM)}
    if f.suffix=='.gm1':
        assert len(layer.objects)==4
        w,h=layer.size(MM)
        assert abs(w-40.05)<.001 and abs(h-36.05)<.001,(w,h)
    if 'NPTH' in f.name:assert len(layer.objects)==2
    if f.suffix=='.gtl':
        svg=W/'gerber-top.svg';svg.write_text(str(layer.to_svg(fg='#B5472F',bg='white')),encoding='utf-8')
        d=pymupdf.open(svg);page=d[0]
        page.get_pixmap(matrix=pymupdf.Matrix(1400/page.rect.width,1400/page.rect.width)).save(W/'gerber-top.png')
(P/'gerber-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
