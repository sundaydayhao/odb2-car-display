# OBD CAN / BLE 采集节点：A3 测试与定位结构

板框 **38.5 × 16.6 mm**，四层、双面贴装。配套固件位于同级板包的 [`../../README.md`](../../README.md)，目标为 ESP32-C3 / ESP-IDF 5.5.x；采集节点通过 BLE 直连副屏，手机仅作为调试采集工具。

**请打开本目录的 `obd_ble_node.kicad_pro` 查看完整采集板。** OBD 16 个焊孔、电源保护、ESP32-C3 蓝牙、CAN 收发器及低功耗控制已整合在同一块主 PCB 内，共 64 个封装（含 9 个测试点、2 个定位孔；C23 初始不贴）。J1 的 16 脚供电、4/5 脚接地、6/14 脚 CAN 均接入真实电路网络。`mechanical-reference/` 中原来的纯孔位试配件仅保留作尺寸对照，不是本产品主板。

当前采用笔直同轴尾脚的机械假设：同排 4.00 mm、两排 8.60 mm，16 个 2.40 × 1.20 mm 镀铜槽孔；槽孔是工程试配值。F.Cu 从插头背面观察，宽边朝上，上排 1–8、下排 9–16。接插件塑料本体假设位于 PCB 下方，元件避开金属焊脚；现有 MINI 插头的尾脚、板厚和壳体高度仍须实物确认。

A3 将 9 个背面测试点集中为间距 2 mm 的一排，新增 +5V 检测点、一圆一槽定位孔及两处顶面支撑区，并修正 OBD 焊盘正反面阻焊开窗。完整针位、插头所需避让窗口及焊接判定见 [制造与夹具接口](manufacturing-interface.md)。**PCB 焊接结构检查通过；现有插头是否匹配、焊后是否挡住探针仍待该型号尺寸与实物试配。**

## 已实现的设计

| 功能 | 器件／连接 |
|---|---|
| 主控 | ESP32-C3FH4X，5 × 5 mm QFN，内置 Flash；19–24 脚明确不连接 |
| 电源 | F1 保险丝 → D1 反接保护 → D2 TVS → LMR51610YDBVR 降压 → TLV75533PDBVR |
| CAN | TCAN1044AVDRBRQ1，5 V 总线侧、3.3 V 逻辑侧，PESD2CANFD24V-T 保护 |
| 硬件禁止发送 | SN74LVC1G32：`CAN_TX OR TX_INHIBIT`；GPIO4 高电平强制 TXD 隐性 |
| 默认休眠状态 | GPIO4 / GPIO5 外部上拉；TX 禁止、CAN 收发器待机 |
| 唤醒 | TCAN1044A RXD → C3 GPIO3，支持深睡眠低电平唤醒 |
| 射频 | 40 MHz 晶体、C3 CLC 匹配、独立天线匹配、AANI-CH-0133-Q-T 板载陶瓷天线 |
| 电压检测 | 1 MΩ / 100 kΩ 分压，检测 `VIN_PROT`，不是反接二极管前的电池端电压 |
| 调试 | 9 个集中背面 pogo 测试点：GND、3V3、5V、EN、BOOT、UART、USB D+/D− |

烧录针床接线、实际触点坐标和单工位生产流程见 [烧录与测试夹具 V1](../obd-programming-fixture/README.md)。其中包含离线制包、显式端口烧录、读回校验和追溯工具；夹具实物尚未制作。

OBD 使用 16 脚供电、4/5 脚接地、6 脚 CAN_H、14 脚 CAN_L。节点不安装 120 Ω 终端电阻。范围为 12 V 乘用车经典 CAN，支持 250/500 kbit/s、11/29 位 ID；不提供 CAN FD 控制器、K-Line、J1850 或 ECU 写入功能。

## A2 陶瓷天线

已删除 J2 天线座。AE1 为 Abracon AANI-CH-0133-Q-T，陶瓷本体 1.6 × 0.8 × 0.4 mm；按原厂推荐的 5 × 3 mm 净空和辐射铜箔图制作本地封装。天线安装在右上角，铜图整体向板内偏移 0.25 mm 以满足铜到板边间距，实际四层净空扩展至 3.25 × 5.25 mm。1 脚为馈电；2 脚连接原厂图中的悬空 L 形铜箔，不能接地。背面和内层均禁止在该区域布线／覆铜／打孔，顶层仅保留天线焊盘、辐射铜箔及指定馈线通道。原指示灯和电阻已移至原天线座腾出的空间。

C20 / L2 / C21 仍是 C3 芯片端匹配。新增 L4（射频侧并联 1.8 nH）、L3（串联 5.6 nH）、C23（天线侧并联调试位，初始 DNP）构成独立天线匹配，数值来自原厂评估板，**不是本板实测匹配值**。所有射频走线均在 F.Cu，无射频信号过孔。`ceramic_rf_routes.json` 保存通过 DRC 的射频走线，重建布局时先锁定这些路径，避免通用布线器将馈线换层。

原厂评估板为 46 × 20 mm，自由空间数据不能作为本板或车内的实测性能。量产前应锁定板厂叠层、校准 50 Ω 馈线，再在实际 OBD 插头及塑料外壳内进行 VNA 调谐和 BLE 丢包／连接距离测试。OBD 金属脚和壳内结构不得侵占天线净空；采购价格、备料周期和替代料需向供应商确认，不能只按相同外形替换。

## 文件与验证

- `obd_ble_node.kicad_pro`：KiCad 10 工程。
- `obd_ble_node.kicad_sch`：64 个带封装元件的原理图，另有电源标志。
- `obd_ble_node.kicad_pcb`：含 J1 在内的 64 个封装及整板线路。
- `Node.pretty/` / `fp-lib-table`：本地 16 针直焊封装和陶瓷天线封装。
- `antenna_footprint.py`：按原厂尺寸生成陶瓷天线焊盘、辐射铜箔及原生净空规则。
- `fixture_footprints.py`：生成定位孔封装、探针避让图示及顶面支撑区规则。
- `Node.3dshapes/`：按尺寸绘制的天线外形模型；不是原厂 STEP。
- `placement.json`：整板双面布局，坐标相对于 38.5 × 16.6 mm 板框左上角。
- `Node.kicad_sym` / `sym-lib-table`：按数据手册建立的本地符号。
- `design.json`：器件、位置、网络及说明；`build/netlist-audit.json` 是导出网表的逐网核对结果。
- `build/a3-front.png`、`build/a3-back.png`、`build/a3-mask.png`：本机实际 KiCad 导出的 A3 检查图（生成文件不纳入 Git）；`ceramic-*` 图属于此前 A2 记录。
- `VERIFICATION.md`：通过项与未完成项分开记录。

本机 3D 库缺少 U3 的 VSON 外形模型，因此背面 3D 预览只显示其焊盘；U3 的原理图、9 个电气焊盘和 CAN 连接均已包含并通过检查。3D 预览不能用来证明现有 OBD 塑料外壳的装配兼容性。

生成脚本依赖 KiCad 10 原生 Python、KiSkill 和官方库：

```powershell
python generate.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
python stage_pcb.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
```

`generate.py` 重建原理图和元数据；`stage_pcb.py` 重建未布线布局，**会覆盖现有 PCB 布线**。检查已交付 PCB 时直接运行 `verify.py`，不要先运行布局重建脚本。所有设计网络都与 KiCad 导出的网表逐引脚比较；几何校验器包含植入交叉短路必须失败的检查。天线净空使用 PCB 内嵌的原生规则区域，另有逐焊盘／逐走线的几何复核；试验副本中向净空植入顶层、底层走线和过孔时，原生 DRC 会拒绝。

`route_pcb.py` 在布局上建立固定的射频、电源走线、主控引出过孔和 In1.Cu 接地平面，导出本地 Specctra DSN；`--import-ses <路径>` 导入本地路由结果并重新覆铜。本次使用本地 Freerouting 2.2.4，再以 `finish_routes.py` 完成局部连接；最终由 KiCad 完整 DRC 验收。已交付 PCB 是最终布线文件，重建布局不等于恢复全部布线。

信号布线使用 F.Cu、In2.Cu、B.Cu；In1.Cu 保留作接地参考。过孔为 0.20 mm 钻孔，信号焊盘 0.40 mm、接地焊盘 0.45 mm；常规信号最小线宽、间距均为 0.127 mm（5 mil），孔到异网铜最小 0.20 mm。订板前需确认板厂工艺。射频走线宽 0.18 mm 是待与板厂叠层校准的起始值，不宣称已实测 50 Ω。

## 量产前必须解决

连接器试配、板厚、可用高度和外壳金属避让；板厂叠层与阻抗、射频匹配及天线效率；晶振起振与频偏；电源热设计、冷启动和车载瞬态测试；待机电流与误唤醒；生产测试和固件升级策略。EPAD 接地过孔的焊锡流失需通过钢网、堵孔或填孔工艺解决并检查焊接质量。

LMR51610 的 65 V 输入能力不等于整板通过 load dump。保险丝、TVS、反接器件和输入电容仍需联合测试。XGL4030 电感已核对原厂推荐焊盘：0.98 × 3.4 mm、中心距 2.37 mm，与使用的 KiCad XAL4030 封装焊盘一致。晶体为标准 10 pF 负载版本，焊盘与 ABM8G 图纸一致；16 pF 负载电容是按约 2 pF 杂散计算的起始值，装配后需测频偏。通用阻容尚未全部锁定采购料号。板载陶瓷天线及其净空需要与实际插头、塑料壳联合验证。四层板最终叠层、50 Ω 线宽、接地过孔工艺和天线匹配仍需实物验证。

## 原厂依据

- [AANI-CH-0133-Q 原厂手册：第 3/4 页尺寸及净空，第 9 页匹配网络](https://abracon.com/datasheets/AANI-CH-0133-Q.pdf)
- [ESP32-C3 数据手册](https://documentation.espressif.com/esp32-c3_datasheet_en.pdf)
- [Espressif C3 硬件检查表](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/schematic-checklist.html)
- [LMR516xx 数据手册](https://www.ti.com/lit/ds/symlink/lmr51606.pdf)
- [TCAN1044A-Q1 数据手册](https://www.ti.com/lit/ds/symlink/tcan1044a-q1.pdf)
- [TLV755P 数据手册](https://www.ti.com/lit/ds/symlink/tlv755p.pdf)
- [SN74LVC1G32 数据手册](https://www.ti.com/lit/ds/symlink/sn74lvc1g32.pdf)
- [PESD2CANFD24V-T 数据手册](https://assets.nexperia.com/documents/data-sheet/PESD2CANFD24V-T.pdf)
- [XGL4030 原厂尺寸与电气规格](https://www.coilcraft.com/getmedia/032d9c73-4222-482f-b6bc-7808590e27c9/xgl4030.pdf)
- [ABM8G 原厂规格与焊盘图](https://abracon.com/Resonators/ABM8G.pdf)

用户拆机观察到现有适配器芯片丝印为 **YM1121**。这是一条实物信息；尚未找到可核验的原厂数据手册或开放 SDK，未据此推断其 MCU 内核，也未将其用作可编程替代芯片。
