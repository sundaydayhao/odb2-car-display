"""Two-page Chinese wiring and assembly guide for the routed chip-based board."""
from pathlib import Path
import shutil
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
import pymupdf

ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'display_board/hardware/fm-mono-switch'; OUT=P/'output'; PDF=OUT/'pdf'
PDF.mkdir(parents=True,exist_ok=True)
shutil.copyfile(ROOT/'build/fm-mono-switch/board-3d.png',OUT/'board-3d.png')
shutil.copyfile(ROOT/'build/fm-mono-switch/schematic-final.pdf',PDF/'schematic.pdf')
pdfmetrics.registerFont(TTFont('CN','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
W,H=1300,1070
pdf=canvas.Canvas(str(PDF/'wiring.pdf'),pagesize=(W,H))
pdf.setTitle('RDA5807M + TS5A22364 单声道小板 A6：接线与安装')
INK='#17304A';MUTED='#587085';BLUE='#266ACB';GREEN='#00876F';ORANGE='#BD6818'
def box(x,y,w,h,fill,stroke=None,r=12):
    pdf.setFillColor(HexColor(fill));pdf.setStrokeColor(HexColor(stroke or fill))
    pdf.roundRect(x,H-y-h,w,h,r,fill=1,stroke=bool(stroke))
def t(s,x,y,size=17,color=INK):
    pdf.setFont('CN',size);pdf.setFillColor(HexColor(color));pdf.drawString(x,H-y-size,s)
def line(points,color=BLUE,width=3,arrow=False):
    pdf.setStrokeColor(HexColor(color));pdf.setFillColor(HexColor(color));pdf.setLineWidth(width)
    p=pdf.beginPath();p.moveTo(points[0][0],H-points[0][1])
    for x,y in points[1:]:p.lineTo(x,H-y)
    pdf.drawPath(p)
    if arrow:
        x,y=points[-1];xx,yy=points[-2];d=8
        pts=[(x-d,y-d/2),(x-d,y+d/2)] if xx<x else [(x+d,y-d/2),(x+d,y+d/2)]
        if y!=yy:pts=[(x-d/2,y-d),(x+d/2,y-d)] if yy<y else [(x-d/2,y+d),(x+d/2,y+d)]
        p=pdf.beginPath();p.moveTo(x,H-y)
        for a,b in pts:p.lineTo(a,H-b)
        p.close();pdf.drawPath(p,fill=1,stroke=0)
def picture(path,x,y,w,h):
    pdf.drawImage(str(path),x,H-y-h,width=w,height=h,preserveAspectRatio=True,anchor='c')
def background(title,subtitle):
    box(0,0,W,H,'#F3F6FA',r=0)
    t(title,40,27,28);t(subtitle,42,75,17,MUTED)

background('RDA5807M + TS5A22364：FM / MP3 / 小智共用一个喇叭',
           'ESP32-P4 Function EV Board v1.4  |  单声道  |  40 × 36 mm 双层小板  |  A6 首板方案')
box(40,118,1220,52,'#E6F5F0')
t('直接焊接 RDA5807M 芯片，晶振、电源、天线接口都在小板上；继续使用开发板原功放。',60,132,19,GREEN)
box(40,195,310,355,'#FFFFFF','#CBD7E4');t('ESP32-P4 开发板',62,215,22)
t('拆掉 R83、R84，共引出 4 根线',58,255,15,ORANGE)
box(65,285,260,76,'#E8F0FE');t('ES8311：MP3 / 小智',80,307,20)
box(65,435,260,80,'#E6F5F0');t('NS4150B → 原喇叭 J16',80,460,18)
box(415,195,845,355,'#EAF6F1','#99C8B8');t('小板内部：芯片直接集成',435,214,22,GREEN)
box(445,289,240,140,'#FFFFFF','#99C8B8')
t('RDA5807M',481,303,22);t('32.768 kHz 晶振',461,342,17,MUTED);t('3.0 V 电源 / 天线接口',461,376,16,MUTED)
box(729,304,145,105,'#FFFFFF','#99C8B8');t('隔直 + 合声',745,320,19)
t('L / R 各串 10kΩ',740,355,15,MUTED);t('合成单声道',755,378,15,MUTED)
box(923,280,308,165,'#FFFFFF','#99C8B8');t('TS5A22364',955,297,23,GREEN)
t('NC：ES8311 两根音频线',940,335,16);t('NO：FM 单声道 + 参考端',940,367,16);t('COM：接原功放输入',940,400,16)
line([(685,351),(729,351)],GREEN,3,True);t('L / R',686,326,13,GREEN)
line([(874,351),(923,351)],GREEN,3,True)
line([(325,321),(377,321),(377,263),(898,263),(898,290),(923,290)],BLUE,3,True)
t('CP / CN：来自 ES8311 侧，分别经 R5 / R6 串 1kΩ',486,242,14,BLUE)
line([(1077,445),(1077,486),(325,486)],GREEN,3,True)
t('AP / AN：返回 R83、R84 的功放侧',561,461,16,GREEN)
t('GPIO6 = 0：MP3 / 小智（上电默认）    GPIO6 = 1：FM',441,517,17,GREEN)
box(40,575,620,305,'#FFFFFF','#CBD7E4');t('4 根音频线：接已拆除电阻的四个焊盘',60,593,19)
rows=[('小板端','开发板焊盘','断电测导通辨认'),
      ('J1.1 / CP','R83 的 ES8311 侧','通 C39 的 R83 侧焊盘'),
      ('J1.2 / CN','R84 的 ES8311 侧','通 C40 的 R84 侧焊盘'),
      ('J2.1 / AP','R83 的功放侧','通 C59 的 R83 侧焊盘'),
      ('J2.2 / AN','R84 的功放侧','通 C58 的 R84 侧焊盘')]
for i,row in enumerate(rows):
    yy=632+i*43
    if i==0:box(52,yy-4,596,36,'#EAF0F7',r=3)
    for s,x in zip(row,[67,208,408]):t(s,x,yy,14,INK if i else MUTED)
t('两个电阻都要拆；每个电阻的两端各接一根线。',60,850,15,ORANGE)
box(680,575,580,305,'#FFFFFF','#CBD7E4');t('5 根电源 / 控制线：接开发板 J1 排针',701,593,19)
rows=[('小板 J4','开发板 J1','功能'),('1 / 3V3','第 17 脚','3.3 V 电源'),
      ('2 / GND','第 20 脚','共地'),('3 / SEL','第 15 脚','GPIO6：选择音源'),
      ('4 / SDA','第 21 脚','GPIO2：FM 数据'),('5 / SCL','第 19 脚','GPIO3：FM 时钟')]
for i,row in enumerate(rows):
    yy=632+i*37
    if i==0:box(692,yy-4,556,32,'#EAF0F7',r=3)
    for s,x in zip(row,[703,869,1000]):t(s,x,yy,15,INK if i else MUTED)
t('小板 J1 与开发板 J1 是不同接口；请按完整名称接线。',701,857,13,MUTED)
box(40,901,1220,129,'#E8EDF5');t('安装与切换要点',60,917,18)
t('• 保留 C39、C40、C58、C59、R146、R147；扬声器仍接开发板 J16。',60,947,16)
t('• 先静音 / 关闭功放 → 切换 GPIO6 → 等待稳定 → 恢复功放；原 PA 控制为 GPIO53。',60,976,16)
t('• J3.1 接 FM 天线，J3.2 为地。小板只接 3.3 V；本次交付未修改固件。',60,1004,16)
t('A6 / 2026-09-24  ·  电气连接示意，非实物比例  ·  1 / 2',43,1040,13,MUTED)
pdf.showPage()
background('小板布局与开发板焊点位置','图左为本工程 PCB 的实际 3D 渲染；图右为官方 v1.4 开发板音频区丝印。')
box(40,118,790,710,'#FFFFFF','#CBD7E4');picture(OUT/'board-3d.png',50,130,770,650)
t('40 × 36 mm · 两层 · 2 个 Ø2.2 mm 安装孔 · 排针间距 2.54 mm',65,800,15,MUTED)
box(850,118,410,710,'#FFFFFF','#CBD7E4');t('开发板 R83 / R84 在这里',869,137,20)
picture(ROOT/'build/rda5807-datasheet-review/p4-audio-placement.png',863,180,384,238)
box(870,445,370,83,'#FFF1D6');t('C40 旁边：R84',886,458,19,ORANGE);t('C39 旁边：R83',886,489,19,ORANGE)
t('图上 U3 是开发板 ES8311。',871,552,17);t('不要与小板 U3 稳压器混淆。',871,582,17)
t('断电拆下两个 0Ω 电阻后，',871,627,17);t('按第一页导通关系确认四个焊点。',871,657,17)
t('不要把 FM 接到 J16 的 SPK±。',871,702,17,ORANGE);t('四根音频线尽量短，并做应力固定。',871,747,16,MUTED)
box(40,850,1220,179,'#E8EDF5');t('装配与首板验证',60,867,20)
t('1. U1 为 VSSOP-10、U2 为 MSOP-10，均为 0.5 mm 间距外露引脚；按圆点 / 三角标识确认 1 脚。',60,904,17)
t('2. Y1 为 32.768 kHz 手表晶振，按 1.9 mm 孔距成形引脚；不要按参考截图的 33.768 kHz 误写选型。',60,939,17)
t('3. 先测 FM_VDD 约 3.0 V，再测 I²C、搜台和音频。晶振起振、接收效果与切换爆音需首板实测。',60,974,17)
t('A6 / 2026-09-24  ·  ERC / DRC / 原理图与 PCB 一致性检查通过，尚未实物验证  ·  2 / 2',43,1040,13,MUTED)
pdf.showPage();pdf.save()
doc=pymupdf.open(PDF/'wiring.pdf')
for i,page in enumerate(doc):
    page.get_pixmap(matrix=pymupdf.Matrix(1.3,1.3)).save(OUT/('wiring.png' if i==0 else 'assembly.png'))
assert len(doc)==2
print(PDF/'wiring.pdf')
