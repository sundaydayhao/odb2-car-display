# 显示板硬件

这里是 ESP32-P4 显示板的硬件工程，与同级 `../main/` 固件源码组成一个完整板级包。

| 目录 | 用途 |
| --- | --- |
| [`display-carrier/`](display-carrier/README.md) | 当前方向：兼容 OSPTEK P4C5 / P4C6 核心板的 7 寸显示底板；LCD/触摸 FPC 同向，可选 4G＋GPS/北斗；2026-09-19 修订 B，尚未创建 SCH/PCB |
| [`display-singleboard/`](display-singleboard/README.md) | 已放弃的裸 P4 + C6 单板方案，保留原 SCH/PCB 和 `enclosure/` 供追溯，不作为新板投产依据 |
| [`display-mainboard/`](display-mainboard/README.md) | 合板前的主控板参考检查点 |
| [`display-lcd-adapter/`](display-lcd-adapter/README.md) | 合板前的 LCD / 触摸转接板参考检查点 |

新设计以 `display-carrier/` 的规划为准。先确认核心板封装、现有屏幕两通道 MIPI 配置、触摸排线和电源预算，再开始 SCH/PCB；前期实机验证可继续使用官方 ESP32-P4 Function EV Board。其余三个目录仅用于追溯，旧板检查通过不能证明新底板可用。

## 文件关系

- `../main/`：显示板固件源码。
- `display-singleboard/*.kicad_sch`：原理图入口及分层原理图。
- `display-singleboard/*_singleboard.kicad_pcb`：历史显示板 PCB。
- `display-singleboard/enclosure/`：参数化外壳脚本、结构检查、渲染和交付说明。
- `display-singleboard/Display.pretty/`、`Display.3dshapes/`：项目本地封装和 3D 模型。

以下现有检查命令仍指向历史 `display_board/hardware/display-singleboard` 工程；新底板目前只有方案文档，不适用这些 ERC/DRC/结构检查：

```powershell
python tools/check_display_hardware.py
python tools/check_display_3d_models.py
python tools/export_display_enclosure.py
```
