# CABIN · P4 双主题界面

1024×600 触屏。设计工具为 **Inkscape 1.4.4**，`cabin-ui.svg` 保留八个可编辑图层：
驾驶、音乐、声音设置和 FM 电台，各有深浅两版。`cabin-overview.png` 是整套设计预览。
图中数值和播放列表用于设计演示，不代表当前车辆数据或 SD 内容。

## 视觉规则

- 石墨底色 / 雾白底色，低饱和青色强调；警告和错误继续用独立状态色。
- 主区域外边距约 24–28 px，卡片间距约 20 px，主要圆角 20–24 px。
- 大数字突出车速，单位和次要状态降低视觉权重；底部导航改为统一线条图标。
- 音乐播放器居左、列表居右；圆形主播放键与上一首、下一首、停止区分层级。
- FM 左侧为大字频率、刻度和微调，右侧为六个常用频率；接收器未接入时提供重试连接并禁用搜台。
- 同一套卡片、图标、间距延伸到车况、胎压、行程、性能、地图轨迹和设置。
- 无数据仍显示 `--`；没有新增模拟进度条、虚构定位或假车况。

`album.svg` 是本地音乐的通用插画，不是读取到的唱片封面。`car.svg` 是胎压页车型无关的示意图。
`icons/*.svg` 为本项目绘制的统一线条图标。Inkscape 导出后由 `ui_assets.c` 以 A8 / ARGB8888 图像接入 LVGL，
单色图标可随主题着色，不需要深浅两份大图片。控件和数据仍由原 LVGL 页面运行。

## 编辑与导出

先在 Inkscape 修改对应 SVG，再导出；`--render-only` 会保留绘图软件中的修改：

```powershell
python tools/design_dashboard.py --inkscape <inkscape.exe路径> --render-only
```

Python 需要 Pillow。省略 `--render-only` 会从脚本中的设计定义重新生成 SVG。
页面设计稿的布局调整需同步到 `dashboard.c`、`media_panel.c`、`service_panel.c`；图标和插画的修改可直接重导出。
UI 构建入口为 `tools/ui_preview`，实际 LVGL 截图输出在 `build/ui-cabin/`，不同于设计演示稿。
固件构建继续使用 `tools/idf55.ps1 build`；未改 SDK、分区和车辆读取逻辑。

本地绘图程序位于被 Git 忽略的 `build/host-tools/inkscape/`，来自
[Inkscape 官方 Windows 发行包](https://inkscape.org/release/inkscape-1.4.4/windows/64-bit/compressed-7z/dl/)，
下载文件 SHA-256 与官方公布值一致：`c4dbd64a92628abe7d7316c43f9325396e4d48417866027ee3d993d4f5b54c6a`。

## 验证记录（2026-09-20）

深浅主题的实际 LVGL 音乐页预览为 `implemented-music-dark.png`、`implemented-music-light.png`。
主机布局与交互检查通过：8,119 个渲染标签，0 错误；已检查驾驶、车况、胎压、行程、性能和音乐页渲染。
ESP-IDF 5.5.5 固件编译通过，应用大小 6,027,232 字节（`0x5bf7e0`），6 MiB 应用分区剩余约 4%。
应用文件为 `display_board/build/odb2_dash.bin`，SHA-256：
`db5ea805d06f0b2fed62f36c18a752e39831bf40b0458f2195cb951c2fb5c9d1`。
本次未烧录；触屏操作、音频和车辆数据仍需板端验证。

### FM 界面补充（同日）

`implemented-fm-dark.png`、`implemented-fm-light.png` 为实际 LVGL 主机渲染，收藏项来自交互测试。
三种配色的深浅主题均通过布局与交互检查：8,623 个渲染标签，0 错误。
覆盖选频上下限、微调、收藏去重、长按移除、队列拒绝、保存失败保留原值以及离开页面后保存完成。
最新固件编译通过，大小 6,034,096 字节（`0x5c12b0`），应用分区剩余约 4%；SHA-256：
`cb0d91bef8370cb029cbc4bd569a44b15f19fca767e72c43b3241cea214f31a1`。
随后按用户要求烧录至 COM28 / ESP32-P4 rev1.0，四镜像摘要校验通过；45 秒观察确认固件摘要匹配、
显示和触摸初始化、FM 页面打开命令成功，无崩溃或再次重启，串口已释放。
当时 NVS 收藏跨重启恢复尚未实板验证，FM 接收与音源切换驱动未实现；后续驱动接入见下方记录。

### 音乐页音量入口精简（同日）

移除播放器内的音量滑块、图标和百分比，音量统一在「设置 → 声音」调整，顶部扬声器仍为快捷入口。
深浅音乐设计稿和实际渲染已更新；现有交互与布局检查通过（8,583 个标签，0 错误），固件编译通过。
最新应用 SHA-256：`5385445c21aeba1de8b0f7f9c080ccb63207a4250fea09d6d226f03c40729b9f`。
随后已烧录至 COM28 / ESP32-P4 rev1.0，四镜像摘要校验通过。45 秒启动观察确认 ELF 前缀 `95d9bd11d`、
显示/触摸初始化和音乐页打开命令正常，无崩溃或再次重启，串口已释放；证据见 `build/flash-music-volume-20260920/`。

### 地图轨迹与独立 GPS（同日）

`implemented-map-dark.png`、`implemented-map-light.png` 为无定位时的实际 LVGL 中国概览。
`implemented-map-track-fixture-dark.png`、`implemented-map-track-fixture-light.png` 为主机测试轨迹，非真实车辆位置。
地图支持缩放拖动、全国视图、位置跟随和轨迹显隐；没有重复的内层页面标题。
已接入 ATGM336H-5N 的 UART1 RX=GPIO4 / 9600 / 8N1，接线依据用户提供资料和开发板 v1.4 官方排针定义。

9 组 C 协议/模型测试通过；布局和交互检查覆盖 9,277 个标签，0 错误，包括反复切换主题时画布尺寸的检查。
ESP-IDF 5.5.5 编译通过，应用大小 6,171,728 字节（`0x5e2c50`），6 MiB 应用分区剩余 119,728 字节。
应用 SHA-256：`1362770c8195d3312966ac5ed590f164f17eaa8eaaf2dd0bd45e872bf06d9e81`。
随后已按用户要求烧录至 COM28 / ESP32-P4 rev1.0，四镜像校验通过。45 秒启动观察确认 ELF 前缀 `c7ab1eae6`、显示/触摸和 GPS UART 初始化，无崩溃或再次重启，串口已释放。GPS 模块尚未接线实测，地图触屏操作待验证。功能边界、数据来源和验证说明见 [MAP.md](../../MAP.md)。

### RDA5807M 驱动接入（同日）

FM 页接入调谐、硬件搜台/停止、RSSI/立体声反馈、重试连接和全局音量，顶部状态随收听更新。
`implemented-fm-receiver-fixture-dark.png`、`implemented-fm-receiver-fixture-light.png` 是实际 LVGL 渲染，
频率与接收状态来自主机测试夹具，非实板接收到的电台。

10 组 C 协议/模型测试通过；主机交互与布局检查覆盖 9,553 个标签，0 错误。
新检查包括接收器缺席时重试、调谐请求等待/中断后的频率反馈、收听/搜台/停止派发、顶部图标快捷打开 FM，以及全局静音状态。
ESP-IDF 5.5.5 编译通过，应用大小 6,180,736 字节（`0x5e4f80`），6 MiB 应用分区剩余 110,720 字节。
应用 SHA-256：`c495a08c2048463ec91ef65aa8390113c556062cba9e9d97fea6c68ab35db599`。
本次未烧录，模块尚未接线实测。默认 GPIO2 / SDA、GPIO3 / SCL；音频使用模块模拟 L/R 输出，见 [接线与实现说明](../../FM.md)。

### 小智设备控制、摄像头与闹钟（同日）

已注册 15 个小智 MCP 工具，覆盖车辆有效数据查询、音量、FM、本地和在线音乐、闹钟与本地拍照。摄像头默认启用，控制和预览位于「影音中心 → 摄像头」；闹钟位于「设置 → 闹钟」，响铃时跨页面显示停止提醒入口。在线音乐支持 HTTP/HTTPS MP3 直链及保存的最近音源，未接入按歌名搜索的音乐服务。

`implemented-camera-controls-dark.png`、`implemented-camera-controls-light.png` 是实际 LVGL 摄像头控制页渲染。`implemented-alarms-fixture-dark.png`、`implemented-alarms-fixture-light.png` 是闹钟设置页渲染，时间和名称来自主机测试夹具，非用户已设置的闹钟。

12 组 C 测试全部通过，其中包含实际 MCP 注册与回调、车辆数据有效性、延后播放及取消、在线 URL 校验和闹钟调度规则；传输与硬件 I/O 在主机测试中使用替身。深浅主题布局与交互检查覆盖 10,316 个渲染标签，0 错误。

ESP-IDF 5.5.5 编译通过；使用标准体积优化配置，保留断言和原有分区。应用为 6,062,704 字节（`0x5c8270`），6 MiB 应用分区剩余 228,752 字节。SHA-256：`778fff66c48eaa9e00fd68efe814717321c1fb7480b93c33be0b8cea0d9d6379`。

本次未烧录。云端语音工具调用、真实车辆问答、网络播放听音、闹钟提示音和摄像头与语音并行运行仍需实板验证。使用示例、边界及验证日志见 [VOICE_CONTROL.md](../../VOICE_CONTROL.md)。

随后同日完成触控响应优化并烧录：应用 SHA-256 `f6f49e0cd778e4133a4043b956d9ee91f99a819299b2765a9fbcad86d2e44fc2`，四镜像校验及 45 秒启动观察通过。触摸轮询为 8 ms、刷新周期为 16 ms；主机 50 次短点按测试通过，未测量实体触摸延迟。详见 [触控优化与烧录记录](../../P4_BRINGUP.md#2026-09-20-触控与界面响应优化已烧录)。
