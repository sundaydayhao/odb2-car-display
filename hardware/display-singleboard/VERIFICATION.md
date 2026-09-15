# A6 新屏 MIPI 适配检查记录

检查日期：2026-09-15。引擎：KiCad 10.0.5。CAD 比较基线：`43095f1`（A4 电路及 A6 屏幕评估说明）。

本版完成指定 JD9165 屏的 MIPI 引脚接线与 PCB 布线。屏幕模组额定电源、两通道设置、触摸 IC 和 FPC 接触面尚待实物/厂家确认；本记录是 CAD 检查结果，不是点亮或生产放行记录。

| 检查项 | 结果 |
| --- | --- |
| PCB | 120 × 74 mm，4 层，名义厚度 1.6 mm |
| 物理器件 / 唯一编号焊盘 | 418 / 1161 |
| ERC / DRC / 未连接 | 0 / 0 / 0 |
| SCH / PCB 逐引脚网络 | 1161 项一致 |
| 位号、铜与封装 UUID、原生载入网络身份 | 无重复、无漂移 |
| 新屏接口针表 | LCD 32 + 触摸 8 项，包含接地安装焊盘 |
| 装配表 | 位号、值、封装与装配状态一致 |
| MIPI 参考地复核 | 完整路径、背面参考走廊和返回地过孔通过；详见下述采样边界 |
| 外壳接口平面检查 | 六处支撑、十四处接口操作空间无新增冲突；开关位于上侧板边 |
| 项目内 3D 模型 | 40 个模型文件，344 个已装实体器件，引用/哈希/变换检查通过 |

## 本版变更

- J302 位于朝屏幕的 B 面中下部，30 针 MIPI DSI 使用 CLK、D0、D1；D2/D3 不接。J308 根据所选正面左出线触摸布置在背面视图右侧，针序为 Shutdown、VCC、GND、INT、SDA、SCL。
- J9 摄像头 F 面竖插，重布完整 CSI 链路；官方参考的六颗 100 Ω 串联电阻保留原值，几何检查明确跨接对应串联器件，不把它们当作 0 Ω BOM。
- 保留一个 USB HS A 母座 Host 和独立 USB Serial/JTAG 调试口；稳压 5 V 输入及大尺寸侧拨开关位于可从上/侧壳操作的位置。下边留给底座。
- USB HS 的 D21 / D22 更换为默认装配的 TI TPD1E05U06DPYR，使用匹配的 DPY 焊盘与模型；1 脚为信号、2 脚为地，原理图、PCB、采购和装配表同步。
- 重新布置显示、触摸和摄像头接口，完成相关供电、音频与控制信号连接；保留 MIPI 背面线在 L3 上的局部参考地和换层回流过孔。

## 完整差分路径几何

以下从 U4 焊盘中心到连接器焊盘中心计算，包含明确已装串联器件的焊盘中心间距与名义板厚 F/B 换层。不含 FPC、器件电气延迟和芯片内部延迟。

| 接口 / 对 | N（mm） | P（mm） | 对内差（mm） |
| --- | ---: | ---: | ---: |
| DSI DATA0 | 53.8189 | 53.7961 | 0.0228 |
| DSI DATA1 | 54.4575 | 54.4921 | 0.0346 |
| DSI CLK | 53.9730 | 53.9471 | 0.0259 |
| CSI DATA0 | 43.4220 | 43.4221 | 0.0001 |
| CSI DATA1 | 43.4346 | 43.4346 | 0.0000 |
| CSI CLK | 43.5578 | 43.4219 | 0.1359 |

DSI 三对平均长度最大差 0.6673 mm；CSI 为 0.0678 mm。均在对内 0.254 mm、对间 0.762 mm 的既定几何目标内。

MIPI 100 Ω、USB 90 Ω 的实际阻抗仍需结合板厂叠层求解。连续参考地、回流过孔、线长与 DRC 是独立检查项；不能用等长结果代替阻抗或眼图验证。

参考地以不大于 0.05 mm 的纵向步距、七个横向点采样。B 面走线在 L3 上的线边外 0.5 mm 走廊通过；采样采用已记录的本接口信号过孔反焊盘局部例外。F 面在 L2 的中心参考满足检查条件，但部分横向边缘受反焊盘影响存在缺口，未声称整个 F 面走廊均无缺口。机器可读结果见 `CHECK_RESULTS.json`。

## 复核方法

在仓库根目录使用 KiCad Python：

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_hardware.py
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_panel.py
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_3d_models.py
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/export_display_enclosure.py
```

DRC 在检查时重填覆铜，不覆盖输入 PCB。检查器同时对比原始铜网络与 KiCad 原生载入后的网络，避免相交的悬空铜被自动改网而掩盖错误。

沿用的 DRC 未检查项目：`missing_courtyard`, `track_not_centered_on_via`, `tuning_profile_track_geometries`, `footprint_filters_mismatch`, `footprint_type_mismatch`。最终没有为剩余布线错误新增排除项。

## 交付边界

新屏电源暂保留参考电路名义值：VDD 1.8 V，AVDD 9.6 V，VGH 18 V，VGL −6 V，VCOM 3.2 V，背光约 182 mA。它们尚不是厂家确认的所选模组额定值。详细待确认项见 `PANEL_TARGET.md`。

本板输入为稳压 5 V，未实现车载 12 V 的反接、抛负载与冷启动前端。没有进行新屏点亮、触摸、电源动态/温升、无线/EMC 或完整外壳装配验证。3D 模型覆盖不代表采购料号和装配公差已批准。

PCB SHA-256：`6cc3d1f65b0163bfbed17d0b3335e14dab2bc5229f3b056f51ffd567d6328260`。
