# 板载 FM、光照与温湿度核查

2026-09-24，基于主工程修订 N，检查基线 `4f0b605`。本次沿用现有板载器件，核对原理图、原生网表、PCB 焊盘与默认装配状态；没有重复新增模块。

| 功能 | 默认装配器件 | 原理图页 | 7 位 I²C 地址 | F 面中心坐标 / mm |
| --- | --- | --- | --- | --- |
| FM 收音 | U815 RDA5807M | 12_fm_audio | 0x10，保留随机访问 0x11 | 104, 66 |
| 环境光照 | U1300 OPT3001DNPR | 17_cabin_sensors | 0x45，ADDR 接 +3V3 | 87, 4 |
| 空气温湿度 | U1301 SHT40-AD1B-R2 | 17_cabin_sensors | 0x44 | 23, 80 |

坐标从屏幕背面看，PCB 左上角为原点；KiCad 绝对坐标另加 (50,50) mm。F 面朝后壳。位置见 [布局总览](FLOORPLAN.svg)，本次补上 FM 和光照标签，并移开被隔热槽遮挡的 SHT40 标签。

## 接线核查

- 三项共用 P4 GPIO32/33（核心板 U100 的 59/60 脚）系统总线。现有 R110/R111 为 4.7 kΩ 上拉；传感器支路各串 22 Ω。与 ES8389 的 0x12、BQ25896 的 0x6A 不重叠。
- FM 经 U810 ME6211C30M5G-N 获得可关断 3.0 V，GPIO51 控制，默认下拉关闭。Q810/Q811 的 G/S/D 和支路上拉已核对；FM 断电不把传感器电源一起关断。Y810 为 32.768 kHz 晶体。J810 只有天线和地，包含 C822 隔直、D810 ESD。
- FM 左右声道分别经隔直、偏置和 U811～U814 进入双功放。GPIO52 低选 Codec、高选 FM；先用 GPIO48 静音，再切换音源或 FM 电源。详见 [音频架构](AUDIO_ARCHITECTURE.md)。
- 两颗传感器使用系统 +3V3，C1300/C1301 各 100 nF，C1302 为 1 µF。OPT3001 中断脚明确 NC，采用轮询；SHT40 保留底边隔热槽和禁铺铜区域。详见 [传感器与外壳要求](CABIN_SENSORS.md)。

## 本次 PCB 修正

C814（22 nF）原在 (103,59) mm、180°，距 U815 供电脚偏远。移动至 **(99.5,65.75) mm、270°（KiCad 显示 −90°）**。C814 的电源焊盘到 U815.7 的中心直线距离从 **6.765 mm 缩短至 2.457 mm**。这是焊盘位置距离，实际走线长度和供电回路仍待布线验收。

仅 C814 封装树发生变化；其余 353 个封装、全部非封装 PCB 对象、20 张原理图及项目规则均保持。器件数量和 BOM 不变。按 RDA 手册第 17 页的近端旁路要求修正位置，后续优先连接 U815.7 与 C814，并就近接地。

## 检查结果与待办

KiCad 10.0.5 原生 ERC：0 error、13 个既有接口 warning；物理 DRC：0；原理图/PCB 一致性：0 差异；逐焊盘网名：0 不一致。FM 与传感器两页几何检查通过，并检查了原生渲染图。327 个实体器件的模型均可加载。机器记录见 [FM_SENSOR_REVIEW.json](FM_SENSOR_REVIEW.json) 与 [VERIFICATION.json](VERIFICATION.json)。

**整板仍为未布线草案：0 条走线、848 项未连接，不能据此投板。** FM 天线/起振、传感器外壳光窗/通风和装壳校准需实机验证；整机既有 LCD/摄像头电源缺口见 [设计状态](DESIGN_STATUS.md)。采购表中 RDA5807M 和 SHT40 的供应状态仍为 HOLD，本次没有重新查询库存或替换料号。

当前 A8 小板固件使用 DHT20/VEML7700 和另一套总线/音频配置，不能直接作为本载板固件。OPT3001/SHT40、GPIO32/33 和 ES8389 仍需相应适配；本次未修改固件。

复现：用 KiCad Python 执行 `tools/carrier_design/fm_sensor_review.py --baseline <修正前工程> --output <新的临时目录>`；脚本保留原对象并拒绝覆盖既有输出。然后运行原生 DRC/parity、`verify.py` 与模型检查，确认通过后再同步。

引脚与地址依据：[TI OPT3001 Rev C](https://www.ti.com/lit/ds/symlink/opt3001.pdf)、[Sensirion SHT4x v7.3](https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf)、[本地 RDA5807M 手册](../../../fm-module/RDA5807M%20datasheet_v1.pdf)第 14、17 页。
