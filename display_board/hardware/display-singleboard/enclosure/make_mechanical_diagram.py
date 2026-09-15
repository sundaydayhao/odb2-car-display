from pathlib import Path
import json,math,html
p=Path(__file__).resolve().parent
d=json.loads((p/'case-parameters.json').read_text())
r=json.loads((p/'models/mechanical-check.json').read_text())
s=['<svg xmlns="http://www.w3.org/2000/svg" width="1800" height="1100" viewBox="0 0 1800 1100">','<rect width="1800" height="1100" fill="#f4f7fa"/>','<style>text{font-family:Arial,Microsoft YaHei,sans-serif;fill:#263646}.title{font-size:30px;font-weight:bold}.h{font-size:23px;font-weight:bold}.t{font-size:19px}.small{font-size:16px}</style>']
def txt(x,y,text,cls='t',color=None):s.append(f'<text x="{x}" y="{y}" class="{cls}"'+(f' fill="{color}"' if color else '')+'>'+html.escape(text)+'</text>')
def rect(x,y,w,h,fill,stroke='#667788',dash=False):s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="2"'+(' stroke-dasharray="7 5"' if dash else '')+'/>')
def line(x1,y1,x2,y2,stroke='#667788',width=2,dash=False):s.append(f'<path d="M{x1},{y1}L{x2},{y2}" fill="none" stroke="{stroke}" stroke-width="{width}"'+(' stroke-dasharray="7 5"' if dash else '')+'/>')
txt(55,55,'A7 / 屏幕、主板与外壳装配样件','title')
txt(55,91,'参数化机械包络，不是模具图；屏幕盖板、触点面、FPC 长度和开窗仍需样品确认。','t')
txt(55,140,'背面平面投影','h')
k=4.1; ox=65;oy=175
def xy(x,y):return ox+(x+60)*k,oy+(y+92.5)*k
def brect(box,fill,stroke='#667788',dash=False):
    x,y=xy(box[0],box[1]);rect(x,y,(box[2]-box[0])*k,(box[3]-box[1])*k,fill,stroke,dash)
brect([-60,-92.5,118,33.5],'#e4e9ef','#41576b')
brect([-53.43,-73.48,111.43,26.48],'#d1dbe4','#8496a8')
brect([-35,-88.5,93,-6.5],'none','#788a99',True)
brect([-1,-7.5,59,33.5],'none','#788a99',True)
brect([-31,-84.5,89,-10.5],'#cce7d7','#357552')
brect(d['rf']['antenna_xy'],'#f1bdd6','#a22b67')
for x,y in d['pcb']['holes']:
    xx,yy=xy(x,y);s.append(f'<circle cx="{xx}" cy="{yy}" r="{3*k}" fill="#fff" stroke="#466379" stroke-width="2"/><circle cx="{xx}" cy="{yy}" r="{1.5*k}" fill="#bdccd7"/>')
for box_,label,color in [([-24.2,-26.3,28.2,-20.6],'J1','#6375b8'),([46.8,-21.6,53.2,-12.4],'J2','#6375b8'),([19.25,-19.93,38.75,-13.83],'LCD F0','#b87922'),([65,-22,73,-17],'CTP F0','#b87922'),([1.4,-77.7,24.6,-72.2],'CAM F','#b87922')]:
    brect(box_,'#f4ddaf' if color=='#b87922' else '#d8ddf4',color);xx,yy=xy((box_[0]+box_[2])/2,box_[1]-1);txt(xx-20,yy,label,'small')
brect([19,-13.62,39,26.48],'none','#c8872c',True)
x1,y1=xy(29,-47.5);x2,y2=xy(29,-23.5);line(x1,y1,x2,y2,'#416394',3);txt(x1+10,(y1+y2)/2,'24 mm','small')
xx,yy=xy(44,-83);txt(xx+60,yy-10,'C6 天线上向','small')
xx,yy=xy(-31,-54);txt(xx+35,yy,'120 × 74 mm PCB','h')
xx,yy=xy(-45,10);txt(xx,yy,'LCM 164.86 × 99.96 mm','t')
txt(65,728,'外壳外形：178 × 126 × 37.2 mm','h')
txt(65,760,'电子仓：128 × 82 mm；下方独立喇叭 / 底座承力仓','t')
txt(65,792,'四角 PCB 孔距：110 × 64 mm，孔心距板边 5 mm','t')
txt(65,824,'F 面朝可拆后盖：排线锁扣与维护排针均从后方操作','t')
txt(890,140,'Y–Z 装配剖面与排线长度估算','h')
sk=4.8; sox=885; soy=465
def yz(y,z):return sox+(y+92.5)*sk,soy-z*sk
def section(y0,z0,y1,z1,fill,stroke):
    x,y=yz(y0,z1);rect(x,y,(y1-y0)*sk,(z1-z0)*sk,fill,stroke)
section(-73.48,1.2,26.48,7.2,'#bdcedc','#6d8294')
section(-84.5,20,-10.5,21.6,'#acd7bd','#3c8660')
section(-84.3,21.6,-78.9,22.0,'#ecaecf','#a22b67')
section(-88.5,35.2,33.5,37.2,'#d1d9e1','#4f657b')
f=d['fpc']['lcd'];rootz=7.2;cz=r['lcd_fpc']['contact_z_assumed'];R=f['bend_radius'];edge=-9.5;turn=edge+2*R
path=[(26.48,rootz),(turn,rootz)]
for i in range(1,13):
    a=-math.pi/2-i*math.pi/24;path.append((turn+R*math.cos(a),rootz+R+R*math.sin(a)))
path.append((edge+R,cz-R))
for i in range(1,13):
    a=i*math.pi/24;path.append((edge+R*math.cos(a),cz-R+R*math.sin(a)))
path.append((f['mouth_xy'][1],cz))
poly=' '.join(f'{x},{y}' for x,y in [yz(*q) for q in path]);s.append(f'<polyline points="{poly}" stroke="#c47a15" stroke-width="4" fill="none"/>')
aa=yz(-78.9,21.6);bb_=yz(-73.48,7.2);line(*aa,*bb_,'#b02b70',3,True)
txt(927,285,'天线 → 屏金属：15.386 mm','t')
txt(1060,420,'屏总厚暂按 6 mm','small')
txt(1120,349,'R = 2 mm（待确认）','small')
txt(890,520,'LCD 已改中央 X29、下沿 Y−17；不依靠横向硬折排线。','t')
txt(890,555,f'几何路径 {r["lcd_fpc"]["centerline_estimate_mm"]:.2f} + 插入 3 + 装配余量 4 = {r["lcd_fpc"]["required_usable_length_estimate_mm"]:.2f} mm','t')
txt(890,590,f'商品参考 66.54 mm，余量约 {r["lcd_fpc"]["reference_margin_mm"]:.2f} mm：仍未确认可装配。','t')
txt(890,625,'真实尾根、可用尾长、允许弯折半径、接触面均需核对。','t')
txt(890,681,'固定和维修','h')
for i,t in enumerate(['前框承屏，承载壳承 PCB / 底座，后盖可单独拆卸。','前框卡扣预定位 + 4 螺钉，PCB 四角固定。','USB 调试 / SD / 5V / 电源拨键从侧面操作。','RST / BOOT 是凹入维护小孔，不使用大操作帽。','喇叭 3W 候选：约 5cc 后腔，左向导声；USB-A 取消。']):txt(890,716+i*35,t,'t')
rect(55,925,1685,120,'#fff2d8','#d6a355')
txt(75,962,'未确认：触摸盖板外形、厚度、有效显示区，端口配套插头，麦克风 / 喇叭实物尺寸。','t')
txt(75,997,'当前显示窗口 154 × 86 mm 仅为可修改占位，可能遮挡实际显示区；RF 15 mm 仅通过名义实体几何检查。','t')
txt(75,1030,'没有热、振动、材料强度或实物装配验证；所有模型保留参数源文件，不能直接作为量产放行。','small')
s.append('</svg>');(p/'models/assembly-section.svg').write_text('\n'.join(s),encoding='utf8')
