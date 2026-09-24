"""Electrical wiring draft; deliberately does not invent the FM module footprint."""
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import HexColor
import pymupdf

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'display_board/hardware/fm-mono-switch'
OUT.mkdir(parents=True,exist_ok=True)
pdfmetrics.registerFont(TTFont('CN','C:/Windows/Fonts/msyh.ttc',subfontIndex=0))
W,H=1300,1070
pdf=canvas.Canvas(str(OUT/'wiring-draft.pdf'),pagesize=(W,H))
pdf.setTitle('SGM3718 单声道切换小板 - 成品 FM 模块安装方案电气草图')
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

box(0,0,W,H,'#F3F6FA',r=0)
t('FM 模块 + SGM3718：一块小板，共用原来的喇叭',40,27,28)
t('适用 ESP32-P4 Function EV Board v1.4  |  单声道  |  电气接线草图',42,75,17,MUTED)
box(40,118,1220,52,'#FFF1D6')
t('现有成品 FM 模块焊到小板上；模块型号、尺寸和焊盘顺序待确认，暂不能定稿 PCB。',60,132,19,ORANGE)

box(40,195,310,355,'#FFFFFF','#CBD7E4')
t('ESP32-P4 开发板',62,215,22)
box(65,285,260,76,'#E8F0FE')
t('ES8311：MP3 / 小智',80,307,20)
box(65,435,260,80,'#E6F5F0')
t('NS4150B → 原喇叭 J16',80,460,18)
t('拆掉 R83、R84，共引出 4 根线',58,255,15,ORANGE)

box(415,195,845,355,'#EAF6F1','#99C8B8')
t('新小板：包含成品 FM 模块安装区',435,214,22,GREEN)
box(445,289,240,140,'#FFFFFF','#99C8B8')
t('你的成品 FM 模块',461,307,20)
t('自带晶振 / 接收电路',461,346,16,MUTED)
t('保留模块天线接口',461,378,16,MUTED)
box(729,304,145,105,'#FFFFFF','#99C8B8')
t('隔直 + 合声',745,320,19)
t('L、R 各串电阻',742,355,15,MUTED)
t('再合成单声道',742,378,15,MUTED)
box(923,280,308,165,'#FFFFFF','#99C8B8')
t('SGM3718',985,297,23,GREEN)
t('NC：ES8311 两根音频线',940,335,16)
t('NO：FM 单声道 + 参考地',940,367,16)
t('COM：接原功放输入',940,400,16)
line([(685,351),(729,351)],GREEN,3,True)
t('L / R',685,326,13,GREEN)
line([(874,351),(923,351)],GREEN,3,True)
line([(325,321),(377,321),(377,263),(898,263),(898,290),(923,290)],BLUE,3,True)
t('CP / CN：来自 R83、R84 的 ES8311 侧',486,242,14,BLUE)
line([(1077,445),(1077,486),(325,486)],GREEN,3,True)
t('AP / AN：返回 R83、R84 的功放侧',561,461,16,GREEN)
t('GPIO6 = 0：MP3 / 小智（上电默认）    GPIO6 = 1：FM',441,517,17,GREEN)

box(40,575,620,305,'#FFFFFF','#CBD7E4')
t('4 根音频线：先拆 R83 / R84，再接四个焊盘',60,593,19)
rows=[('小板端','开发板焊盘','用万用表核对通向'),
      ('CP','R83 的 ES8311 侧','C39 的 R83 侧焊盘'),
      ('CN','R84 的 ES8311 侧','C40 的 R84 侧焊盘'),
      ('AP','R83 的功放侧','C59 的 R83 侧焊盘'),
      ('AN','R84 的功放侧','C58 的 R84 侧焊盘')]
for i,row in enumerate(rows):
    yy=632+i*43
    if i==0:box(52,yy-4,596,36,'#EAF0F7',r=3)
    for text,x in zip(row,[67,160,389]):t(text,x,yy,15,INK if i else MUTED)
t('按电路连接辨认焊盘，不按照片中的左右方向猜测。',60,850,15,ORANGE)

box(680,575,580,305,'#FFFFFF','#CBD7E4')
t('5 根供电 / 控制线：接 P4 的 J1 排针',701,593,19)
rows=[('小板信号','开发板 J1','用途'),('3V3','第 17 脚','模块 + SGM3718 供电'),
      ('GND','第 20 脚','共地'),('SEL / GPIO6','第 15 脚','选择 FM 或 ES8311'),
      ('SDA / GPIO2','第 21 脚','转接至 FM 模块 SDIO'),('SCL / GPIO3','第 19 脚','转接至 FM 模块 SCLK')]
for i,row in enumerate(rows):
    yy=632+i*37
    if i==0:box(692,yy-4,556,32,'#EAF0F7',r=3)
    for text,x in zip(row,[703,869,1000]):t(text,x,yy,14,INK if i else MUTED)
t('针脚依据 v1.4；模块实际 VCC / SDA / SCL 焊盘待核对。',701,857,13,MUTED)

box(40,901,1220,129,'#E8EDF5')
t('安装与切换要点',60,917,18)
t('• C39、C40、C58、C59 保留；FM 音频不接扬声器输出 J16 的 SPK+ / SPK−。',60,947,16)
t('• 模式切换先关闭 / 静音功放，再改变 GPIO6，稳定后恢复功放；原 PA 控制为 GPIO53。',60,976,16)
t('• 图中模块安装区为功能示意，不代表真实尺寸。PCB 封装和板框需收到模块资料后完成。',60,1004,14,ORANGE)
t('设计草稿 A1 / 2026-09-24  ·  未完成模块适配、PCB 布线及实物验证',43,1040,13,MUTED)
pdf.showPage();pdf.save()
doc=pymupdf.open(OUT/'wiring-draft.pdf')
doc[0].get_pixmap(matrix=pymupdf.Matrix(1.6,1.6)).save(OUT/'wiring-draft.png')
print(OUT/'wiring-draft.pdf')
