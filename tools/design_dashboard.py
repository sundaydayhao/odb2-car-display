"""Editable Inkscape design boards and the small vector assets used by LVGL.

Run with a Python containing Pillow and --inkscape pointing to inkscape.exe.
SVG is the source; Inkscape does the raster export used in the firmware.
"""
import argparse
import html
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT / "docs/design/cabin"
MAIN = ROOT / "display_board/main"
ICONS = {
    "drive": '<path d="M3 17a10 10 0 1 1 18 0M6 16h2m-1-8 2 2m3-5v3m5 0-2 2m-3 5 5-3"/><circle cx="12" cy="16" r="2"/>',
    "health": '<rect x="3" y="4" width="18" height="17" rx="4"/><path d="M6 13h3l2-5 3 10 2-5h2"/>',
    "tires": '<rect x="5" y="2" width="14" height="20" rx="6"/><ellipse cx="12" cy="12" rx="3" ry="7"/><path d="m5 6 3 2m-3 4 3 2m-3 4 3 2m11-14-3 2m3 4-3 2m3 4-3 2"/>',
    "trip": '<circle cx="5" cy="5" r="2"/><circle cx="19" cy="19" r="2"/><path d="M7 5h9a4 4 0 0 1 0 8H8a3 3 0 0 0 0 6h9"/>',
    "power": '<path d="m13 2-8 12h6l-1 8 9-13h-6z"/>',
    "music": '<path d="M9 18V5l12-3v13M9 9l12-3"/><ellipse cx="6" cy="18" rx="3" ry="3"/><ellipse cx="18" cy="15" rx="3" ry="3"/>',
    "map": '<path d="m2 6 6-3 8 3 6-3v15l-6 3-8-3-6 3zM8 3v15m8-12v15"/>',
    "settings": '<path d="m9 3 1-2h4l1 2 2 1 3 0 2 3-1 3v3l1 3-2 3h-3l-2 1-1 3h-4l-1-3-2-1H4l-2-3 1-3v-3L2 7l2-3h3z"/><circle cx="12" cy="12" r="4"/>',
    "volume": '<path d="M3 9h4l5-5v16l-5-5H3zM16 8a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14"/>',
    "voice": '<rect x="9" y="2" width="6" height="13" rx="3"/><path d="M5 11v1a7 7 0 0 0 14 0v-1m-7 8v3m-4 0h8"/>',
    "wifi": '<path d="M2 8a16 16 0 0 1 20 0M5 12a11 11 0 0 1 14 0m-11 4a6 6 0 0 1 8 0"/><circle cx="12" cy="20" r=".8"/>',
    "bluetooth": '<path d="m7 5 11 12-6 5V2l6 5L7 19"/>',
    "sd": '<path d="M5 2h11l4 4v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V3zM8 3v5m4-5v5m4-3v3"/>',
    "update": '<path d="M12 3v12m-4-4 4 4 4-4M4 16v5h16v-5"/>',
    "radio": '<rect x="2" y="8" width="20" height="13" rx="3"/><path d="m5 8 13-6M6 12h12M6 16h5m-5 2h5"/><circle cx="17" cy="17" r="2"/>',
}

def svg(body, w, h):
    return f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{body}</svg>'

def rect(x,y,w,h,fill,r=0,stroke="none"):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}"/>'

def text(x,y,value,size=18,fill="#e9edf2",weight=400,anchor="start"):
    return f'<text x="{x}" y="{y}" fill="{fill}" font-family="Microsoft YaHei, sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}">{html.escape(value)}</text>'

def icon(name,x,y,color,size=26):
    return f'<g transform="translate({x},{y}) scale({size/24})" fill="none" stroke="{color}" stroke-width="1.65" stroke-linejoin="round" stroke-linecap="round">{ICONS[name]}</g>'

def palette(day):
    return dict(bg="#F2F4F6",panel="#FFFFFF",ink="#202B35",muted="#576575",line="#DEE4E8",accent="#176B5B",tint="#E1EFEB",track="#E6EBEF") if day else dict(bg="#0D1117",panel="#181F28",ink="#EDF2F7",muted="#9AA8B8",line="#2A3441",accent="#73DDC4",tint="#213A38",track="#2B3542")

def frame(title,selected,day):
    p=palette(day)
    out=rect(0,0,1024,600,p['bg'],24)+text(30,43,title,24,p['ink'],600)
    out+=text(355,41,"设计演示",14,p['muted'])
    for i,n in enumerate(["music","sd","wifi","bluetooth","volume","settings"]):
        out+=icon(n,766+i*38,23,p['accent'] if n=="music" else p['muted'],23)
    out+=rect(28,66,968,1,p['line'])+rect(0,525,1024,75,p['panel'])
    for i,(name,label) in enumerate(zip(list(ICONS)[:7],["驾驶","车况","胎压","行程","性能","影音中心","地图轨迹"])):
        cx=82+i*144
        if i==selected: out+=rect(cx-62,532,124,61,p['tint'],16)
        c=p['accent'] if i==selected else p['muted']
        out+=icon(name,cx-13,539,c,24)+text(cx,584,label,16,c,500,"middle")
    return out

def drive(day):
    p=palette(day); o=frame("驾驶概览",0,day)
    o+=rect(28,87,476,304,p['panel'],24)+text(56,120,"当前车速",18,p['muted'])+text(458,120,"km/h",16,p['muted'],400,"end")
    o+=text(58,282,"86",150,p['ink'],500)+rect(424,184,52,50,p['tint'],14)+text(450,217,"D",26,p['accent'],500,"middle")
    o+=text(56,337,"发动机",16,p['muted'])+text(473,337,"2,450 rpm",18,p['ink'],400,"end")
    for i in range(32): o+=rect(56+i*13,357,8,6,p['accent'] if i<10 else p['track'],3)
    o+=rect(528,87,468,304,p['panel'],24)+text(558,120,"预估续航",18,p['muted'])+text(820,120,"燃油余量",18,p['muted'])
    o+=text(556,202,"438",60,p['ink'])+text(692,202,"km",18,p['muted'])+text(820,202,"68",60,p['ink'])+text(913,202,"%",18,p['muted'])
    o+=rect(558,241,407,6,p['track'],3)+rect(558,241,277,6,p['accent'],3)
    o+=text(558,286,"即时油耗",18,p['muted'])+text(558,355,"6.8",48,p['ink'])+text(690,355,"L/100km",18,p['muted'])
    for i,(name,num,unit) in enumerate([("水温","89","°C"),("机油温度","98","°C"),("系统电压","14.2","V"),("平均油耗","7.6","L/100km")]):
        x=28+i*246; o+=rect(x,405,230,80,p['panel'],16)+text(x+18,431,name,16,p['muted'])+text(x+18,472,num,32,p['ink'])+text(x+145,471,unit,14,p['muted'])
    o+=text(31,510,"DHX30 温湿度     温度 -- °C     湿度 -- %",15,p['muted'])
    return o

def music(day):
    p=palette(day); o=frame("影音中心",5,day)
    o+=rect(24,79,112,42,p['tint'],14)+text(80,107,"音乐",18,p['accent'],600,"middle")+text(208,107,"FM 电台",18,p['muted'],400,"middle")
    o+=rect(24,141,562,354,p['panel'],24)+text(50,174,"本地音乐",16,p['muted'])
    o+='<image href="album.svg" x="50" y="195" width="144" height="144"/>'
    o+=text(218,227,"海阔天空 - Beyond",22,p['ink'],600)+text(218,266,"正在播放音乐",17,p['accent'])+text(218,320,"01:11  ·  SD 卡",16,p['muted'])
    for x in [171,335]: o+=text(x,422,"◀" if x==171 else "▶",26,p['ink'],500,"middle")
    o+=rect(222,386,64,64,p['accent'],32)+rect(244,407,7,23,p['bg'],2)+rect(258,407,7,23,p['bg'],2)+rect(456,410,15,15,p['muted'],3)
    o+=rect(604,141,396,354,p['panel'],24)+text(625,177,"播放列表",18,p['ink'],500)+text(978,177,"12 首",16,p['muted'],400,"end")
    songs=["海阔天空 - Beyond.mp3","喜欢你 - Beyond.mp3","光辉岁月 - Beyond.mp3","真的爱你 - Beyond.mp3","不再犹豫 - Beyond.mp3"]
    for i,s in enumerate(songs):
        y=194+i*48
        if i==0: o+=rect(616,y,372,44,p['tint'],12)+rect(616,y+12,3,20,p['accent'],2)
        o+=text(630,y+29,f"{i+1:02}",14,p['accent'] if i==0 else p['muted'])+text(669,y+29,s,17,p['ink'])
    o+=text(632,477,"上一页",16,p['muted'])+text(803,477,"1 / 3",16,p['muted'],400,"middle")+text(964,477,"下一页",16,p['accent'],400,"end")
    return o

def fm(day):
    p=palette(day); o=frame("影音中心",5,day)
    o+=text(80,107,"音乐",18,p['muted'],400,"middle")+rect(148,79,112,42,p['tint'],14)+text(204,107,"FM 电台",18,p['accent'],600,"middle")
    o+=rect(24,141,624,354,p['panel'],24)+icon("radio",50,162,p['accent'],28)+text(90,185,"待调谐频率",18,p['muted'])
    o+=rect(464,159,158,34,p['tint'],17)+text(543,182,"接收器未接入",18,p['muted'],400,"middle")
    o+=text(50,282,"98.5",72,p['ink'],500)+text(400,274,"MHz",22,p['muted'])
    for i in range(42):
        h=24 if i%5==0 else 12
        o+=rect(64+i*544/41,332-h,2,h,p['track'],1)
    o+=rect(64,348,544,5,p['track'],3)+rect(64,348,292,5,p['accent'],3)+f'<circle cx="356" cy="350" r="9" fill="{p["accent"]}"/>'
    o+=text(50,382,"87.5",14,p['muted'])+text(336,386,"FM · 0.1 MHz",18,p['muted'],400,"middle")+text(622,382,"108.0",14,p['muted'],400,"end")
    for x,w,t,disabled in [(50,118,"− 0.1",False),(182,164,"开始收听",True),(360,118,"+ 0.1",False),(492,130,"自动搜台",True)]:
        o+=f'<g opacity="{0.4 if disabled else 1}">'+rect(x,408,w,48,p['tint'] if t=="开始收听" else p['bg'],14)+text(x+w/2,440,t,18,p['accent'] if t=="开始收听" else p['muted'],400,"middle")+'</g>'
    o+=text(50,488,"接入接收器后可搜台和收听",18,p['muted'])
    o+=rect(668,141,332,354,p['panel'],24)+text(688,183,"常用频率",18,p['ink'])+text(980,183,"2 / 6",18,p['muted'],400,"end")
    for i,f in enumerate(["87.5","98.5","","","",""]):
        x=686+(i%2)*154; y=205+(i//2)*74; selected=f=="98.5"
        o+=rect(x,y,142,62,p['tint'] if selected else p['bg'],14)+text(x+12,y+21,f"{i+1:02}",14,p['muted'])
        o+=text(x+71,y+51,f or "收藏当前",22 if f else 18,p['accent'] if selected else p['ink'],400,"middle")
    o+=text(688,455,"点空位收藏 · 长按移除",18,p['muted'])+text(688,483,"收藏已保存",18,p['muted'])
    return o

def sound(day):
    p=palette(day); o=rect(0,0,1024,600,p['bg'],24)+rect(0,0,272,600,p['panel'])+text(36,57,"设置",24,p['ink'],600)+text(36,93,"设备与偏好",16,p['muted'])
    for i,(n,t) in enumerate([("wifi","网络与连接"),("bluetooth","蓝牙设置"),("settings","显示与外观"),("volume","声音"),("voice","语音助手"),("sd","记录与同步"),("update","系统更新")]):
        y=120+i*60; c=p['accent'] if i==3 else p['muted']
        if i==3: o+=rect(20,y,232,52,p['tint'],14)
        o+=icon(n,39,y+14,c,24)+text(78,y+33,t,18,p['ink'] if i!=3 else c)
    o+=text(336,57,"声音",26,p['ink'],600)+text(956,57,"×",32,p['muted'],400,"middle")+rect(336,112,644,220,p['panel'],24)
    o+=text(366,158,"全局音量",22,p['ink'])+text(948,158,"75%",26,p['accent'],500,"end")+rect(368,224,580,8,p['track'],4)+rect(368,224,435,8,p['accent'],4)+f'<circle cx="803" cy="228" r="13" fill="{p["accent"]}"/>'
    o+=text(366,296,"音乐与语音助手共用；调到 0 即静音",18,p['muted'])+text(336,385,"正在播放：海阔天空 - Beyond",18,p['ink'])+text(336,441,"音量自动保存，重启后恢复",18,p['muted'])
    return o

def generate_sources():
    DESIGN.mkdir(parents=True,exist_ok=True); (DESIGN/"icons").mkdir(exist_ok=True)
    for name,body in ICONS.items():
        (DESIGN/"icons"/f"{name}.svg").write_text(svg(f'<g fill="none" stroke="white" stroke-width="1.65" stroke-linejoin="round" stroke-linecap="round">{body}</g>',24,24),encoding="utf-8")
    album='''<defs><linearGradient id="cover" x2="1" y2="1"><stop stop-color="#164D51"/><stop offset="1" stop-color="#101B2B"/></linearGradient><linearGradient id="wave"><stop stop-color="#93E2CB"/><stop offset="1" stop-color="#5B8DA6"/></linearGradient></defs><rect width="144" height="144" rx="20" fill="url(#cover)"/><circle cx="117" cy="21" r="58" fill="#296366" opacity=".38"/><circle cx="121" cy="21" r="41" fill="none" stroke="#5D9D99" stroke-opacity=".18"/><path d="M-10 107Q33 20 67 73T159 35M-10 119Q33 32 67 85T159 47M-10 131Q33 44 67 97T159 59" fill="none" stroke="url(#wave)" stroke-width="3"/><path d="M18 27h23m-23 6h14" stroke="#C1E6E0" stroke-width="2" stroke-linecap="round"/>'''
    (DESIGN/"album.svg").write_text(svg(album,144,144),encoding="utf-8")
    car='''<g fill="none" stroke="white" stroke-width="2" stroke-linejoin="round"><path d="M53 9Q80-3 107 9L119 28Q127 65 127 105V237Q126 272 108 281H52Q34 272 33 237V105Q33 65 41 28Z"/><path d="M45 64Q80 50 115 64L110 106H50Z"/><path d="M49 121Q80 112 111 121V213Q80 225 49 213Z"/><path d="M49 225Q80 236 111 225L116 251Q80 263 44 251Z"/><path d="M39 113v112m82-112v112M48 24l18-3m28 0 18 3M43 264h24m26 0h24M78 128v78M41 95l-16 8v13l12-3m86-18 12 8v13l-12-3"/></g>'''
    (DESIGN/"car.svg").write_text(svg(car,160,290),encoding="utf-8")
    boards=[]
    for row,(name,fn) in enumerate([("drive",drive),("music",music),("sound",sound),("fm",fm)]):
        for col,day in enumerate([False,True]):
            ident=("light" if day else "dark")+"-"+name
            boards.append(f'<g id="{ident}" inkscape:groupmode="layer" inkscape:label="{ident}" transform="translate({36+col*1060},{112+row*674})">{fn(day)}</g>')
    head=rect(0,0,2168,2834,"#E5E9ED")+text(36,52,"CABIN / P4",26,"#253642",600)+text(36,84,"1024 × 600 · 深浅双主题 · 矢量组件 / 设计演示",18,"#596C79")
    (DESIGN/"cabin-ui.svg").write_text(svg(head+''.join(boards),2168,2834),encoding="utf-8")

def export(inkscape):
    out=ROOT/"build/cabin-assets"; out.mkdir(parents=True,exist_ok=True)
    jobs=[(n,DESIGN/"icons"/f"{n}.svg",28,28,True) for n in ICONS]
    jobs += [("car",DESIGN/"car.svg",160,290,True),("album",DESIGN/"album.svg",144,144,False)]
    arrays=[]; descriptors=[]
    for name,src,w,h,mask in jobs:
        png=out/f"{name}.png"
        subprocess.run([str(inkscape),str(src),"--export-type=png",f"--export-filename={png}",f"--export-width={w}",f"--export-height={h}"],check=True,capture_output=True)
        image=Image.open(png).convert("RGBA")
        raw=image.getchannel("A").tobytes() if mask else image.tobytes("raw","BGRA")
        arrays.append(f'static const uint8_t pixels_{name}[] = {{\n'+''.join('    '+','.join(f'0x{v:02x}' for v in raw[i:i+24])+',\n' for i in range(0,len(raw),24))+'};\n')
        cf="A8" if mask else "ARGB8888"
        descriptors.append(f'    {{.header={{.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_{cf},.w={w},.h={h},.stride={w if mask else w*4}}},.data_size=sizeof(pixels_{name}),.data=pixels_{name}}}')
    (MAIN/"ui_assets.c").write_text('/* Generated by tools/design_dashboard.py through Inkscape. */\n#include "ui_assets.h"\n'+''.join(arrays)+'\nconst lv_image_dsc_t ui_assets[UI_ASSET_COUNT]={\n'+',\n'.join(descriptors)+'\n};\n',encoding="utf-8")
    for page in ["dark-drive","light-drive","dark-music","light-music","dark-sound","light-sound","dark-fm","light-fm"]:
        subprocess.run([str(inkscape),str(DESIGN/"cabin-ui.svg"),f"--export-id={page}","--export-id-only",f"--export-filename={DESIGN/page}.png"],check=True,capture_output=True)
    subprocess.run([str(inkscape),str(DESIGN/"cabin-ui.svg"),"--export-width=1600",f"--export-filename={DESIGN/'cabin-overview.png'}"],check=True,capture_output=True)
    print(f"Inkscape exported {len(jobs)} firmware assets and nine design previews to {DESIGN}")

if __name__=="__main__":
    parser=argparse.ArgumentParser(); parser.add_argument("--inkscape",type=Path,required=True)
    parser.add_argument("--render-only",action="store_true",help="Preserve edits made in Inkscape; only export SVG sources")
    args=parser.parse_args()
    if not args.render_only: generate_sources()
    export(args.inkscape)
