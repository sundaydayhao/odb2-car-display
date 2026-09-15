# 7 寸显示主板 A7：MIPI 单板与外壳试装

ESP32-P4NRW32X、ESP32-C6-MINI-1、LCD 电源/背光、触摸、音频和接口集成在四层板上；加入电池电源翼后板框约 **120 × 99 mm**（原核心区约 120 × 74 mm）。打开 `display_singleboard.kicad_pro`；原理图入口为 `display_singleboard.kicad_sch`。本轮按整机装配重新排列器件与接口，提供 178 × 126 × 37.2 mm 可参数化外壳样件。

目标屏为用户选定的淘宝商品 983165147604 / SKU 5946336205446，JD9165、1024 × 600、P+G 电容触摸左出线，选择两通道 MIPI。屏幕电气参数、触摸 IC、接触面和实物装配仍有待确认项，见 [PANEL_TARGET.md](PANEL_TARGET.md)。**A7 电气验收以最终冻结 PCB 的 [VERIFICATION.md](VERIFICATION.md) 为准；旧版检查通过记录不适用于重布后的板。**

| 功能 | A7 布置 |
| --- | --- |
| LCD / 触摸 | J302 / J308 在 F 面下部，0°，插口朝 PCB 下沿；拆后盖可操作锁扣 |
| 排针 | J1 位于下左，J2 位于下部，保留中央 LCD 绕线通道 |
| C6 | 顶部偏右、天线朝上；PCB 相对屏上移 24 mm 留非金属区域 |
| 摄像头 | J9 长边横向，保留原竖插连接器，沿 +Z 插拔 |
| 复位 / 下载 | 上侧 SKRTLBE010 侧按，通过凹入维修孔操作 |
| 外部接口 | 左侧 5 V / USB Serial-JTAG，右侧 microSD，上侧大拨开关 |
| 音频 | ES8311、外接两线麦克风、NS4150B、外接扬声器；器件固定在壳体 |
| 固定 | PCB 四角孔、前框承屏、承载壳承 PCB/底座、后盖独立拆卸 |
| 电池 | J23 三针 1S LiPo（BAT+ / GND / 10 kΩ NTC），SGM41524 充电，TPS61088 升压至 5 V；电池电源翼位于板框下部 |

以太网、全部 USB-OTG / HS 外接口及备用 FS 口已移除，独立 USB Serial/JTAG 调试口保留。OBD 采集节点和烧录夹具保持独立工程。前期实机验证可以继续使用官方 ESP32-P4 Function EV Board。

## 装配样件

F 面朝背盖，B 面朝屏幕；F 面不是产品显示正面。屏幕尾线从 PCB 下沿绕到可见的 FPC 插座，PCB 下部仍留给底座。外壳采用窄后仓，使 USB / SD 插头能够从侧面接近。麦克风有独立声孔，喇叭有独立约5 cc后腔、密封左向导声压框及可调容积垫块；内部 J16 线束有封闭口袋。

可打印壳件、STEP、爆炸图和复跑脚本见 [enclosure/README.md](enclosure/README.md)，结构与装配顺序见 [MECHANICAL.md](MECHANICAL.md)。实际 PCB 模型、关键接口与代表插头包络已做条件性机械检查；报告有具体输入哈希。屏幕厚度/盖板、实际尾长、扬声器外形与连接器采购匹配仍未完成实物验证，样件不作为开模放行。

## 电气边界

输入为稳压 5 V，本板没有车载 12 V 反接、抛负载和冷启动前端。SW3 控制 U401；从扩展口 VCC_5V 反向供电会绕过开关，整机线束应使用指定输入。

电池与 USB-C 复用、充电电流、续航估算和量产前验证项目见 [BATTERY_POWER.md](BATTERY_POWER.md)。J19 是唯一 USB-C 口，同时保留 USB-UART/JTAG 调试并接收 5 V 充电输入；电池翼的高电流铜线仍需按板厂叠层和实际电池包冻结后完成布线与温升验证。

LCD 现有名义值 VDD 1.8 V、AVDD 9.6 V、VGH 18 V、VGL −6 V、VCOM 3.2 V，背光约 182 mA，均待所选 MIPI 模组确认，不能直接套用 RGB 参数表。GPIO26 背光默认关闭，GPIO27 经开漏输出控制 LCD 复位；触摸 INT / Shutdown 为 GPIO22 / GPIO23，按实际 IC 定时序。

MIPI 目标 100 Ω、USB 目标 90 Ω，应由板厂叠层求解。CAD 网表、原生 ERC/DRC、路径长度、参考地和实机测试是不同检查；名义板厚不能确定阻抗。新屏点亮、热、振动、EMC、射频和长期车载验证尚未完成。

[INTERFACES.md](INTERFACES.md) 给出逐针定义；[A7_SERVICE_AUDIO.md](A7_SERVICE_AUDIO.md) 给出侧按键与音频线束；[3D_MODELS.md](3D_MODELS.md) 记录模型覆盖和精度；来源与许可见 [SOURCES.md](SOURCES.md)、[LIBRARY_LICENSE.md](LIBRARY_LICENSE.md)。


外壳已按无USB-OTG及3W喇叭候选重新生成，维持178×126×37.2 mm，左侧密封导声与约5 cc独立后腔。实际PCB/关键接口无硬干涉，物料温度/尺寸冲突和车辆实测仍未放行；见 `enclosure/README.md`、`enclosure/SPEAKER_ASSEMBLY.md`。
