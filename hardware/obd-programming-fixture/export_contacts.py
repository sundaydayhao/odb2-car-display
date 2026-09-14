#!/usr/bin/env python3
"""Read actual KiCad pads; export needle coordinates without saving the board."""
import csv
import hashlib
import json
from pathlib import Path
import pcbnew as k

HERE = Path(__file__).resolve().parent
BOARD = HERE.parent / "obd-ble-node" / "obd_ble_node.kicad_pcb"
ROLES = {
    "TP1": ("GND", "common ground"),
    "TP2": ("+3V3", "high-impedance voltage sense ONLY; no power injection"),
    "TP3": ("EN", "open-collector reset from automatic download circuit"),
    "TP4": ("BOOT", "GPIO9 open-collector boot from automatic download circuit"),
    "TP5": ("U0RX", "fixture 3.3V UART TX to DUT GPIO20"),
    "TP6": ("UART_TX", "fixture UART RX from DUT GPIO21 via R12"),
    "TP7": ("USB_DM", "USB D-; disconnect for sleep current measurement"),
    "TP8": ("USB_DP", "USB D+; disconnect for sleep current measurement"),
}


def export():
    board = k.LoadBoard(str(BOARD))
    edge_points = []
    for drawing in board.GetDrawings():
        if drawing.GetLayer() == k.Edge_Cuts:
            edge_points.extend([drawing.GetStart(), drawing.GetEnd()])
    x0, y0 = (min(k.ToMM(getattr(p, axis)) for p in edge_points) for axis in ("x", "y"))
    x1, y1 = (max(k.ToMM(getattr(p, axis)) for p in edge_points) for axis in ("x", "y"))
    width, height = round(x1 - x0, 6), round(y1 - y0, 6)
    assert (width, height) == (38.5, 16.6), "Board changed; review fixture dimensions"
    rows = []
    for footprint in board.GetFootprints():
        ref = footprint.GetReference()
        for pad in footprint.Pads():
            number = pad.GetNumber()
            if ref not in ROLES and not (ref == "J1" and number in ("4", "5", "6", "14", "16")):
                continue
            net = pad.GetNetname().removeprefix("/")
            assert pad.IsOnLayer(k.B_Cu), f"{ref} not reachable on bottom copper"
            if ref in ROLES:
                assert net == ROLES[ref][0], f"{ref} net changed"
                assert k.ToMM(pad.GetSize().x) == k.ToMM(pad.GetSize().y) == 1.0
            x, y = k.ToMM(pad.GetPosition().x), k.ToMM(pad.GetPosition().y)
            bx, by = round(x - x0, 6), round(y - y0, 6)
            role = ROLES[ref][1] if ref in ROLES else {
                "4": "OBD chassis ground", "5": "OBD signal ground", "6": "CAN_H test bus",
                "14": "CAN_L test bus", "16": "switched current-limited bench input"}[number]
            rows.append({"ref": ref, "pad": number, "net": net,
                         "board_abs_x_mm": x, "board_abs_y_mm": y,
                         "board_top_x_mm": bx, "board_top_y_mm": by,
                         "needle_face_x_mm": round(width - bx, 6), "needle_face_y_mm": by,
                         "copper_width_mm": k.ToMM(pad.GetSize().x),
                         "copper_height_mm": k.ToMM(pad.GetSize().y),
                         "contact": "pogo" if ref in ROLES else "OBD adapter; slot center is NOT a needle location",
                         "role": role})
    rows.sort(key=lambda r: (r["ref"] == "J1", int(r["ref"][2:]) if r["ref"] != "J1" else int(r["pad"])))
    assert len(rows) == 13 and len({r["ref"] for r in rows if r["ref"] != "J1"}) == 8
    with (HERE / "contacts.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    meta = {"source": "../obd-ble-node/obd_ble_node.kicad_pcb",
            "board_sha256": hashlib.sha256(BOARD.read_bytes()).hexdigest(),
            "board_origin_mm": [x0, y0], "board_mm": [width, height],
            "coordinate_rule": "Top-view origin at board upper left; x right, y down. Needle face is bottom view with same top edge: x=38.5-top_x, y=top_y.",
            "mechanical_release": "NOT_RELEASED: drill diameters, supports and physical plug fit pending"}
    (HERE / "contacts-source.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="118mm" height="62mm" viewBox="0 0 118 62">',
           '<rect width="118" height="62" fill="white"/>',
           '<style>text{font-family:Arial,sans-serif;fill:#172536;font-size:1.8px}.label{font-size:1.5px}.line{fill:none;stroke:#8b98a8;stroke-width:.12}</style>',
           '<text x="5" y="4" style="font-size:2.5px;font-weight:bold">OBDNode A2 — fixture contact map</text>',
           '<text x="5" y="7">1:1 at 100% print / mm / geometry from actual PCB / NOT a drill release</text>']
    for ox, mode, title in [(9, "board_top", "PCB TOP VIEW (through board)"),
                            (69, "needle_face", "NEEDLE FACE / PCB BOTTOM VIEW")]:
        oy = 17
        svg += [f'<text x="{ox-4}" y="11">{title}</text>',
                f'<rect x="{ox}" y="{oy}" width="{width}" height="{height}" fill="#eef3f8" stroke="#20354c" stroke-width=".2"/>',
                f'<path d="M {ox} {oy-1} v -2 h {width} v 2" class="line"/>',
                f'<text x="{ox+16}" y="{oy-3.3}">38.5</text>',
                f'<text x="{ox-6}" y="{oy+9}">16.6</text>',
                f'<circle cx="{ox}" cy="{oy}" r=".45" fill="#cc4b35"/>',
                f'<text x="{ox-3}" y="{oy-1}">0,0</text>']
        rf_x = 35.25 if mode == "board_top" else 0
        svg += [f'<rect x="{ox+rf_x}" y="{oy}" width="3.25" height="5.25" fill="#f9cbba"/>']
        for row in rows:
            x, y = ox + row[mode + "_x_mm"], oy + row[mode + "_y_mm"]
            if row["ref"] == "J1":
                label_y = y + 2.6 if row["board_top_y_mm"] < 8 else y - 1.6
                svg += [f'<rect x="{x-1.6}" y="{y-1}" width="3.2" height="2" rx="1" fill="none" stroke="#929da8" stroke-width=".15"/>',
                        f'<rect x="{x-1.2}" y="{y-.6}" width="2.4" height="1.2" rx=".6" fill="white" stroke="#929da8" stroke-width=".1"/>',
                        f'<text x="{x}" y="{label_y}" text-anchor="middle" class="label">J1.{row["pad"]}</text>']
            else:
                svg += [f'<circle cx="{x}" cy="{y}" r=".5" fill="#e4a924" stroke="#8e6617" stroke-width=".1"/>']
                n = int(row["ref"][2:])
                if n in (3, 4, 5, 6):
                    lx = ox + (23 + (n-3)*5 if mode == "board_top" else 15 - (n-3)*5)
                    ly = oy + 22
                    svg += [f'<path d="M {x} {y+.6} L {lx} {ly-1.8}" class="line"/>']
                else:
                    lx, ly = x, y + (2.5 if n == 1 else -2.3)
                    if n in (7, 8):
                        left = (n == 7) == (mode == "board_top")
                        lx, ly = x + (-.65 if left else .65), y + 3.5
                        svg += [f'<path d="M {x} {y+.6} L {lx} {ly-1.8}" class="line"/>']
                svg += [f'<text x="{lx}" y="{ly}" text-anchor="middle" class="label">{row["ref"]}</text>']
    svg += ['<text x="5" y="44">Gold = bottom 1.0 mm pogo pad. Grey = selected OBD plated slots (adapter references only).</text>',
            '<text x="5" y="48">Orange = antenna zone: no metal fixture parts. Supports must also avoid components on BOTH sides.</text>',
            '<text x="5" y="52">Needle face: x = 38.5 − PCB top x; y unchanged. Keep the same top edge. Do not mirror twice.</text>',
            '<text x="5" y="56">Print scale check:</text><path d="M 25 54 v 3 M 25 55.5 h 20 M 45 54 v 3" class="line"/>',
            '<text x="31" y="59">20.0 mm</text></svg>']
    (HERE / "contact-map.svg").write_text("\n".join(svg) + "\n", encoding="utf-8")
    print(f"Exported {len(rows)} contacts from {BOARD.name}; board was not modified")


if __name__ == "__main__":
    export()
