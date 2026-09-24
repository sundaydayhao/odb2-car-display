# 0603 → 0402 封装调整 · 修订 Q

2026-09-25，按用户允许缩小常用阻容的要求，**99 个电阻＋56 个电容，共 155 个位置**改用英制 0402（公制 1005，1.0 × 0.5 mm）。原理图、PCB、设计源和采购表同步；值、连接、位号、焊盘 UUID、中心位置和朝向保持。板框仍为 145 × 85 mm。

| 值 | 位数（含 DNP） | 立创料号 / 型号 | 直营观察库存 |
| --- | ---: | --- | ---: |
| 1n | 3 | [C1523](https://item.szlcsc.com/1875.html) / 0402B102K500NT | 2,984,380 |
| 100n | 43 | [C1525](https://item.szlcsc.com/1877.html) / CL05B104KO5NNNC | 6,220,759 |
| 22n | 1 | [C1532](https://item.szlcsc.com/1884.html) / 0402B223K500NT | 451,016 |
| 100p | 5 | [C1546](https://item.szlcsc.com/1898.html) / 0402CG101J500NT | 1,899,411 |
| 22p | 3 | [C1555](https://item.szlcsc.com/1907.html) / 0402CG220J500NT | 899,102 |
| 33p | 1 | [C1562](https://item.szlcsc.com/1914.html) / 0402CG330J500NT | 478,070 |
| 100R | 6 | [C25076](https://item.szlcsc.com/25819.html) / 0402WGF1000TCE | 1,671,507 |
| 22R | 16 | [C25092](https://item.szlcsc.com/25835.html) / 0402WGF220JTCE | 1,638,624 |
| 100k | 33 | [C25741](https://item.szlcsc.com/26484.html) / 0402WGF1003TCE | 4,831,349 |
| 10k | 27 | [C25744](https://item.szlcsc.com/26487.html) / 0402WGF1002TCE | 12,254,428 |
| 22k | 1 | [C25768](https://item.szlcsc.com/26511.html) / 0402WGF2202TCE | 528,405 |
| 47k | 8 | [C25792](https://item.szlcsc.com/26535.html) / 0402WGF4702TCE | 4,570,494 |
| 4.7k | 8 | [C25900](https://item.szlcsc.com/26643.html) / 0402WGF4701TCE | 11,882,767 |

电阻均为 1%、62.5 mW、50 V、±100 ppm/°C；用于上拉、下拉、反馈/补偿和小信号串联。100 nF 选择三星 16 V X7R，其余所改电容为 50 V；100 pF、22 pF、33 pF 保持 C0G。原 1 nF C0G 音频滤波件没有换成 X7R。

保留尺寸的器件：1 µF 及以上的电源/LDO/储能电容、10/47 nF 环路和自举等电容、470 nF FM DBYP、0 Ω 配置/射频跳线、精密分压、NTC 和稀疏 E96 阻值，以及功率电感。R501 是 SD 电源 QOD 放电电阻，3.3 V/100 Ω 的初始功耗可达约 109 mW，未直接换成普通 62.5 mW 0402。R1014 保持 1206 功率规格。板上没有需要缩小的普通 0603 电感，现有功率电感按饱和电流、温升和 DCR 保留。

所有本次选用料号在立创大陆站均为正常供货、直营有货，观察库存至少为 100 块板＋20% 备料需求的 3 倍。库存记录采用页面独立时间戳，不代表锁货或长期保证。替换后全板共 86 个选定料号：85 个直营，BQ25896 一个合作供应；主控和 ML307 单采，F300 原有待定项保留。

检查：20 张原理图只改变隐藏封装属性；原生 SVG 去掉导出时间后全部相同，代表页 PNG 像素差为零。338 个实体器件模型均能加载，SCH/PCB 焊盘网络一致。逐位号清单见 [PASSIVE_SHRINK.json](PASSIVE_SHRINK.json)，实时观察摘要见 [LCSC_STOCK_SNAPSHOT.json](LCSC_STOCK_SNAPSHOT.json)，订购分组见 [PROCUREMENT_100PCS.csv](PROCUREMENT_100PCS.csv)。
