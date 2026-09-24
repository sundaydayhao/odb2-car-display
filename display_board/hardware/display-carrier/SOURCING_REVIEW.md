# 修订 P：国内采购核查与替换

2026-09-24，基于修订 N / `90df33e`，主控 U100 和 ML307 U1000 按用户要求保留。已核对载板全部实体装配位置，以立创商城大陆站实际商品数据为依据；搜索摘要、贴片仓库存和海外可售数量不混作国内现货。

当前 338 个实体位置、默认装配 331 个。排除主控/ML307 后，328 个装配位置已落实为 **85 个具体立创料号**：84 个国内直营现货料号，1 个合作供应料号；剩余 **F300 保险丝尚待电池条件定型**。7 个原有 DNP 位置保持不装。

沿用原采购规模：100 块＋20% 备料，按合并料号后的用量向上取整到最小订购量。全部 85 个所选料号的观察库存覆盖所需数量，84 个直营料号均至少为本次采购需求的 3 倍。这是本批次库存缓冲判据，不能证明长期供货、锁仓或量产资格；下单前须再确认实时库存和批次。

## 已实际落图的替换与定料

| 位号 | 原问题 | 当前料号 / 立创链接 | 现货数量 | 电气 / 封装处理 |
| --- | --- | --- | ---: | --- |
| U800 | ES8389 / C22380673，仅 52 个 | ES8388 / [C365736](https://item.szlcsc.com/339918.html) | 46,135 | 改用 ES8388；QFN28、外围与引脚重建；保留双麦/立体声，需要 I²S 驱动迁移 |
| U815 | RDA5807M / C82537，0 库存且售罄 | FSV4730-D60-GU / [C19191931](https://item.szlcsc.com/20433014.html) | 747 | 改用 FSV4730-D60-GU / SSOP24；64–108 MHz 模拟立体声，**不支持 RDS**，需要新驱动 |
| U1301 | SHT40-AD1B-R2 当前现货未确认 | SHT40-AD1F-R2 / [C7461846](https://item.szlcsc.com/8492038.html) | 1,922 | 改用带 PTFE 膜的 SHT40-AD1F-R2；同引脚、同地址，模型高度改为 0.69 mm |
| SW310 | EG1224 / C273395，仅 139 个 | SK22D02L4 / [C609825](https://item.szlcsc.com/639830.html) | 3,080 | SK22D02L4 双刀侧滑，只用一刀，另三脚 NC；壳脚接地，新开孔/行程需试装 |
| SW101/SW102 | 原 PTS645 未锁定现货 | TS-038-060250-D2 / [C52178082](https://item.szlcsc.com/54999684.html) | 970 | TS-038-060250-D2；按厂图重建信号孔、壳脚孔与定位槽，操作力 2.5 N |
| R109 | NTC 缺完整订货号 | ST10603X103F3950FB / [C49352955](https://item.szlcsc.com/51854434.html) | 3,000 | ST10603X103F3950FB，0603，10k，R25/B25/50 各 ±1%；是板温探头 |
| D211/D212/D310/D311 | BAT54 占位与 SOD-123F 待复核 | BAT54H,115 / [C426769](https://item.szlcsc.com/420788.html) | 8,350 | Nexperia BAT54H,115；实际换成 SOD-123F 封装，1 脚阴极 |
| L300 | 1 µH 仅有参数占位 | CXP0520-1R0M-AG(1.0uh) / [C7461343](https://item.szlcsc.com/8489120.html) | 1,330 | CXP0520-1R0M-AG；5.2×5.7×2.0 mm，DCR≤17mΩ，厂商表 Isat 9A / Irms 8A |
| L410 | 2.2 µH 仅有参数占位 | MWSA0402S-2R2MT / [C408335](https://item.szlcsc.com/397737.html) | 23,340 | MWSA0402S-2R2MT；DCR≤58mΩ；资料表最小 Isat 4.0A、Irms 3.8A |
| L1010/L820 | 1 µH 仅有参数占位 | MWSA0402S-1R0MT / [C408332](https://item.szlcsc.com/397734.html) | 19,690 | MWSA0402S-1R0MT；DCR≤27mΩ；资料表最小 Isat 5.6A、Irms 5.4A |
| C1012/C1013 | 220 µF 低 ESR 缺具体料号 | T520D227M010ATE025 / [C116743](https://item.szlcsc.com/117998.html) | 1,479 | T520D227M010ATE025：220µF/10V 聚合物，25mΩ；D7343/3.1mm，图纸明确正极 |
| R1010 | 536k/1% 精确料号待定 | RC0603FR-07536KL / [C137701](https://item.szlcsc.com/149017.html) | 4,800 | Yageo RC0603FR-07536KL，保持原分压值 |
| R1014 | 100Ω/0.25W/1206 待定 | 1206W4F1000T5E / [C17901](https://item.szlcsc.com/18589.html) | 2,668,200 | Uniroyal 1206W4F1000T5E，保持原功耗与尺寸等级 |
| C407/C408/C409 | 原 47µF C2932478 库存 975，100 板需 360 | GRM32ER71A476KE15L / [C84494](https://item.szlcsc.com/85664.html) | 40,658 | Murata GRM32ER71A476KE15L，47µF/10V/X7R/1210；有效容量仍需偏压核算 |
| Y810 | 原 ABS07 ESR 最大 70kΩ，不符合新 FM ≤50kΩ 条件 | SXTF3215R32768A20F / [C22386778](https://item.szlcsc.com/23941597.html) | 695 | SXTF3215R32768A20F，32.768kHz/12.5pF/20ppm/ESR≤50kΩ；两端各 22pF，起振与频偏待测 |
| U816（新增） | FSV4730 需要独立上电复位 | MCP120T-270I/TT / [C145625](https://item.szlcsc.com/156957.html) | 2,854 | MCP120T-270I/TT，开漏输出＋10k 上拉；随 FM 电源断电，上电总线等待 1s |

上述电感电流是各厂商指定室温测试条件下的值，不能等同于密闭车载外壳内的保证电流。保留环路、热态饱和、瞬态和 MLCC 有效容量的实测任务。

## 保留的器件与例外

- OPT3001 / C90462：5,522 个；双 MEMS 麦克风 / C54320678：5,747 个；SN74LVC1G3157 / C38663：214,680 个；TLV62585 / C485117：1,360 个。现货满足本批用量，保留。
- L400 FEXU0640S2R2MGS / C54140025：745 个，本批需 120 个；保留既有国产紧凑方案。其高负载验证条件仍见 INDUCTOR_SELECTION.md。
- **BQ25896RTWR / C181475：2,800 个为 Waldom 合作供应库存，页面交期 1–3 天**。标记 `PARTNER_CONFIRM`，没有称为立创自营仓库存；订单前确认批次、交期与可分配数量。其数量足够，暂不改动已核对的充电电路。
- **F300：HOLD**。电池接口仍是 PH 2A；缺电池包短路电流、线束及温度降额条件，不能把任意有货的“2A 保险丝”当成合格替代。外部电池、线束、扬声器、屏幕与天线不在载板贴装 BOM 范围内。
- 其余电源 IC、USB/电平转换器、连接器、电阻、电容均见逐位号表；没有未分类的装配元件。

## 功能差异与后续驱动

ES8388 与 FSV4730 均不是原芯片的焊盘或寄存器直替。工程已经同步符号、引脚、封装、外围电路、局部位置、采购表和模型；本次没有实现新固件驱动。

ES8388 为标准 I²S 从机，7 位地址 0x10。GPIO9/10/11/12/13 分别为播放数据/LRCK/录音数据/BCLK/MCLK，对应芯片 pin6/7/8/5/1；沿用网络名 `CODEC_TDMIN` 只是历史命名，已不表示 ES8389 的 TDM 复用。两个 ADC 分别使用 LIN1−RIN1、LIN2−RIN2，Reg10=0xFC、Reg11.DS=0；双麦保留。LOUT1/ROUT1 是单端输出，功放负输入保留 AC 地和中点偏置，不能把 OUT2 接成反相输出。最高采样率按 ES8388 的 96kHz 规格，不能沿用原 192kHz 能力。

FSV4730 只保留 FM 模拟立体声需求，不提供 RDS。SEN 接地选 0x11（厂家资料引用的 AN332 2-wire 规则）；与 ES8388 0x10、OPT3001 0x45、SHT40 0x44、BQ25896 0x6A 不冲突。Y810 改接 RCLK 与 GPO3/XTAL，增加两只 22pF 负载，DBYP 为 470nF，FMI 串联 1nF C0G。MCP120 复位释放最长 700ms：启用 FM 电源后，固件先让**整条系统 I²C 保持空闲高电平 1s**，随后以 CTS 完成状态推进命令，并配置晶体模式。实际晶体起振时间必须实测，不能用固定延时替代 CTS 检查。

新侧按键、侧滑的孔位、操作力与突出长度有变化；SHT40 膜版高度不同。已重建/替换封装，后壳开口和屏侧焊脚仍需样件试装。新加的 3D 模型是数据手册包络，未模拟全部引脚、膜形态和机械公差，不能据此认定外壳无干涉。

## 验证与证据

KiCad 10.0.5：370 个原理图符号、361 个电路封装＋4 安装孔；0 几何违规、0 错网、0 ERC error、13 条既有未完成接口 warning、0 物理 DRC、0 SCH/PCB 差异。354 个原有封装 UUID 保留，主控/ML307、FPC 接口、板框/四处开槽与设计规则未改变。338 个实体器件模型均可解析。

新增 11 个外围元件，整板仍为 **0 走线、885 项未连接**。检查没有验证射频接收、音频性能、测量精度、功耗或制造装配；LCD/摄像头既有电路缺口仍见 DESIGN_STATUS.md。

- [100 块采购汇总](PROCUREMENT_100PCS.csv)、[逐位号采购表](PROCUREMENT_REFERENCES.csv)、[采购审计](PROCUREMENT_AUDIT.json)。
- [库存快照](LCSC_STOCK_SNAPSHOT.json)：115 个所查候选的商品链接、时间、国内/合作渠道、现货量、原页 SHA-256 与参数；包含未入选候选，不应直接全量下单。
- [替换与保存检查](SOURCING_REVIEW.json)、[原生电气/PCB 检查](VERIFICATION.json)、[3D 检查](MODEL_VERIFICATION.json)、[资料版本与 SHA](SOURCING_SOURCES.json)。
- 本地原始商品页、厂家 PDF、原生 ERC/DRC/网表及图片位于 `build/carrier-sourcing-20260924/`。`stage_domestic_sourcing.py` 用于从 N 基线重建临时 P 工程；`review_domestic_sourcing.py` 独立核对封装关键尺寸、保存边界与采购覆盖率。

资料：[ES8388 Rev5](https://atta.szlcsc.com/upload/public/pdf/source/20190124/C365736_5E84B6C58F11FDCF085DB830A572A0FC.pdf)、[FSV4730 Rev1.1](https://datasheet.lcsc.com/lcsc/2410122036_Forsinve-FSV4730-D60-GU_C19191931.pdf)、[AN332 §6.1](https://www.skyworksinc.com/-/media/Skyworks/SL/documents/public/application-notes/AN332.pdf)、[MCP120 DS11184D](https://ww1.microchip.com/downloads/en/DeviceDoc/11184d.pdf)、[SHT4x v7.3](https://sensirion.com/media/documents/33FD6951/6A7C10A0/HT_DS_Datasheet_SHT4x_V7.3.pdf)。

用户补充布局要求已落实：双麦位于 (138,73)/(7,73)mm，中心距131mm；扬声器接口 J820/J821 分别在 (137.5,45.5)/(6.5,44)mm。10个封装随此次分离调整，器件/网表/采购数量不变，物理DRC与一致性复核为0。详见 AUDIO_EDGE_LAYOUT.json。
