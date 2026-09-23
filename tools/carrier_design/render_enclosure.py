"""Native PCB placement overview with the enclosure/flex assumptions annotated."""
import html,json
import pcbnew as k

def render(project):
    b=k.LoadBoard(str(project/'display_carrier.kicad_pcb'))
    m=json.loads((project/'SCREEN_BACK_LAYOUT.json').read_text(encoding='utf-8'))
    fps={f.GetReference():f for f in b.GetFootprints()}
    s=['<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1000" viewBox="0 0 1600 1000">',
       '<style>text{font-family:"Microsoft YaHei","Segoe UI",sans-serif}</style>',
       '<defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="6" refY="3" orient="auto"><path d="M0 0L6 3L0 6" fill="none" stroke="#ac7620" stroke-width="1.4"/></marker></defs>',
       '<rect width="1600" height="1000" fill="#f3f6fa"/>']
    def text(x,y,t,size=17,color='#243b53',bold=False):
        s.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}">{html.escape(t)}</text>')
    def rect(x,y,w,h,fill,stroke='none',r=0):
        s.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="1.2"/>')
    text(55,53,'显示载板 · 外壳独立固定 / 排线绕下沿',30,bold=True)
    text(55,87,'修订 M · 145 × 85 mm · 屏板间距约 3 mm · FPC 穿板槽已取消 · 尚未布线',19)
    text(55,132,'F 面 / 朝后壳：黄色为排线无器件区，蓝色为焊盘出线区；均可在 PCB 上布线。',17)
    ox,oy,sc=65,175,6
    def pt(x,y):return ox+x*sc,oy+y*sc
    def area(box,fill,stroke):
        x1,y1,x2,y2=box;rect(*pt(x1,y1),(x2-x1)*sc,(y2-y1)*sc,fill,stroke)
    rect(ox,oy,145*sc,85*sc,'#e5f0e9','#527f6d')
    for name,box in m['component_clear_areas_mm'].items():area(box,'#fff0ce' if 'FLEX' in name else '#d5e8fb','#c89b43' if 'FLEX' in name else '#729ac0')
    for ref,f in fps.items():
        z=f.GetBoundingBox(False,False);box=[k.ToMM(v)-50 for v in [z.GetX(),z.GetY(),z.GetRight(),z.GetBottom()]]
        if ref.startswith('H'):continue
        s.append(f'<g><title>{html.escape(ref+" · "+f.GetValue())}</title>')
        area(box,'#fff' if ref.startswith(('J','U','L')) else '#c7d5dc','#7f95a2');s.append('</g>')
    for x,y in [(4,4),(141,4),(4,81),(141,81)]:
        px,py=pt(x,y);s.append(f'<circle cx="{px}" cy="{py}" r="8.1" fill="#f3f6fa" stroke="#527f6d"/>')
    for x in [18.5,27.5]:rect(*pt(x-.5,72.5),sc,11*sc,'#f3f6fa','#527f6d',3)
    labels={'U100':('P4C5 / P4C6',-45,0),'J1200':('LCD FPC',-29,-19),'J1210':('触摸 FPC',-33,-26),
      'J1220':('CAM',-14,-20),'J300':('电池',-14,-28),'J301':('NTC',-14,-25),
      'U300':('充电',-12,1),'U310':('系统开关',-30,-15),'U400':('5V',-8,0),'L400':('L400',-16,0),
      'U410':('3V3',-15,-18),'U1200':('LCD 1V8',-44,-13),'U1201':('控制',-11,2),
      'J810':('FM ANT',-22,-20),'U815':('FM',-8,2),'J820':('SPK L',-22,-22),'J821':('SPK R',-22,-22),
      'U802':('MIC L',-18,-21),'U803':('MIC R',-18,-21),'U1301':('RH/T',-15,-17)}
    for ref,(label,dx,dy) in labels.items():
        f=fps[ref];x,y=pt(k.ToMM(f.GetPosition().x)-50,k.ToMM(f.GetPosition().y)-50);text(x+dx,y+dy,label,12,'#285579',True)
    for x,top,label in [(109,23,'触摸 9 mm'),(74.5,63,'LCD 22 mm')]:
        x1,y1=pt(x,83);x2,y2=pt(x,top)
        s.append(f'<path d="M{x1} {y1}L{x2} {y2}" stroke="#ac7620" stroke-width="1.6" stroke-dasharray="6 5" marker-end="url(#arrow)"/>')
        tx,ty=pt(x,84);text(tx-32,ty+31,label,14,'#885d1b',True)
    text(65,745,'坐标从 F 面看：板框左上角 (0,0)，X 向右、Y 向下；黄色区域是完整 PCB，不是开孔。',16)
    rect(990,150,555,250,'#fff','#d2dce5',12)
    text(1012,184,'插座下移，保留装配空间',23,bold=True)
    text(1012,226,'触摸 J1210：(109, 15.5) mm',19)
    text(1012,258,'LCD J1200：(74.5, 54.0) mm',19)
    text(1012,296,'相对上一版分别下移 7.925 / 7.825 mm。',16)
    text(1012,332,'两座仍在 F 面，入口朝下；针序保持。',16)
    text(1012,367,'绕下沿后向上插入，余量需用实物核对。',16,'#825b26')
    rect(990,420,555,267,'#fff','#d2dce5',12)
    text(1012,455,'器件为排线和出线让位',23,bold=True)
    text(1012,499,'LCD：左侧放电源/控制，右侧放扫描电阻。',16)
    text(1012,534,'触摸：焊盘前留 5 mm 深的无器件区。',16)
    text(1012,569,'LCD：焊盘前留 9.45 mm 深的无器件区。',16)
    text(1012,604,'主升压在右侧；FM、双麦避开排线路径。',16)
    text(1012,652,'铜线可走这些区域，尚未验证实际布通率。',16,'#825b26')
    rect(55,798,1490,150,'#fff','#d2dce5',12)
    text(78,833,'屏幕与 PCB 均由外壳支撑 · 名义背面间距 3 mm',22,bold=True)
    text(78,871,'B 面无器件本体；侧按键焊尾模型最高约 2.39 mm，名义剩余约 0.61 mm，尚未计焊锡、公差和绝缘层。',17)
    text(78,912,'两处温湿度隔热槽和四个安装孔保留。排线长度、折弯、插入量、开盖空间与外壳声孔仍需实物试装。',17,'#825b26')
    s.append('</svg>')
    for name in ['FLOORPLAN.svg','FPC_FIT.svg']:(project/name).write_text('\n'.join(s)+'\n',encoding='utf-8')
