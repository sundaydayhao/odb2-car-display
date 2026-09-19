# 显示底板 3D 模型检查

2026-09-19，针对 `display_carrier.kicad_pcb`。当前 **237 个已放置的实体器件均有可加载的模型**；另外 25 个裸铜测试点及 4 个 NPTH 安装孔没有实体模型，属于正常情况。尚未绘制的屏幕、触摸和摄像头连接器等不计入这个数字，工程整体仍处于未布线草稿阶段。

本轮修复 10 个器件：2 个失效路径、7 个缺失文件引用及 1 个没有绑定模型的器件。新增模型和修复后的封装随 `Carrier.3dshapes` / `Carrier.pretty` 保存，不需要复制到 KiCad 安装目录。标准 R/C 等仍使用 KiCad 10 的官方库。

| 位号 | 器件 | 处理及模型精度 |
| --- | --- | --- |
| U1000 | ML307A-GCLN | 恢复已有 STEP，替换失效 WRL 路径；保留 270° 旋转，外形 17.7 × 15.8 mm |
| U210 | TUSB320 | 恢复已有 STEP；1.6 × 1.6 mm，确认一脚方向 |
| J200 | TYPE-C-31-M-12 | 复用已有 C165948 模型；旋转 180°、Y 偏移 −1.05 mm，对齐壳脚和定位柱，未缩放 |
| J1000 | SIM8060 | C2943124 模型；Y 偏移 +0.35 mm、Z 偏移 +0.03 mm，对齐接点、壳脚和贴板面 |
| U800 | ES8311 | C962342 模型，修正一脚方向；3 × 3 mm、20 脚、0.4 mm 间距，EP 细节示意 |
| U200 | CH343P | C2846043 模型，修正一脚方向及贴板高度；3 × 3 mm、16 脚，EP 细节示意 |
| U310 | TPS22990 | **尺寸参考模型**：TI DML0010A，3 × 2 mm、0.8 mm 最大高度，含输出合并端子和 EP |
| U400 | TPS61088 | **尺寸参考模型**：TI RHL0020A，3.5 × 4.5 mm、1 mm 最大高度，20 个周边引脚及 EP |
| L400 | SRP1038C 外形 | **尺寸参考模型**：10 × 10 mm 本体、端子方向 11 mm、3.8 mm 高，端子细节简化 |
| J300 | JST B2P-VH | **尺寸参考模型**：7.86 × 8.5 mm、10.9 mm 高、3.96 mm 针距、3.7 mm 焊尾，锁扣细节简化 |

上述四个尺寸参考模型不是厂家 CAD，不能用于精密卡扣、注塑开模或焊接尺寸验收。ES8311 / CH343P 模型的 EP 为 1.7 mm，现有封装分别为 1.65 / 1.8 mm；本轮只用于外形显示，未据此改动焊盘。所有模型的制造公差、装配间隙仍需实物核对。

排除了两份不适合直接使用的下载模型：C87357 的 TPS61088 模型实际绘有 24 个周边端子；C160315 的 `VH3.96-2A` 是通用外形，原点、外宽和贴板后的高度与目标 JST 图纸不同。没有将它们作为精确模型写入工程。

## 装配检查

- [GCT SIM8060](https://gct.co/connector/sim8060) 是**翻盖式** nano-SIM 卡座，模型表示关闭状态。外壳需要让用户打开顶盖，不能仅按从侧面插卡的槽口设计。
- [Bourns SRP1038C 官方资料](https://www.bourns.com/docs/technical-documents/obsolete-parts/SRP1038C_obsolete.pdf) 已标记停产。L400 尚未锁定采购料号，本次只补齐现有封装外形；锁 BOM 时需选定在产电感并重新核对封装、饱和电流和温升。
- J300 焊尾朝屏幕侧，按 [JST VH 图纸](https://www.jst-mfg.com/product/pdf/eng/eVH.pdf) 表示 3.7 mm。板厚 1.6 mm 时，名义露出另一面约 2.1 mm；支柱及绝缘间隙还要考虑焊料和制造公差。
- 核心板沿用 OSPTEK 共用外形参考，模型上印的是 P4C6；不能据文字判断装配的 C5/C6 型号及具体批次高度。

## 可选器件显示

4G/GNSS 默认 DNP（不装配），因此 KiCad 默认装配的 3D 视图会隐藏相应模型。这与模型缺失不同。需要查看时，在 3D 查看器的显示设置中启用显示 DNP 器件；不要为查看模型取消正式工程的 DNP 属性。本次用临时副本生成了“全部选件装配”预览，正式装配选项保持原样。

## 来源与复查

`model-manifest.json` 记录来源网址、SHA-256、偏移、旋转、精度类别和实际模型包围尺寸。供应商 STEP 保留原文件字节，所有模型比例均为 1。四个参考模型用 `tools/carrier_design/generate_reference_models.py`（CadQuery 2.6+）重建；其依据为 [TI DML 图纸](https://www.ti.com/lit/pdf/MPDS574)、[TI RHL 图纸](https://www.ti.com/lit/ds/symlink/tps61088.pdf)、上述 Bourns / JST 图纸。

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/carrier_design/check_models.py --project display_board/hardware/display-carrier --output build/carrier-3d/coverage.json
```

加 `--baseline <原工程副本>` 可核对除模型及对应库名称外的原生 PCB/SCH 结构。此次保留器件位置、方向、UUID、DNP、焊盘、铜层、网络及项目规则；ERC 仍为 22 条待闭合接口警告，DRC 仍为 0 条物理违规、0 条原理图一致性问题、499 个未连接项目。**原有功能缺口没有因补齐模型而解决。**

已检查 IC 一脚方向、USB 壳脚/定位柱、SIM 原点偏移、参考模型高度，并查看原生 KiCad 默认装配和全部选件渲染。生成器会复制经过检查的本地封装，防止重新生成时恢复失效路径。核心板模型许可说明见 `FOOTPRINT_SOURCES.md`。
