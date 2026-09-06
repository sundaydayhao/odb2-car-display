# ELM327 BLE 汽车副屏

微雪 ESP32-S3-Touch-LCD-7、ESP-IDF 5.5.5、LVGL 9.2。项目只保留 **ELM327 BLE** 数据入口，
不再提供开发板直连车辆总线的代码、引脚配置或构建预设。开发板与车辆之间不接数据线。

## 使用

1. 将支持 BLE 的 ELM327 适配器接到车辆诊断口，车辆上电。
2. 断开手机诊断软件与适配器的连接，多数适配器只允许一个客户端。
3. 开发板上电后自动扫描、连接、订阅串口服务、初始化适配器并轮询车辆参数。
4. 页面显示「搜索设备 → 连接设备 → 同步设备 → 等待车辆/实时车况」。缺失数据为 `--`。

ESP32-S3 仅支持 Bluetooth LE，不支持 Classic SPP；标注“蓝牙 ELM327”的适配器不一定是 BLE。
参见 [Espressif 蓝牙说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/ble/overview.html)。
适配器的蓝牙串口传输的是 ELM ASCII 指令/响应，本机不会把每个通知包误当成独立 CAN 帧。

## 已实现

- NimBLE Central 主动扫描和自动重连；连接/发现看门狗；失败设备短暂退避。
- 按名称或指定地址筛选设备；自动发现服务、特征与真实 CCCD，支持 Notify/Indicate。
- FFF0、FFE0、Nordic UART 三类串口服务，以及自定义服务/特征 UUID。
- MTU 协商、写入应答/无应答、通知分片拼接、单指令串行事务、`>` 结束符和容量检查。
- 超时或溢出后关闭当前连接，隔离迟到响应；用连接代次防止旧连接数据进入新会话。
- ELM/STN 身份检查、AT 初始化、自动识别 ISO 15765-4 协议 6/7/8/9。
- 支持 PID 位图查询和单 ECU 固定；11 位/29 位响应头、填充字节、边界校验以及有界 ISO-TP 重组。
- 每项数值独立失效时间；断开连接立即清空实时有效标记，UI 不阻塞等待蓝牙。
- 可选 BLE 配对、明确配置的 SMP passkey、NVS 绑定保存。不自动覆盖冲突的旧绑定。

| 数据 | 标准 PID | 单位 |
| --- | --- | --- |
| 车速 | 01 0D | km/h |
| 发动机转速 | 01 0C | rpm |
| 发动机负载 | 01 04 | % |
| 节气门开度 | 01 11 | % |
| 冷却液温度 | 01 05 | °C |
| 油量 | 01 2F | % |
| 控制模块电压 | 01 42 | V |
| 进气温度 | 01 0F | °C |
| 机油温度 | 01 5C | °C |
| 进气歧管绝对压力 | 01 0B | kPa |

只轮询所选 ECU 声明支持的参数。增压相对压力不使用固定大气压猜测；档位、胎压、
变速箱温度、续航、油耗等尚未接入的参数保持不可用。此版本不执行清码、刷写、控制执行器或全总线监听。

## 配置

在 `idf.py -B build menuconfig` 的 **ELM327 BLE Dashboard** 中配置：

- 名称：默认留空，匹配包含 OBD、ELM、VLINK、V-LINK、VEEPEAK、VGATE 的名称，忽略大小写。
- 地址：可指定 `AA:BB:CC:DD:EE:FF`，优先于名称筛选。隐私地址可能轮换。
- UUID：三项同时设置或全部留空，详见 [BLE 协议实现](docs/ELM327_BLE.md)。
- 配对：默认按服务的加密要求触发；写入特征需加密的适配器可打开预先配对。
- 指令间隔：默认 50 ms；普通响应 1500 ms；初次车辆搜索 20000 ms。

工程使用统一配置：默认值由根 `sdkconfig.defaults` 和 `main/Kconfig.projbuild` 提供，
当前构建配置保存在根 `sdkconfig` 中，包含 BLE、屏幕和 PSRAM 参数，无需额外的项目预设。

## 构建与烧录

继续使用原来的 `build` 目录。本机该目录绑定的环境：

```powershell
$env:IDF_TOOLS_PATH = 'C:\Users\zhang\.espressif'
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\zhang\.espressif\python_env\idf5.5_py3.11_env'
. 'C:\esp\v5.5.5\esp-idf\export.ps1'
idf.py -B build build
idf.py -B build -p COM9 flash
idf.py -B build -p COM9 monitor
```

开发板供电按微雪板卡要求使用 USB/合适的稳压电源，不要将车辆 12 V 直接接到开发板供电引脚。
板载 EXIO5 保持低电平，保留原生 USB 路由，不再启动车辆收发器。
包含 BLE 协议栈后采用 IDF 的 1500 KiB 单应用分区；应用仍从 `0x10000` 开始，
NVS 和 PHY 分区地址、大小保持不变，不需要全片擦除。首次升级应使用 `flash` 写入新分区表，不能只用 `app-flash`。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `main/elm327_ble.c` | NimBLE 连接、服务订阅、串行传输和重连 |
| `main/elm327_protocol.c` | 有界 ASCII/ISO-TP 响应解析，不依赖硬件 |
| `main/obd2.c` | AT 会话、能力发现、参数调度与快照发布 |
| `main/obd2_decode.c` | 数值换算和逐字段过期策略，不依赖硬件 |
| `main/ui.c` | 屏幕/触摸驱动及显示模型映射 |
| `main/dashboard.c` | 五页彩色界面、主题、手动导航、显式演示模式 |

## 验证

协议测试：`tools/protocol_tests`，在 `build/protocol_tests` 下编译，运行 `ctest --test-dir build/protocol_tests --output-on-failure`。
测试包含每个通知分片边界、回显、提示语、多个 ECU、两类响应头、ISO-TP 序号错误、截断、溢出、
负响应、不支持参数、换算、逐字段过期和随机字节输入。板端 UI 同源测试见 [UI 说明](docs/UI_DESIGN.md)。

实际适配器的 UUID、配对方式和响应行为需要实机确认，软件测试不等同于已经连通真实车辆。
2026-09-06 已使用原 `build` 构建并烧录到 COM9，写入校验成功；启动后观察 65 秒，
屏幕/触摸初始化及 BLE 扫描正常，未发现崩溃。观察期间未连接到匹配的适配器，尚未完成真车数据验收。
