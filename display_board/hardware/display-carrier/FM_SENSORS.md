# 板载 FM、光照与温湿度 · 修订 R

2026-09-25。沿用板载方案；FM 为 FSV4730，SHT40 按本次降本改回无膜版。当前依据见 [COST_REDUCTION.md](COST_REDUCTION.md)，前次 FM 变更见 SOURCING_REVIEW.md。

| 功能 | 当前默认装配 | 7 位地址 | F 面原点坐标 / mm |
| --- | --- | --- | --- |
| FM | U815 FSV4730-D60-GU / C19191931 | 0x11 | 104,64 |
| 光照 | U1300 OPT3001DNPR / C90462 | 0x45 | 87,4 |
| 空气温湿度 | U1301 SHT40-AD1B-R2 / C2909890 | 0x44 | 23,80 |

三项使用 P4 GPIO32/33。传感器保留 +3V3、22Ω 串阻、100nF 近端去耦和1µF储能，复用原系统 4.7kΩ 上拉；OPT3001 中断 NC，轮询。SHT40 保留底边隔热槽和禁铺铜，无膜感湿窗口保持通风并防液体/污染，模型高度 0.6mm。

FM 为独立可关断 3.0V 支路，Q810/Q811 隔离 I²C。FSV4730 新增 MCP120 复位、DBYP 470nF 与双脚晶体负载；SEN 接地，FM 天线串1nF C0G及ESD。上电后系统 I²C 保持空闲1s，再使用晶体模式与 CTS 状态；FM 不支持 RDS。左右声道分别进入选择器及双功放，先静音再切源。详见 [当前音频架构](AUDIO_ARCHITECTURE.md)。

本版重新核查 SCH、网表与 PCB 焊盘一致性；没有把前版 RDA5807 的复核结果直接用于新芯片。C814 到 FSV4730 VA 焊盘中心距 2.477mm，其他旁路距离见 SOURCING_REVIEW.json；实际回路尚未布线。

整板 0 ERC error、13 个既有接口 warning、0 物理 DRC、0 SCH-PCB 差异，338 个实体器件模型可解析；当前为 1242 个走线/过孔对象、519 项未连接。光窗、通风、自热校准、FM接收/起振及固件均未实机验证。A8 的 DHT20/VEML7700 固件不能直接当作本载板驱动。见 CABIN_SENSORS.md / DESIGN_STATUS.md。
