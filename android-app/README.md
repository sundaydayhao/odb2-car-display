# 车况采集 Android App

0.2.0 开发验证版，Android 8.0+（API 26）。手机直接连接 BLE ELM327，限时采集并导出到文件。
与 P4 仪表固件分别运行，不依赖副屏 AP、云服务器或手机热点；没有 INTERNET 权限。
手机用于临时采样和离线调试，最终产品仍由副屏直接连接 ELM327。

2026-09-13 已通过真实 B02-Z 适配器取得 2019 款凯美瑞的车速、转速、水温响应；详情见 [实车采集记录](../docs/OBD_CAPTURE_2026-09-13.md)。

## 使用

1. 驻车操作，车辆上电，将适配器插到诊断口。在 P4 0.2.2 的「设置 → 蓝牙设置」关闭车辆连接（旧固件可关闭副屏），并断开其他诊断 App，释放适配器连接。
2. 安装 `app/build/outputs/apk/debug/app-debug.apk`，打开「车况采集」。这是开发签名 APK，尚非商店发布包。
3. 在「采集」点击「选择适配器 → 扫描附近设备」，允许蓝牙相关权限，选择自己的设备连接。Android 11 及以下扫描需要位置权限及系统定位开关；Android 12+ 使用附近设备权限。通知权限在「设置 → 连接与权限 → 采集通知」管理。
4. 在「采集方案」选择方式和 30 秒／1 分钟／3 分钟／5 分钟时长，开始采集；也可随时「停止并保存」。计时从初始化成功后开始，初始化另有超时。方案会保存供下次使用，默认标准参数记录。
5. 结束后进入「记录 → 查看记录 → 导出文件」，通过系统文件选择器保存 `.jsonl`。错误或中断也会保存；未封存的 `.part` 可照常导出分析。
6. 再次采集需重新连接。手机完成并断开后，在副屏蓝牙设置里重新开启车辆连接（或启动已关闭的副屏）。

### 0.2.0 界面与设置

- 三个独立页面：采集、记录、设置；手机设置按功能分组，宽度达到 720 dp 时使用左分类／右详情布局，内容宽度有上限。
- 浅色、深色、跟随系统；采集方案、时长、CAN 过滤、定向诊断和屏幕常亮偏好持久保存。更换协议清除旧过滤，防止 11/29 位地址混用。
- 手动连接上次设备；搜索超时、无设备、权限拒绝、系统蓝牙关闭均有反馈。不会自动后台搜索或循环重连车辆。
- 接入服务的连接／初始化／记录／结束状态与计时。诊断模式显示接收通知数（含初始化），不冒充已解码 CAN 帧或有效 PID 样本数。
- 记录列表在后台读取有界头尾元数据，显示真实日期、大小、方式和会话结果；封存的 `.jsonl` 也可能显示“需检查”，不会仅按扩展名宣称成功。

2026-09-14 验证：32 项 JUnit 通过；Android 30 本地模拟器实际启动并检查浅色／深色首页、手机设置、1920 × 1200 大屏分类设置和历史真实记录的显示。模拟器中的记录来自项目既有 2026-09-13 文件，**不是本轮新抓包**。当前未发现 USB 真机，未安装到用户手机，未验证本轮真实 BLE 连接及后台采集。

本版 APK 仍使用开发签名；界面改版不代表已完成商店上架、车型兼容性和整机商业验收。

### 两种模式

| 模式 | 工作方式 | 输出用途 |
| --- | --- | --- |
| CAN 监听 | 手选协议 6/7/8/9，分别对应 11/29 位、500/250 kbit/s；可选精确 CAN ID 过滤；`ATMA` 监听 | 诊断口可见的经典 CAN ID、DLC、数据及原始通信 |
| 诊断记录 | 自动检测 CAN 诊断协议，先读 PID 支持位图，再轮询支持的车速/转速/水温 | 标准参数请求与响应，定位连接/响应问题；不计作原始 CAN 帧 |

监听模式初始化要求 `ATCSM1` 静默、`ATCFC0` 禁止自动流控、`ATCAF0` 关闭 CAN 自动格式化、`ATH1`/`ATD1` 输出头部/DLC。
不支持这些指令会明确中断，不会自动退成主动诊断。不执行清码、刷写、执行器控制或私有 PID 扫描。
诊断模式会发送标准读取请求和 ISO-TP 流控，不能视为被动监听。
可选「诊断兼容：定向读取已发现 ECU」：仅对能力响应中已发现的 11 位 OBD ECU 使用对应请求地址和接收过滤；默认仍为功能广播。
初始化识别到钰明 B02-Z 的有效会话挑战后，会完成该适配器的厂家握手。握手失败中断并保留原始响应；不会把固定挑战响应当作长期密钥复用。
普通 ELM 的 `AT+VERSION` 不支持响应可跳过；其他带挑战的厂家型号不自动套用 B02-Z 握手。

首版支持 FFF0（FFF2 写/FFF1 收）、FFE0（FFE1 双向）、Nordic UART 三种 BLE 串口服务，真实发现 CCCD，支持 Notify/Indicate 和有应答/无应答写入。
暂不支持经典蓝牙 SPP、Wi-Fi ELM、任意自定义 UUID、App 内输入配对 PIN、CAN FD、K-Line/J1850。
需要系统配对的适配器先在系统设置中配对；克隆适配器兼容性需逐型号验证。

### 记录的边界

- 手机看到的是适配器输出，车辆网关可能屏蔽其他总线；没有报文不证明总线空闲。
- ELM 带宽与内部缓冲有限；遇到 `BUFFER FULL`、CAN/RX 错误、超时、断连或手机队列溢出会中断并标注不完整。无法检测的总线丢帧仍可能存在。
- 时间戳是手机收到 BLE 通知的单调时钟微秒值；同一通知中的多帧可能同时间，不是硬件 CAN 时间戳。采样周期、延迟分析只能按手机可见结果解释。
- 正常结束发送停止字符并等待 ELM 提示符；2 秒内没有确认就记为中断。异常断连或进程被杀无法保证适配器已停止监听，必要时重新连接/重新给适配器上电。
- 每次最多 16 MiB，手机接收队列最多 256 KiB；新建会话前检查已有文件不超过 64 个/128 MiB。一场采集最多可将总量增加 16 MiB，不自动删除旧记录。
- 文件每 2 秒同步到存储。突然断电/系统杀进程可能损失最后一段，`.part` 保留已写部分。前台服务和有时限的 CPU 唤醒锁用于完成限时记录，厂商后台管理仍需真机验收。
- 文件只存 App 私有目录，不申请通用存储权限，不启用系统备份；只有用户主动导出。卸载会删除未导出的 App 内文件。

ELM 指令和监听/缓冲限制依据 [ELM327 官方手册](https://elmelectronics.com/wp-content/uploads/2020/05/ELM327DSL.pdf)；
Android 权限及前台服务依据 [蓝牙权限](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions)、[connectedDevice 前台服务](https://developer.android.com/develop/background-work/services/fgs/service-types#connected-device)。

## 离线分析

项目根目录运行（Python 标准库，无需联网）：

```powershell
python tools/analyze_android_capture.py C:/captures/capture-example.jsonl --output build/phone-analysis-01
# 未封存片段也可输入；每次选择新的输出目录，已有结果不会覆盖
python tools/analyze_android_capture.py C:/captures/capture-example.part --output build/phone-analysis-02
```

- `frames.csv`：经过 ID/DLC/数据长度校验的 CAN 帧、手机时间、相对时间。
- `events.csv`：发送指令、原始通知十六进制、逐行文本、完整响应、未解析行等，保留事务号。
- `summary.json`：每个 CAN ID 数量/首尾时间、头尾元数据、序号/截断/计数问题。

`recording_intact` 只表示文件结构、序号与计数通过检查；`adapter_session_completed` 来自 ELM 会话结束标记。
`bus_loss_unknown` 始终为 true，两者都不表示无损采集全车总线。
工具退出码 0=文件检查和会话结束标记通过，1=中断/损坏但已尽可能恢复并输出，2=无法读取或格式不支持。

手机格式为 `vehicle-capture/1`：每行 JSON 均含 `kind/sequence/monotonic_us/transaction`。
`header` 保存模式、协议、过滤、请求时长、版本、UTC 与单调时钟锚点；`rx` 保存原始通知及通知序号；
0.1.1 起头部还包含所选蓝牙 MAC 和实际服务清单，0.1.2 起包含诊断寻址方式。最近一次服务清单也保存在 App 私有目录的 `last-bluetooth-services.json`。
`frame` 保存 ID/extended/DLC/data；`end` 保存结束原因、已解析/未解析/通知计数和传输结束状态。
这与 P4 的 SD 日志格式不同，不能直接传给 `tools/replay_recording.py`。目前未实现 DBC 信号解码、私有协议解释或手机内波形回放。

## 构建与检查

本机使用 JDK 21（Android Studio JBR）、Gradle Wrapper 8.7、Android Gradle Plugin 8.5.2、Android SDK Platform 34 / Build Tools 34.0.0。
源代码 Java 17；`compileSdk/targetSdk=34`。Android Studio 可直接打开本目录，SDK 路径放本机 `local.properties`（已忽略，不提交）。
当前目标用于开发侧载，正式上架前另行处理当期目标 API 要求、发布签名和车型/适配器验收。

```powershell
# 从仓库根目录执行；按实际安装位置设置 JAVA_HOME 和 Android SDK
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME='C:\Users\zhang\AppData\Local\Android\Sdk'
.\android-app\gradlew.bat -p android-app :app:testDebugUnitTest :app:lintDebug :app:assembleDebug
python -m unittest discover -s tools -p test_android_capture.py -v
```

APK：`android-app/app/build/outputs/apk/debug/app-debug.apk`。
2026-09-11：Debug APK 构建通过，17 项 JUnit、8 项离线分析测试通过，Lint 无问题。
2026-09-13：0.1.3 构建通过，25 项 JUnit 通过，Lint 无问题；真实手机完成 60 秒诊断记录、USB 导出及 SHA-256 一致性检查。生产 C 解析器回放取得 151 个有效参数样本；过程中仍有 NO DATA，详见实车记录。
生产协议状态机由 JUnit 直接测试，覆盖分片边界、11/29 位解析、ACK/提示符乱序、静默初始化、能力位图、错误、限时停止和超时。
Python 使用明确标注的合成文件检查离线恢复，不作为真实车辆抓包证据。

真机验收仍需：蓝牙权限拒绝/撤销、三类 BLE 服务、车辆协议与过滤、屏幕关闭后限时停止、手动停止/断连、文件导出、满容量、不同适配器吞吐。
B02-Z V1.0.0 本次在完成厂家握手后仍对 `ATMA` 立即返回 `OK` 和提示符，没有持续监听报文；未验证通过原始 CAN 监听，也不能据此声称支持全车总线抓包。
