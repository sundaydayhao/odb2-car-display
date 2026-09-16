# LCM-070--A0 屏幕专项审查

审查对象是 `display_singleboard.kicad_sch`、`display_singleboard.kicad_pcb` 及 LCD 偏置/外壳参数。依据为项目提供的 [LCM-070--A0 规格书](../../../docs/LCM-070--A0%20规格书.pdf)，重点核对第 5 页机械图、第 6 页 30 针表、第 7 页电源、第 8–9 页上/下电时序、第 10 页 MIPI 时序和第 15 页装配要求。

## 结论

| 项目 | 结论 | 证据 / 后续动作 |
| --- | --- | --- |
| J302 针序 | 通过 | 1–30 针与规格书逐针一致；31/32 是 PCB 接地安装焊盘，不接屏信号 |
| MIPI | 通过板内几何检查 | CLK、D0、D1 两数据通道与规格书接法一致；同对最大差 0.1034 mm，仍需实屏两 lane 配置和极性确认 |
| LCD VDD | 目标匹配 | U302 为 1.8 V，规格书范围 1.7–2.0 V |
| AVDD | 已按典型值修正，待实测 | R315 22.6 kΩ，按 1.25 V FB 估算约 9.8 V |
| VGH | 待实测 | 目标 18–20 V，电荷泵实际输出和负载能力不能只由网表判断 |
| VGL | 原设计不匹配，已修正目标 | 原 LMSZ5233 为约 6.0 V；D310 改为 LMSZ5237BT1G 8.2 V 标称，需在 TP301 验证容差/负载 |
| VCOM | 目标匹配，待实测 | R323=2 kΩ、R322=1 kΩ，AVDD≈9.8 V 时约 3.3 V |
| 背光 | 目标基本匹配，待实测 | R312=1.1 Ω，约 180 mA；确认 LED 串电压 8.1–9.9 V 和热裕量 |
| 上/下电时序 | 不通过量产门槛 | STBYB 只有 LCD VDD 上拉，没有独立 MCU 控制，不能证明规格书要求的 20 µs/100 ms 时序；下一版补 GPIO 开漏或经验证 RC |
| 机械/FPC | 不通过当前候选路径 | 规格书第 5 页 56.54 mm 尺寸小于当前估算所需 61.713 mm，短约 5.17 mm；必须按实物尾根/接触面重定位 J302 和壳体通道 |

## J302 逐针核对

| 针 | 规格书信号 | 工程网络 | 针 | 规格书信号 | 工程网络 |
| ---: | --- | --- | ---: | --- | --- |
| 1 | LEDA | PANEL_LED+ | 16 | GND | GND |
| 2 | LEDA | PANEL_LED+ | 17 | MIPI_TCP/CLKP | DSI_A_CLKP |
| 3 | VGH | PANEL_LCD_VGH | 18 | MIPI_TCN/CLKN | DSI_A_CLKN |
| 4 | VGL | PANEL_LCD_VGL | 19 | GND | GND |
| 5 | UPDN | PANEL_UPDN | 20 | MIPI_TDP1/D1P | DSI_A_DATA1P |
| 6 | SHLR | PANEL_SHLR | 21 | MIPI_TDN1/D1N | DSI_A_DATA1N |
| 7 | LEDK | PANEL_LED- | 22 | GND | GND |
| 8 | LEDK | PANEL_LED- | 23 | MIPI_TDP0/D0P | DSI_A_DATA0P |
| 9 | AVDD | PANEL_LCD_AVDD | 24 | MIPI_TDN0/D0N | DSI_A_DATA0N |
| 10 | GND | GND | 25 | GND | GND |
| 11 | MIPI_TDP3 (D3) | NC（两 lane 不用） | 26 | STBYB | PANEL_STBYB |
| 12 | MIPI_TDN3 (D3) | NC（两 lane 不用） | 27 | RESET | PANEL_RESET_LCD |
| 13 | GND | GND | 28 | VDD | PANEL_LCD_VDD |
| 14 | MIPI_TDP2 (D2) | NC（两 lane 不用） | 29 | VDD | PANEL_LCD_VDD |
| 15 | MIPI_TDN2 (D2) | NC（两 lane 不用） | 30 | VCOM | PANEL_LCD_VCOM |

规格书第 6 页的命名顺序与工程连接器的差分命名存在编号表达差异，但物理位置和成对关系已按工程 J302 17/18=CLK、20/21=D1、23/24=D0 固定；未引出的 D2/D3 保持 NC，不能在软件中当作第三/第四 lane 使用。

## PCB 与装配核对

- J302 位于 F 面、下沿中央、0°、入口朝 PCB +Y，和规格书第 5 页“屏幕底边中间出 FPC”相符。FPC 接触面、连接器上下接触方向和 0.30 mm 左右尾厚仍必须拿到实物后确认。
- LCM 外形采用 164.86 ±0.3 × 99.96 ±0.3 × 2.8 ±0.3 mm；P+G 盖板、胶层和完整金属背板不包含在这个裸 LCM 尺寸内。第 4 页文字出现 163.86 mm，已按第 5 页机械图建模并列为供应商确认项。
- 有效显示区为 154.21 × 85.92 mm。外壳的 154 × 86 mm 开窗仍是占位值，不能直接用于量产模具。
- 板内 MIPI 路径复核结果：DATA0 对内差 0.0000 mm，DATA1 0.1034 mm，CLK 0.0001 mm；三对平均长度差 0.0643 mm。该结果不包含 FPC、连接器和芯片内部延迟，板厂仍需按叠层实测 100 Ω 差分阻抗。
- 规格书第 15 页要求四角/四边均匀支撑并避免局部压屏；当前外壳采用前框承屏、PCB 四角固定、可拆后盖，结构方向合理，但需在实物屏到货后做压合、振动和热循环验证。

## 上电和关机实现门槛

规格书第 8 页要求 VDD/VDDIO 稳定后等待至少 1 ms 再释放 RESET，随后至少等待 20 µs 再释放 STBYB；第 9 页要求关机先拉低 STBYB，并保持 MIPI 有效数据约 100 ms 后才撤掉偏置/逻辑电源。当前 GPIO27 只经 U304 控制 RESET，STBYB 由 R303 被动上拉，工程中没有能执行这两个延时的独立控制点。因此即使首板能够点亮，也不能把当前时序当作量产合格。建议下一版给 STBYB 增加 GPIO 控制的开漏级，并在驱动中把“电源稳定 → RESET → STBYB → MIPI/背光”和关机反序列化成可测状态机。

## 本次已写回的设计变更

1. `D310`：`LMSZ5233BT1G` → `LMSZ5237BT1G`，保持 SOD-123 封装，VGL 目标由约 −6 V 改为约 −8.2 V。
2. `R315`：`22 kΩ` → `22.6 kΩ`，把 AVDD 反馈目标从约 9.6 V 调到约 9.8 V。
3. 同步更新原理图、PCB 属性、`parts.json`、`assembly.csv`、屏幕配置和外壳参数；FPC 参考尺寸改为规格书第 5 页的 56.54 mm。

这些改动属于“按规格书典型值收敛”的样件设计，不替代供应商确认和 TP301–TP305 的实测。当前工程仍因电池电源翼未完成高电流布线以及既有 DRC/未连接基线而处于原型阶段，不能直接输出量产 Gerber。
