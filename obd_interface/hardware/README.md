# OBD 接口板硬件

这里是 ESP32-C3 OBD 接口板的硬件工程，与同级 `../main/` 固件源码组成一个完整板级包。

| 目录 | 用途 |
| --- | --- |
| [`obd-ble-node/`](obd-ble-node/README.md) | 当前 OBD CAN / BLE 采集节点，包含 KiCad 原理图、PCB、器件库、射频走线和 3D 模型 |
| [`obd-programming-fixture/`](obd-programming-fixture/README.md) | 配套烧录与测试夹具的触点图、定位数据、制包和追溯工具 |

当前 OBD 板工程入口为 `obd-ble-node/obd_ble_node.kicad_pro`；固件入口为 `../main/`。`obd-ble-node/mechanical-reference/` 保留早期 OBD 插头孔位参考，不能替代当前 PCB。当前工程没有已验证的量产外壳模型，外壳适配仍需结合实际插头、板厚和射频净空完成。

## 文件关系

- `../main/`：ESP32-C3 固件源码。
- `obd-ble-node/obd_ble_node.kicad_sch`：原理图。
- `obd-ble-node/obd_ble_node.kicad_pcb`：已布线 PCB。
- `obd-ble-node/Node.pretty/`、`Node.3dshapes/`：项目本地封装和器件 3D 模型。
- `obd-programming-fixture/`：生产夹具资料，不参与固件构建。
