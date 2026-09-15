# USB HS ESD correction — D21 / D22

D21 and D22 are fitted **Texas Instruments TPD1E05U06DPYR**, LCSC **C436349**. DPYR is ACTIVE on the [TI orderable page](https://www.ti.com/product/TPD1E05U06/part-details/TPD1E05U06DPYR); DPYT is a different orderable and is not the BOM selection. The [LCSC listing](https://www.lcsc.com/product-detail/C436349.html) confirms a domestic distributor listing for the TI part. Availability is a point-in-time check, not a supply commitment.

The former onsemi ESD7951ST5G was DNP and its current original datasheet is marked discontinued. The two retained USB HS signal lines now receive fitted protection. This change does not certify whole-product ESD immunity.

| Reference | Pin 1: I/O | Pin 2 | Fitted |
|---|---|---|---|
| D21 | USB_DP_A | GND | Yes |
| D22 | USB_DM_A | GND | Yes |

The [TI datasheet SLVSBO7O](https://www.ti.com/lit/ds/symlink/tpd1e05u06.pdf), table 4-1, defines pin 1 I/O and pin 2 GND. For DPY, typical capacitance is 0.42 pF at VIO = 2.5 V, 1 MHz; working voltage is 5.5 V. TI specifies high-speed interfaces up to 6 Gbit/s. These specifications support use on this USB 2.0 HS port; no invented maximum capacitance is used.

The dedicated local footprint `Display:TPD1E05U06_DPY0002A_TI` follows TI pages 34–36: body 1.0 × 0.6 mm, maximum height 0.45 mm, two 0.30 × 0.50 mm lands at 0.70 mm pitch, corner radius 0.05 mm. Solder mask clearance is 0.05 mm using the permitted non-solder-mask-defined option. Pin 1 has a matching dot and fabrication stripe. Other SOD882 instances remain unchanged.

Both existing footprint positions, rotations, UUIDs and four physical pad UUIDs/centers/copper nets are preserved. Pad numbers are deliberately swapped to match the TI part: the former physical signal pad 2 becomes pin 1, and former ground pad 1 becomes pin 2. The new explicit schematic symbol preserves wire endpoints and names I/O/GND correctly. No copper track, via, routing width or USB length tuning changes.

The STEP is the C436349 supplier model, acquired via the project component workflow. It is not an official TI mechanical model. Its 1.0 × 0.6 mm body and 0.39 mm total envelope were measured; a +0.03 mm Z offset places the source seating bottom at zero. Native KiCad coupon rendering confirms its left-hand marking matches local pin 1 and the leads sit on the lands. Electrical pin numbering and the land geometry are independently defined from TI, rather than copied from the supplier footprint.

Validation: native ERC 0, DRC 0 and unconnected items 0 on the final integrated board; all 1161 numbered SCH/PCB nodes agree. D21/D22 are fitted with pin 1 connected to the respective USB data net and pin 2 to ground. Their physical pad UUIDs, centers and copper nets were preserved during the replacement. Footprint/model bindings, pin-1 orientation and schematic text were reviewed. See [VERIFICATION.md](VERIFICATION.md) for the final board hash and whole-board checks. These CAD checks do not replace assembled-product ESD testing.
