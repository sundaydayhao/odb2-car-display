# A6 单板接口

LCD 按指定 JD9165 屏的 MIPI 针表接线，触摸按通用 I²C 针表定义。供电、两通道配置和接触面尚需对应成品模组确认，见 [PANEL_TARGET.md](PANEL_TARGET.md)。

## LCD：J302

30 针、0.5 mm，朝屏幕的 B 面。现有 AFC01-S30FCA-00 几何为候选，最终接触面与 FPC 厚度须确认。

| 针号 | 信号 | 针号 | 信号 |
| --- | --- | --- | --- |
| 1 / 2 | LED+ | 17 | CLK+ |
| 3 | VGH | 18 | CLK− |
| 4 | VGL | 19 | GND |
| 5 | UPDN，LCD VDD 域 | 20 | D1+ |
| 6 | SHLR，LCD VDD 域 | 21 | D1− |
| 7 / 8 | LED− | 22 | GND |
| 9 | AVDD | 23 | D0+ |
| 10 | GND | 24 | D0− |
| 11 / 12 | D3，未连接 | 25 | GND |
| 13 | GND | 26 | STBYB，LCD VDD 域 |
| 14 / 15 | D2，未连接 | 27 | LCD RESET，开漏上拉到 LCD VDD |
| 16 | GND | 28 / 29 | LCD VDD，电路现为 1.8 V |
| — | — | 30 | VCOM |

31 / 32 为接地安装焊盘，不是屏幕排线信号针。板内使用 CLK、D0、D1 三对，成品屏需支持相同的两数据通道配置。

## 触摸：J308

6 针、0.5 mm，B 面。封装来自参考 `FFC6P_0P5_UP`，采购前核对接触面；“B 面”不等于连接器内部下接触。

| 针号 | 信号 | 连接 |
| --- | --- | --- |
| 1 | Shutdown / control | GPIO23；有效电平待确认 |
| 2 | VCC | ESP_3V3；模组额定电压待确认 |
| 3 | GND | GND |
| 4 | INT | GPIO22 |
| 5 | SDA | GPIO7，经 R8 选路 |
| 6 | SCL | GPIO8，经 R9 选路 |

7 / 8 为接地固定焊脚。尚未确认触摸 IC，不能套用 GT911 的地址选择或复位时序。

## 摄像头、USB 与电源

- J9 保留 MIPI CSI，F 面竖插；接线延续参考相机接口，移动后重布 CSI 差分线。
- J18 是唯一 USB HS A 母座，作为 Host 使用，由 U9 控制 VBUS。该外形不提供 Type-C 角色协商。
- J19 是独立 USB Serial/JTAG 调试口；GPIO24 / GPIO25 接 P4 原生调试 USB。UART 备用下载接触点与 BOOT / RESET 保留。
- J22 输入稳压 5 V，经输入汇流与 U401 受控供电。SW3 是上侧壁可操作的大拨杆开关。不要将车载 12 V 直接接本板。
- 移除额外 HS 端口、备用 FS 端口及其专用外围；以太网口、PHY 和晶振已移除。Wi-Fi / BLE、microSD、音频功能保留。

## 屏电源测量点

| 测量点 | 当前电路名义值 |
| --- | --- |
| TP301 | VGL，约 −6 V |
| TP302 | VGH，约 18 V |
| TP303 | VCOM，约 3.2 V |
| TP304 | AVDD，约 9.6 V |
| TP305 | LCD VDD，1.8 V |

这些是现有电路设置，不是已获厂家确认的新屏工作电压。背光 R312=1.1 Ω 对应约 182 mA，同样需要按新屏额定值核定。

## USB HS protection population

D21/D22 are fitted TI TPD1E05U06DPYR (C436349). D21.1=USB_DP_A, D22.1=USB_DM_A; both pin 2=GND. Dedicated TI DPY0002A lands and matching model are used; USB routing is unchanged. See ESD_PROTECTION.md.
