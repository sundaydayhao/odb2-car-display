"""Two-face assembly view from the native revision-K PCB, not a wiring diagram."""
import html,json
import pcbnew as k

def render(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    info=json.loads((project/'SCREEN_BACK_LAYOUT.json').read_text(encoding='utf-8'))
    mipi=info.get('revision')=='N'
    fps={f.GetReference():f for f in b.GetFootprints()}
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="1500" height="940" viewBox="0 0 1500 940">',
       '<style>text{font-family:"Microsoft YaHei","Segoe UI",sans-serif}</style>',
       '<rect width="1500" height="940" fill="#f4f7fb"/>']
    def text(x,y,t,size=16,color='#243b53',bold=False):
        s.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}">{html.escape(t)}</text>')
    def rect(x,y,w,h,fill,stroke='none',r=0):
        s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="1.3"/>')
    text(45,48,'显示载板 · 主控高速接口布局 / 排线穿板' if mipi else '显示载板 · 正面器件 / 背面贴屏 / 排线穿板',30,bold=True)
    text(45,81,f'{b.GetTitleBlock().GetRevision()} · 145 × 85 mm · 四层 · 0 条走线 · 穿线槽和屏板间隙尚待实物核对',18)
    scale=4.4;oy=160
    for side,ox,title,sub in [('F',55,'F 面 · 正面 / 朝后壳','从屏幕背面看：USB 在右，TF / SIM 在左'),
                              ('B',805,'B 面 · 背面 / 朝屏幕','此图左右镜像：0 个器件本体，仍有通孔焊脚')]:
        text(ox,122,title,22,bold=True);text(ox,146,sub,15)
        rect(ox,oy,145*scale,85*scale,'#ecf5f0','#4c896e')
        def point(x,y):return ox+(x if side=='F' else 145-x)*scale,oy+y*scale
        def region(x,y,w,h,fill,stroke='none',radius=0):
            px,py=point(x if side=='F' else x+w,y);rect(px,py,w*scale,h*scale,fill,stroke,radius*scale)
        if side=='F':
            if mipi:
                for name,(x1,y1,x2,y2) in info['component_clear_areas_mm'].items():
                    region(x1,y1,x2-x1,y2-y1,'#d6eafb','#82acce')
            for r,f in fps.items():
                if r.startswith(('H','TP')):continue
                z=f.GetBoundingBox(False,False)
                region(k.ToMM(z.GetX())-50,k.ToMM(z.GetY())-50,k.ToMM(z.GetWidth()),k.ToMM(z.GetHeight()),'#dce6ec','#c0ccd8')
            labels={'U100':'P4C5 / P4C6','J1200':'LCD FPC','J1210':'触摸 FPC','J1220':'CAM',
                    'J300':'电池 2Pin','J301':'NTC 2Pin','J810':'FM ANT','J820':'SPK L','J821':'SPK R',
                    'L400':'L400','U802':'MIC L','U803':'MIC R','U1301':'SHT40'}
            for r,label in labels.items():
                f=fps[r];z=f.GetBoundingBox(False,False)
                region(k.ToMM(z.GetX())-50,k.ToMM(z.GetY())-50,k.ToMM(z.GetWidth()),k.ToMM(z.GetHeight()),'#fff','#456e9d')
                x,y=point(k.ToMM(f.GetPosition().x)-50,k.ToMM(f.GetPosition().y)-50)
                # Separate the two crowded upper-edge connector labels.
                if r=='J300':text(x-11,y+35,label,12,'#325d91',True)
                elif r=='J301':text(x-26,y-21,label,12,'#325d91',True)
                elif r=='U100':text(x-40,y,label,14,'#325d91',True)
                elif r=='U1301':text(x-17,y-14,label,12,'#325d91',True)
                else:text(x-25,y-14,label,12,'#325d91',True)
        else:
            for f in fps.values():
                for p in f.Pads():
                    if p.GetDrillSize().x<=0 or f.GetReference().startswith('H'):continue
                    x,y=point(k.ToMM(p.GetPosition().x)-50,k.ToMM(p.GetPosition().y)-50)
                    s.append(f'<circle cx="{x}" cy="{y}" r="3" fill="#d6ad51" stroke="#79652d"/>')
            text(ox+208,oy+157,'背面没有器件本体',20,'#347359',True)
        for x,y in [(4,4),(141,4),(4,81),(141,81)]:
            px,py=point(x,y);s.append(f'<circle cx="{px}" cy="{py}" r="{1.35*scale}" fill="#f4f7fb" stroke="#4c896e"/>')
        for slot in info['slots']:
            x,y=slot['center_mm'];w,h=slot['size_mm']
            region(x-w/2,y-h/2,w,h,'#fff','#b36328',1.5)
            px,py=point(x,y)
            text(px-w*scale/2,py+23,f'{w} × {h} mm 穿线槽',12,'#a1521f',True)
        for x in [18.5,27.5]:region(x-.5,72.5,1,11,'#fff','#64937a',.5)
        text(ox,567,'同一块 PCB 的两面；图中槽口为实际 Edge.Cuts，非丝印示意。',14)
    cards=[(45,'穿板连接','触摸：J1210  →  10 × 3 mm 槽',
            '屏幕：J1200  →  20 × 3 mm 槽','排线从 B 面穿到 F 面，再插入座内。'),
           (535,'背面高度','全部器件本体都在 F 面',
            '未处理通孔脚：模型最大约 2.4 mm','不含焊锡与公差；需要绝缘间隙。'),
           (1025,'装配待定','7.5 / 46.1 mm 是原平面尖端距离',
            '穿板预留 4 mm 补偿，尚未实测','间隙、线长、折弯和针序需样件核对。')]
    if mipi:
        cards[2]=(1025,'MIPI 局部调整','主控上移 6 mm；LCD 净距 8.88 mm',
            '摄像头座移至 (94,39)，入口朝下','12 根信号试连通；等长与阻抗待定。')
    for x,title,a,c,e in cards:
        rect(x,605,435,171,'#fff','#d6e0eb',10);text(x+18,638,title,21,bold=True)
        text(x+18,672,a,16);text(x+18,704,c,16);text(x+18,744,e,14,'#665041')
    core=info['positions']['U100']['xy_mm']
    text(45,817,f'当前 F 面坐标：触摸座 (109, 7.575)；LCD 座 (74.5, 46.175)；核心板 ({core[0]:g}, {core[1]:g}) mm。',17)
    text(45,848,'双麦间距 60 mm；电池端子 J300 与 NTC 端子 J301 均为 PH 2.0 mm 2Pin。',17)
    text(45,889,'主工程未布线；屏板名义间距 3 mm。蓝色是局部出线/开盖空间；装配与高速性能仍待验证。' if mipi else '工程仍未布线；通过间距检查不代表排线已经够长，也不代表可直接贴金属屏背板。',17,'#97452b')
    s.append('</svg>')
    for name in ['FLOORPLAN.svg','FPC_FIT.svg']:(project/name).write_text('\n'.join(s)+'\n',encoding='utf-8')
