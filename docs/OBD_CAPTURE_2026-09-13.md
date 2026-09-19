# 2019 款凯美瑞 OBD 实车采集

2026-09-13 使用本项目 Android App 在真实手机上连接指定适配器并保存记录。手机仅用于采样和离线调试，最终仍由 ESP32-P4/C6 副屏直接连接适配器。本轮未修改或烧录板子固件。

## 可用文件与结果

推荐原始记录：`captures/2026-09-13-obdii/capture-1789287482215-bddf5bf6.jsonl`。

- App 0.1.3；有效采样窗口 60 秒；192,280 字节。
- SHA-256：`19928041ff6c0b61649b8a36e8f2c603cbe6bb5d57031b501ff142b5eab3d228`，与手机原件一致。
- 文件结构、连续序号和尾部计数检查通过；会话按时结束并释放蓝牙。
- 共 151 个有效实时参数样本，使用本项目 `display_board/main/elm327_protocol.c` 和 `display_board/main/obd2_decode.c` 在主机上解析和换算。

| 参数 | 有效样本 | 实测范围 |
| --- | ---: | --- |
| 车速 `01 0D` | 51 | 0 km/h（驻车） |
| 转速 `01 0C` | 52 | 682–711 rpm |
| 冷却液温度 `01 05` | 48 | 88℃ |

采集仍包含 103 次 `NO DATA`，另有一次读取在限时停止时未取得可解码结果。缺失值在解析表中留空，不补为零。本次不证明长时间连接稳定、所有副屏参数可读或界面实机显示已验收。

配套文件位于 `captures/2026-09-13-obdii/`：

- `manifest.json`：每份原始记录的文件哈希、模式、版本、结束状态和参数统计。
- `bluetooth-services.json`：实际发现的适配器蓝牙服务。
- `analysis-capture-1789287482215-bddf5bf6/diagnostic-transactions.csv`：每次请求、接收时间、有效性、ECU、载荷及由生产 C 代码换算的数值。
- 同目录 `firmware-replay.json`：生产解析器回放结果、原始响应和数值范围。
- 同目录 `events.csv`、`summary.json`：原始通信事件与文件完整性分析。

原始可用 JSONL 和参数 CSV 也已复制到手机 `Download/OBD-captures-20260913/`，再次核对原始文件 SHA-256 一致。App 私有目录内的全部原件仍保留。

`captures/` 为本地实车数据目录，已从 Git 中忽略。原始记录保留蓝牙通知分片及事务号；不应拿整理后的 CSV 替代原件。诊断记录没有 `frame` 行，`frames.csv` 为空是格式设计，不代表没有收到 OBD 响应。

## 设备与定位结论

- 手机：BVL-AN16，Android 16，通过 USB ADB 操作。
- 采集时用户提供车型为 2019 款凯美瑞；2026-09-20 用户进一步确认为 **2.5L 汽油版**，更正此前的“2.5T”描述。采集时用户确认车辆怠速／READY。
- 蓝牙：OBDII，`11:AC:88:B5:8C:EE`。
- 厂家版本响应：SHENZHEN YUMING ELECTRONICS CO., LTD.，B02-Z，V1.0.0，ELM 接口自报 V2.1。
- BLE 服务 FFF0，写 FFF2（Write），接收 FFF1（Notify，CCCD 2902）。额外 AE00 服务未用于采集。
- 自动协议响应 A6：ISO 15765-4，11 位 CAN、500 kbit/s；能力响应来自 7E8、7E9。

未执行 B02-Z 会话握手时，0100 能力查询有响应，三项实时参数持续返回 NO DATA。仅对齐 ATAT1/ATST64 或使用 7E0/7E8 定向寻址均未解决。用户提供 YMOBD 可读的对照后，检查本机安装 App 的初始化行为，发现该型号使用厂家版本挑战及会话响应；在本项目加入严格的型号识别和逐会话握手后，同一适配器的标准参数开始返回有效数据。

仅在完整识别钰明 B02-Z 且挑战格式正确时发送对应适配器会话响应；未知厂家挑战会中断。普通 ELM 不支持扩展版本查询时可继续标准流程。这里没有执行 ECU 写入、清码、诊断会话切换或控制指令。

握手前后都进行了 CAN 监听尝试；完成握手后，ATMA 仍立即返回 `OK\r\r>`，未进入持续监听，记录标记为中断。当前适配器没有通过本项目原始 CAN 监听验收，不能把已有 OBD 响应文件解释为全车 CAN 报文采集。

## 对副屏实现的影响

1. 标准 OBD 响应的生产 C 解析及数值换算已通过这份真实文件回放；这属于主机验证，不是板子 BLE 或显示实机验证。
2. 采集当时板子初始化尚无 B02-Z 厂家握手。2026-09-20 已在板端补齐逐会话识别和握手；握手命令为 19 个字符，加回车共 20 字节，命令长度检查与终止符缓冲已同步扩充。主机测试覆盖与 Android 相同的五组挑战向量以及畸形/未知挑战拒绝，仍需板子直连车辆验证。
3. 保留 NO DATA、缺失和超时状态；后续用这份文件检查缓存过期及界面无效值显示，不能用上次值长期冒充实时值。
4. 油量、电压、负荷等其余副屏参数不在本次三项轮询范围内，需后续按能力位图继续采集；DBC 与厂商私有 CAN 信号尚未验证。

定向请求地址的标准规则参考 [ELM327 官方手册第 42 页](https://www.elmelectronics.com/wp-content/uploads/2017/01/ELM327DS.pdf#page=42)。厂家的 B02-Z 行为以上述本机版本响应与实测记录为依据，不推广到其他自称 ELM327 的设备。

## 复现与检查

安装 `android-app/app/build/outputs/apk/debug/app-debug.apk`，关闭其他诊断 App 的连接，在「车况采集」选择同一 MAC，选择「诊断记录」、60 秒，定向选项不勾选。完成后导出原始 JSONL。

```powershell
python tools/analyze_android_capture.py captures/2026-09-13-obdii/capture-1789287482215-bddf5bf6.jsonl --output build/another-obd-analysis
cmake --build build/host-protocol --target replay_response elm_protocol_tests
ctest --test-dir build/host-protocol -R elm_protocol --output-on-failure
```

`replay_response.exe <PID十六进制>` 从标准输入读取适配器原始字节流，输出 ECU、载荷及可用的副屏参数值；例如 PID 0C 的响应必须保留末尾 `>`。回放输出现在复用生产 `obd_cache_apply`，没有在 Python 中另写数值公式。

本轮验证：Android 25 项 JUnit、8 项 Python 离线分析测试、Lint、Debug APK 构建通过；主机 C 协议测试通过；真实诊断文件导出校验与生产 C 回放通过。经典蓝牙 SPP、其他 BLE 服务型号、屏幕关闭后的录制、长时间稳定性和手机系统文件选择器导出不属于本轮实测结果。

手机已安装的 0.1.3 Debug APK SHA-256：`721fb4f465a546687eb27b0048d5fd9d68f1c21d7aab7109f33674bdb4ad5e53`。
