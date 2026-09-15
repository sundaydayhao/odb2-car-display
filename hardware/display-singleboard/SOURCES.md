# 参考来源与授权

本工程把以下参考电路重建为可编辑 KiCad 文件，并改变板框、屏幕连接、部分复位与默认状态和 DSI 布线。不是 Espressif 发布的官方单板，也没有继承官方板的验证或认证。

- [ESP32-P4X Function EV Board 官方资料页](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4x-function-ev-board/user_guide.html)。电路以 SCH v1.7 / PCB v1.6 为参考。
- [P4X 原理图、PCB、BOM 参考包](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/_downloads/3ccd3fcf4b943035f2313b42bcff07e7/ESP32-P4X-Function-EV-Board-EN.zip)。
- [Espressif LCD Adapter Board Reference Design](https://dl.espressif.com/schematics/LCDAdapterBoardReferenceDesign.zip)：屏幕偏压、背光和面板接插件的参考。
- [P4 原理图检查指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html)。
- [P4 PCB 布局指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/pcb-layout-design-esp32p4.html)：MIPI 100 Ω ±10%，对内差值 10 mil，对间差值 30 mil；USB 90 Ω ±10%。
- 历史屏幕选型参考（不作为本次新屏适配依据）：[Wireless-Tag WTP4C5MP07S 产品页](https://shop.wireless-tag.com/products/7inch-lcd-touch-screen-1024x600-mipi-smart-displays-wtp4c5mp07s-esp32-lcd-board-used-with-esp32-p4-and-esp32-c5-dev-board)、[厂家屏幕规格书](https://img03.71360.com/w3/77q677/20250929/9cc75013e0ffa4cd305119a363cb940d.pdf?dl=1)、[厂家板卡资料](https://wiki.wireless-tag.com/docs/zh/WT99P4C5-S1/board_features.html)。
- 历史屏幕选型参考：[ESP32 Display Panel 中的 WTP4C5MP07S 初始化配置](https://raw.githubusercontent.com/esp-arduino-libs/ESP32_Display_Panel/master/src/board/supported/wirelesstag/BOARD_WIRELESSTAG_WTP4C5MP07S.h)：仅用于核对屏幕控制器与 2 lane 配置，不能照抄该板的 GPIO。
- [TI SN74LVC1G07 数据手册](https://www.ti.com/lit/ds/symlink/sn74lvc1g07.pdf)：开漏输出 LCD 复位电平适配。

符号来源包括 [KiCad 官方库](https://www.kicad.org/libraries/license/) 与 [Espressif KiCad Libraries](https://github.com/espressif/kicad-libraries)。项目局部修改保留来源说明；`symbol-provenance.json` 追溯到被整合的工程。Espressif 库许可文本见 `LIBRARY_LICENSE.md`；KiCad 库许可包含对使用库设计的电路板的例外。参考设计本身的权利与使用条件以源文件和原发布者说明为准。

A3 结构复核：

- [XKB SS-12D10L3 厂家机械图 A1](https://datasheet.lcsc.com/datasheet/pdf/562955152e0d35ebc194db3482640805.pdf?productCode=C319013)：13 × 6.8 mm 本体、6.4 mm 本体高度、3 mm 柄、2.2 mm 行程、4.7 mm 引脚间距。
- [Espressif C6 整机天线布置指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c6/pcb-layout-design.html)：外壳内天线净空与整机无线测试。

## A4 以太网删除记录

3D 模型来源、逐文件哈希和机械精度类别见 `3D_MODELS.md` / `model-manifest.json`。KiCad 模型附库许可，Espressif 和其他料号关联模型保留原来源；自建的五个简化模型附生成器。

以 A3（`5c00310`）为基线，删除以太网接口、PHY、25 MHz 晶振和专用外围。原官方参考图及旧双板目录作为来源检查点保留；A4 原理图和 PCB 是删除以太网时的检查点。原归入以太网页的 SD 电源开关和 C6 去耦依据原生逐引脚网络与 PCB 位置识别后保留，不随页整体删除。


## A5 电源接口与下载

- Molex 43650-0200：[厂家产品页](https://www.molex.com/en-us/products/part-detail/436500200)。KiCad 10 `Connector_Molex` 原生 43650-0200 封装；模型通过 easyeda2kicad 1.0.1 从 C192562 获取，保持模型来源标识。
- [LCSC C192562](https://www.lcsc.com/product-detail/Wire-To-Board-Connector_MOLEX-436500200_C192562.html) 提供国内采购入口和对应模型。封装采用 KiCad 官方版本，没有把第三方电气封装替换进主板。
- [ESP32-P4 硬件设计检查表](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/schematic-checklist-esp32p4.html)：UART0 TX / RX 为 GPIO37 / GPIO38；保留 P4X USB_DP 下拉等 HS 必需外围。

## A6 选定屏幕

所选淘宝 SKU、MIPI 和触摸针表、JD9165 芯片及两通道限制见 [PANEL_TARGET.md](PANEL_TARGET.md)。采用两数据通道 MIPI 接法，RGB 不增加独立并行电路。商品私人账户 HTML 不随工程提交。

## Retained USB HS ESD, D21/D22

TI TPD1E05U06DPYR (ACTIVE orderable): https://www.ti.com/product/TPD1E05U06/part-details/TPD1E05U06DPYR . Pin map and DPY0002A land pattern: https://www.ti.com/lit/ds/symlink/tpd1e05u06.pdf (SLVSBO7O, Table 4-1 and pp.34–36). Supplier STEP: https://www.lcsc.com/product-detail/C436349.html . See ESD_PROTECTION.md for independent land/pin verification.
