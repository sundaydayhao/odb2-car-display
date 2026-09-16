# A7 参数化外壳与音频样件

外形 **178×126×37.2 mm**，采用前框承屏、承载壳承 PCB/底座、可拆后盖。USB-OTG / J18 已取消，右側仅保留 microSD 开口，左侧保留 5 V 输入与 USB Serial/JTAG 调试，上侧保留大拨开关和凹入 RST/BOOT 维修孔。所有底座载荷经壳体筋传递，PCB 不作为承力梁。

## 文件

`case-parameters.json` 与 Python 脚本可修改重跑；`models/actual-assemblies.zip` 包含实际 KiCad 主板、器件、外接咪头载板、壳体总装 STEP 与爆炸 STEP。七个可打印件各有独立 STEP/STL：`front_frame`、`pcb_carrier`、`rear_service_cover`、`power_slide_cap`、`microphone_retainer`、`speaker_baffle_duct`、`speaker_volume_insert`。其中导声压框和容积垫块替代旧两条喇叭压条。

`front/rear/rear_open/exploded.png` 是实际 CAD 视图；`speaker-section.png` 为生成声腔的真实剖切；`assembly-section.png` 说明屏/PCB/FPC 相对关系；`lcd-real-section.png` 证明屏插座 F0 的真实入口方向。音频装配及原厂 PDF 证据见 [SPEAKER_ASSEMBLY.md](SPEAKER_ASSEMBLY.md)，麦克风独立工程见 [microphone-carrier/README.md](microphone-carrier/README.md)。

## 已完成的几何检查

七件均为单一有效实体，主壳与导声组件没有硬干涉；实际无USB主板545个实体、14个关键器件、8组代表插头/操作包络及4个OD8工具柱均已检查，报告 `actual-component-check.json` 没有硬干涉项。麦克风实际载板也已加入总装。插头胶壳仍是代表包络，不等于已选供应商公差验收。主板最终电气验收独立进行。

原生 PCB 导出期间前后 SHA-256 一致，见 `models/actual-export-provenance.json`；它对应无USB布局快照，未冒充后续最终铜线哈希。`mechanical-fingerprint.json` 记录所有器件位置、焊盘、模型内容/变换及板边，不含铜线/过孔/丝印；最终铜改变后用该指纹核对机械几何是否仍相同。

## 屏幕和装配

完整 PCB 约 120×99 mm（核心区 120×74 mm，电源翼向下延伸25 mm）、四孔(−26,−79.5)/(84,−79.5)/(−26,−15.5)/(84,−15.5)，孔心距边5 mm。支撑/头最大OD6，维护工具OD8。F面朝可拆后盖；J1/J2均在下部，LCD J302 F(29,−17)/0、触摸J308 F(69,−19)/0，入口朝板底+Y。屏尾绕PCB下边后从开放后盖锁入，不能跨J1或硬折排线。

屏LCM名义164.86×99.96×2.8 mm（裸 LCM，±0.3 mm；规格书第5页）；PCB相对屏上移24 mm。假定 P+G 总厚6 mm时，天线至屏金属几何距离15.386 mm，该位置屏厚约6.413 mm即到15 mm边界。盖板/胶层/真实金属轮廓未知，整机无线尚未验证。有效区为154.21×85.92 mm；154×86 mm显示窗口仍是可修改占位，须结合黑边重定稿。

LCD按R2 mm与假定根部位置估算路径54.713 mm，加插入3 mm/装配余量4 mm需要约61.713 mm。规格书第5页的56.54 mm只是从屏边到接触区域的尺寸，不能证明有61.713 mm可用尾长，当前候选通道短约5.17 mm，仍标UNCONFIRMED；触摸尾根、长度、接触面同样未确认。实屏到货后先调整参数再打印最终试装件。

RST/BOOT为SKRTLBE010，0.2 mm行程，仅使用顶侧3/2.5 mm绝缘维修孔；不能预压。大拨帽按实际SW3左档、轴高F+3.3 mm设计，名义2.2 mm行程无硬干涉，配合/止挡需校准。J16配套线壳在左侧封闭口袋内弯线，该口袋与USB调试窗口间最薄隔墙约1 mm，需材料和试装强度验证。

## 声学样件

GSPK4020057TN-4R3W / C3012621 为4Ω/3W额定/4W最大裸单元候选，按原厂图总高8 mm保守设计。相对5.7 mm商品参数与温度资料的冲突全部保留，未作为汽车量产料冻结。现NS4150B约2 W低失真，未宣称满足行车噪声。

单元与电子仓隔离在后盖独立声杯内，前声经密封导管从左侧朝驾驶员出声，后盖不设格栅。可拆垫块按名义篮架模型给约5.032 cc净后腔，实际排体积、频响、泡棉和线孔气密仍须实测。麦克风位于左上独立前声管，两者线束分开。

## 重跑

需要KiCad10 CLI、Python/CadQuery2.6.1/VTK9.3；在此目录执行，实际Python环境自行指定：

```powershell
python export_actual_pcb.py --project .. --kicad-cli "C:/Program Files/KiCad/10.0/bin/kicad-cli.exe"
python generate_case.py --windows-fast-exit
python check_actual_case.py
python render_case.py
python render_speaker.py
python make_mechanical_diagram.py
```

Node/sharp用于SVG转PNG，可通过SHARP_MODULE指定包位置。精简交付不包含可重建中间网格和多份大PCB STEP；顺序重跑会创建它们。STEP/STL为右手坐标X=PCB X、Y=−PCB Y、Z朝后，实际PCB没有镜像；参数/二维图保留PCB+Y向下。

当前为可修改试装样件。尚未完成盖板/FPC供应商确认、车内声学、屏幕亮度温升、RF、材料耐热、卡扣寿命、振动/跌落、底座载荷、插头公差和防护试验，不可直接据此开量产模具。
