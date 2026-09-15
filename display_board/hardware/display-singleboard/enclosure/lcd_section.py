from pathlib import Path
import cadquery as cq,os
p=Path(__file__).resolve().parent/'models'
s=cq.importers.importStep(str(p/'actual-J302.step')).val().mirror('XZ').translate((0,0,20.005))
q=s.intersect(cq.Workplane('XY').box(.04,10,5).translate((29,-17,23)).val())
vv,tt=q.tessellate(.02)
out=['<svg xmlns="http://www.w3.org/2000/svg" width="1500" height="650"><rect width="1500" height="650" fill="white"/>']
out += ['<polygon points="'+ ' '.join(str(100+(vv[i].y+20)*180)+','+str(520-(vv[i].z-21.5)*180) for i in tri)+'" fill="#72889b"/>' for tri in tt]
out.append('<text x="60" y="50" font-size="26">J302 actual STEP mid-section X29 / F0 / +Y to right</text>')
out.append('<text x="60" y="605" font-size="24">PCB F Z21.6 / solder tails toward -Y / flex entrance toward +Y</text></svg>')
(p/'lcd-real-section.svg').write_text(''.join(out))
os._exit(0)
