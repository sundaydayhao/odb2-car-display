"""Inspect JSONL recordings and optionally replay Mode 01 through the firmware C parser."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import subprocess

from lab_server import MAX_REPORT, validate_report, strict_json


def transactions(paths):
    groups = {}
    warnings = []
    seen = set()
    for path in paths:
        if path.stat().st_size > MAX_REPORT:
            raise ValueError(f"Report too large: {path.name}")
        body = path.read_bytes()
        first = strict_json(body.splitlines()[0])
        rows = validate_report(body, first.get("device"))
        boot = first["boot"]
        if rows[-1].get("recovered"):
            warnings.append(f"{path.name}: recovered after interruption; tail may be missing")
        for row in rows[1:-1]:
            if "seq" in row:
                identity = (first["device"], boot, row["seq"])
                if identity in seen:
                    raise ValueError("Duplicate boot sequence; provide each report once")
                seen.add(identity)
            if row["kind"] in ("gap", "parser_gap"):
                warnings.append(f"{path.name}: {row['kind']}")
            if row["kind"] not in ("tx", "rx", "result", "parser_gap"):
                continue
            key = (first["device"], boot, row["generation"], row["transaction"])
            group = groups.setdefault(key, [])
            group.append(row)
    output = []
    for key, rows in groups.items():
        rows.sort(key=lambda row: row["seq"])
        tx = [row for row in rows if row["kind"] == "tx"]
        result = [row for row in rows if row["kind"] == "result"]
        complete = len(tx) == len(result) == 1 and result[0]["result"] == 0
        raw = bytearray()
        notifications = defaultdict(list)
        for row in rows:
            if row["kind"] == "rx":
                notifications[row["notification"]].append(row)
            if row["kind"] == "parser_gap":
                complete = False
        for fragments in notifications.values():
            expected, total = 0, fragments[0]["total"]
            for fragment in fragments:
                data = bytes.fromhex(fragment["hex"])
                if fragment["offset"] != expected or fragment["total"] != total:
                    complete = False
                expected += len(data)
                raw.extend(data)
            if expected != total:
                complete = False
        request = bytes.fromhex(tx[0]["hex"]).decode("ascii", errors="replace").strip() if tx else ""
        # Transaction boundary records and all intermediate wire sequences must exist.
        if tx and result:
            complete &= tx[0]["seq"] < result[0]["seq"] and all(tx[0]["seq"] <= row["seq"] <= result[0]["seq"] for row in rows)
            complete &= result[0]["seq"] - tx[0]["seq"] < 65536
            if complete:
                complete = all((key[0], key[1], seq) in seen for seq in range(tx[0]["seq"], result[0]["seq"] + 1))
        output.append({"boot": key[1], "generation": key[2], "transaction": key[3],
                       "request": request, "complete": bool(complete), "raw_hex": raw.hex()})
    return output, warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", type=Path, nargs="+")
    parser.add_argument("--decoder", type=Path, help="build/host-protocol/replay_response.exe")
    args = parser.parse_args()
    rows, warnings = transactions(args.reports)
    for row in rows:
        if args.decoder and row["complete"] and re.fullmatch(r"01[0-9A-Fa-f]{2}", row["request"]):
            result = subprocess.run([str(args.decoder.resolve()), row["request"][2:]],
                input=bytes.fromhex(row["raw_hex"]), capture_output=True, timeout=5, check=False)
            if result.returncode not in (0, 1):
                raise RuntimeError("Native decoder failed")
            row["decode"] = strict_json(result.stdout)
        print(json.dumps(row, separators=(",", ":")))
    for warning in warnings:
        print(f"WARNING: {warning}", file=__import__("sys").stderr)


if __name__ == "__main__":
    main()
