# OBD CAN / BLE 采集节点：A1 整板工程

板框 **38.5 × 16.6 mm**，四层、双面贴装。固件位于 `../../firmware/obd_ble_node`，目标为 ESP32-C3 / ESP-IDF 5.5.x；采集节点通过 BLE 直连副屏，手机仅作为调试采集工具。

**请打开本目录的 `obd_ble_node.kicad_pro` 查看完整采集板。** OBD 16 个焊孔、电源保护、ESP32-C3 蓝牙、CAN 收发器及低功耗控制已整合在同一块主 PCB 内，共 58 个封装。J1 的 16 脚供电、4/5 脚接地、6/14 脚 CAN 均接入真实电路网络。`mechanical-reference/` 中原来的纯孔位试配件仅保留作尺寸对照，不是本产品主板。

当前采用笔直同轴尾脚的机械假设：同排 4.00 mm、两排 8.60 mm，16 个 2.40 × 1.20 mm 镀铜槽孔；槽孔是工程试配值。F.Cu 从插头背面观察，宽边朝上，上排 1–8、下排 9–16。接插件塑料本体假设位于 PCB 下方，元件避开金属焊脚；现有 MINI 插头的尾脚、板厚和壳体高度仍须实物确认。

## 已实现的设计

| 功能 | 器件／连接 |
|---|---|
| 主控 | ESP32-C3FH4X，5 × 5 mm QFN，内置 Flash；19–24 脚明确不连接 |
| 电源 | F1 保险丝 → D1 反接保护 → D2 TVS → LMR51610YDBVR 降压 → TLV75533PDBVR |
| CAN | TCAN1044AVDRBRQ1，5 V 总线侧、3.3 V 逻辑侧，PESD2CANFD24V-T 保护 |
| 硬件禁止发送 | SN74LVC1G32：`CAN_TX OR TX_INHIBIT`；GPIO4 高电平强制 TXD 隐性 |
| 默认休眠状态 | GPIO4 / GPIO5 外部上拉；TX 禁止、CAN 收发器待机 |
| 唤醒 | TCAN1044A RXD → C3 GPIO3，支持深睡眠低电平唤醒 |
| 射频 | 40 MHz 晶体、CLC 匹配起始值、U.FL 外接 FPC 天线 |
| 电压检测 | 1 MΩ / 100 kΩ 分压，检测 `VIN_PROT`，不是反接二极管前的电池端电压 |
| 调试 | 8 个背面 pogo 测试点：GND、3V3、EN、BOOT、UART、USB D+/D− |

OBD 使用 16 脚供电、4/5 脚接地、6 脚 CAN_H、14 脚 CAN_L。节点不安装 120 Ω 终端电阻。范围为 12 V 乘用车经典 CAN，支持 250/500 kbit/s、11/29 位 ID；不提供 CAN FD 控制器、K-Line、J1850 或 ECU 写入功能。

## 文件与验证

- `obd_ble_node.kicad_pro`：KiCad 10 工程。
- `obd_ble_node.kicad_sch`：58 个符号的原理图。
- `obd_ble_node.kicad_pcb`：含 J1 在内的 58 个封装及整板线路。
- `Node.pretty/` / `fp-lib-table`：本地 16 针直焊封装，逐孔双面焊接避让区。
- `placement.json`：整板双面布局，坐标相对于 38.5 × 16.6 mm 板框左上角。
- `Node.kicad_sym` / `sym-lib-table`：按数据手册建立的本地符号。
- `design.json`：器件、位置、网络及说明；`build/netlist-audit.json` 是导出网表的逐网核对结果。
- `build/schematic.pdf`、`build/pcb-front.png`、`build/pcb-back.png`：实际 KiCad 导出的检查图。
- `VERIFICATION.md`：通过项与未完成项分开记录。

本机 3D 库缺少 U3 的 VSON 外形模型，因此背面 3D 预览只显示其焊盘；U3 的原理图、9 个电气焊盘和 CAN 连接均已包含并通过检查。3D 预览不能用来证明现有 OBD 塑料外壳的装配兼容性。

生成脚本依赖 KiCad 10 原生 Python、KiSkill 和官方库：

```powershell
python generate.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
python stage_pcb.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
```

`generate.py` 和 `stage_pcb.py` 会重建原理图和未布线布局，**会覆盖现有布线**；检查已交付 PCB 时直接运行 `verify.py`，不要先运行重建脚本。所有设计网络都与 KiCad 导出的网表逐引脚比较；几何校验器包含植入交叉短路必须失败的检查。

`route_pcb.py` 在布局上建立固定的射频、电源走线、主控引出过孔和 In1.Cu 接地平面，导出本地 Specctra DSN；`--import-ses <路径>` 导入本地路由结果并重新覆铜。本次使用本地 Freerouting 2.2.4，再以 `finish_routes.py` 完成局部连接；最终由 KiCad 完整 DRC 验收。已交付 PCB 是最终布线文件，重建布局不等于恢复全部布线。

信号布线使用 F.Cu、In2.Cu、B.Cu；In1.Cu 保留作接地参考。过孔为 0.20 mm 钻孔，信号焊盘 0.40 mm、接地焊盘 0.45 mm；常规信号最小线宽、间距均为 0.127 mm（5 mil），孔到异网铜最小 0.20 mm。订板前需确认板厂工艺。射频走线宽 0.18 mm 是待与板厂叠层校准的起始值，不宣称已实测 50 Ω。

## 量产前必须解决

连接器试配、板厚、可用高度和外壳金属避让；板厂叠层与阻抗、射频匹配及天线效率；晶振起振与频偏；电源热设计、冷启动和车载瞬态测试；待机电流与误唤醒；生产测试和固件升级策略。EPAD 接地过孔的焊锡流失需通过钢网、堵孔或填孔工艺解决并检查焊接质量。

LMR51610 的 65 V 输入能力不等于整板通过 load dump。保险丝、TVS、反接器件和输入电容仍需联合测试。XGL4030 电感已核对原厂推荐焊盘：0.98 × 3.4 mm、中心距 2.37 mm，与使用的 KiCad XAL4030 封装焊盘一致。晶体为标准 10 pF 负载版本，焊盘与 ABM8G 图纸一致；16 pF 负载电容是按约 2 pF 杂散计算的起始值，装配后需测频偏。通用阻容尚未全部锁定采购料号。外接天线需要安装在塑料区并远离 OBD 金属。四层板的最终叠层、50 Ω 线宽、裸芯片接地过孔和天线布置需要在连接器机械条件确定后完成。

## 原厂依据

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
