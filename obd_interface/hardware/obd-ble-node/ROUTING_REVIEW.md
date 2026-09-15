# A3 红框走线清理与差分线审查

以 `c9ab071` 为基线，清理正面 3D 图标出的两处同网交叉：

- ESP32 左侧的 VDD_RF：删除 U5.2/U5.3 之间的三角重复路径，保留 0.2 mm 公共供电连接及 C12 去耦分支；整理 FB1.2 至公共连接的细碎折线，保持与 VBAT_ADC 过孔的设计间距。
- 下排 J1.12/J1.13 槽孔之间的 +3V3：删除绕 J1.13 再与主干交叉的重复供电路径；保留竖向主干，下方供电过孔通过水平支线接入主干的明确节点。FB1.1 及旁边供电过孔保持连接。

本次删除 57 段、新增 11 段，仅修改 F.Cu 的 VDD_RF 与 +3V3；当前 **1523 段走线、104 个过孔、64 个封装**。元件、全部过孔、板框、焊孔、测试与定位结构、原理图和检查设置保持一致。射频信号 RF_SOC / RF50 / ANT_FEED 以及 USB、CAN 走线没有改变。

正式 PCB 已应用经审查的副本，原生 KiCad 10.0.5 ERC、完整 DRC、未连接及原理图一致性均为 0 项；未新增规则排除。原生正面铜图前后对比、像素差分和 PCB 语义比较已检查。夹具坐标未变，`contacts-source.json` 已更新 PCB 校验值。本机证据位于 `build/a3-front-cleanup/`，其中 `work/obd_ble_node.kicad_pcb` 与正式文件字节一致。

此前以 `aa76353` 为基线完成底层清理：删除 CAN 收发器附近 +3V3 的重复绕行和 EN 回折，61 段替换为 4 段，当时为 1569 段走线、104 个过孔。该阶段证据保留在 `build/a3-route-cleanup/`。

## USB 与 CAN 是否要等长

| 信号 | 要求 | 当前端到端 PCB 走线估算 |
|---|---|---|
| USB D− / D+ | 应按差分对平行、等长布线，90 Ω ±10%，减少换层并保持连续参考地 | D− 38.9656 mm / 5 个过孔；D+ 47.6272 mm / 3 个过孔；长度差约 8.6617 mm |
| CAN_H / CAN_L | 应短、对称、尽量并行等长；本板 250/500 kbit/s、厘米级路径无需为约半毫米差值添加蛇形线 | U3 至 OBD 主路径：H 10.3144 mm，L 9.7778 mm，差约 0.5366 mm；各经过 1 个过孔 |
| CAN_TX / CAN_RX | MCU 与收发器之间的独立逻辑信号，不要求两者配对等长 | 无配对等长约束 |

长度由实际 PCB 的走线中心线、过孔连接及焊盘位置求路径，不含过孔垂直长度、封装内部和外部夹具／线缆。CAN 网络还有保护器件分支，不能把全网铜线总长当作 U3 到 OBD 的主路径长度；过孔、保护分支及回流路径仍需作为 EMC 设计的一部分审查。

**当前 USB 几何仍不满足原厂推荐的平行等长布局。** 红框清理不包含 USB 重布线，DRC 为零也不等于 USB 信号完整性通过。USB 专项调整应先缩短并合并 D+/D− 走廊，统一换层及回流，再依据板厂实际叠层计算差分线宽／间距，最后微调长度；不能只给较短一根添加蛇形线凑数字。目前未确认 90 Ω，也未进行实板 USB 波形／可靠性测试。

依据：

- [Espressif ESP32-C3 PCB 指南：USB](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/pcb-layout-design.html#usb)：平行等长、90 Ω ±10%、减少换层及连续参考地。
- [TI CAN 布线答复](https://e2e.ti.com/support/interface-group/interface/f/interface-forum/811553/sn65hvd232-pcb-layout)：短、对称、尽量等长；没有适用于所有 CAN PCB 的统一长度差数值。本文不为本板约 0.54 mm 差值加蛇形线，是结合本板速率与尺寸作出的工程判断。
- [TCAN1044A-Q1 手册 §9.5](https://www.ti.com/lit/ds/symlink/tcan1044a-q1.pdf)：保护器件靠近总线接口、避免瞬态电流绕路，重视去耦与回流。

本机审查证据保存在 `build/a3-route-cleanup/`：`review.json`、`pixel-review.json`、`signal-paths.json` 和 `routing-comparison.png`。生成图片不纳入 Git；PCB 文件和本记录纳入版本管理。
