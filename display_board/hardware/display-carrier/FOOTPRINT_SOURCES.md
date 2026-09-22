# 本地库来源与检查边界

库均用 `${KIPRJMOD}` 相对路径，可直接打开工程；标准库依赖 KiCad 10。没有要求用户安装嘉立创EDA或重新下载元件。

| 本地封装 | 来源 | 已检查 / 待检查 |
| --- | --- | --- |
| `OSPTEK_P4C5_P4C6_Compatible` | OSPTEK C5 非 EP 版本 PcbLib，由 KiCad Altium 导入器转换；对照 C6 PcbLib | 88 个编号与信号一致，焊盘中心和名义矩形覆盖已核对。沿用 C5 径向 1.4 mm，横向调整为 0.70 mm；不导入 C5 专属中心 EP，也不导入 C6 文件中的空编号伪焊盘 |
| `TPS22990_DML10` | easyeda2kicad，LCSC C962990 | 1～11 脚对照 TI DML 包装；EP=VIN；合并输出焊盘仍在同网。钢网/热设计未签核 |
| `TUSB320_RWB12` | easyeda2kicad，LCSC C132554 | 12 脚与 TI RWB 对照；量产公差和采购批次仍待核对 |
| `ML307A_GCLN_LGA94` | easyeda2kicad，LCSC C48982535 | 94 脚编号集合已核对；**完整几何尚未签核**。必须与中移 GCLN 当前生产版推荐焊盘逐项对照；不能因网表一致就按供应商下载库直接量产 |
| `USB_C_HRO_TYPE_C_31_M_12_Edge` | KiCad 官方 `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | 焊盘/钻孔未改，仅把伸出板边的丝印收至板内，完整结构保留在 Fab 层；独立保存避免库更新恢复板外丝印 |

其余封装来自本机 KiCad 10.0.5 标准库。其中 SIM8060、JST B2P-VH、SRP1038C、TI RHL20（ES8311 QFN20 为历史保留、当前不再使用）、WCH QFN16 已复制到 `Carrier.pretty`，仅补充模型绑定，焊盘和几何不变；SCH/PCB 同步使用这些本地库名称。详见 [3D 模型记录](3D_MODELS.md)。TPS22918 的 DBV 引脚明确为 1=VIN、2=GND、3=ON、4=CT、5=QOD、6=VOUT，不能使用其他引脚顺序。

## 核心板共用封装

两份库的径向中心分别约 12.373 和 12.500 mm，相差 0.127 mm。共用封装对两份库的 **88 个名义焊盘矩形覆盖检查均无失败**；部分方向最小额外裕量为 0，因此这个结果不是制造公差/焊接良率确认。仍需对齐实物邮票孔、钢网及厂家装配指导。

`Carrier.3dshapes/OSPTEK_P4C5.step` 来自 C5 仓库 `docs/P4模组3D模型.step`，原模型表面文字为 P4C6，当前仅作为共用外形参考。保留供应商模型坐标偏移 `(-12.5, -12.5, 1.05) mm`，避免模型偏离实际焊盘。它不能证明两个具体批次的全部高度相同。

OSPTEK 源资料：

- [P4C5 核心板](https://github.com/osptek/esp32-p4c5-core-board)，本机修订 `f00d33f574f1f12732f9c3ecfc08c6b9770dbf81`。
- [P4C6 核心板](https://github.com/osptek/esp32-p4c6-core-board)，本机修订 `8eac0d9fa51e8ae5b77ffd3084e8023b39ba63f3`。
- Copyright (c) 2026 OSPTEK（深圳鱼鹰光电），CC BY 4.0；许可副本见 [OSPTEK_LICENSE.txt](OSPTEK_LICENSE.txt)。本工程进行了格式转换、焊盘宽度调整及共用封装组合，非 OSPTEK 原版底板或官方认证产品。

KiCad 官方库的许可及模型来源沿用其仓库说明：[KiCad libraries](https://www.kicad.org/libraries/license/)。其他下载模型的来源为上述器件条目，不能据模型存在推定实际供应商、外形公差或长期供货已经确认。

## 散热与装配

功率器件采用标准无内置散热孔的基础封装。后续按板厂钻孔、孔铜、塞孔/钢网工艺添加实际散热过孔，避免套用另一工艺的微小孔。**当前没有散热过孔与铜面，不是热设计完成。**

连接器模型、屏幕/触摸 FPC、摄像头 FPC、侧开关拨帽和通孔焊尾均需实物/壳体共同验证。DNP 器件在默认 3D 图中不显示，不能据此认为蜂窝区域没有器件。

## FPC 实装补充（修订 F）

J1200/J1210/J1220 采用 XUNPU FPC-05FB-30PH20 / 6PH20 / 24PH20，对应 C2856833 / C2856827 / C2856831。easyeda2kicad 下载后逐项对照厂家系列图 Rev A；导入固定脚 3.24 mm 排距改为 3.25 mm，庭院覆盖所有铜焊盘；两个固定脚统一编号 MP 并接地。新增本地 STEP，Y 偏移 +1.625 mm，比例不变。模型和配套公差限制见 FPC_INTERFACES.md 与 model-manifest.json。未将插座已建库等同于屏幕装配已认可。

## 修订 G：新增传感器封装

- `OPT3001_DNP0006A`：按 TI OPT3001 Rev C 附录 DNP0006A、4221434/C 01/2018 绘制。6 个 0.50×0.25 mm 椭圆焊盘，0.65 mm 节距、1.90 mm 排距；中心 EP 0.65×1.35 mm，编号 7 接 GND，不擅加微孔。视觉检查与 SCH/PCB 逐脚核对完成。
- `SHT40_DFN4_NoCentralPad`：复制本机 KiCad 10 官方 Sensor_Humidity/Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad；保留焊盘与本体禁铜，仅改库名及本地参考模型。与 Sensirion SHT4x v7.3 推荐焊盘核对。不得增加中心铜焊盘。

本轮两颗传感器的 3D 仅为脚本生成包络，来源、模型哈希与限制见 model-manifest.json / CABIN_SENSORS.md。

## 修订 H：音频库

- `ES8389_QFN24_3x3_P0.35`：LCSC C22380673 / easyeda2kicad，按 ES8389 Rev 1 完整手册核对 24 周边端子、0.35 mm 节距、1.7 mm EP。修复供应商重复/零长度庭院线，采用 4×4 mm 闭合庭院；EP 缩小锡膏开口，钢网开窗与回流工艺仍待工厂审核。
- `LMA3729T381_TopPort`：按厂商 M141 V1.1 第 6 页绘制四个 Ø0.8 mm 圆焊盘，中心间距 1.70×2.46 mm；1=VDD、2/3=GND、4=OUT。本体 2.95×3.76 mm，顶声孔位置在 Fab 层；本地 3D 为尺寸参考。
- RDA5807M 使用官方 MSOP-10 3×3 mm / 0.5 mm；NS4150B 使用 MSOP-8 3×3 mm / 0.65 mm；TLV62585DRLR 使用 SOT-563。引脚编号与原生网表逐项核对，制造公差/采购批次不由此自动通过。

资料链接与电气边界统一见 [AUDIO_ARCHITECTURE.md](AUDIO_ARCHITECTURE.md)，新模型 SHA-256 见 model-manifest.json。
