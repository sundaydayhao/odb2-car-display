# 新屏 MIPI 适配记录

目标是用户选定的[淘宝商品 983165147604 / SKU 5946336205446](https://item.taobao.com/item.htm?id=983165147604&skuId=5946336205446)：7 寸 1024×600、JD9165、显示屏＋电容触摸左边出线、P+G。MIPI / RGB 是该产品提供的两种接口方式，本主板优先采用 MIPI DSI，不增加 RGB 并行接口。

A7 已按可拆后盖装配重新布局，LCD/触摸改为 F 面 0°，布线与全板验收须看最终冻结 PCB 的 [VERIFICATION.md](VERIFICATION.md)。所选模组的额定电源、两通道配置、触摸 IC 及 FPC 接触面仍需确认，尚未完成新屏实机点亮。

## 已实施的接口定义

- J302：30 针、0.5 mm。CLK 为 17/18，D1 为 20/21，D0 为 23/24；D2、D3 保留为空脚。其余供电、背光、方向控制、待机、复位及接地针已按 MIPI 针表核对。
- J308：6 针、0.5 mm。1 Shutdown/control、2 VCC、3 GND、4 INT、5 SDA、6 SCL。删除原有 GT911 型号假设，保留 GPIO23 / GPIO22 和 I²C 接口。
- `panel-profile.json` 记录 40 项 CAD 对应关系，包含连接器安装焊盘。`tools/check_display_panel.py` 检查原理图网表与 PCB 焊盘，`tools/check_display_hardware.py` 进行整板核对。
- P4 采用两条 DSI 数据通道。成品屏必须配置为两通道；只设置主机驱动参数不能改变屏幕物理通道配置。

## 电源与初始化仍需确认

商品资料中明确标为 Parallel RGB 24-bit 的参数表列出 3.3 V 和“3×6 chips white LED”。这些参数尚不能作为所选 MIPI 接法的电气额定值。

| 电路 | 当前名义设置 | 适配边界 |
| --- | --- | --- |
| LCD VDD | U302 ME6211C18M5G-N，1.8 V | 固定输出；R313 是使能上拉，不能用它调输出电压 |
| AVDD / VGH / VGL / VCOM | 约 9.6 / 18 / −6 / 3.2 V | 保留参考电路设置，尚未获新模组工作点确认 |
| 背光 | ETA1611，R312 1.1 Ω，约 182 mA | 须确认灯串、电流和最大电压后定值 |
| 触摸 VCC | ESP_3V3 | 须确认触摸 IC、电源、Shutdown 极性和 I²C 地址 |

JD9165 芯片资料支持两通道，但 LANSEL 不出现在这张 30 针排线表中。Espressif 默认驱动初始化主要是唤醒与显示命令，不替代模组 LANSEL 配置。须取得这个成品模组的两通道初始化、时序和电源时序。

## FPC 与装配方向

PCB F 面朝可拆背盖，B 面朝屏幕。触摸“左出线”按屏正面观察，在 PCB 背面视图位于 LCD 右侧。两个屏幕接口均为 F 面 0°，入口朝 +Y，从后盖操作锁扣；排线从板下沿圆弧绕入，避开下左 J1。

| 接口 | CAD 位置 / 角度 | 装配方式 |
| --- | --- | --- |
| J302 LCD | F (29, −17) mm / 0° | 中央下沿，真实 STEP 入口约 (29, −14.8)，朝 +Y |
| J308 触摸 | F (69, −19) mm / 0° | 下右，入口朝 +Y；对应显示正面左出线 |
| J9 摄像头 | F (13, −75) mm / 0° | 长边横向，保留竖插，沿 +Z 插拔 |

![接口位置与装配方向](FPC_PLACEMENT_PROPOSAL.svg)

PCB 120 × 74 mm 来自电路、接口与固定空间，不能从屏幕“7 寸”换算。LCM 164.86 ±0.3 × 99.96 ±0.3 mm 不等于 P+G 触摸盖板完整尺寸。为避开屏金属背板，PCB 相对 LCM 上移 24 mm；外壳样件 178 × 126 × 37.2 mm。按假设屏厚 6 mm，C6 天线到屏金属实体最近 15.386 mm；尺寸变化需重算。

LCD 以 R=2 mm 的候选路径绕入，中心线约 54.713 mm，加 3 mm 插入与 4 mm 余量约需 61.713 mm；商品 66.54 mm 参考伸出量并不能确认有效尾长足够。实际尾根、接触面、针 1、厚度、弯曲区域及装配余量均待实测。触摸尾线也未确认。显示开窗 154 × 86 mm 为可改占位，可能遮挡实际有效区。

先将 PCB 固定到承载壳四角支柱，再从后侧连接和锁定屏尾线。支撑柱、螺钉、合壳面和底座加强筋不能压线。详细结构及实际模型条件性检查见 [MECHANICAL.md](MECHANICAL.md) 和 [enclosure/README.md](enclosure/README.md)。

## 验证边界与资料来源

CAD 检查包括原生 ERC、DRC / 未连接、全部焊盘网表对应、装配表、铜对象网络身份、完整 MIPI 路径长度及装配方向。MIPI 同对长度差目标 ≤0.254 mm，三对平均长度差 ≤0.762 mm。线路必须有连续参考地，实际 100 Ω 差分阻抗需由板厂叠层求解；名义总厚度 1.6 mm 不足以确定阻抗。

该主板尚未进行新屏实物点亮、触摸、热测试、EMC 和外壳装配验证。模组电气参数、两通道配置及机械确认是后续打样和上屏前必须闭合的事项。

- 用户提供的选中规格、MIPI 针表和触摸针表已用于复核；原商品 HTML 含私人账户信息，不随工程提交。当前项目目录未检索到原 HTML / 图片副本，下列文件名仅作为已读资料的索引。
- MIPI 针表：`O1CN01Dea86t1iXKUyfv1qf_!!2640614422.jpg`；触摸针表：`O1CN01oT1DSe1iXKUyjQ6FC_!!2640614422.jpg`。
- [JD9165A-A 数据手册](https://dl.espressif.com/AE/esp-iot-solution/JD9165A-A_DS_V0.0.1-0621.pdf)。
- [Espressif JD9165 驱动 2.0.2](https://components.espressif.com/components/espressif/esp_lcd_jd9165/versions/2.0.2)。
- [ESP32-P4 PCB 布局指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/pcb-layout-design-esp32p4.html)。
