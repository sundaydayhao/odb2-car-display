# A7 USB OTG 删除与保留保护范围

A7 已按需求取消 J18 USB OTG / HS 外接口。删除 J18、D21/D22、R220/R221、R234、U9、C72/C73/C74/C75、D15、R145/R151/R152、TP28、R187；对应 USB_DM/DP、USB_DM/DP_A、VBUS_EN/OUT、N18248775、GND_EARTH 专属铜线与铺铜一并删除。P4 的 USB HS 数据脚 49/50 明确标记未连接。

J19 USB Serial/JTAG、GPIO24/25、该调试口保护与供电路径继续保留；C71 公共 5 V 去耦、主电源灯以及 P4 必需的供电/偏置也保留。下面仅记录已经取消的 A6 ESD 选型历史，不是 A7 的装配要求。完整删除及保留核验由 `tools/check_display_hardware.py` 完成；整机 ESD 仍需装配后实测。

## A6 历史资料：D21 / D22

The historical A6 design fitted D21 and D22 as **Texas Instruments TPD1E05U06DPYR**, LCSC **C436349**. The [TI orderable page](https://www.ti.com/product/TPD1E05U06/part-details/TPD1E05U06DPYR) and [LCSC listing](https://www.lcsc.com/product-detail/C436349.html) were the selection sources. A7 removes both parts with the host port.

The former onsemi ESD7951ST5G was DNP and its current original datasheet is marked discontinued. The two retained USB HS signal lines now receive fitted protection. This change does not certify whole-product ESD immunity.

| Reference | Pin 1: I/O | Pin 2 | Fitted |
|---|---|---|---|
| D21 | USB_DP_A | GND | Yes |
| D22 | USB_DM_A | GND | Yes |

The [TI datasheet SLVSBO7O](https://www.ti.com/lit/ds/symlink/tpd1e05u06.pdf), table 4-1, defines pin 1 I/O and pin 2 GND. For DPY, typical capacitance is 0.42 pF at VIO = 2.5 V, 1 MHz; working voltage is 5.5 V. TI specifies high-speed interfaces up to 6 Gbit/s. These specifications support use on this USB 2.0 HS port; no invented maximum capacitance is used.

The dedicated local footprint `Display:TPD1E05U06_DPY0002A_TI` follows TI pages 34–36: body 1.0 × 0.6 mm, maximum height 0.45 mm, two 0.30 × 0.50 mm lands at 0.70 mm pitch, corner radius 0.05 mm. Solder mask clearance is 0.05 mm using the permitted non-solder-mask-defined option. Pin 1 has a matching dot and fabrication stripe. Other SOD882 instances remain unchanged.

During the historical A6 ESD-only migration, both existing footprint positions, rotations, UUIDs and four physical pad UUIDs/centers/copper nets are preserved. Pad numbers are deliberately swapped to match the TI part: the former physical signal pad 2 becomes pin 1, and former ground pad 1 becomes pin 2. The new explicit schematic symbol preserves wire endpoints and names I/O/GND correctly. That historical migration did not change copper. A7 subsequently repositions interfaces and reroutes signals; revalidate against its final PCB.

The STEP is the C436349 supplier model, acquired via the project component workflow. It is not an official TI mechanical model. Its 1.0 × 0.6 mm body and 0.39 mm total envelope were measured; a +0.03 mm Z offset places the source seating bottom at zero. Native KiCad coupon rendering confirms its left-hand marking matches local pin 1 and the leads sit on the lands. Electrical pin numbering and the land geometry are independently defined from TI, rather than copied from the supplier footprint.

The historical A6 ESD migration passed native ERC/DRC and its 1161-node check. This result does not cover A7. The A7 microphone migration and host-port removal produce 399 board components and 1120 numbered nodes. Final A7 results and PCB hash belong in [VERIFICATION.md](VERIFICATION.md). These CAD checks do not replace assembled-product ESD testing.
