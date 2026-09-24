# 设计依据

核对日期：2026-09-24。以下资料用于电气和封装设计，并不代表已进行实物验证。

1. 用户 `fm-module/RDA5807M datasheet_v1.pdf`，V1.0，23 页。工程内副本 `reference/RDA5807M.pdf`。重点为裸芯片引脚、推荐电路、32.768 kHz 时钟、I²C 寄存器与 MSOP-10 封装。目录中的 RRD-102 模块截图只作应用背景，模块焊盘顺序不用于裸芯片封装。
2. [TI TS5A22364官方Rev.H手册](https://www.ti.com/lit/ds/symlink/ts5a22364.pdf)，工程副本 `reference/TS5A22364-RevH.pdf`。第3页DGS引脚、第6页3.3V电气特性、第16–18页负向信号/放电支路及±10mA建议、第28–29页封装及焊盘。已视觉核对引脚、3×3mm本体、4.75–5.05mm总宽、0.5mm间距。标准KiCadMSOP焊盘覆盖外露引脚包络；与TI示例尺寸不完全相同，详见VERIFICATION.md。
3. [Espressif ESP32-P4 Function EV Board v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html#header-block)：开发板 J1 针脚；页面链接的官方 v1.4 原理图和 PCB 丝印用于 R83/R84、C39/C40、C58/C59、NS4150B、J16 的定位。
4. [Microne ME6211 V22 手册](https://atta.szlcsc.com/upload/public/pdf/source/20190709/C403651_B6C31F2E850C49363974D5C4E4AF6FD2.pdf)：工程副本 `reference/ME6211.pdf`。第3页 -N 命名、第4页 SOT-23-5 引脚、第9页3.0V特性及第2页1µF应用电容；替代件 ME6211C30M5G-N 的公开采购记录见 [C403651](https://www.lcsc.com/product-detail/C403651.html)。
5. [Nexperia PESD5V0F1BL官方手册](https://assets.nexperia.com/documents/data-sheet/PESD5V0F1BL.pdf)，2023-04-14，11页；工程副本 `reference/PESD5V0F1BL.pdf`。第2页双向引脚及DFN1006-2/SOD882封装，第4页VRWM=5.5V、Cd典型0.4pF/最大0.55pF（1MHz、0V、25°C），第6页应用及接地，第7–8页封装/推荐焊盘。焊盘0.4×0.7mm、中心距0.7mm与现板一致。原LRC手册保留作历史资料，不用于A7 D1的参数依据。
6. [扬兴 Y6OEL89CGI-STUYLC-32.768K / C19272781](https://www.lcsc.com/product-detail/C19272781.html)：目录 DT-26、2×6mm、12.5pF、50kΩ、±10ppm、−40～85°C。精确料号承认书下载未成功，当前为目录核对，保留采购承认书及首板起振确认事项。
7. [风华 CBG160808U601T 厂商规格](https://fhcomp.com/zh-cn/product_info/?prod_code=CBG160808U601T)：600Ω@100MHz、DCR≤0.5Ω、额定200mA。
8. 用户 `BOM-立创_20260924.xls`：BOM260924002306，2026-09-24 11:54:52。原1套配单中的实际人民币价格、MOQ、渠道、交期与错配依据，详见 `PROCUREMENT.md`。

全部替代料号和采购页面记录在 `procurement.json` / `BOM-cost.csv`。LCSC公开网站库存、美元价格并非国内账户成交保证，重新配单时必须确认“国内现货”和实际MOQ。RDA5807M在用户报价中为订货7–10个工作日。

A2历史参考：[TI TLV755P](https://www.ti.com/lit/ds/symlink/tlv755p.pdf)、[Abracon AB26T](https://abracon.com/Resonators/AB26T.pdf)。这些不是A7采购型号的参数依据。

9. 用户A3百板报价 `BOM-LCSC-100boards-A3-立创_20260924.xls`：BOM260924002453，2026-09-24 12:29:53。SGM3718为100颗¥220、MOQ1；RDA5807M为100颗¥104.83、MOQ5、订货7–10工作日。
10. A5 U1：[国内RS2117HYN / C5372463](https://item.szlcsc.com/6223515.html)、[公开LCSC采购页](https://www.lcsc.com/product-detail/C5372463.html)。2026-09-24刷新记录库存465、MOQ/步进5、50颗档USD0.2401；整盘4000不是起购量。早期搜索缓存970颗/USD0.1961已被新页面记录替代，未据其报价。未提交A5国内配单或订单。

11. A6 U1：[TI在产状态](https://www.ti.com/product/TS5A22364/part-details/TS5A22364DGSR)、[立创国内目录](https://item.szlcsc.com/106396.html)、[LCSC公开现货](https://www.lcsc.com/product-detail/C105181.html)。2026-09-24查询4719颗，MOQ/步进1，100颗报价阶梯USD0.4479；库存未锁定，国内新报价未提交。第10项RS2117HYN仅为A5历史来源。
12. 比较依据：[TS5A22362 LCSC](https://www.lcsc.com/product-detail/C201663.html)刷新186颗/100颗阶梯USD3.3122；[DigiKey](https://www.digikey.com/en/products/detail/texas-instruments/TS5A22362DGSR/1909714)显示缺货。[SGM3718官方](https://www.sg-micro.com/product/SGM3718)当前仅UTQFN封装。比较仅反映查询时页面记录。

13. A7 D1：[安世PESD5V0F1BL,315 / C45961](https://www.lcsc.com/product-detail/C45961.html)。2026-09-24公开页库存57925、MOQ/步进5，50颗报价阶梯USD0.0812；10000为整盘数量。旧分类页缓存不作为本次库存依据。国内新配单未提交，库存未锁定。
