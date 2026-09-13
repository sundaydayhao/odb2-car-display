# OBD CAN / BLE 采集节点：A0 工程草案

板框 **38.5 × 16.6 mm**，四层、双面贴装。固件位于 `../../firmware/obd_ble_node`，目标为 ESP32-C3 / ESP-IDF 5.5.x；采集节点通过 BLE 直连副屏，手机仅作为调试采集工具。

**主采集板尚不能打样。** 用户要求直接焊接现有 OBD 插头金属脚，已提供蓝色 MINI ELM327 的插头正面图片，并要求查阅 OBD-II 规范。2026-09-14 已核对 SAE 插接面网格并生成 [16 针直脚试配板](mechanical-reference/README.md)，板框同为 38.5 × 16.6 mm。该试配件按焊尾与插接触点同轴的假设绘制，只有孔位，没有采集电路；没有证据证明现有插头的内部焊尾满足此假设。主板 J1 仍保留完整逻辑引脚、未指定已验证的物理封装。目前主 PCB 是带网络的核心器件布局，尚未布线。确认焊尾与壳体后还需调整器件、孔位与金属避让区。

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
- `obd_ble_node.kicad_pcb`：57 个实际封装的核心布局，J1 待定。
- `Node.kicad_sym` / `sym-lib-table`：按数据手册建立的本地符号。
- `design.json`：器件、位置、网络及说明；`build/netlist-audit.json` 是导出网表的逐网核对结果。
- `build/schematic.pdf`、`build/pcb-front.png`、`build/pcb-back.png`：实际 KiCad 导出的检查图。
- `VERIFICATION.md`：通过项与未完成项分开记录。

生成脚本依赖 KiCad 10 原生 Python、KiSkill 和官方库：

```powershell
python generate.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
python stage_pcb.py --engine <KiSkill-main目录> --libraries <KiCad/share/kicad目录>
```

脚本会重建本目录的原理图和布局；后续手工修改应同步到脚本后再生成。它不会修改副屏主工程。所有设计网络都与 KiCad 导出的网表逐引脚比较；几何校验器包含植入交叉短路必须失败的检查。

## 量产前必须解决

连接器定位、可用高度、外壳内金属分布、最终布线和完整 DRC；射频匹配与天线效率；电源热设计、冷启动和车载瞬态测试；待机电流与误唤醒；生产测试和固件升级策略。

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
