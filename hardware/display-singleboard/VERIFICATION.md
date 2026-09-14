# A1 单板检查记录

检查日期：2026-09-14。引擎：KiCad 10.0.5。对象：本目录 `display_singleboard.kicad_sch` / `display_singleboard.kicad_pcb`。

## 文件与电气连接

| 项目 | 结果 |
| --- | --- |
| 原理图 | 12 页：根页 + 11 个功能子页 |
| PCB | 134 × 74 mm，4 层，名义厚度 1.6 mm |
| 物理器件 / 唯一编号焊盘 | 477 / 1365 |
| 原生 ERC | 0 项 |
| 原生 DRC | 0 项 |
| PCB 未连接 | 0 项 |
| 原生原理图网表与 PCB 逐引脚比较 | 1365 个节点一致，无重复器件编号 |
| 原理图实例 UUID 与 PCB 路径 | 一致 |
| 装配表与 PCB | 位号、封装、值、装配状态一致 |
| 原理图几何检查 | 11 个功能页及新增支撑孔连线：0 项 |
| 局部器件库 | 59 种完整封装；全部使用项目本地库 |

检查脚本在仓库根目录运行，结果写入忽略目录 `build/display-singleboard-verification/`：

```powershell
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tools/check_display_hardware.py
```

脚本运行原生 ERC、DRC、XML 网表导出，核对每个编号焊盘、装配表、LCD / GT911 关键网络、移除旧板间接插件，以及 MIPI 连通路径长度。`summary.json` 包含原理图和 PCB 的 SHA-256；`mipi-lengths.json` 记录几何路径结果。修改 CAD 后应重新运行，当前结果不能代表修改后的文件。

本次没有通过增加忽略项或排除具体违规来消除错误。以下项目仍采用导入工程 / KiCad 的忽略配置，因此“DRC 0”不代表检查了这些项目：`missing_courtyard`、`track_not_centered_on_via`、`tuning_profile_track_geometries`、`footprint_filters_mismatch`、`footprint_type_mismatch`。

## MIPI 几何长度

从 U4 对应焊盘中心到 J302 面板接口焊盘中心，跨越已安装的 0 Ω 串联电阻；通孔换层按 1.6 mm 计算。支路不计入有效路径。屏幕排线和芯片内部延迟未计入。

| 差分对 | N（mm） | P（mm） | 对内差（mm） |
| --- | ---: | ---: | ---: |
| DATA0 | 63.1287 | 63.1287 | 0.0000 |
| DATA1 | 63.1639 | 63.1640 | 0.0001 |
| CLK | 62.8876 | 63.0521 | 0.1645 |

三对平均长度的最大差为 0.1941 mm。符合参考指南的对内 0.254 mm、对间 0.762 mm 目标。新增 DSI 名义线宽 0.1016 mm、中心距 0.24 mm；这些数字是布线几何，不证明 100 Ω 阻抗。参考目标见 [Espressif PCB 指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/pcb-layout-design-esp32p4.html)。

## 变更与图面复核

相对 Git 检查点 `356250b` 的双板参考，保留 P4 / C6 及原有扩展功能，把 LCD 偏压、背光、复位适配、触摸合入单板；移除多余板间接口和未用 DSI 选路支路。`integration-map.json` 记录 LCD 位号映射。

新增 DSI 成对布线和等长段、屏电源主干、控制信号、地回流及左侧两个支撑孔。KiCad 原生 PDF 的 12 页总览及 LCD 接口、背光、偏压页已检查；PCB 正反铜层导出图已检查。图纸使用同名全局网络标签连接器件；排线逐针定义见 `INTERFACES.md`。

PCB SHA-256：`7c40cc7945cfd7d5866aee8f8494ebbf599481e25fd772bba5ac148be90e2aa5`。

## 验证边界

这是完成布线并通过文件检查的工程样机设计，尚未打样或点亮。待实物验证包括：板厂叠层和阻抗、屏幕与两只 FPC 座的接触面和机械匹配、5 V 满载供电、LCD 偏压与背光温升、复位时序、USB / MIPI 信号质量、无线天线与金属背板间距、整机 EMC。

当前仅接稳压 5 V，不能直接车载 12 V。J308 具体采购料号及量产屏供货仍需核验。官方验证板可用于现有固件与屏幕验证，不能代替自研单板测试。本次未导出生产放行 Gerber，也未进行烧录或实机测试。
