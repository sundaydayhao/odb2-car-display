# A3 红框走线清理与差分线审查

以 `aa76353` 为基线，清理 CAN 收发器附近的 +3V3 重复路径和背面测试排附近的 EN 回折。正式 PCB 已应用经验证的副本，原生 KiCad 10.0.5 ERC、完整 DRC、未连接及原理图一致性均为 0 项。

大框内重复绕行的是 +3V3 电源网。保留原有供电分支，删除反复汇合的网格路径，用短支线连接邻近同网过孔。小框内是 EN 复位线，下探尖角改为水平线加 45° 转角。合计删除 61 段、新增 4 段，当前 1569 段走线、104 个过孔；仅 +3V3 与 EN 的底层线路改变。已检查原生前后对比图和像素差分。

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
