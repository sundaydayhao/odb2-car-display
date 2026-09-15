# 显示板硬件

这里是 ESP32-P4 显示板的硬件工程，与同级 `../main/` 固件源码组成一个完整板级包。

| 目录 | 用途 |
| --- | --- |
| [`display-singleboard/`](display-singleboard/README.md) | 当前 7 寸单板方案，包含 P4 + C6、LCD 电源/背光、触摸、音频、SD、连接器、PCB 和 `enclosure/` 3D 外壳工程 |
| [`display-mainboard/`](display-mainboard/README.md) | 合板前的主控板参考检查点 |
| [`display-lcd-adapter/`](display-lcd-adapter/README.md) | 合板前的 LCD / 触摸转接板参考检查点 |

当前制作与外壳验证以 `display-singleboard/` 为准；另外两个目录仅用于追溯早期双板设计。显示屏自带的 LCD 和触摸排线直接连接单板，前期实机验证可继续使用官方 ESP32-P4 Function EV Board。

## 文件关系

- `../main/`：显示板固件源码。
- `display-singleboard/*.kicad_sch`：原理图入口及分层原理图。
- `display-singleboard/*_singleboard.kicad_pcb`：当前显示板 PCB。
- `display-singleboard/enclosure/`：参数化外壳脚本、结构检查、渲染和交付说明。
- `display-singleboard/Display.pretty/`、`Display.3dshapes/`：项目本地封装和 3D 模型。

从仓库根目录运行显示板硬件检查时，默认工程路径已经指向 `display_board/hardware/display-singleboard`，例如：

```powershell
python tools/check_display_hardware.py
python tools/check_display_3d_models.py
python tools/export_display_enclosure.py
```
