# 地图轨迹与 ATGM336H-5N

地图页已接入离线中国概览和独立串口 GPS。未接模块或尚未定位时也能打开地图，不依赖联网或 SD 卡。深浅主题共用同一套布局。

## 开发板接线

以下按 ESP32-P4 Function EV Board **v1.4** 的 J1 排针定义，当前工程 GPIO4/5 未被显示、音频、SD、C6 通信或串口控制台占用。

| ATGM336H-5N 扩展板 | P4 开发板 | 用途 |
| --- | --- | --- |
| VCC / VCCIN | J1 第 17 脚，3V3 | 供电 |
| GND | J1 第 20 脚，GND | 共地 |
| TX / TXD | J1 第 18 脚，GPIO4 | GPS 输出接 P4 UART1 RX |
| RX / RXD | 暂不接 | 以后需要配置模块可预留 GPIO5（J1 第 16 脚），当前固件不发送命令 |
| PPS | 暂不接 | 当前定位与轨迹不需要秒脉冲 |

依据：[Espressif 官方 v1.4 排针定义](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html#header-block)。连接前按开发板版本和排针标号核对，GPIO 编号与排针序号不同。

用户提供资料目录：`C:/baidunetdiskdownload/ATGM336H-5N模块资料`。

- `使用手册/ATGM336H-5N模块使用手册.pdf` 第 2 页标注扩展板供电 3.3–5V，第 5 页标注默认 9600 波特率，第 6 页给出真实 GNRMC 示例。
- `原理图/ATGM336H-5N原理图.pdf` 第 1 页显示 VCCIN 经 RT9193 稳压至 VCC3.3；TXD/RXD 引出，JP1 依次标有 VCCIN、GPS_TXD、GPS_RXD、GND、PPS。
- `使用手册/ATGM336H-5N模块数据手册.pdf` 区分了裸模块的 2.7–3.6V 供电范围。上述接线统一采用 3.3V，串口使用 3.3V TTL 电平。

接好天线后在开阔处等待有效定位。冷启动并非立即出坐标；状态栏箭头和地图侧栏会随有效数据变色。

## 固件行为

- 默认 UART1、RX=GPIO4、9600、8N1、无流控，只接收。可以在 `idf.py menuconfig → Vehicle P4 services` 修改 `DEVICE_GPS_RX_GPIO` 和 `DEVICE_GPS_BAUD`，重新编译生效。
- 接收 GN/GP/BD 等 talker 的 RMC 报文，校验 XOR checksum、字段范围和 A/V 有效状态；将度分坐标转换为十进制度，将节转换为 km/h。模拟/手动/无效模式不进入轨迹。
- 只由 UART 任务发布定位快照；LVGL 定时器每 100 ms 检查状态变化。定位数据不从 CAN/OBD 推断。
- 开机无定位显示中国概览。有效定位后自动跟随；拖动退出跟随，点击「跟随位置」恢复。支持加减缩放、全国视图、隐藏/显示轨迹。
- 收到 V 无效定位即撤下当前位置；10 秒没有有效更新也失效。由有效转无效时自动回到中国概览，已记录轨迹仍保留。
- 显示经纬度、GPS 速度、轨迹累计里程及点数；缺失速度显示 `--`。普通 OBD 仪表数据的有效性独立处理。
- 轨迹在内存中保留最近 512 个点，移动至少 5 米才追加。定位中断或大幅跳点分段，分段间不画连线、不累计距离；连续定位静止时不会定期制造新轨迹点。粗略异常过滤不是道路匹配或里程计校准。
- 轨迹里程为本次开机累计的有效段距离；512 点覆盖后累计距离继续增加。当前未实现轨迹持久化、历史回放或 GPX 导出，重启清空。
- 串口 `status` 包含 `gps enabled/error/positioned/rmc_sequence/age_ms`，方便接线后检查；不输出坐标，也不新增位置上传。

## 离线地图范围

内置地图是省级区域概览，具有轮廓、省界、标签和比例尺；**没有街道、卫星影像、路线规划或导航功能**。大比例尺下主要查看相对位置和轨迹，不会自动下载细节瓦片。

采用 Natural Earth 1:50m 数据，坐标为 WGS84，与 NMEA 经纬度一致，再统一进行 Mercator 投影；没有混用国内在线地图的偏移坐标。按原始数据概化边界，不应作为精密测绘底图。地图几何裁剪到 65–146°E、4–61°N 的概览范围。

来源与许可：[Natural Earth 数据说明](https://www.naturalearthdata.com/features/)、[公共领域许可](https://www.naturalearthdata.com/about/terms-of-use/)。

生成工具：`tools/generate_map_data.py`。生成后的 `map_geometry.c/.h` 随固件保存；普通编译不需要下载数据。重建时，将官方仓库的以下文件放进 `build/map-sources/`，运行 `python tools/generate_map_data.py`：

- [ne_50m_admin_0_countries.geojson](https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_admin_0_countries.geojson)，SHA-256 `3e458fc036ad0a66411f2c1e6cac49c5d7bfb81cb1123bc513b22511a2b7fdeb`
- [ne_50m_admin_1_states_provinces.geojson](https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_admin_1_states_provinces.geojson)，SHA-256 `69a0e06e640b2d505858ae1cb63034e4677f3000b35a98e16312932b98c426b9`

几何共 10,911 个顶点、270 个环，约 94 KB；724×420 RGB565 图层约 594 KiB，使用 PSRAM。地图前后台各一个图层，连同模型、快照和工作区约 1.3 MiB。页面离开时取消绘制作业、删除控件，保留最后一帧和固定工作区；状态未变时再次进入直接复用缓存，避免反复分配大块内存和重画。地图只在定位变化或操作时请求重画，未显示页面时只更新模型。

绘制采用按扫描行维护的活动边表，避免每一行重新遍历所有多边形顶点；地理投影仍保留双精度，像素交点使用预计算的单精度斜率。打开页面只建立控件及加载提示，随后由独立 LVGL 定时器分批处理，单次计算以 4 ms 为让出目标，并设固定步数上限；这不是硬实时保证，诊断记录实际最长批次。完成后才切换前后台图层，未完成或过期图像不显示。切页、主题变化或新的缩放操作会取消旧任务；GPS 更新合并到下一份模型快照。所有控件和绘制作业都由同一 LVGL 线程操作。

本地串口可用 `map open` 打开地图并报告函数耗时（微秒），用 `map status` 查看是否绘制中、完成帧数、缓存命中、最长批次、上一帧计算完成时间和 LVGL 最小空闲栈字节数。这些是软件耗时，不等同于手指触碰到液晶可见变化的端到端延迟。

## 验证与未验证项

`tools/protocol_tests/test_gps_map.c` 覆盖资料中的 GNRMC 原文、分段串口输入、校验错误、字段越界、V 状态、无 UTC 冷启动、缓冲溢出恢复、32 位计时回绕、定位过期、轨迹分段、点数上限及深度缩放时的画布边界。

`tools/ui_preview/preview.c` 覆盖深浅主题和现有配色、地图缩放/拖动/回到全国、轨迹显隐、跟随和失去定位；测试轨迹仅存在于主机测试程序，设备固件不会生成模拟坐标。

模块尚未接线，因此 UART 电气接收、真实搜星、触屏拖动流畅度与定位精度仍需实板验证。随后已按用户要求烧录至 COM28 / ESP32-P4 rev1.0，四镜像校验通过；45 秒观察确认显示、触摸和 GPS UART 初始化，两次状态查询中 GPS 驱动无错误、尚无定位报文，无崩溃或再次重启，串口已释放。烧录证据在 `build/flash-map-gps-20260920/`。

2026-09-20 验证结果：9 组 C 测试通过；UI 检查 9,277 个标签、0 错误；ESP-IDF 5.5.5 编译通过。
应用 `display_board/build/odb2_dash.bin` 为 6,171,728 字节（`0x5e2c50`），SHA-256
`1362770c8195d3312966ac5ed590f164f17eaa8eaaf2dd0bd45e872bf06d9e81`。
构建记录：`build/map-firmware-build.log`、`build/map-ui-preview.log`；预览图见 `docs/design/cabin/implemented-map-*.png`。

### 2026-09-20 地图打开响应修复

原先 `map_panel_create()` 在点击回调内同步绘制完整地图，导致 LVGL 不能及时处理后续输入。现已改为上述分批绘制和缓存方案。主机 40 个对照视图与原实现的地图像素一致；同次运行的计算时间约为原实现 94 ms、新实现 16 ms，仅作电脑端算法对比。另有逐步绘制与同步绘制一致性、中途取消后重新开始、绘制中切页、主题替换、缓存复用和连续缩放合并检查。

实板首轮验证还发现 `taskLVGL` 在地图文字格式化的 `_dtoa_r` 中触发栈保护。反汇编确认 `service_tick()` 的局部状态、设备列表等快照共占栈 4,544 字节；已改为由该定时器独占、从 PSRAM 一次分配的快照，回调自身栈帧降至 112 字节，保留默认 7,168 字节任务栈。首次失败的日志及对应 ELF 保存在 `build/flash-map-response-20260920/`，不计为通过。

12 组 C 测试通过，实际 LVGL 深浅主题与交互检查覆盖 10,427 个标签，0 错误。ESP-IDF 5.5.5 编译通过；应用 6,066,144 字节（`0x5c8fe0`），SHA-256 `edaf522f4f42c8012f6edfc262fc52c036b1672572452d6deccd9f75175d828a`。主机测试和构建证据在 `build/map-response/`。

修正版随后烧录到同一 COM28 / ESP32-P4 rev1.0，四镜像摘要校验通过。45 秒观察仅一次启动，ELF 前缀 `23857b0e0` 匹配，无 panic、断言、看门狗异常或额外重启。第一次地图页面切换函数耗时 137,138 µs，底图分批完成耗时 1,472 ms（此时小智正在后台建立连接）；两次缓存切换分别为 112,187 和 104,588 µs，绘制作业数始终为 1。最长单批回调 28 ms，包含抢占和最终标签更新，4 ms 只是计算循环让出目标。LVGL 最小空闲栈 3,848 字节。

13 条诊断/切页命令中 12 条返回 ESP_OK。首次绘制期间的一次 `map status` 未能在 100 ms 内取得 LVGL 锁，返回 ESP_ERR_TIMEOUT；下一次查询成功得到绘制中状态，其后查询确认完成和缓存复用。因此自动报告保留 `commands_successful=false`，不能声称全部串口检查通过。该超时没有触发重启，也未影响后续页面切换。证据为 `build/flash-map-response-fixed-20260920/{flash.log,boot.log,verification.json}`。观察后串口已释放；仍未测量实体手指至液晶画面变化的延迟。
