from pathlib import Path
import cadquery as cq
import math,os,sys
p=Path(__file__).parent/'microphone-carrier/Microphone.3dshapes/B4013AM422-042_nominal.step'
s=cq.Workplane('XY').circle(2).extrude(1.3).val()
for i in range(6):
 a=i*math.pi/3;x,y=math.cos(a),math.sin(a)
 s=s.cut(cq.Workplane('XY').circle(.125).extrude(.2).translate((x,y,1.1)).val())
assert s.isValid();cq.exporters.export(s,str(p))
sys.stdout.flush();os._exit(0)
