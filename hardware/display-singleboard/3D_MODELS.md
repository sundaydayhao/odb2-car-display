# 显示单板的 3D 模型

2026-09-14，在 A4 电路和布局上补齐模型。原工程 55 种封装中只有 3 种带模型引用；现在全部 **354 个实际装配器件**都有模型，另有 **27 个 DNP 选装器件**保留模型绑定。39 个裸铜测试点、6 个安装孔没有实体外壳，不添加假器件。KiCad 默认装配视图不显示 DNP。

37 个 STEP 文件随工程保存在 `Display.3dshapes/`，PCB 和 `Display.pretty/` 使用一致的 `${KIPRJMOD}` 相对路径。无需在另一台电脑上安装下载工具、建立绝对路径或联网获取模型。应打开本目录的 `display_singleboard.kicad_pro` / `display_singleboard.kicad_pcb`；若此前已开着 3D 查看器，重新载入 PCB 并刷新 3D 视图。

## 模型来源与精度

| 器件 | 模型及核对范围 |
| --- | --- |
| C6 MINI 模组 | Espressif 官方 STEP，保留屏蔽罩、板载天线等结构；按本地封装原点对齐 |
| P4、J302 LCD、J9 CSI、USB-C、J16 扬声器座 | 按料号获取 STEP；核对主要外形、焊脚位置、安装方向和板面基准 |
| 阻容、二极管、晶振、小封装 IC、排针、电感 | KiCad 官方标准封装模型；引脚方向按本地封装匹配，LED 按阳极/阴极匹配 |
| SW3 电源开关 | 根据 SS-12D10L3 尺寸图重建简化模型：本体 13 × 6.8 × 6.4 mm，柄高 3 mm，行程 2.2 mm；外壳细节、引脚长度为示意 |
| J7 麦克风 | 直径 4 mm、高 1.3 mm 的名义简化模型，示出声孔 |
| SW1 / SW2 按钮 | 暂按 4.5 × 4.5 × 4.3 mm 包络。下载的 B4SA 通用模型高 3.8 mm，原封装名写 2.9 mm，不能据此确定 B4SA-01 实物高度 |
| J308 触摸 FPC | 尚未锁定采购 MPN。XY 按原封装 7.2 × 4.5 mm 外形、暂定 2 mm 高度，示出上接触插口和锁扣 |
| J21 microSD | XY 按导入封装外形，暂定 1.85 mm 高度；尚无核验过的 TF-1007F 厂家机械图 |
| J18 USB-A | 使用与本板焊脚位置一致的标准外形参考模型，未改变 BOM；采购匹配问题见下文 |

`model-manifest.json` 逐封装记录来源、精度类别、偏移、旋转、SHA-256、位号和装配数量。所有模型比例均为 1；没有为了贴合焊盘而拉伸实物尺寸。部分下载的原始 STEP 坐标与转换器生成的 WRL 不一致，本工程采用核对后的 STEP 原点和板面基准，未直接套用转换器的 WRL 偏移。

简化模型生成器为 `tools/generate_display_nominal_models.py`（CadQuery 2.6+）；输出目录通过 `--output` 显式指定。模型覆盖完整不等于供应商机械数据完整，尤其不能从 J308、J21 和 SW1/SW2 的暂定高度直接开模。一般阻容的高度也是标准封装外形，并非每个采购 MPN 的公差上限。

生成工具的本机环境说明：测试的 OCP/VTK Windows 组合在 Python 退出阶段出现原生库析构异常，甚至仅导入 CadQuery 也会触发。可对生成器显式传入 `--windows-fast-exit`：仅在所有实体有效、STEP 完整写入并关闭后跳过该阶段，导出异常仍正常失败。已独立用 KiCad 加载所交付模型；日常查看 PCB 不需要 CadQuery 或此参数。

## 与外壳有关的修正和待核对项

- **J9 是竖插式 FPC**，厂家图外形为 22.1 × 4.5 × 5.4 mm；插拔方向应为板面上方 `+Z`。`enclosure.json` 已纠正，原二维区域继续作为排线弯折/操作空间，而不是侧插方向。PCB 上原有的引脚指示箭头不代表排线插拔方向。
- **J18 的采购与封装待统一**：本板壳脚中心距为 13.14 mm；BOM 的 `901-211A1021D10100` 对应 C2346 下载库为 14 mm。此次模型采用能对准现有焊孔的参考外形，不能视为认可该 BOM 料号可直接焊接。量产前应按最终厂家尺寸图核验并统一 BOM / 焊孔 / 外壳开口。
- **U2 的封装命名与 BOM 不同**：`GD25Q128ESIG` 对应 SOP8-208mil，原封装名为 `WSON6X8`，且存在第 9 个接地中心焊盘。模型按 BOM 显示 SOP8，侧脚落在现有长焊盘上；仍需确认此兼容焊盘的钢网开口、中心焊盘锡膏与最终装配工艺，不能将 3D 对齐作为焊接放行依据。
- SW3 仍位于背壳上沿凹槽，位置和铜箔未改。J1 排针、SW3、竖插 J9、FPC 抬起/弯折高度和扬声器线束都需要计入后盖空间；当前没有包含屏总成和外壳实体的完整干涉验证。

## 检查与来源

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_3d_models.py
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_hardware.py
```

覆盖检查核对 PCB / 封装库模型绑定一致、文件存在、哈希、装配数量、旋转和比例；不替代实际尺寸或干涉验证。已渲染并检查 KiCad 原生俯视、背面和斜视图。相对 `0a64945`，去除 `model` 节点后的 PCB 和封装语义完全相同；原生正反铜层、丝印和板框 SVG 字节一致。原理图、网表、铜箔、器件位置、焊盘、板框、安装孔和 DRC 配置未改。

模型来源：

- [KiCad 3D 库](https://gitlab.com/kicad/libraries/kicad-packages3D)，`KiCad_*.step` 保留原始文件内容，许可见 `Display.3dshapes/KICAD-LICENSE.md`（CC BY-SA 4.0 加电路设计例外）。
- [Espressif C6 MINI 官方 STEP](https://www.espressif.com/sites/default/files/3dmodel/ESP32-C6-MINI-1.STEP)、[P4 封装尺寸](https://documentation.espressif.com/esp32-p4_datasheet_en.html#packaging)。
- [HDGC 1.0K-GT-15PB 厂家尺寸图](https://dl.espressif.com/dl/schematics/1.0K-GT-15PB_specification.pdf)。
- [SS-12D10L3 厂家尺寸图](https://datasheet.lcsc.com/datasheet/pdf/562955152e0d35ebc194db3482640805.pdf?productCode=C319013)、[GigaDevice GD25Q128E 订购/封装表](https://cdn.ozdisan.com/public/product/assets/GD25Q128E-GIGADEVICE.pdf)。
- 其他料号关联 STEP 经 easyeda2kicad 1.0.1 从 EasyEDA/LCSC 获取；逐文件产品页记录在 manifest，保留模型原始内容和原权利人归属，不将这些文件重标为 KiCad 库许可。自建的五个名义模型由本项目生成器维护。
