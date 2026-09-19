# 可选 4G 与 GPS/北斗定位规划

修订 B，2026-09-19。仅为 SCH 前的规划；尚无新原理图、PCB 或联机验证。

## 选型与装配

首选 **Quectel EC200U-CN 的 GNSS 版本**，使用标准 AT 固件。型号后缀及固件必须由供应商确认；普通 EC200U-CN 不必然带 GNSS。模块提供蜂窝联网、GPS/北斗定位数据，地图、路径规划和语音导航由 P4 应用及地图服务实现，不是装上模块就具备完整导航。

预留 28 × 31 mm、144 脚 LCC/LGA 模组位置，以及约 40 × 44 mm 的总体功能区。模块、3.8 V 电源、SIM、天线及电平转换作为一组选装料；不为多个不兼容模块叠放“万能封装”。基本版空贴，选装版在工厂贴装，普通用户只操作 SIM 卡。国内分销/淘宝采购要按 GNSS 订货后缀、频段和固件核单，当前不作现货或价格承诺。

## 接口分配

以下数字中“核心板焊盘”“P4 GPIO”“4G 焊盘”是不同编号。

| 接口 | 核心板 / P4 | 4G 端 | 规划 |
| --- | --- | --- | --- |
| USB 数据 | 核心板 48=DM，49=DP | EC200U 70=DM，69=DP | 内部 USB Host→Device；使用 USB PPP 数据口，UART/独立 USB AT 口负责控制与定位查询 |
| USB 检测 | 可关断的内部 5 V | 71=USB_VBUS | 只作连接检测，不能用它代替模组 3.8 V 主电源；与外部 USB-C VBUS 防反灌隔离 |
| AT UART | GPIO2 TX，GPIO3 RX；核心板 17/18 | 68=MAIN_RXD，67=MAIN_TXD | 3.3↔1.8 V 定向转换；默认低速 AT 控制，网络数据主要走 USB，不在同一裸 UART 混入 PPP 与 NMEA |
| 电源使能 | GPIO0；核心板 88 | 3.8 V 支路 EN | 默认关闭；独立于屏幕背光和功放电源 |
| 开关机 | GPIO1；核心板 16 | 21=PWRKEY | 开漏/集电极开路拉低，释放后由模组内部上拉，不能由 P4 直接推高至 3.3 V |
| 睡眠 | GPIO4；核心板 19 | 66=MAIN_DTR | 电平转换，配合 AT 睡眠及 USB suspend/VBUS 管理 |
| 状态 | GPIO5；核心板 20 | 61=STATUS | 1.8→3.3 V；用于判断正常开机及关机完成；VBAT 已断时不再用 STATUS 推断状态 |
| 维修 | 内部触点 | RESET_N 20、USB_BOOT 115、DBG_RXD 11、DBG_TXD 12 | 复位/下载电平严格按厂家；保留与 P4 USB 主机隔离的 USB 下载接入方式 |
| Nano-SIM | 直接接 4G 模组 | USIM1 | 带检测，1.8/3.0 V 由模组供电；不接 P4 的 3.3 V，ESD 紧邻卡座 |
| LTE / GNSS | 两条独立 50 Ω 射频线 | ANT_MAIN / ANT_GNSS | 独立天线和匹配网络；有源 GNSS 天线增加低噪声限流偏置供电 |

GPIO0～5 不再作为通用扩展或同时运行的外部四线 JTAG；保留原 GPIO24/25 的内部 USB 调试候选，外部 CH343P 串口下载不变。没有装 4G 时相关驱动与探测关闭。无线 SDIO GPIO14～19、GPIO6/54 仍只属于核心板内部连接。

USB PPP 是首选软件路径。乐鑫已有 P4 通用 USB 调制解调器驱动，但其当前已验证型号表没有 EC200U，不能标为“已兼容”；必须核对采购固件的 USB PPP 能力、VID/PID、端点、AT/GNSS 通道，再做拨号与持续音频下载测试。GNSS 可先用独立 AT UART 查询位置，避免把“USB NMEA 口一定存在”当默认条件。

## 电源、关机与低功耗

- EC200U 电源范围 3.3～4.3 V，规划 3.8 V；主路径是 SYS→5 V 主升压→独立 3.8 V 降压。SYS 可能高于 4.3 V，不直连模组。
- 厂家要求 LTE 版本至少 2 A、带 GSM 版本至少 3 A 供电能力。本板支路按 3 A 候选 TLV62585 规划，RF/BB 星形分配，各自近端低 ESR 储能及高频去耦。5 V 主升压相应升级为 4 A 峰值目标，输出能力要在最低电池电压、热态验证。
- 原 SGM41513 6 A 电池通路不够覆盖新增峰值，改用 BQ25896 候选；整机暂限 20 W 峰值预算，3.2 V/85% 效率时约 7.35 A。连接器与线束按 10 A 条件选择，电芯/PCM 也需至少 10 A 连续能力。20 W 是规划上限，尚未测量，不代表各支路满载可无限同时使用。
- 正常关机先停止网络传输、执行 `AT+QPOWD`，观察返回及 STATUS；完成后断 USB_VBUS、隔离 UART，再关闭 3.8 V。PWRKEY 不长期接地，否则可能无法正常软关机。USB_VBUS/PWRKEY 上电先后关系按手册处理；不机械套用其他模组的 100 ms 脉冲。
- 屏幕先关闭；整机为蜂窝退网保留最长约 45 s 的设计窗口，具体按固件关机时限核定。硬切只作异常超时兜底；重新供电前确保模组 VBAT 已放电至厂家规定的低电位。
- 关闭蜂窝选项时允许整个 4G/GNSS 支路断电；普通关机不后台持续定位。待机若需要保留注册状态，采用 DTR/USB 休眠并量测整机电流，不能把断电漏电与联网待机电流混为一谈。未采用 RI 唤醒线，睡眠时的自动下行唤醒不作本版承诺。
- 5 V/3 A USB 电源不能覆盖全部峰值并同时大电流充电；电池补峰，输入不足先降充电电流，再限制音量/亮度。5000 mAh、常用 9～12 W 的估算续航为约 1.2～1.6 h，需实测。

## 机械与射频

模组放 B 面右上/右中，Nano-SIM 在右侧、SD 上方，壳体分别标注并留独立操作空间。LCD/触摸两条排线仍在 F 面中下方，入口同朝下，不穿越 SIM 卡口。

LTE 天线与 Wi-Fi 天线分别安排在塑料区，避开电池、屏幕金属背板和功放线束。GNSS 天线优先朝天空，可经后壳上沿同轴引出到外置 GPS/北斗有源天线；不能因为 PCB 位置方便就把定位天线夹在金属屏后。天线及同轴插座也属于可选 BOM，未装版用封盖处理外壳开口。

板框暂维持 145 × 85 mm；40 × 44 mm 区域是空间预留，并不表示已验证全部连接器、散热和外壳间隙。布线时先布内部 USB、高电流和 RF，再安排普通信号；SIM 与 MIC 远离 LTE 发射及开关节点。

## 资料

- [移远 EC200U 产品页](https://www.quectel.com/product/lte-ec200u-series/)：GNSS 为选配，模组外形及网络等级。
- [EC200U 硬件设计 V1.2](https://www.quectel.com/content/uploads/2024/02/Quectel_EC200U_Series_Hardware_Design_V1.2.pdf)：手册页 21～25 引脚/电平，37～43 供电/开关机，47～50 USB/UART，68～69 GNSS/RF。
- [GNSS 应用说明 V1.2](https://quectel.com/content/uploads/2024/04/Quectel_EC200U_SeriesEG912U-GL_GNSS_Application_Note_V1.2.pdf)：GPS/北斗与 GNSS 版本要求。
- [乐鑫 USB PPP](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_host/usb_ppp.html)：驱动和新模组适配步骤。
- [BQ25896](https://www.ti.com/product/BQ25896)、[TLV62585](https://www.ti.com/product/TLV62585)、[JST-VH](https://www.jst-mfg.com/product/index.php?lang=2&series=262)：电源及连接器条件。
