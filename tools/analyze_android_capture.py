"""Offline reader for the Android app's vehicle-capture/1 JSONL (standard library only).

Preserves raw input; exports validated classical CAN frames and communication events.
Phone BLE delivery timestamps and ELM completion cannot establish lossless bus capture.
"""
import argparse
import csv
import json
import re
from collections import Counter
from pathlib import Path

MAX_LINE = 64 * 1024
MAX_FILE = 128 * 1024 * 1024
KINDS = {"header", "end", "frame", "rx", "tx", "line", "response", "unparsed",
         "partial_line", "capture_started"}


def analyze(source, frame_sink=None, event_sink=None):
    source = Path(source)
    if source.stat().st_size > MAX_FILE:
        raise ValueError("Input exceeds 128 MiB")
    issues, issue_count = [], 0

    def issue(message):
        nonlocal issue_count
        issue_count += 1
        if len(issues) < 100:
            issues.append(message)

    header = footer = None
    counts, ids = Counter(), {}
    expected = 0
    parsed_frames = notifications = unparsed = 0
    last_kind = None
    with source.open("rb") as stream:
        number = 0
        while True:
            raw = stream.readline(MAX_LINE + 1)
            if not raw:
                break
            number += 1
            if len(raw) > MAX_LINE:
                issue(f"Line {number}: exceeds bounded reader capacity; remaining input not parsed")
                break
            try:
                row = json.loads(raw)
                if not isinstance(row, dict):
                    raise ValueError("record must be an object")
            except (ValueError, UnicodeDecodeError) as exc:
                issue(f"Line {number}: invalid/truncated JSON ({exc})")
                continue
            kind = row.get("kind")
            if not isinstance(kind, str) or kind not in KINDS:
                issue(f"Line {number}: unknown record kind")
                continue
            if footer is not None:
                issue(f"Line {number}: data after footer")
            last_kind = kind
            counts[kind] += 1
            seq, us = row.get("sequence"), row.get("monotonic_us")
            if type(seq) is not int or seq != expected:
                issue(f"Line {number}: expected sequence {expected}, got {seq}")
            expected = seq + 1 if type(seq) is int else expected + 1
            if type(us) is not int or us < 0:
                issue(f"Line {number}: invalid timestamp")
                continue
            if kind == "header":
                if header is not None or number != 1:
                    issue("Header must occur exactly once at the start")
                if row.get("schema") != "vehicle-capture/1":
                    raise ValueError("Unsupported schema (use this tool only for Android capture exports)")
                header = row
            elif header is None:
                raise ValueError("Missing vehicle-capture/1 header")
            elif kind == "end":
                footer = row
            elif kind == "frame":
                ident, data, dlc, extended = (row.get(k) for k in ("id", "data", "dlc", "extended"))
                width, limit = (8, 0x1FFFFFFF) if extended is True else (3, 0x7FF)
                valid = (type(extended) is bool and isinstance(ident, str) and
                         re.fullmatch(rf"[0-9A-Fa-f]{{{width}}}", ident) and int(ident, 16) <= limit and
                         type(dlc) is int and 0 <= dlc <= 8 and isinstance(data, str) and
                         re.fullmatch(r"[0-9A-Fa-f]*", data) and len(data) == 2 * dlc)
                if not valid:
                    issue(f"Line {number}: invalid CAN ID, DLC or payload")
                    continue
                ident, data = ident.upper(), data.upper()
                parsed_frames += 1
                stats = ids.setdefault(ident, {"frames": 0, "extended": extended, "first_us": us, "last_us": us})
                stats["frames"] += 1
                stats["first_us"] = min(stats["first_us"], us)
                stats["last_us"] = max(stats["last_us"], us)
                if frame_sink:
                    frame_sink([seq, us, f"{(us-header['monotonic_us'])/1e6:.6f}", ident, int(extended), dlc, data])
            else:
                if kind == "rx":
                    value = row.get("hex")
                    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9A-Fa-f]{2})+", value):
                        issue(f"Line {number}: invalid raw notification hex")
                    notifications += 1
                    if row.get("notification_id") != notifications:
                        issue(f"Line {number}: missing/reordered raw notification")
                if kind in {"unparsed", "partial_line"}:
                    unparsed += 1
                if event_sink:
                    value = str(row.get("hex" if kind == "rx" else "value", ""))
                    # Opening events.csv in a spreadsheet must not execute adapter text as a formula.
                    if value.startswith(("=", "+", "-", "@", "\t")):
                        value = "'" + value
                    event_sink([seq, us, row.get("transaction", ""), kind, value])
    if header is None:
        raise ValueError("Empty input or missing header")
    if footer is None or last_kind != "end":
        issue("No final footer: recording was not sealed or is truncated")
    if footer is not None:
        for key, actual in (("frames", parsed_frames), ("unparsed", unparsed), ("notifications", notifications)):
            if footer.get(key) != actual:
                issue(f"Footer {key}={footer.get(key)} differs from observed {actual}")
        if type(footer.get("transport_complete")) is not bool:
            issue("Footer missing boolean transport_complete")
    warnings = ["Timestamps are Android BLE delivery times, not hardware bus timestamps.",
                "Bus loss cannot be excluded, even for a sealed and completed recording."]
    if unparsed:
        warnings.append(f"{unparsed} lines were not parsed as CAN frames; inspect events.csv and raw JSONL.")
    if not parsed_frames and header.get("mode") == "can_monitor":
        warnings.append("No valid CAN frames: this does not establish that the vehicle bus was idle.")
    return {"schema": "vehicle-capture-analysis/1", "source": source.name,
            "capture": header, "footer": footer, "recording_intact": issue_count == 0,
            "adapter_session_completed": bool(footer and footer.get("transport_complete") is True),
            "bus_loss_unknown": True, "record_counts": dict(counts), "parsed_frames": parsed_frames,
            "ids": ids, "issue_count": issue_count, "issues": issues, "warnings": warnings}


def export(source, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    targets = [output / name for name in ("frames.csv", "events.csv", "summary.json")]
    if any(path.exists() for path in targets):
        raise ValueError("Output files already exist; choose another directory")
    with targets[0].open("x", newline="", encoding="utf-8-sig") as f, targets[1].open("x", newline="", encoding="utf-8-sig") as e:
        frames, events = csv.writer(f), csv.writer(e)
        frames.writerow(["sequence", "monotonic_us", "seconds_from_header", "can_id", "extended", "dlc", "data_hex"])
        events.writerow(["sequence", "monotonic_us", "transaction", "kind", "value_or_rx_hex"])
        report = analyze(source, frames.writerow, events.writerow)
    with targets[2].open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Directory for CSV and JSON analysis")
    args = parser.parse_args()
    try:
        report = export(args.capture, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"Analysis failed: {exc}\n")
    print(f"Frames: {report['parsed_frames']}; CAN IDs: {len(report['ids'])}; integrity issues: {report['issue_count']}")
    print(f"Output: {args.output.resolve()}")
    if not report["recording_intact"] or not report["adapter_session_completed"]:
        parser.exit(1, "Recording is incomplete or interrupted; recovered data remains available.\n")


if __name__ == "__main__":
    main()
