# 设计依据

核对日期：2026-09-24。以下资料用于电气和封装设计，并不代表已进行实物验证。

1. 用户 `fm-module/RDA5807M datasheet_v1.pdf`，V1.0，23 页。工程内副本 `reference/RDA5807M.pdf`。重点为裸芯片引脚、推荐电路、32.768 kHz 时钟、I²C 寄存器与 MSOP-10 封装。目录中的 RRD-102 模块截图只作应用背景，模块焊盘顺序不用于裸芯片封装。
2. [SG Micro SGM3718 官方手册](https://www.sg-micro.com/rect/assets/3d47e91a-f3bb-4f8e-9305-8e041848aeda/SGM3718.pdf)：引脚、NC/NO 真值表、3.3 V 下负模拟信号范围；第 11 页推荐焊盘。SGM3718 封装按厂商尺寸创建，供应商模型仅作外观复核。
3. [Espressif ESP32-P4 Function EV Board v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html#header-block)：开发板 J1 针脚；页面链接的官方 v1.4 原理图和 PCB 丝印用于 R83/R84、C39/C40、C58/C59、NS4150B、J16 的定位。
4. [Microne ME6211 V22 手册](https://atta.szlcsc.com/upload/public/pdf/source/20190709/C403651_B6C31F2E850C49363974D5C4E4AF6FD2.pdf)：工程副本 `reference/ME6211.pdf`。第3页 -N 命名、第4页 SOT-23-5 引脚、第9页3.0V特性及第2页1µF应用电容；替代件 ME6211C30M5G-N 的公开采购记录见 [C403651](https://www.lcsc.com/product-detail/C403651.html)。
5. [LRC LESD8LL5.0CT5G 厂商手册，唯样托管](https://uploadcdn.oneyac.com/attachments/files/brand_pdf/lrc/B2/3D/LESD8LL5.0CT5G.pdf)：工程副本 `reference/LESD8LL5.0CT5G.pdf`。双向，0.25pF典型/0.3pF最大；第3页 SOD-882 推荐焊盘0.4×0.7mm、中心距0.7mm，与现封装一致。
6. [扬兴 Y6OEL89CGI-STUYLC-32.768K / C19272781](https://www.lcsc.com/product-detail/C19272781.html)：目录 DT-26、2×6mm、12.5pF、50kΩ、±10ppm、−40～85°C。精确料号承认书下载未成功，当前为目录核对，保留采购承认书及首板起振确认事项。
7. [风华 CBG160808U601T 厂商规格](https://fhcomp.com/zh-cn/product_info/?prod_code=CBG160808U601T)：600Ω@100MHz、DCR≤0.5Ω、额定200mA。
8. 用户 `BOM-立创_20260924.xls`：BOM260924002306，2026-09-24 11:54:52。原1套配单中的实际人民币价格、MOQ、渠道、交期与错配依据，详见 `PROCUREMENT.md`。

全部替代料号和采购页面记录在 `procurement.json` / `BOM-cost-100.csv`。LCSC公开网站库存、美元价格并非国内账户成交保证，重新配单时必须确认“国内现货”和实际MOQ。RDA5807M在用户报价中为订货7–10个工作日。

A2历史参考：[TI TLV755P](https://www.ti.com/lit/ds/symlink/tlv755p.pdf)、[Nexperia PESD5V0F1BL](https://assets.nexperia.com/documents/data-sheet/PESD5V0F1BL.pdf)、[Abracon AB26T](https://abracon.com/Resonators/AB26T.pdf)。这些不是A3采购型号的参数依据。
