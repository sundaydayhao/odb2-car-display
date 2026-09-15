# 7 寸显示主板 A6：MIPI 单板方案

本工程将 ESP32-P4X、ESP32-C6、LCD 电源、背光、I²C 触摸、音频和接口集成在一块 120×74 mm、4 层 PCB 上。打开 `display_singleboard.kicad_pro`，原理图入口为 `display_singleboard.kicad_sch`。

新屏采用用户指定的淘宝商品 983165147604 / SKU 5946336205446，JD9165、1024×600、P+G 电容触摸左出线。主板选择 MIPI DSI 接法；详细引脚、供电与机械确认项见 [PANEL_TARGET.md](PANEL_TARGET.md)。A6 已完成原理图与 PCB 布线，ERC / DRC / 未连接检查均为 0，逐引脚网络一致；见 [VERIFICATION.md](VERIFICATION.md)。所选屏幕模组的电气与机械参数仍需厂家确认后再放行打样。

| 功能 | 当前设计 |
| --- | --- |
| 主控 / 无线 | ESP32-P4NRW32X、ESP32-C6-MINI-1，SDIO 互联 |
| LCD | J302，30 针、0.5 mm，2 数据通道 MIPI DSI，B 面朝屏幕 |
| 触摸 | J308，6 针、0.5 mm，I²C / INT / Shutdown；具体 IC 待确认 |
| LCD 电源 / 背光 | 独立 VDD、AVDD、VGH、VGL、VCOM；ETA1611 恒流背光 |
| 音频 | ES8311、麦克风、NS4150B 功放和扬声器接口 |
| 摄像头 | J9 MIPI CSI，F 面朝背壳竖插 |
| USB | 一个 USB HS A 母座 Host；另保留 USB Serial/JTAG 调试口 |
| 供电 / 开关 | J22 稳压 5 V，大尺寸上侧壁拨杆 SW3 控制 U401 |
| 其他 | microSD、BOOT / RESET、UART 备用下载接触点、C6 下载排针 |

以太网口、PHY 及专用外围、额外 HS 口、备用 FS 口已从本版移除。OBD 采集节点和烧录夹具是不同功能的独立工程，不合并到本显示板目录。

屏幕仍通过自带排线接入主板：P4 → 板内 MIPI 走线 → J302 → LCD 排线。触摸排线单独接 J308。前期验证可以使用官方 ESP32-P4 Function EV Board；旧 `display-mainboard` / `display-lcd-adapter` 保留为电路来源。

## 装配与外壳

F 面朝背壳，B 面朝屏幕。LCD 位于 B 面中下部；从屏幕正面看左出线的触摸，在背面视图位于右侧。摄像头位于 F 面左上区域。左右边和上边提供外部操作口，下侧壳体留给底座。

大拨杆位于上侧板边，侧向拨动，配合背壳上沿开口。USB-C 调试和 5 V 输入在左侧；USB-A、microSD 在右侧。内部 FPC 先插入并锁紧，再固定主板和后盖。排线、锁扣、配套插头、线束弯折以及天线周围的金属净空均须用实际器件和外壳检查。

`enclosure.json` 记录接口操作空间，`FPC_PLACEMENT_PROPOSAL.svg` 说明三个 FPC 的板面与方向。可用 KiCad Python 执行 `tools/export_display_enclosure.py --project hardware/display-singleboard` 导出坐标。平面包络检查不替代三维装配验证，也不构成开模尺寸放行。

## 电气与验证边界

输入为稳压 5 V，本板没有车载 12 V 反接、抛负载和冷启动前端。SW3 控制输入汇流后的供电；从扩展口 VCC_5V 反向供电会绕过开关，整机线束不得这样连接。

LCD 电路现有名义值为 VDD 1.8 V、AVDD 9.6 V、VGH 18 V、VGL −6 V、VCOM 3.2 V，背光约 182 mA。它们来自参考电路，尚未获得所选 MIPI 模组确认；不得将 RGB 参数表的 3.3 V 直接套用。先取得模组额定值及上电时序，测量电源后再连接屏幕。

GPIO26 控制背光并保留默认关闭下拉；GPIO27 经 U304 的开漏输出控制 LCD 复位，上拉到 LCD VDD 域。GPIO22 / GPIO23 接触摸 INT / Shutdown，需按实际触摸 IC 实现时序。原 GT911 假设已经移除。

L2 保留完整地参考，L3 为电源和部分信号。MIPI 目标 100 Ω，USB 目标 90 Ω，需用板厂确认的叠层计算；1.6 mm 名义总板厚不能确定阻抗。完整链路长度、连续参考地、返回过孔和原生 DRC 是相互独立的检查项。

`tools/check_display_hardware.py` 检查 ERC、DRC、未连接、SCH / PCB 网表、装配表、对象身份以及 MIPI / CSI 长度；`tools/check_display_panel.py` 单独核对新屏 40 项针脚对应。检查结果不代表新屏实机点亮、热测试、EMC 或量产放行。

原有项目相对路径 3D 库保留。新开关、连接器及采购几何仍需实物复核；模型显示完整不代表外壳公差和零件型号已经批准。器件与参考设计授权见 `SOURCES.md`，逐针定义见 [INTERFACES.md](INTERFACES.md)。
