"""Dimensioned front-projection overview from the actual staged PCB."""
import argparse, html, json
from pathlib import Path
import pcbnew as k


def render(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    m=json.loads((project/'MECHANICAL_LAYOUT.json').read_text(encoding='utf-8'))
    fps={f.GetReference():f for f in b.GetFootprints()}
    ox,oy=70,145;scale=5.6
    def pt(x,y):return ox+x*scale,oy+y*scale
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="1480" height="850" viewBox="0 0 1480 850">',
       '<style>text{font-family:"Microsoft YaHei","Segoe UI",sans-serif} .small{font-size:13px}</style>',
       '<rect width="1480" height="850" fill="#f5f7fb"/>']
    def text(x,y,t,size=17,color='#20324a',weight='400'):
        s.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{weight}">{html.escape(t)}</text>')
    def rect(x,y,w,h,fill,stroke,dash=''):
        px,py=pt(x,y);s.append(f'<rect x="{px}" y="{py}" width="{w*scale}" height="{h*scale}" fill="{fill}" stroke="{stroke}" stroke-width="1.5" '+(f'stroke-dasharray="{dash}"' if dash else '')+'/>')
    text(60,53,'显示载板 · 折后排线与车内传感器布局',30,weight='700')
    text(60,87,'2026-09-23 / 修订 G / 实际 PCB 坐标投影；尚未布线',18)
    text(60,117,'正视投影：F 面朝屏幕，B 面朝后壳；与用户背面照片左右镜像。',16)
    rect(0,0,164.86,99.96,'#e4e9f0','#a5b0c0','7 5')
    bx,by=m['pcb_offset_from_screen_top_left_mm'];rect(bx,by,145,85,'#fbfffc','#43866b')
    text(*pt(3,98),'裸屏名义 164.86 × 99.96 mm；总成盖板尺寸待确认',14)
    # Every body envelope comes from the PCB, including the revised side.
    for ref,f in fps.items():
        z=f.GetBoundingBox(False,False)
        x=k.ToMM(z.GetX())-50+bx;y=k.ToMM(z.GetY())-50+by
        w=k.ToMM(z.GetWidth());h=k.ToMM(z.GetHeight())
        if ref.startswith(('TP','H')):continue
        rect(x,y,w,h,'#e8eef0' if f.GetLayer()==k.B_Cu else '#c8dedc','#c4ced6')
    for x,y,w,h,c in [(30,8,12,77,'#cde9f5'),(59,46.3,23,38.7,'#ffebcc')]:
        px,py=pt(bx+x,by+y)
        s.append(f'<rect x="{px}" y="{py}" width="{w*scale}" height="{h*scale}" fill="{c}" fill-opacity="0.75" stroke="#8697a6" stroke-dasharray="4 3"/>')
    selected=[('U100','P4C5 / P4C6 · B','#64758b'),('J1210','J1210','#167ea4'),
              ('J1200','LCD J1200','#b87013'),('J1220','CAM J1220','#435ba1'),
              ('U1300','OPT3001 / B','#6855a8'),('U1301','SHT40 / B','#0c8d76')]
    for ref,label,color in selected:
        f=fps[ref];z=f.GetBoundingBox(False,False)
        x=k.ToMM(z.GetX())-50+bx;y=k.ToMM(z.GetY())-50+by
        rect(x,y,k.ToMM(z.GetWidth()),k.ToMM(z.GetHeight()),'none' if ref=='U100' else '#ffffff',color)
        px,py=pt(k.ToMM(f.GetPosition().x)-50+bx,k.ToMM(f.GetPosition().y)-50+by)
        text(px-35,py-21,label,14,color,'600')
    for ref,tip in m['tail_tip_from_screen_top_mm'].items():
        x=m['fpc_positions_mm'][ref][0]+bx
        px,py=pt(x,tip)
        s.append(f'<path d="M {px-25} {oy} H {px-13} V {py} H {px+13}" fill="none" stroke="#b33b51" stroke-width="1.5"/>')
        text(px+17,py-3,f'尖端 Y={tip:g}',14,'#b33b51','600')
    for x in [117.5,126.5]:rect(bx+x-.5,by+72.5,1,11,'#ffffff','#61847d')
    text(*pt(bx+99,by+91),'隔热槽 + 全层禁铺铜',14,'#0c8d76')
    x=1030
    notes=[('PCB', '145 × 85 mm，四层；尺寸未扩大'),
           ('屏幕内位置','左偏移 9.93 mm；顶部偏移 2.50 mm'),
           ('触摸 J1210 · F','X=36.000，Y=3.575 mm'),
           ('LCD J1200 · F','X=70.500，Y=42.175 mm'),
           ('名义装配换算','入口偏移 3.525；插入深度暂按 2.10'),
           ('光照 U1300 · B','OPT3001 / 0x45；顶部光窗或导光件'),
           ('温湿度 U1301 · B','SHT40 / 0x44；底部独立通风腔'),
           ('实际修改','移动/翻面 30 个已有器件，新增 7 个'),
           ('待实测项','触摸 X、盖板边界、屏板间距、插接方向')]
    for i,(title,detail) in enumerate(notes):
        y=159+i*57;text(x,y,title,16,weight='700');text(x,y+23,detail,14)
    text(60,770,'触摸横向位置按照片估算；7.5 / 46.1 mm 为用户更正后的尖端测量值。',18,'#91364a')
    text(60,802,'连接器名义插深、屏板堆叠、触摸针序和外壳光窗/通风仍须样件核对。0 条走线，不能据此投板。',16,'#91364a')
    s.append('</svg>')
    (project/'FLOORPLAN.svg').write_text('\n'.join(s)+'\n',encoding='utf-8')
    (project/'FPC_FIT.svg').write_text('\n'.join(s)+'\n',encoding='utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True)
    render(p.parse_args().project.resolve())
