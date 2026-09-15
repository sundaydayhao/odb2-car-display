# 显示单板 A7 的 3D 模型与装配检查

本轮 PCB 有 **399 个封装**：330 个实际装配实体、20 个 DNP 实体、45 个裸铜测试点和 4 个安装孔。350 个有本体封装全部绑定模型，使用 38 个在用 STEP 文件；KiCad 默认装配视图不显示 DNP。计数和逐文件哈希见 `model-manifest.json`，应随最终 PCB 复核。

模型位于 `Display.3dshapes/`，PCB 与 `Display.pretty/` 使用 `${KIPRJMOD}` 相对路径。LCD / 触摸均在 F 面 0°，入口朝板下沿。J9 保留竖插器件、长边横向；SW1/SW2 为无定位柱的 ALPS SKRTLBE010 侧按；J7 是外接麦克风用 GH 两针插座。

| 类别 | 模型及精度 |
| --- | --- |
| C6 MINI | Espressif 官方 STEP，按本地原点匹配，保留板载天线 |
| P4、J302、J9、J22、J16 | 料号关联 STEP，核对主要外形、焊脚、安装方向和板面基准 |
| SW1 / SW2 | C127481 关联 STEP，ALPS SKRTLBE010 原厂 Drawing 2 定义无定位柱版本、1.7 mm 推杆轴高和 0.2 mm 行程 |
| J7 | JST GH SM02B-GHS-TB 水平两针锁扣插座，配套 GHR-02V-S 线壳 |
| SW3 | SS-12D06G5 自建包络，本体 12.7 × 6.4 × 6.6 mm、侧杆长 5 mm、行程 2.2 mm；实际 STEP 杆轴在 F+3.3 mm |
| J308 | MPN 未锁定；XY 参考封装、暂定 2 mm 高，上接触入口示意 |
| J21 | TF-1007F 参考 XY，暂定高 1.85 mm，缺完整厂家尺寸图 |
| 阻容、普通 IC 等 | KiCad 标准模型，不等于每个采购 MPN 的最大公差包络 |

全部比例为 1；没有为匹配焊盘拉伸器件。`model-manifest.json` 记录来源、偏移、旋转、精度类别、SHA-256、位号与数量。名义模型生成器仍为 `tools/generate_display_nominal_models.py`。

## 实际器件与外壳

`enclosure/` 包含前框、PCB 承载壳、后盖、大拨帽、喇叭导声压框、容积垫块和真实主板装配预览。机械快照检查了实际 KiCad STEP 的 545 个实体、14 个关键器件、8 组代表插头/线束包络和 4 根 OD8 工具柱。具体输入与结果见 `enclosure/models/actual-component-check.json`；最终 PCB 几何变化后必须重导 STEP 重算，不把机械快照结果写成最终电气验收。

STEP/STL 采用右手坐标 X=PCB X、Y=−PCB Y、Z 朝后盖，真实 PCB 不被镜像。参数 JSON 与二维 PCB 图继续使用 KiCad +Y 向下坐标。真实 AFC01-S30FCA-00 剖面确认 F0 的入口朝 PCB +Y；上下接触与实物屏尾仍须配对核实。

## 尚需采购和实物确认

- **U2 Flash**：`GD25Q128ESIG` 是 SOP8-208mil，历史封装名 `WSON6X8` 且有第 9 接地中心焊盘。模型与现有长焊盘对齐不构成钢网、中心焊盘和工艺放行。
- J308 / J21 暂定高度和通用阻容模型不是最大尺寸公差。外壳配套 USB 插头、线束胶壳和取卡空间按代表包络检查，实际选件须再试装。
- 麦克风 φ4 × 1.3 mm 是 B4013AM422-042 SMT 驻极体外形，需小载板与线束固定；喇叭采用C3012621原型候选，以40×20×8 mm保守包络及原厂图近似篮架/磁体显示；尺寸/温度冲突和未实测声学表现见 `enclosure/SPEAKER_ASSEMBLY.md`。
- 屏金属与天线最近 15.386 mm 仅基于 6 mm 假设屏总厚；当前相对位置的约 6.413 mm 屏厚上限须随实物重算。未进行整机射频验证。

## 检查与来源

在仓库根目录，用 KiCad Python 执行 `tools/check_display_3d_models.py --project display_board/hardware/display-singleboard`，核对绑定、文件、哈希、计数、旋转和比例；机械重跑见 `enclosure/README.md`。Windows CadQuery/OCP 退出阶段已知析构问题可在成功写完并检查所有实体后显式跳过，导出异常仍失败。KiCad 日常查看不需要 CadQuery。

J18 / USB HS 专用外围及 D21 / D22 已取消，对应旧模型/验证为历史记录。J19 USB Serial/JTAG 调试口保留。见 `ESD_PROTECTION.md`。
模型来源：

- [KiCad 3D 库](https://gitlab.com/kicad/libraries/kicad-packages3D)，`KiCad_*.step` 保留原始文件内容，许可见 `Display.3dshapes/KICAD-LICENSE.md`（CC BY-SA 4.0 加电路设计例外）。
- [Espressif C6 MINI 官方 STEP](https://www.espressif.com/sites/default/files/3dmodel/ESP32-C6-MINI-1.STEP)、[P4 封装尺寸](https://documentation.espressif.com/esp32-p4_datasheet_en.html#packaging)。
- [HDGC 1.0K-GT-15PB 厂家尺寸图](https://dl.espressif.com/dl/schematics/1.0K-GT-15PB_specification.pdf)。
- [SS-12D10L3 厂家尺寸图](https://datasheet.lcsc.com/datasheet/pdf/562955152e0d35ebc194db3482640805.pdf?productCode=C319013)、[GigaDevice GD25Q128E 订购/封装表](https://cdn.ozdisan.com/public/product/assets/GD25Q128E-GIGADEVICE.pdf)。
- 其他料号关联 STEP 经 easyeda2kicad 1.0.1 从 EasyEDA/LCSC 获取；逐文件产品页记录在 manifest，保留模型原始内容和原权利人归属，不将这些文件重标为 KiCad 库许可。自建的五个名义模型由本项目生成器维护。

U401 为 TPS22965DSGR，使用料号关联 STEP；SW3 的 Mechanical_SS12D06G5.step 是自建机械包络。采购匹配、额定工作温度和外壳公差须实物核验。


A7 侧按键、麦克风插座与外接音频的原厂依据及许可见 `A7_SERVICE_AUDIO.md` / `LIBRARY_LICENSE.md`。模型完整与几何检查不代表采购、环境可靠性或外壳量产许可已完成。
