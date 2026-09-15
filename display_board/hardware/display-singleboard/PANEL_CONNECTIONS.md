# 官方分板连线与本单板的对应关系

官方 ESP32-P4-Function-EV-Board v1.5.2 与 LCD 适配板通常连接一条 MIPI FFC，以及 RST_LCD、PWM、5 V、GND 四根线。LCD 适配板若由其 USB 独立供电，则省去 5 V 和 GND 两根供电跳线，保留复位与 PWM。

本项目已把 LCD 适配电路合并到主板，并适配所选 JD9165 30 针屏；以下功能在原理图内连接，不再需要板间杜邦线。网表证据为同目录 `PANEL_INTERNAL_CONNECTIONS.json`。PCB 物理连通以最终 `CHECK_RESULTS.json` 为准，不能把原理图连通当作实机点亮。

| 官方连线 | 本项目单板连接 | 作用 |
| --- | --- | --- |
| 主板 MIPI DSI ↔ 屏幕适配板 J3 | P4 的 CLK、D0、D1 经板内差分线到 J302，再接屏幕自身 FPC | 图像传输 |
| GPIO27 ↔ RST_LCD | GPIO27 → R227 33 Ω → CNN_GPIO27 → U304 → PANEL_RESET_LCD → J302.27 | 复位；开漏输出由 R304 上拉到屏幕逻辑电源 |
| GPIO26 ↔ PWM | GPIO26 → R228 33 Ω → CNN_GPIO26 → R311 → U301.4 | 背光控制；R341 使上电默认关闭 |
| 5 V ↔ 5 V | 公共 VCC_5V → U301、U302、U303 | 板内产生背光、LCD 逻辑与偏压电源 |
| GND ↔ GND | PCB 公共地 → LCD 电源与 J302 接地脚 | 电源与信号回流 |

5 V 是电源转换电路的输入，**不是直接送入 LCD 的 1.8 V 逻辑脚**。J302.28/29 为当前设定的 PANEL_LCD_VDD；AVDD、VGH、VGL、VCOM 和 LED 正负端按屏幕针表分别连接。最终电压、背光电流及两通道 MIPI 初始化仍须与所购成品屏规格核定。

触摸另接 J308 六针 FPC，含控制、3.3 V、GND、INT、SDA、SCL。它不属于官方那四根跳线；目前不能在未确认触摸 IC 时默认套用 GT911 复位时序。装配需要屏幕本体 FPC 和触摸 FPC，不需要另加一块屏幕适配 PCB。

依据：[乐鑫官方接线说明](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide.html)。官方参考板与本项目所选屏不是同一总成，FFC 接触面、针序和电源不能混插验证。
