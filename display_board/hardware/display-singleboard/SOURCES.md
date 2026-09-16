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

- [LCM-070--A0 规格书（项目本地副本）](../../../docs/LCM-070--A0%20规格书.pdf)：第 5 页机械尺寸/FPC，第 6 页 30 针表，第 7 页电源与背光，第 8–10 页上/下电及 MIPI 时序，第 15 页装配建议。该 PDF 的第 4 页文字尺寸与第 5 页机械图有 1 mm 矛盾，以机械图为当前布置依据并要求供应商书面确认。
- [LRC LMSZ5237BT1G（LCSC C32646）](https://www.lcsc.com/product-detail/C32646.html)：8.2 V、500 mW、SOD-123 齐纳，作为 VGL 样件替代；量产前仍按温度和负载测量确认。
- [Diodes AP3012 数据手册](https://www.diodes.com/datasheet/download/AP3012.pdf)：FB 典型约 1.25 V；R315=22.6 kΩ、R316=3.3 kΩ 的 9.8 V 目标由此估算，首板仍须实测。

## A6 历史 USB HS ESD：A7 已随 J18 删除 D21/D22

TI TPD1E05U06DPYR (ACTIVE orderable): https://www.ti.com/product/TPD1E05U06/part-details/TPD1E05U06DPYR . Pin map and DPY0002A land pattern: https://www.ti.com/lit/ds/symlink/tpd1e05u06.pdf (SLVSBO7O, Table 4-1 and pp.34–36). Supplier STEP: https://www.lcsc.com/product-detail/C436349.html . See ESD_PROTECTION.md for independent land/pin verification.

## A7 整机装配与音频

A7 当前结构与接口见 `MECHANICAL.md`、`enclosure/README.md`。以上 A3–A6 段落是来源历史，不代表 A7 的电气或机械验收。

- [ALPS SKRTLBE010 原厂页](https://tech.alpsalpine.com/e/products/detail/SKRTLBE010/) 与 [SKRT 系列原厂图](https://tech.alpsalpine.com/cms.media/product_catalog_ta_02_skrt_en_01ec237785.pdf)：选无定位柱 Drawing 2 版本、0.2 mm 行程。
- [JST GH 原厂图](https://www.jst-mfg.com/product/pdf/eng/eGH.pdf)：麦克风板端 SM02B-GHS-TB、线端 GHR-02V-S。
- [Goertek B4013AM422-042 原厂资料副本](https://datasheet.lcsc.com/datasheet/pdf/db444a5b583e31a2b036db3eb8e7819d.pdf?productCode=C233793)：φ4 × 1.3 mm SMT 驻极体，外接时需载板/匹配线束。
- [立创 ESP32S3 官方资料](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/)：DB1811AB50 仅作为外接腔体喇叭组织方式的早期参考。本版另选额定 3 W 单元候选，见 `AUDIO_VALIDATION.md`，不把该 1 W 喇叭作为装配料。
- [NS4150B 原厂规格书（乐鑫托管）](https://dl.espressif.com/dl/schematics/NS4150B.pdf)：5 V、4 Ω、1% THD 时典型 2 W 输出。
- [赢海 GSPK4020057TN-4R3W / C3012621](https://item.szlcsc.com/3517613.html)：样件候选，额定 3 W；尺寸与温度资料冲突及原厂测试条件在外壳选型说明中保留。
- [Espressif C6 布局指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c6/pcb-layout-design.html)：整机天线与周围金属关系。几何 15 mm 条件不替代实机 RF 验证。

外壳 STEP/STL 与脚本为本项目参数化生成，屏幕盖板、厚度、有效显示区和尾线尺寸保留未确认状态。
