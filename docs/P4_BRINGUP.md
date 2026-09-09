# P4 v1.4 设备与联调

当前目标：ESP32-P4 Function EV Board **v1.4**，原配 7 寸 EK79007 / 1024×600 MIPI-DSI 屏、GT911 触摸、MIPI-CSI 摄像头。
固件版本 0.2.0，ESP-IDF **5.5.x**；本机验证版本 5.5.5。不是 P4X / 芯片 rev3.x 板型。
本文件描述新实现；旧 S3 烧录记录与 Linux/CarPlay 规划不代表此固件能力。

## 1. 实现边界

| 模块 | 当前实现 | 尚待验证或后续工作 |
| --- | --- | --- |
| 仪表 | 官方 BSP 屏幕/触摸；五页、三配色、中文、手动切页；设备中心 | 实板显示方向、触摸边缘与长时间刷新 |
| 车辆 | P4 NimBLE + C6 Hosted VHCI，ELM327 BLE 自动连接，10 项标准参数 | 真实适配器、手机热点与语音并发测试；更多车型字段 |
| 小智 | 乐鑫 `esp_xiaozhi 0.1.2`；绑定信息、会话、16 kHz 单声道 Opus、麦克风与扬声器 | 真实账号绑定/服务端协议、声学调试；未实现唤醒词/AEC/全双工打断 |
| 摄像头 | `esp_video`，SC2336 / OV5647 探测，RGB565 本地预览，320×240 画布内等比例显示，最高 8 次/秒 UI 更新 | 附带摄像头实际型号/方向/色彩；无录像、视频上传、疲劳检测 |
| 记录 | SD 卡 JSONL；原始指令/通知分片、事务结果、解析快照、丢失标记；有界队列 | 实卡拔插、断电恢复与掉电尾部损失测试；不支持运行中热插卡恢复 |
| 联网 | 手机 2.4 GHz 热点 STA、退避重连、网络校时、串口持久配置 | 热点隔离与实际网络质量；没有配网手机 App |
| 上传 | 用户启用后 HTTPS 上传封口文件；摘要匹配 ACK 后删除；失败重试 | 云端生产服务、每台设备独立凭据和隐私流程 |
| 更新 | 手动检查/确认，HTTPS + RSA 签名清单 + SHA256 + 板型/项目/版本校验；双槽与首次启动自检 | 真板断网/断电/回滚验收；量产安全启动、密钥保护和发布审批 |

没有独立导航、CarPlay、蓝牙音箱 A2DP、音乐库或驾驶员疲劳算法。C6 只支持 BLE，不能连接仅支持经典蓝牙 SPP 的 ELM327。
摄像头和麦克风不会开机自动采集；麦克风只在本地按住语音按钮的对话期间向小智服务发送音频，最长 30 秒。
音视频不写入车辆记录，也不发给诊断上传服务。小智返回的固件 URL 和远程系统命令不执行。

## 2. 硬件核对

- 屏幕使用原配转接板，DSI 接口连接 LCD；RST_LCD 接 GPIO27，PWM 接 GPIO26；按官方要求供电、共地。
- 触摸使用 BSP 的 I2C 总线：SDA7、SCL8。移除了 S3 的 CH422G、RGB 并口和手写 GT911 初始化。
- MIPI 摄像头接 CSI，不要将 LCD/摄像头排线互换；原配 LCD 与摄像头排线方向不同，断电后插拔。
- ES8311：I2S MCLK13、BCLK12、WS10、DOUT9、DIN11；功放使能 GPIO53。连接合适的扬声器后才能听到回复。
- SD 卡用板载卡槽，数据线 39/40/41/42、CMD44、CLK43，SDMMC slot0。使用已格式化的兼容 FAT 卡；固件不会格式化卡。
- C6 通过 SDIO slot1：CLK18、CMD19、D0/1/2/3=14/15/16/17、C6 reset=54。不要在这些引脚接外设。
- v1.4 的 **USB-to-UART / CP2102N** 口用于 P4 烧录、日志和配置，控制台为 UART0；不要套用新版板子的 USB Serial/JTAG 接口说明。C6 使用板上的独立编程连接器，不能把 P4 的串口号当成 C6 串口。
- 开发板不是车规电源模块，不能直接接车辆 12 V。真车测试需独立稳压、反接及瞬态保护，不代替原车仪表。

参考：[官方 v1.4 板卡文档](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html)、
[BSP](https://components.espressif.com/components/espressif/esp32_p4_function_ev_board/versions/5.2.2)、
[乐鑫小智组件](https://components.espressif.com/components/espressif/esp_xiaozhi/versions/0.1.2/readme)。

## 3. 构建与首次烧录

```powershell
.\tools\idf55.ps1 build
.\tools\idf55.ps1 menuconfig
# 确认 P4 的实际串口后，将 COMx 替换为它
.\tools\idf55.ps1 -IdfArgs @('-p', 'COMx', 'flash', 'monitor')
```

始终使用根 `build`。原来的 `build` 已切换到 P4；项目主动拒绝 S3 和非 5.5.x IDF。
环境包装器使用本机真实存在的 `C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1`。
分区为 16 MB：NVS 0x9000、otadata 0xf000、ota_0 0x20000/6 MB、ota_1 0x620000/6 MB、预留存储约 3.875 MB。
首次必须整套 `flash` 写入 bootloader、分区表、otadata 和应用，不用旧 S3 分区，也不能仅 `app-flash`。
持久日志在 SD 卡，预留的内部存储分区暂未挂载使用。NVS 异常不会静默擦除用户凭据。

C6 必须运行匹配的 ESP-Hosted **2.12.11** 固件，启用 Wi-Fi + BT controller-only / RAM HCI，通过同一条 SDIO 向 P4 提供 VHCI。
板上出厂固件不一定匹配，单独升级 P4 不能证明无线链路可用：

```powershell
.\tools\build_c6.ps1
```

脚本复制已锁定版本的官方协处理器工程到 `build/c6-source`，产物在 `build/c6`，不修改受管理组件，也不创建 `projects`。
通过独立 C6 编程口进入下载模式后，使用 `build/c6/flash_args` 或该工程的 `idf.py -p COMy flash` 写入 **C6**。
本次实现没有自动烧录 P4 或 C6，也没有开启不可逆 eFuse 安全设置。

## 4. 手机热点与小智

USB 串口监视器出现 `vehicle>` 后：

```text
wifi "手机热点名称" "热点密码"
status
```

热点应启用 2.4 GHz 兼容模式。设置保存在 NVS，不写入 SD 记录。串口会回显输入，配置凭据时不要保存或分享完整终端日志；命令历史不持久保存。
联网并校时成功后，按住底部「小智语音」。首次会话可能先显示绑定码，在小智服务的设备管理中完成绑定后再按一次。
如果握手期间已经松开按钮，不会在连接成功后偷偷开始录音，需要重新按住。说完松开，设备等待并播放回复。
只显示确定的中文状态，不把任意服务端文本塞进裁剪字库。若服务端要求组件尚未实现的额外激活挑战，需要后续对接，不能视作已绑定。

设备中心可查看绑定码与完整错误状态；「车内视野」打开本地预览，关闭弹窗停止采集。
串口亦支持 `voice start` / `voice stop`、`camera on` / `camera off`、`record seal`，用于台架测试。

## 5. 本地 HTTPS 联调

手机只提供互联网出口，不需要手机 App 来转发文件。局域网测试时，电脑和开发板须彼此可达；有些手机热点禁止客户端互访。
下例 `192.168.1.100` 只是占位，替换为板子可访问的电脑 IP；证书 SAN 必须包含该 IP。

```powershell
$python = 'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe'
& $python tools\lab_server.py --root build\my-lab init --host 192.168.1.100
& $python tools\lab_server.py --root build\my-lab serve --bind 0.0.0.0 --port 8443
```

依赖 `cryptography` 和 `esptool`，本机 IDF Python 环境已具备。工具默认只监听 127.0.0.1，只有显式 `--bind` 才对局域网开放。
`private` 保存 CA 私钥、服务器私钥、升级签名私钥和 `token.txt`；`trust` 只有 CA 证书和升级公钥。
只把 `trust` 的绝对路径填入 menuconfig → Vehicle P4 services → DEVICE_TRUST_DIR，重新编译/首次 USB 烧录。
不要将私钥目录发给用户、打进固件或提交 Git；生产签名材料也不要放在会被 fullclean 删除的 build 目录。
默认配置路径为空，升级入口关闭。证书不会通过跳过 TLS 校验来“兼容”。

在设备串口填写自己的服务地址和 token，再明确开启上传：

```text
endpoint upload https://192.168.1.100:8443/v1/reports
endpoint ota https://192.168.1.100:8443/v1/ota/manifest
token <private/token.txt 中的值>
upload on
record seal
upload now
```

`upload off` 禁止后续上传；`now` 不会绕过关闭开关。上传完成后服务端文件在 `reports/<device-id>/*.jsonl`。
每个请求有设备 ID、报告 ID、SHA256、长度；相同 ID/相同内容重传幂等，不同内容冲突拒绝。
ACK 的报告 ID、摘要和长度全部匹配，设备才删除本地文件。服务器默认报告总量 256 MB；容量满拒绝新文件，不静默覆盖。
文件读取限额、鉴权、路径约束和 HTTPS 已实现，但该工具仍是本地单进程测试服务，不应直接暴露到公网作为量产平台。

## 6. 发布与安装

修改代码后提高根 CMakeLists 的三段数字版本（例如 0.2.0 → 0.2.1），构建，再发布实际应用二进制：

```powershell
.\tools\idf55.ps1 build
& $python tools\lab_server.py --root build\my-lab publish build\odb2_dash.bin --base-url https://192.168.1.100:8443
```

发布工具读取二进制中的真实项目名、IDF 和版本，生成内容寻址的固件文件及 RSA-3072 / PKCS#1 v1.5 / SHA256 签名清单。
清单 payload 包含 schema、target、board、project、idf、version、size、sha256、url；签名与 payload 均用 Base64 包装。
设备拒绝错误板型、旧版本/同版本、非 HTTPS、超分区大小、签名或镜像摘要不符；不跟随重定向，也不跨域转发 token。
当前版本等于服务器版本时不会重装。OTA 仅更新 P4，不升级 C6 协处理器。

```text
ota check
ota install parked-confirmed
```

UI 中「检查更新」不安装；「安装更新」还需二次确认。只有实际采集的有效车速为 0 才允许安装，演示车速与缺失车速均不算驻车。
安装过程中持续检查车速和联网状态；开始 OTA 后停止摄像头、暂停语音并让上传任务让路。
台架无车辆时可显式打开 `DEVICE_BENCH_MAINTENANCE`，仅通过串口 `ota install bench-confirmed` 测试；正式固件保持关闭。
写入非活动槽，验证后才切换启动分区；新固件连续 30 秒检查 UI 心跳、内部空闲内存和堆完整性，通过后确认版本，否则请求回滚。
外置适配器、联网、SD 卡、语音账号和摄像头不是首次启动必须存在的条件；该自检不等于完整硬件验收。
回滚仍须真板验证。签名清单不是 Secure Boot、Flash Encryption 或硬件防降级的替代品，量产前另行设计密钥与安全生产流程。

## 7. 记录与回放

SD 路径 `/sdcard/vehicle`，`veh-<boot16hex>-<counter8hex>.part` 写入中，封口后改名 `.jsonl`。
默认最多 16 个自有文件，每文件约 256 KiB 或 60 秒封口；因此低数据率下约 16 分钟未上传就会占满文件数。
这些是开发阶段保守上限，可在 menuconfig 调整文件数/大小；不应把它当全天候循环行车录像。
满额后停止追加、报告丢失，不覆盖未确认文件；每 2 秒 flush/fsync，不保证突然断电时最后 2 秒落盘。
重启尝试恢复完整 JSON 行并标记 recovered；缺失文件头保留为 `.broken` 供人工检查。存储卡损坏可能阻止自动恢复。

原始记录包括 ELM ASCII 指令发送尝试、通知 ID/分片偏移/总长度、连接代次、事务结果；`metrics` 是带独立有效性的 UI 数值快照。
`tx` 表示提交尝试，不代表车辆执行确认；通知和结果中的时间为单调微秒，文件头给出可选 UTC 映射。
这是 **诊断请求/响应记录器，不是全车总线嗅探器**。当前没有 VIN 查询、ATMA、清码、写 ECU 或自动试探私有服务。
无法从没有记录到的数据推导任意车型私有参数，后续仍需经过验证的车型配置与标准支持位图策略。

```powershell
.\tools\run_host_tests.ps1
& $python tools\replay_recording.py <报告1.jsonl> <报告2.jsonl> --decoder build\host-protocol\replay_response.exe
```

回放工具按 boot/连接代次/事务组织数据，检查通知缺片、序列缺失、失败事务和恢复尾部，调用与固件相同的 C 解析器重放完整 Mode 01 响应。
跨文件事务请同时提供相邻文件，缺失不能被当成解析成功。只有原始输入确实存在时，软件修复才可能离线复现车型问题。

## 8. 验收顺序

2026-09-08 软件验证：IDF 5.5.5 的 P4/C6 构建通过，实板修正后的 P4 应用大小 2600720 字节；
另行验证带 OTA 公钥构建。2 项 C 测试、10 项本地 HTTPS/签名/回放测试通过；
1024×600 同源 UI 检查 4808 个标签无错误并人工检查截图。测试图中的车辆数值及相机色条为测试输入，非实板采集。
随后已在 COM28 完整烧录 P4 v0.2.0。首次上板修正了两个启动问题：Hosted 传输池启用
`ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`，以及在 BLE 初始化完成后再启动 Wi-Fi，避免两个任务同时复位/初始化共享 C6 总线。
修正后持续观察 65 秒，没有崩溃或重启；1024×600 屏幕与 GT911 初始化成功，BLE 持续扫描，串口 `status` 返回正常。
物理触摸、画面、音视频和真车连接仍待人工验收。板载 C6 报告版本不匹配（Host 2.12.0 / Co-proc 0.0.0），
需要核对并升级配套 C6 固件，本次没有烧录 C6；存储卡初始化返回超时，热点尚未配置。
日志保存在 `build/flash-com28-final-boot.log`。以下完整硬件验收仍需继续，不能用短时启动观察替代。

1. 独立供电台架：屏幕、触摸全区域、手动导航、启动稳定性。
2. 核对 C6 版本，先连接热点，再同时连接 BLE 适配器，确认断开后数值失效。
3. 绑定小智，录音/播放、握手中松手、断网重试、长按超时测试。
4. 摄像头开关/重复打开/拔除启动、方向/颜色检查，再做音频并发与内存监测。
5. SD 记录、容量满、重启恢复、原始流回放、上传 ACK 丢失后重传测试。
6. 台架签名升级：同版本、错误签名、错误摘要、错误板型、断网、掉电、新版自检失败回滚。
7. 最后驻车验证真实车辆数据；驾驶中不操作配置或安装更新。
