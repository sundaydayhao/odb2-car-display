#!/usr/bin/env python3
"""Single-station OBDNode-C3 programmer. No port is opened without `flash`."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

ESPTOOL_VERSION = "4.12.0"
PROJECT = "obd_ble_node"
SETTINGS = {"flash_mode": "dio", "flash_size": "4MB", "flash_freq": "40m"}
# A2/A3 factory layout. Fail closed if partitions change; review the station then.
REGIONS = {"bootloader": (0, 0x8000), "partition-table": (0x8000, 0x9000),
           "app": (0x10000, 0x210000)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def utc():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def local_file(root, name):
    require(isinstance(name, str) and name, "Missing image path")
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), f"Image outside package/build: {name}")
    require(path.is_file(), f"Missing image: {name}")
    return path


def check_runtime():
    import esptool
    require(esptool.__version__ == ESPTOOL_VERSION,
            f"Use esptool {ESPTOOL_VERSION}; found {esptool.__version__}")


def check_image(path):
    from esptool.bin_image import LoadFirmwareImage
    from esptool.targets.esp32c3 import ESP32C3ROM
    image = LoadFirmwareImage("esp32c3", str(path))
    require(image.chip_id == ESP32C3ROM.IMAGE_CHIP_ID, f"Not an ESP32-C3 image: {path}")
    require(image.checksum == image.calculate_checksum(), f"Image checksum failed: {path}")
    require(image.append_digest and image.stored_digest == image.calc_digest,
            f"Image digest missing or invalid: {path}")
    require(image.flash_mode == 2 and image.flash_size_freq == 0x20,
            f"Image must already contain DIO / 4MB / 40MHz header: {path}")


def validate(package):
    check_runtime()
    manifest = read_json(package / "manifest.json")
    require(manifest.get("schema") == 1 and manifest.get("project") == PROJECT,
            "Wrong package/project")
    require(manifest.get("chip") == "esp32c3", "Wrong chip")
    require(manifest.get("esptool_version") == ESPTOOL_VERSION, "Wrong esptool version")
    require(manifest.get("flash_settings") == SETTINGS, "Wrong flash settings")
    images = manifest.get("images", [])
    require(len(images) == 3 and {i["role"] for i in images} == set(REGIONS),
            "Expected exactly bootloader, partition-table and app")
    for item in images:
        path = local_file(package, item["file"])
        start, end = REGIONS[item["role"]]
        require(item["offset"] == start, f"Unexpected {item['role']} offset")
        size = path.stat().st_size
        require(0 < size == item["size"] and start + ((size + 4095) // 4096) * 4096 <= end,
                f"Image exceeds approved erase region: {item['role']}")
        require(digest(path) == item["sha256"], f"SHA256 mismatch: {item['file']}")
        if item["role"] != "partition-table":
            check_image(path)
        else:
            check_partitions(path)
    # Exact partition bytes captured from the IDF build, protected by package SHA256.
    # The host package is a consistency check, not a signed release authorization.
    return manifest


def make_package(build, output, revision, hardware="A3"):
    check_runtime()
    require(not output.exists(), "Output already exists; use a new package directory")
    desc = read_json(build / "project_description.json")
    args = read_json(build / "flasher_args.json")
    require(desc.get("project_name") == PROJECT and desc.get("target") == "esp32c3",
            "Build is not the OBDNode-C3 project")
    require(str(desc.get("git_revision", "")).startswith("v5.5."), "Expected IDF v5.5.x")
    require(args.get("extra_esptool_args", {}).get("chip") == "esp32c3", "Wrong flash chip")
    require(args.get("flash_settings") == SETTINGS, "Wrong flash settings")
    require(len(args.get("flash_files", {})) == 3, "Unexpected extra flash images")
    manifest = {"schema": 1, "project": PROJECT, "chip": "esp32c3", "created_utc": utc(),
                "source_revision": revision, "project_version": desc.get("project_version"),
                "idf_version": desc.get("git_revision"), "esptool_version": ESPTOOL_VERSION,
                "flash_settings": SETTINGS, "hardware": "OBDNode " + hardware,
                "release_status": "engineering-unqualified", "images": []}
    sources = []
    for role, (start, _) in REGIONS.items():
        entry = args[role]
        require(entry.get("encrypted") in ("false", False), "Encrypted image not supported")
        require(int(entry["offset"], 0) == start, f"Changed {role} layout; review station")
        require(args["flash_files"].get(entry["offset"]) == entry["file"], "Inconsistent flash files")
        src = local_file(build, entry["file"])
        name = role + ".bin"
        manifest["images"].append({"role": role, "offset": start, "file": name,
                                   "size": src.stat().st_size, "sha256": digest(src)})
        sources.append((src, name))
    output.mkdir(parents=True)
    for src, name in sources:
        shutil.copyfile(src, output / name)
    write_json(output / "manifest.json", manifest)
    validate(output)
    return manifest


def check_partitions(path):
    """Read the standard 32-byte ESP-IDF partition entries; reject layout drift."""
    import struct
    entries = []
    data = path.read_bytes()
    md5_found = False
    for i in range(0, len(data), 32):
        entry = data[i:i + 32]
        if len(entry) != 32:
            raise ValueError("Truncated partition table")
        if entry[:2] == b"\xeb\xeb":
            require(entry[:16] == b"\xeb\xeb" + b"\xff" * 14 and
                    entry[16:] == hashlib.md5(data[:i]).digest(), "Partition MD5 failed")
            require(all(b == 0xff for b in data[i + 32:]), "Unexpected partition trailer")
            md5_found = True
            break
        if entry[:2] == b"\xff\xff":
            break
        magic, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
        require(magic == 0x50AA, "Invalid partition magic")
        entries.append((label.rstrip(b"\0").decode("ascii"), kind, subtype, offset, size, flags))
    expected = [("nvs", 1, 2, 0x9000, 0x6000, 0), ("phy_init", 1, 1, 0xf000, 0x1000, 0),
                ("factory", 0, 0, 0x10000, 0x200000, 0),
                ("capture", 1, 0x82, 0x210000, 0x1f0000, 0)]
    require(md5_found and entries == expected,
            "Partition layout changed or missing MD5; review factory/NVS/capture boundaries")


def commands(package, manifest, port):
    base = [sys.executable, "-m", "esptool", "--chip", "esp32c3", "--port", port,
            "--baud", "460800", "--before", "default_reset"]
    files = []
    for item in sorted(manifest["images"], key=lambda x: x["offset"]):
        files.extend([hex(item["offset"]), str(local_file(package, item["file"]))])
    # Images have already been checked for correct header. Preserve byte-for-byte for verify.
    return [("identity", base + ["--after", "no_reset", "read_mac"]),
            ("write", base + ["--after", "no_reset", "write_flash", "--flash_mode", "keep",
                              "--flash_size", "keep", "--flash_freq", "keep"] + files),
            ("verify", base + ["--after", "hard_reset", "verify_flash"] + files)]


def run(command):
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=180)


def flash(package, port, serial, station, reports, runner=run):
    manifest = validate(package)
    require(port.strip() and serial.strip() and station.strip(), "Port, unit serial and station required")
    reports.mkdir(parents=True, exist_ok=True)
    report_path = reports / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
                             + "_" + uuid.uuid4().hex[:12] + ".json")
    report = {"started_utc": utc(), "serial": serial, "station": station, "port": port,
              "package": manifest, "manifest_sha256": digest(package / "manifest.json"),
              "status": "IN_PROGRESS", "fct_status": "NOT_RUN", "steps": []}
    write_json(report_path, report)  # A writable traceability directory is required before connecting.
    try:
        for stage, command in commands(package, manifest, port):
            print(f"{stage}: {' '.join(command)}", flush=True)
            result = runner(command)
            output = (result.stdout or "") + (result.stderr or "")
            report["steps"].append({"stage": stage, "exit_code": result.returncode, "output": output})
            print(output, flush=True)
            require(result.returncode == 0, f"{stage} failed ({result.returncode})")
            if stage == "identity":
                match = re.search(r"\bMAC:\s*([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\b", output)
                require(match is not None, "ROM MAC was not found; do not program")
                report["rom_mac"] = match.group(1).lower()
            write_json(report_path, report)
        report["status"] = "FLASH_VERIFIED"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "FAILED"
        report["error"] = str(error)
        raise
    finally:
        report["finished_utc"] = utc()
        write_json(report_path, report)
        print(f"Report: {report_path}")
    return report_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    pack = sub.add_parser("package", help="Create an offline engineering package from the node IDF build")
    pack.add_argument("--build", type=Path, required=True)
    pack.add_argument("--out", type=Path, required=True)
    pack.add_argument("--source-revision", required=True, help="Audited source git revision, append -dirty if applicable")
    pack.add_argument("--hardware",choices=["A2","A3"],default="A3",help="Audited PCB revision; firmware pin map is unchanged")
    inspect = sub.add_parser("inspect", help="Validate and print commands WITHOUT opening a port")
    inspect.add_argument("package", type=Path)
    inspect.add_argument("--port", default="COM_EXPLICIT")
    program = sub.add_parser("flash", help="WRITE a named DUT using an automatic UART download circuit")
    program.add_argument("package", type=Path)
    program.add_argument("--port", required=True)
    program.add_argument("--serial", required=True)
    program.add_argument("--station", required=True)
    program.add_argument("--reports", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "package":
            make_package(args.build.resolve(), args.out.resolve(), args.source_revision,args.hardware)
            print(f"Engineering package validated: {args.out.resolve()}")
        elif args.action == "inspect":
            manifest = validate(args.package.resolve())
            print(json.dumps(manifest, indent=2, ensure_ascii=False))
            for stage, cmd in commands(args.package.resolve(), manifest, args.port):
                print(stage, json.dumps(cmd, ensure_ascii=False))
            print("OFFLINE ONLY: no device opened. FCT NOT RUN.")
        else:
            flash(args.package.resolve(), args.port, args.serial, args.station, args.reports.resolve())
    except Exception as error:
        parser.exit(1, f"ERROR: {error}\n")


if __name__ == "__main__":
    main()
