# 7 寸显示系统 A0：主控板

> 已由 [A1 单板工程](../display-singleboard/README.md) 取代。本目录保留为参考检查点，以下双板接线说明仅用于追溯；当前开发使用 A1 单板设计，前期实机验证使用官方 ESP32-P4 验证板。

本目录是可编辑的 KiCad 10 工程，包含完整分层原理图、已布线 PCB 和项目本地器件库。与 [LCD / 触摸转接板](../display-lcd-adapter/README.md) 配套，定位为工程验证版本；尚未经过实板点亮、整机 EMC、热测试和量产放行。

在 KiCad 中打开 `display_mainboard.kicad_pro`，再进入原理图或 PCB 编辑器。原理图入口为 `display_mainboard.kicad_sch`，PCB 为 `display_mainboard.kicad_pcb`。两个工程都不需要安装第三方符号库。

## 已实现的电路

| 模块 | 实现 |
| --- | --- |
| 主控 | ESP32-P4NRW32X，按 silicon rev ≥ 3.0 的 P4X 参考电路，外置 16 MB Flash、芯片封装内 32 MB PSRAM |
| 无线 | ESP32-C6-MINI-1，P4 经 SDIO 连接 C6，提供 Wi-Fi / BLE |
| 显示 | 两通道 MIPI DSI；J5 接 LCD 转接板 J3 |
| 触摸 | I²C GPIO7 / GPIO8；通过辅助线束预留复位和中断 |
| 音频 | ES8311、麦克风、NS4150B 扬声器功放 |
| 扩展 | 摄像头 MIPI CSI、microSD、以太网、GPIO 排针 |
| 调试 | P4 USB Serial/JTAG、BOOT / RESET；C6 独立 UART 下载排针 |
| PCB | 88 × 74 mm 外接矩形，4 层，名义板厚 1.6 mm，沿用参考板机械轮廓 |

这是显示端主板，车辆数据由无线 OBD 节点传入；这里没有新增车端 CAN 收发器。两个显示端工程不涉及已有 OBD 节点文件。

## 默认装配与参考板的差异

- R113、R131、J20 不装。GPIO26 / GPIO27 分配给背光和 LCD 复位，隔离备用 USB FS 数据支路。
- C30 改为 1 µF，配合 CHIP_PU 上拉形成启动延时。
- P4 rev ≥ 3.0 所需的 R128 / R138 = 499 kΩ、C53 = 22 pF 保持安装；不能直接按旧版 P4 的空贴策略生产。
- 恢复原理图实例与 PCB 的 UUID 对应，修复导入时丢失的电源铜区优先级和麦克风环形焊盘偏移，重新铺铜。
- 修正 LED 的 A/K 引脚编号；项目库保留完整封装几何，后续更新 PCB 不应产生重复器件。

逐器件安装状态见 `assembly.csv`。`parts.json` 是本次生成的设计记录；手动修改 KiCad 后应同步更新装配表，电气连接以原理图和 PCB 为准。

## 供电和 USB

输入为**稳压 5 V**。本板没有车载 12 V 的反接、抛负载和冷启动电源前端。验证时建议使用带限流的 5 V 电源，并按屏幕、无线和扬声器同时工作的峰值确定供电容量。

J19 用于 USB Serial/JTAG。J15 与 J18 共用一个 USB HS 控制器，不能同时接两个 USB 对端；设备 / 主机的使用方式需要在固件中选择。J20 默认不装。

## 两板连接与软件引脚

完整接线表见 [INTERCONNECT.md](INTERCONNECT.md)。MIPI FFC 只提供 3.3 V 逻辑电源，屏幕偏压和背光还需要辅助线束的 5 V。

现有应用基于旧版官方评估板。给新 P4X 板烧录前，应确认 ESP-IDF 5.5 配置支持实际芯片修订版，并核对屏幕初始化参数；本次没有更改正在使用的固件配置，也没有烧录设备。

| 外设 | P4 GPIO |
| --- | --- |
| I²C SDA / SCL | 7 / 8 |
| 背光 PWM / LCD RESET | 26 / 27 |
| 触摸 INT / RESET（新辅助线束） | 22 / 23 |
| 音频 MCLK / BCLK / LRCK / DOUT / DIN | 13 / 12 / 10 / 9 / 11 |
| 功放使能 | 53 |
| C6 SDIO CLK / CMD / D0…D3 / RESET | 18 / 19 / 14…17 / 54 |

## 检查与工程边界

实际检查结果见 [VERIFICATION.md](VERIFICATION.md)。检查包含 KiCad ERC、DRC、原理图与 PCB 逐引脚网表对应、参考器件数量、重新铺铜和原生图纸导出。

MIPI 差分目标为 100 Ω，USB 为 90 Ω。保留了官方的高速路径和等长结构；项目中的网络类别是布线目标，不是阻抗测量证书。主板参考叠层的外层到相邻内层介质约 0.08 mm，最终应由板厂按实际材料、铜厚和阻焊做阻抗求解与测试。

在外壳未定义前，安装孔和板框沿用参考设计。C6 天线附近应避开 LCD 金属背板、转接板地铜和线束；不能把主控板任意叠放后就认定无线性能合格。整机安装、FPC 接触面、供电时序、满载温升和屏幕样品还需要实物验证。

主控、屏幕电源、背光和触摸接口现已合入 [A1 单板工程](../display-singleboard/README.md)。本目录仅保留作电路来源，不再作为当前制作方案。

参考来源与库授权见 [SOURCES.md](SOURCES.md)。
