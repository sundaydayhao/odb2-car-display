# ELM327 BLE 实现与验证

当前硬件为 P4 Function EV Board v1.4。NimBLE Host 在 P4，C6 ESP-Hosted 2.12.11 以 SDIO/VHCI 提供 BLE Controller，
Wi-Fi STA 与 BLE 共享这条链路；需要匹配的 C6 固件。详见 [P4 上板与联调](P4_BRINGUP.md)。

## 分层

```text
车辆 -> ELM327 -> BLE GATT 串口 -> 提示符结束的 ASCII 响应
     -> ECU / ISO-TP / Mode 01 解析 -> 逐字段缓存 -> 显示模型 -> LVGL
```

蓝牙由 ESP-IDF 自带 NimBLE 承担，不自行实现无线链路层。开发板只作为 Central/Observer，
不再包含车辆总线控制器的初始化、收发中断、请求发送或总线恢复逻辑。

## 服务发现

| 服务 | 开发板写入特征 | 开发板接收特征 |
| --- | --- | --- |
| FFF0 | FFF2 | FFF1 |
| FFE0 | FFE1 | FFE1 |
| Nordic UART 6E400001-B5A3-F393-E0A9-E50E24DCCA9E | 6E400002-B5A3-F393-E0A9-E50E24DCCA9E | 6E400003-B5A3-F393-E0A9-E50E24DCCA9E |

前两项为常见串口布局，不保证所有 ELM327 克隆都相同。支持手动填写服务、写入、接收 UUID，
三项必须同时有效。检查特征的实际写入和通知属性，并只在接收特征所属范围内发现 2902 描述符，
不假设 CCCD 一定等于特征句柄加一。优先 Notify，没有 Notify 时使用 Indicate。

0.3.6 起，首次连接由「设置 → 蓝牙设置 → 扫描设备」触发一次十五秒扫描，列表显示最多十六个可连接 BLE 设备的名称与地址；点选后才连接。相同名称不合并，地址和地址类型共同标识设备。扫描可以在已有连接期间进行，不主动断开当前适配器。

选中的设备仍须具有支持的 GATT 串口服务、返回 ELM327/STN 标识并接受必需 AT 命令。通过这些检查后才将身份地址和名称保存到 `bluetooth/peer_v1`。旧名称/地址过滤配置不再参与选取，未通过检查的设备不替换已保存适配器。

开机、重新开启蓝牙以及已验证连接的意外断开，只向已保存设备发起一次十秒连接尝试；失败后停在待选状态，不扫描、不连接其他设备。连接后的配对/服务发现也有截止时间。手动断开、关闭、忘记会取消自动重连；忘记同时清理该设备的绑定记录。BLE 加密配对仍按适配器要求进行，不把普通 GATT 连接假称为完成加密配对。未绑定且轮换随机地址的设备可能需要重新扫描选择。

串口调试：`bluetooth scan` → `bluetooth list` → `bluetooth connect INDEX`，以及 `bluetooth reconnect|disconnect|forget|on|off`；`reconnect` 仅尝试已保存设备。

## 会话

初始化顺序：

```text
ATZ -> ATI -> ATE0 -> ATL0 -> ATS1 -> ATH1
-> ATCAF1 -> ATCFC1 -> ATAT1 -> ATST64 -> ATSP0
-> 0100 -> ATDPN -> 0120 / 0140（按支持位图）
```

使用自动 CAN 格式化和自动流控；保留响应头以固定 ECU，避免多个控制器的数据混用。
首次有效能力响应优先选数值较小的响应地址，随后只接受该 ECU。当前产品会话要求
ISO 15765-4 协议 6/7/8/9 和有效 CAN 响应头。纯解析器另有无头响应测试，但产品不会借此绕过 ECU 一致性检查。

只执行标准 Mode 01 读取。没有 ATMA 全总线监听、车型私有控制、清码、刷写和 VIN 读取。
ISO-TP 重组上限 128 字节，最多同时区分 8 个 ECU；总响应容量 2048 字节。
ELM 自动生成流控，开发板不额外发送原始总线流控帧。

## 并发与失败处理

- NimBLE host 回调只完成连接状态机和有界通知入队，不访问 LVGL，不等待整条车辆响应。
- 一个采集任务串行执行 AT/PID 指令；互斥锁防止多个调用者交错命令。
- 以 `>` 为完成边界，允许任意通知分片、CR/LF、大小写与指令回显。
- 命令超时、非法字节、溢出使整个连接失效；不在同一串口会话里继续接收迟到响应。
- 连接代次附在通知和异步回调上；旧连接与新连接缓存互相隔离。
- 连接失败、服务缺失、订阅失败后停止；仅成功验证过的会话意外断开可消费一次重连机会，失败不会重新获得机会。
- 加密错误可触发配对；对于无应答写入且写特征受保护的适配器，应配置预先配对。
- 重复绑定冲突不自动删除现有绑定，日志会明确报告。

## 参数有效期

读取 UI 快照每 100 ms 一次，与蓝牙响应等待分离。每两个快速参数插入一个轮换的慢速参数，
避免慢速参数饿死；实际刷新率受适配器和车辆响应限制，不承诺固定 10 Hz。

| 参数 | 未更新多久后失效 |
| --- | --- |
| 车速、转速 | 2 秒 |
| 负载、节气门、歧管压力 | 4 秒 |
| 水温、电压、进气温度、机油温度 | 10 秒 |
| 油量 | 15 秒 |

明确的无效/不支持响应会使对应字段立即失效。蓝牙断开时所有实时字段立即失效。
支持位图中不存在的参数不持续轮询；连续 12 次无有效参数响应后重新等待车辆并查询能力。
演示模式仍由用户显式开启，和真实数据独立。

## 测试

```powershell
cmake --build build/protocol_tests --parallel 4
ctest --test-dir build/protocol_tests --output-on-failure
cmake --build build/ui_preview --parallel 6
build/ui_preview/dashboard_preview.exe docs/ui-preview/cyber
```

初次配置协议测试可复用本地 Zig 编译器：

```powershell
cmake -S tools/protocol_tests -B build/protocol_tests -G Ninja `
  -DCMAKE_MAKE_PROGRAM=C:/Espressif/tools/ninja/1.12.1/ninja.exe `
  -DCMAKE_C_COMPILER=C:/projects/odb2-car-display/build/host-tools/zig-windows-x86_64-0.13.0/zig.exe `
  -DCMAKE_C_COMPILER_ARG1=cc -DCMAKE_BUILD_TYPE=Debug
```

当前自动化覆盖 357 个分片/响应用例、10000 次随机输入、能力位图、参数换算和独立过期。
UI 检查 3196 个可见标签，包含新的三种连接状态，未发现缺字、裁切和标签重叠。
这些测试不代替真实适配器的服务发现、射频稳定性、休眠唤醒与车辆道路测试。

## 本机烧录记录：2026-09-06

- 使用原 `build` 和 IDF 5.5.5 构建，COM9 写入并校验成功。
- 应用 `display_board/build/odb2_dash.bin`：1081120 字节；1500 KiB 单应用分区剩余约 30%。
- 应用 SHA-256：`ca05d518d8ce2a956081c1f0a4730c5f60e6486ed363f3c9a7b1239b7e4bd1ad`。
- NVS 仍位于 `0x9000`、大小 `0x6000`；PHY 仍位于 `0xF000`、大小 `0x1000`；应用起点仍为 `0x10000`。
- 写入了扩容后的分区表，未执行全片擦除或清空 NVS。
- 固件已定义符号检查未发现 TWAI 符号；SDK 自动生成的芯片能力配置不代表启用了直连功能。
- 日志确认 GT911 ID 911、触摸注册、五页界面、NimBLE 控制器和主动扫描启动。
- 串口观察 65 秒，完成多次扫描启动，无异常重启或崩溃；日志位于 `build/elm327-ble-boot.log`。
- 此期间未连接到匹配适配器，因此尚不能确认实际 ELM 的 GATT 服务、配对、AT 交互或真车参数。

实机验收时需适配器在附近通电、手机诊断软件断开。记录适配器型号、蓝牙名称和
`UART ready`、`Adapter initialized`、`Vehicle ECU=...` 日志，随后核对数值、拔插重连和车辆熄火/唤醒。
物理触摸与实际屏幕显示也需要板上操作验收，主机渲染测试不能替代它们。

## 依据

- [ELM327 官方资料](https://elmelectronics.com/wp-content/uploads/2020/05/ELM327DSL.pdf)：AT 命令、CAF/H1 格式、ISO-TP 和提示符。
- [Espressif BLE 架构](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/ble/overview.html)：S3 与 NimBLE 的能力边界。
- 本机 IDF 5.5.5 的 `examples/bluetooth/nimble/blecent` 和 NimBLE GATT 头文件：本次实际编译使用的 API。
- [Nordic UART UUID](https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/doxygen/html/nus_8h.html)。
- [微雪板卡资料](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)：EXIO5 低电平保留 USB 路由。
