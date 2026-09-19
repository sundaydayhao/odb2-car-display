# 小智车辆查询与设备控制

固件在现有 `esp_xiaozhi 0.1.2` 的 MCP 引擎中注册 15 个设备工具。已有的小智绑定、唤醒词「你好小智」、MQTT/UDP 或 WebSocket 连接及自动对话流程继续使用。小智服务通过 `tools/list` 发现能力，通过 `tools/call` 查询和操作设备；不是本地关键词匹配，也不伪造服务端识别结果。

## 可以这样说

| 语音示例 | 本机行为 |
| --- | --- |
| 现在水温是多少？ | 读取 OBD 缓存的有效冷却液温度，让小智按真实值回答 |
| 查看水温 / 看一下转速 | 查询指标，可切到对应仪表页面 |
| 音量大一点 / 小一点 / 调到百分之三十 | 全局音量增加、减小或设置为指定值 |
| 打开收音机，调到 98.7 | 查询接收器是否连接，回复后调谐并收听 |
| 播放本地音乐 / 播放海阔天空 | 按 SD 文件名匹配或选择第一首 MP3，回复后播放 |
| 暂停音乐 / 继续播放 / 下一首 / 停止播放 | 调用本机播放控制，下一首/上一首使用 SD 列表 |
| 播放在线音乐 | 播放已保存的在线 MP3 直链；未设置时明确要求音源 |
| 每天早上七点半提醒我 / 十分钟后提醒我 | 保存每天重复闹钟或单次计时提醒 |
| 有哪些闹钟？/ 删除这个闹钟 / 停止提醒 | 查询、删除或停止响铃 |
| 打开摄像头 / 帮我拍张照 / 关闭摄像头 | 打开影音中心摄像头页、保存照片到 SD 或关闭预览 |

自然语言意图与具体工具参数由绑定的小智服务决定。主机测试不能证明云端一定识别某种说法；实际识别、调用和播报需要在该设备的小智会话中验证。

## 车辆数据

`self.vehicle.get_metrics` 每次调用 `obd2_read_vehicle_data()`，沿用现有 BLE 会话校验和每个 PID 的过期规则。返回 `available`、`value`、`unit` 和 `calculated`。断线、超时、未收到、车型不支持时 `available=false`、`value=null`，不得用 0 或模型推测代替；实际车速为 0 则仍是有效值。

支持水温、车速、转速、油量、发动机负荷、节气门、ECU 电压、进气温度、机油温度、进气歧管压力、大气压力、燃油流量，以及有有效输入时的压差、瞬时/平均油耗和本次行程统计。是否可用仍取决于 2019 凯美瑞 2.5L 汽油版实际返回的 PID。胎压目前没有数据来源，工具明确返回不可用；没有接入累计里程、胎温或变速箱油温。行程和油耗计算值与 ECU 原始读数分开标记，见 [车型参数说明](VEHICLE_DATA_2019_CAMRY.md)。

查询参数 `metric="all"` 返回当前指标集合；`metric="coolant_temperature"` 指水温。`show=true` 同时跳转对应仪表页，界面操作使用 LVGL 锁，不从 MCP 回调直接修改控件。

## 音源与音量

全局音量 0–100 统一控制语音、音乐、FM 和闹钟；0 为静音。增减命令在播放任务中基于当前音量执行并限制在范围内，避免连续增减读取到同一个旧值。音量继续保存到 NVS。

小智会话占用声音时，播放本地/在线音乐和打开 FM 的请求先放入单个待执行槽位。工具结果明确为“回复后执行”，不是已播放。TTS 队列及 DMA 排空、会话结束后才提交到实际播放服务；新请求覆盖旧请求，停止/暂停清除待执行操作。断线、升级、关闭助手、手动结束对话会取消待执行操作；超过 30 秒的请求过期，避免以后突然播放。

FM 仍需接好 RDA5807M，使用模块的模拟 L/R 输出，不能通过 I²C 把电台声音送入 P4 板载喇叭。没有模块时工具返回失败，见 [FM 接线](FM.md)。

### 在线音乐音源

当前实现 **HTTP/HTTPS 的 MP3 直链流式播放**，支持分块传输，HTTPS 校验证书；使用与本地音乐相同的 MP3 解码及扬声器链路。192 KiB 音频缓冲使用 PSRAM，播放命令队列也使用 PSRAM，不把整首文件载入内存。断网、HTTP 非 200、超时、错误格式会显示失败。

这里没有内置网易云、QQ 音乐等平台的账号或曲库搜索 API。需要登录/DRM 的平台音频、歌单网页、HLS、重定向地址不作为 MP3 直链处理。按歌名联网点歌需要用户提供可使用的音乐服务/API，再接入检索和取播放地址；工具说明明确禁止小智编造链接或声称找到了歌曲。

第一次可由小智使用用户提供的直链，或通过串口设置并播放（替换为实际可用 URL）：

```text
media online https://your-music-host.example/song.mp3 "Song title"
```

最近一次在线音源的 URL 和标题保存到本机 NVS。之后可说“播放在线音乐”，也可点击「影音中心 → 音乐 → 在线播放」或执行 `media online`。默认不带演示音源，不在开机时自动播放。在线源名称不会冒充 SD 列表中的曲目；设置地址入队或 HTTP 连接成功本身都不代表已听到声音。

## 闹钟

「设置 → 闹钟」显示最多四个已保存条目，可选时、分及每天重复，添加或删除。小智还可设置 1–10080 分钟的单次计时提醒并指定简短名称。所有时间按 **北京时间 UTC+8**；单次时分表示下一次到达该时间，过去的时分安排到次日。未校时时拒绝创建。

闹钟保存成功才返回 ID；满额、无效时间、名称超过 47 个 UTF-8 字节或 NVS 写入失败明确返回失败。响铃显示跨页面提示，可点击「停止提醒」或唤醒小智停止；响铃最长两分钟，音乐和 FM 让出音源，语音会话可优先使用声音。全局音量为 0 时仍显示提醒，但不发声。

调度和提示音复用已有音乐任务，未增加常驻任务栈。设备必须保持通电，关机不能响铃，也没有实现定时开机。重启后等待系统校时再调度；超过两分钟的旧提醒不补响，重复闹钟计算下次时间。断网后已校准的系统时钟可继续调度，但不保证长期离线时钟精度。

```text
alarm status
alarm at 07:30 daily
alarm at 18:20
alarm after 10
alarm delete 1
alarm dismiss
```

## 摄像头

`CONFIG_DEVICE_CAMERA_ENABLED=y` 已设为当前配置和默认值。「影音中心」第三个页签为「摄像头」，提供实时预览、拍照保存和相册。设置中保留摄像头入口及关闭控制。功能已启用，开机不自动采集；点击开启或发出语音命令后启动原配 MIPI 摄像头，离开实时预览页关闭采集。

语音拍照会打开该页、启动摄像头，并请求保存下一帧；无需先手动开启。照片保存到 SD 的 `photos` 目录，沿用 1280×720 BMP 和本地相册。无 SD 卡或相册已达 128 张时拒绝拍照。拍照状态明确区分 `queued`、`saving`、`saved`、`failed_or_cancelled`；只有本次保存完成才返回照片结果。连接失败或关闭摄像头会取消等待中的拍照，不在未来重开时突然执行旧请求。

本次只实现本地拍照，没有把照片上传小智或实现视觉问答。

## 工具与代码

| MCP 工具 | 参数概要 |
| --- | --- |
| `self.get_device_status` | 无；返回当前音量、音乐、电台、网络、摄像头及响铃状态 |
| `self.vehicle.get_metrics` | `metric=all`、`show=false` |
| `self.audio_speaker.set_volume` | `volume=0..100` |
| `self.audio_speaker.adjust_volume` | `delta=-100..100`，默认 +10 |
| `self.radio.control` | `action=listen/seek/stop/retry/status`，`frequency_tenths=987` 表示 98.7 MHz |
| `self.music.list_local` | `offset`，每页十首 |
| `self.music.play_local` | 文件名子串 `name` 或从 0 开始的 `index`；重名需要明确选择 |
| `self.music.play_online` | `url`、`title`；空 URL 使用已保存音源 |
| `self.music.control` | `action=pause/stop/resume/next/previous` |
| `self.camera.control` | `action=open/show/close/status` |
| `self.camera.take_photo` | 无；提交本地拍照请求 |
| `self.alarm.list` | 无；返回北京时间、已保存闹钟和响铃状态 |
| `self.alarm.set` | `hour`、`minute`、`daily`、`delay_minutes`、`label` |
| `self.alarm.delete` | `id` |
| `self.alarm.dismiss` | 无 |

注册和设备适配在 `voice_tools.c`，车辆单位/有效性映射在 `vehicle_metrics.c`，闹钟时间规则在 `alarm_model.c`，持久化和提示音在 `alarm_service.c`。SDK 和工具链版本未升级；为容纳摄像头和设备工具，使用 `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`，保留断言及原有分区布局。

## 验证范围

生产 C 代码主机测试覆盖实际 MCP 注册与回调（传输和硬件 I/O 使用明确的替身）、车辆有效性/单位、播放延后/覆盖/取消/超时、队列拒绝、在线 URL 校验、收音机缺席、拍照前提、闹钟未校时/跨日/重复/过期/满额/持久化格式及工具注册内存失败。LVGL 预览检查摄像头、在线播放状态、闹钟设置和跨页响铃的深浅主题。

2026-09-20 验证结果：12 组 C 测试全部通过；LVGL 布局与交互检查覆盖 10,316 个渲染标签，0 错误；ESP-IDF 5.5.5 固件编译通过。应用 `display_board/build/odb2_dash.bin` 为 6,062,704 字节（`0x5c8270`），原有 6 MiB 应用分区剩余 228,752 字节。SHA-256：`778fff66c48eaa9e00fd68efe814717321c1fb7480b93c33be0b8cea0d9d6379`。

本地验证日志为 `build/voice-control-tests.log`、`build/voice-control-ui-preview.log`、`build/voice-control-firmware-build.log`；深浅主题截图与记录见 [界面验证记录](design/cabin/README.md)。

上述构建当时未烧录；随后同日已将包含这些功能及触控优化的固件烧录至 COM28，启动观察通过，详见 [烧录记录](P4_BRINGUP.md#2026-09-20-触控与界面响应优化已烧录)。云端自然语言识别与工具调用、真实车辆问答、网络 MP3 听音、闹钟提示音/重启恢复，以及摄像头与语音并行时的内存和稳定性仍需实板验证。模拟值仅用于标明的测试截图，不写入实际车辆状态。

依据：[小智官方 MCP 使用说明](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage.md)、[Espressif MCP 工具 API](https://docs.espressif.com/projects/esp-iot-solution/en/latest/mcp/tooling_and_data.html)，以及项目锁定版本的组件源码。
