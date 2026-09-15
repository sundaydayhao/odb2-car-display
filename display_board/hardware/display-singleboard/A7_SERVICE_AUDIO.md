# A7 侧按维修键与外接音频装配

2026-09-15。SW1/SW2 与 J7 已在原理图、专用封装、装配表和模型清单中替换。J16 的扬声器电路保持不变。本页是板上接口与装配依据；外接咪头小载板、喇叭实物和最终线束仍需样件确认。

## 侧按 RST / BOOT

采用 Alps Alpine **SKRTLBE010**（LCSC C127481）：侧推常开按键，本体名义 4.5 × 3.4 × 3.3 mm，操作力 1.6 N，行程 0.2 mm。按杆轴距 PCB 正面 1.7 mm，按杆端面在封装局部 Y=+2.04 mm，朝局部 +Y；选用无定位柱版本，PCB 不钻两只定位孔。PCB 旋转 180° 后朝板顶边。外壳采用有导向的绝缘维修针孔，并保留操作行程；不能让外壳预压按钮。

| 引脚 | SW1 | SW2 |
| --- | --- | --- |
| 1、3（内部相连） | GND | GND |
| 2（按下接通） | ESP_EN | GPIO35_BOOTMODE |
| MP 金属固定脚 | GND | GND |

封装为 `Display:SKRTLBE010_SIDE_PUSH`。按原厂图保留 1/3 两个实际端子编号；没有延续旧按钮的 2/4 未连接网名。底部原厂禁止铜区已加在 F.Cu，不能用走线穿过该区域。器件 STEP 来自 C127481 供应商模型，量得总包络约 4.6 × 3.56 × 3.35 mm，Z 最低处为 0、没有定位柱，顶面 Z=3.35 mm；其与原厂名义尺寸有小差别，仅作装配显示，最终公差采用原厂图和实物。

选择无柱 B 版是为遵守现有 0.2 mm 孔到铜工艺要求。A 版原厂 land 的定位孔至自带焊盘只有约 0.10/0.15 mm；本次不采用局部放宽规则，改用原厂 Drawing 2 无柱版，保留焊脚和按压方向。

## 外接麦克风

J7 改为 **JST SM02B-GHS-TB(LF)(SN)**，GH 1.25 mm、2 针、横插、带锁扣。匹配线端外壳 **GHR-02V-S**，端子 **SSHL-002T-P0.2**，26–30 AWG。板座名义本体 5.75 × 4.05 × 4.25 mm；插口朝封装局部 +Y。GH 与 J16 的 PH 2.0 mm 结构、间距不同，避免音频输入输出误插。采购需核实实际库存和原装/兼容料，不能仅按商品标题替换。

| J7 针号 | 连接 | 含义 |
| --- | --- | --- |
| 1 | MICP | 驻极体信号与偏置正端 |
| 2 | N18300564 | 原电路麦克风专用回线 |
| MP | GND | 板座金属固定脚 |

保留 ES8311 的 ADCVREF → R73 → 滤波/偏置电阻网络，以及输入耦合、MICN/回线原连接。该接口用于两线驻极体，不能直接插三线模拟 MEMS 或数字 MEMS。线束将信号与专用回线双绞，避开 D 类喇叭线、开关电源、显示/USB 高速线；长度由机壳实际路径加维修余量确定，仅为装配估算，不能视为抗干扰认证。

保留原 **Goertek B4013AM422-042** 咪头可使用独立小载板后引线：原厂资料确认其为 SMT 驻极体，外径 4.0 mm、高 1.3 mm（一般公差 ±0.1 mm），顶部六个 φ0.25±0.05 mm 声孔、分布于 φ2.0 mm 节圆。测试条件为 2.0 V、2.2 kΩ 负载，工作电压 1–10 V，电流最大 500 µA。偏置接入本板后仍需测量有载工作点及录音质量。其规定工作温度 −30～70°C，外壳日晒温度评估不能忽略。不要直接把承力导线焊在咪头底部接触面，线束应在载板/壳上应力释放。

咪头用弹性密封圈固定在外壳声孔后，前声道与机壳内部声腔隔开，声孔不可被胶堵住；远离喇叭和振动源。参考立创板仅采用其外壳音频组织方式，立创板 ZTS6216 麦克风不作为本板两线接口的直接替换料。

## 外接喇叭

保留 J16 **A2001WR-S-2P / PH 2.0 mm 两针**。针 1 为 N18423897（SPK+），针 2 为 N18423915（SPK−），两者经过原有输出滤波连接 NS4150B。它是桥接输出，**任何一根喇叭线均不可接 GND**。喇叭线成对布置，与咪头线束分开。

当前选用 **GSPK4020057TN-4R3W / C3012621** 做原型候选，4 Ω、额定3 W/最大4 W。原厂高度5.7/8.0 mm和温度说明存在冲突，因此壳体保守留40×20×8 mm，物料仍标 `PROTOTYPE_CANDIDATE`；不能称汽车量产合格。NS4150B 在5 V /4 Ω时按约2 W低失真输出评估，原111 dB单频近场数值不用于预测行车听感。

外壳已改为独立约5 cc后腔、左侧朝驾驶员的密封前声道、可拆压框与容积垫块；后盖不向挡风玻璃出声。具体结构、M2×12压框、线束和数据表冲突见 [扬声器装配](enclosure/SPEAKER_ASSEMBLY.md)。车辆验收与功率限制见 [AUDIO_VALIDATION.md](AUDIO_VALIDATION.md)。旧DB1811/1W只保留为早期参考，不是现装配料；本轮未实现新的固件限幅。

## C6 朝上后的整机净空

天线部分优先伸出基板边缘。Espressif 对整机装配建议天线各方向保留至少 15 mm 净空；屏幕金属背板、金属压框、螺钉、屏蔽件、磁铁和底座都需纳入检查。平面上的 PCB 禁布区不足以证明屏幕后方净空满足要求。塑料壳整体、实际屏幕和线束装好后验证连接距离和吞吐量。

## 资料与检验边界

- [Alps SKRTLBE010 产品](https://tech.alpsalpine.com/e/products/detail/SKRTLBE010/)；[原厂 SKRT 尺寸图](https://tech.alpsalpine.com/cms.media/product_catalog_ta_02_skrt_en_01ec237785.pdf)。
- [JST GH 原厂规格](https://www.jst-mfg.com/product/pdf/eng/eGH.pdf)；[C127481 模型来源](https://www.lcsc.com/product-detail/C127481.html)；[C189893 采购入口](https://www.lcsc.com/product-detail/C189893.html)。
- [Goertek B4013AM422-042 原厂规格，LCSC 托管](https://datasheet.lcsc.com/datasheet/pdf/db444a5b583e31a2b036db3eb8e7819d.pdf?productCode=C233793)，电气第 4 页、机械第 6 页、温度第 10 页。
- [立创实战派 S3 官方硬件表](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/)；[NS4150B 厂家](https://en.nsiway.com.cn/list_38/82.html)。
- [Espressif 麦克风结构指南](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/audio_front_end/Espressif_Microphone_Design_Guidelines.html)；[C6 布局与整机天线指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c6/pcb-layout-design.html)。

器件迁移副本的原生 ERC 为 0；原生网表比较确认 SW1/SW2 指定引脚迁移、J7 新增固定脚接地和 H301/H302 删除以外的节点不变。按钮/麦克风原理图局部及两个新增封装的 3D 渲染均已查看。此项验证不替代最终整板 DRC、布线和外壳干涉检查。

A7 配套载板现已落图，见 [enclosure/microphone-carrier](enclosure/microphone-carrier/README.md)。本次选择保留 B4013 并通过载板固定，不再停留于裸咪头直接焊线建议；中央焊盘过孔必须树脂填孔、铜帽整平。
