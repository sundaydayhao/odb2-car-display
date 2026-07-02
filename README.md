# OBD-II Dashboard for Waveshare ESP32-S3 7inch

这是一个基于 ESP-IDF 的 OBD-II/CAN 仪表项目，目标硬件是微雪 `ESP32-S3-Touch-LCD-7` 开发板。

当前代码实现了基础框架：

1. 使用 ESP32-S3 的 TWAI 控制器读取 CAN 数据。
2. 通过标准 OBD-II PID 请求车速、油量、转速。
3. 先用日志输出数据，后续再接入 7 寸屏幕 UI。

## 硬件结论

OBD2 口出来的 `CANH/CANL` 不能直接接 ESP32-S3 的普通 GPIO。

但微雪 `ESP32-S3-Touch-LCD-7` 板子带 CAN 收发器和 CAN 端子，所以可以这样接：

```text
OBD2 6   -> 开发板 CANH
OBD2 14  -> 开发板 CANL
OBD2 4/5 -> 开发板 GND / 电源 GND
```

不要这样接：

```text
OBD2 6   -> GPIO19/GPIO20
OBD2 14  -> GPIO19/GPIO20
```

原因是 `GPIO19/GPIO20` 是 ESP32-S3 到板载 CAN 收发器之间的逻辑信号，不是车载 CAN 总线信号。

## 微雪 7 寸板 CAN 引脚

微雪 `ESP32-S3-Touch-LCD-7` 的板载 CAN 默认连接：

```text
CANTX -> GPIO20
CANRX -> GPIO19
```

项目里的默认配置已经设置为：

```text
CONFIG_OBD2_CAN_TX_GPIO=20
CONFIG_OBD2_CAN_RX_GPIO=19
```

如果你没有改硬件跳线，直接使用默认值即可。

## OBD2 接口针脚

常用 OBD2 针脚：

```text
4   Chassis Ground
5   Signal Ground
6   CAN High
14  CAN Low
16  Battery +12V
```

## 供电接法

不要把 OBD2 的 `16` 脚 12V 直接接开发板。

推荐接法：

```text
OBD2 16 -> 保险丝 -> 车规降压模块输入 +
OBD2 4/5 -> 降压模块输入 -
降压模块 5V -> 开发板 5V/VIN
降压模块 GND -> 开发板 GND
```

建议：

1. 降压模块选择支持汽车输入范围的型号。
2. 输入侧加保险丝。
3. 有条件时加 TVS 管抑制车载瞬态尖峰。
4. 开发板 GND 必须和 OBD2 GND 共地。

## 凯美瑞 2019 测试说明

2019 款凯美瑞通常可以先用标准 OBD-II PID 测试：

```text
PID 0x0D -> Vehicle Speed
PID 0x2F -> Fuel Level Input
PID 0x0C -> Engine RPM
```

当前项目发送标准功能寻址请求：

```text
Request ID:  0x7DF
Response ID: 0x7E8 - 0x7EF
Mode:        0x01
```

默认 CAN 速率是 `500 kbps`。如果没有数据，优先检查：

1. `CANH/CANL` 是否接反。
2. 是否接到了开发板的 CAN 端子，而不是 GPIO 排针。
3. 开发板和车辆是否共地。
4. 车辆是否上电到 ACC/ON。
5. CAN 速率是否为 `500 kbps`。
6. 板子的 CAN 模式选择是否处于 CAN 功能位置。

## 项目结构

```text
CMakeLists.txt
main/
  app_main.c
  obd2.c
  obd2.h
  ui.c
  ui.h
  CMakeLists.txt
  Kconfig.projbuild
```

代码职责：

```text
app_main.c  -> 应用入口和采集任务
obd2.c      -> TWAI 初始化、OBD-II 请求、响应解析
ui.c        -> UI 占位层，当前只输出日志
```

## 配置

执行：

```bash
idf.py menuconfig
```

进入：

```text
ODB2 Dashboard
```

可配置项：

```text
CAN TX GPIO              默认 20
CAN RX GPIO              默认 19
CAN bitrate              默认 500000
Poll interval            默认 1000 ms
Response timeout         默认 120 ms
```

## 构建

```bash
idf.py set-target esp32s3
idf.py build
```

## 烧录和监视日志

```bash
idf.py flash monitor
```

日志里会看到类似输出：

```text
speed=0 km/h, fuel=53%, rpm=780
```

如果一直是超时，先不要改 UI，先把 CAN 接线和速率确认好。

## 后续接屏幕

当前 `ui.c` 只是日志占位。后续接屏幕时建议保持数据层和显示层分开：

```text
obd2.c -> 只负责读取和解析车辆数据
ui.c   -> 只负责显示车速、油量、转速
```

下一步可以在 `ui.c` 中接入微雪屏幕示例里的 LCD/LVGL 初始化代码，然后把 `ui_render_vehicle_data()` 改成真正的界面刷新。

## 参考

1. 微雪 ESP32-S3-Touch-LCD-7 Wiki: https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7
2. ESP-IDF TWAI 文档: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/twai.html
3. ESP-IDF Get Started: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html
