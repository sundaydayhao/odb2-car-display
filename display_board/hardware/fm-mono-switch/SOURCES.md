# 设计依据

核对日期：2026-09-24。以下资料用于电气和封装设计，并不代表已进行实物验证。

1. 用户 `fm-module/RDA5807M datasheet_v1.pdf`，V1.0，23 页。工程内副本 `reference/RDA5807M.pdf`。重点为裸芯片引脚、推荐电路、32.768 kHz 时钟、I²C 寄存器与 MSOP-10 封装。目录中的 RRD-102 模块截图只作应用背景，模块焊盘顺序不用于裸芯片封装。
2. [润石RS2117H官方RevE.1.1手册](https://www.run-ic.com/upload/goods/20240412/202404121507559540.pdf)：第4页完整订货号、第6页MSOP引脚/真值表、第9页模拟范围/逻辑门限/导通电阻、第16页关断电压差限制、第18页封装尺寸及推荐焊盘。随工程提供同版本完整20页手册 `reference/RS2117H-RevE1.1.pdf`，从[立创托管](https://atta.szlcsc.com/upload/public/pdf/source/20250915/134E6906BCFCB9384AC1CFA27AB399A9.pdf)下载；已视觉核对第6、18页。使用KiCad官方MSOP-10的加长焊盘及模型，模型只作外观参考。
3. [Espressif ESP32-P4 Function EV Board v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html#header-block)：开发板 J1 针脚；页面链接的官方 v1.4 原理图和 PCB 丝印用于 R83/R84、C39/C40、C58/C59、NS4150B、J16 的定位。
4. [Microne ME6211 V22 手册](https://atta.szlcsc.com/upload/public/pdf/source/20190709/C403651_B6C31F2E850C49363974D5C4E4AF6FD2.pdf)：工程副本 `reference/ME6211.pdf`。第3页 -N 命名、第4页 SOT-23-5 引脚、第9页3.0V特性及第2页1µF应用电容；替代件 ME6211C30M5G-N 的公开采购记录见 [C403651](https://www.lcsc.com/product-detail/C403651.html)。
5. [LRC LESD8LL5.0CT5G 厂商手册，唯样托管](https://uploadcdn.oneyac.com/attachments/files/brand_pdf/lrc/B2/3D/LESD8LL5.0CT5G.pdf)：工程副本 `reference/LESD8LL5.0CT5G.pdf`。双向，0.25pF典型/0.3pF最大；第3页 SOD-882 推荐焊盘0.4×0.7mm、中心距0.7mm，与现封装一致。
6. [扬兴 Y6OEL89CGI-STUYLC-32.768K / C19272781](https://www.lcsc.com/product-detail/C19272781.html)：目录 DT-26、2×6mm、12.5pF、50kΩ、±10ppm、−40～85°C。精确料号承认书下载未成功，当前为目录核对，保留采购承认书及首板起振确认事项。
7. [风华 CBG160808U601T 厂商规格](https://fhcomp.com/zh-cn/product_info/?prod_code=CBG160808U601T)：600Ω@100MHz、DCR≤0.5Ω、额定200mA。
8. 用户 `BOM-立创_20260924.xls`：BOM260924002306，2026-09-24 11:54:52。原1套配单中的实际人民币价格、MOQ、渠道、交期与错配依据，详见 `PROCUREMENT.md`。

全部替代料号和采购页面记录在 `procurement.json` / `BOM-cost-100.csv`。LCSC公开网站库存、美元价格并非国内账户成交保证，重新配单时必须确认“国内现货”和实际MOQ。RDA5807M在用户报价中为订货7–10个工作日。

A2历史参考：[TI TLV755P](https://www.ti.com/lit/ds/symlink/tlv755p.pdf)、[Nexperia PESD5V0F1BL](https://assets.nexperia.com/documents/data-sheet/PESD5V0F1BL.pdf)、[Abracon AB26T](https://abracon.com/Resonators/AB26T.pdf)。这些不是A5采购型号的参数依据。

9. 用户A3百板报价 `BOM-LCSC-100boards-A3-立创_20260924.xls`：BOM260924002453，2026-09-24 12:29:53。SGM3718为100颗¥220、MOQ1；RDA5807M为100颗¥104.83、MOQ5、订货7–10工作日。
10. A5 U1：[国内RS2117HYN / C5372463](https://item.szlcsc.com/6223515.html)、[公开LCSC采购页](https://www.lcsc.com/product-detail/C5372463.html)。2026-09-24刷新记录库存465、MOQ/步进5、50颗档USD0.2401；整盘4000不是起购量。早期搜索缓存970颗/USD0.1961已被新页面记录替代，未据其报价。未提交A5国内配单或订单。
