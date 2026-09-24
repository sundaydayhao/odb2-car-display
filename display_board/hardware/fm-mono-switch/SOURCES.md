# 设计依据

核对日期：2026-09-24。以下资料用于电气和封装设计，并不代表已进行实物验证。

1. 用户 `fm-module/RDA5807M datasheet_v1.pdf`，V1.0，23 页。工程内副本 `reference/RDA5807M.pdf`。重点为裸芯片引脚、推荐电路、32.768 kHz 时钟、I²C 寄存器与 MSOP-10 封装。目录中的 RRD-102 模块截图只作应用背景，模块焊盘顺序不用于裸芯片封装。
2. [SG Micro SGM3718 官方手册](https://www.sg-micro.com/rect/assets/3d47e91a-f3bb-4f8e-9305-8e041848aeda/SGM3718.pdf)：引脚、NC/NO 真值表、3.3 V 下负模拟信号范围；第 11 页推荐焊盘。SGM3718 封装按厂商尺寸创建，供应商模型仅作外观复核。
3. [Espressif ESP32-P4 Function EV Board v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/user_guide_v1.4.html#header-block)：开发板 J1 针脚；页面链接的官方 v1.4 原理图和 PCB 丝印用于 R83/R84、C39/C40、C58/C59、NS4150B、J16 的定位。
4. [TI TLV755P 官方手册](https://www.ti.com/lit/ds/symlink/tlv755p.pdf)：TLV75530PDBVR 固定 3.0 V 型号、SOT-23-5 引脚和输入输出电容要求。符号外形复用兼容五脚 LDO 图形，实际值和针脚按 TLV75530 核对。
5. [Nexperia PESD5V0F1BL 官方手册](https://assets.nexperia.com/documents/data-sheet/PESD5V0F1BL.pdf)：低电容双向 ESD 管、典型 0.4 pF、SOD-882。
6. [Abracon AB26T 官方手册](https://abracon.com/Resonators/AB26T.pdf)：32.768 kHz、CL=12.5 pF、ESR、圆柱外形和可成形引脚。该晶振与 RDA5807M 的起振裕量仍需首板验证。

供应商候选料号：SGM3718YUWQ10G/TR → C700220；RDA5807M → C82537。此处未作实时库存或价格承诺。阻容、LDO、晶振、ESD 管的完整供应商料号需采购时按 BOM 规格核对。
