# 单板采购表 · 修订 R

2026-09-25，按用户提供的立创报价重新核查价格和交期。导出数量只表示 **一个板子的装配用量**，不乘批量，不加备料，不按起订量放大。主控和 ML307 保留。

| 文件 | 内容 |
| --- | --- |
| [PROCUREMENT_PER_BOARD.csv](PROCUREMENT_PER_BOARD.csv) | 立创配单用表，六列：商品编号、型号、品牌、封装、位号、数量；按相同商品编号合并 |
| [PROCUREMENT_PER_BOARD.xlsx](PROCUREMENT_PER_BOARD.xlsx) | 单板BOM、另购及不装、价格与交期，共三页；配单仅使用第一页 |
| [PROCUREMENT_REFERENCES.csv](PROCUREMENT_REFERENCES.csv) | 338 个实体位置逐位号记录，Fitted=1/0，便于回查原理图 |
| [COST_REDUCTION.md](COST_REDUCTION.md) | 本次五类、八个位置的替换及保留原因、资料和验证边界 |
| [PROCUREMENT_REVIEW.json](PROCUREMENT_REVIEW.json) | 分组、库存观察、另购/待定/DNP 的机器可读记录 |
| [LCSC_STOCK_SNAPSHOT.json](LCSC_STOCK_SNAPSHOT.json) | 原报价及公开商品页的库存快照；各条记录标明来源和时间 |
| [PROCUREMENT_AUDIT.json](PROCUREMENT_AUDIT.json) | 单板计数、覆盖率及输入文件 SHA-256 |

单板默认装配 331 个实体器件：86 个已选立创料号覆盖 328 个位置；U100 主控、U1000 ML307 两项单采；F300 保险丝待电池与线材条件定型。另有 7 个 DNP，默认用量为零。后面三项和 DNP 不混入立创自动匹配表，另页完整保留。

原报价把 U100 匹配成 M5Stack 商品（407.137542 元、10 个工作日），并将未定型 F300 及部分 DNP C104x 错配；这些报价不能代表本板真实器件成本。原始 `PROCUREMENT_100PCS-立创_20260925.xls` 原样保留作为依据，不是当前配单表。旧批量 CSV/XLSX 已移入 `.history/procurement-before-per-board-20260925/`。

86 个现用立创料号均观察到国内直营库存足够一板；`STOCK_OK` 只表示覆盖本表单板用量，未锁货，不代表大批量或长期供应。BQ25896RTWT 仅观察到 72 个现货，换包装型号是为缩短交期，价格反而较高。

价格页的原价来自用户报价的各行阶梯；新价为网页参考起价，阶梯未核，不能直接相减作为准确节省额。当前 PCB 部分布线，采购表与设计检查不构成投板或量产放行。
