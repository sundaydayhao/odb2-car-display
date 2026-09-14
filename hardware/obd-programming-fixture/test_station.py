"""Host-only tests. Synthetic images and a fake subprocess runner; no serial I/O."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from esptool.bin_image import ESP32C3FirmwareImage, ImageSegment
import station


class StationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.package = self.root / "package"
        self.reports = self.root / "reports"
        for filename in ("bootloader.bin", "app.bin"):
            image = ESP32C3FirmwareImage()
            image.flash_mode = 2
            image.flash_size_freq = 0x20
            segment = ImageSegment(0x3fc80000, bytes(range(32)))
            segment.name = ".data"
            image.segments = [segment]
            image.save(str(self.build / filename))
        rows = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xf000, 0x1000),
                ("factory", 0, 0, 0x10000, 0x200000), ("capture", 1, 0x82, 0x210000, 0x1f0000)]
        data = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, offset, size,
                                    name.encode(), 0) for name, kind, subtype, offset, size in rows)
        data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
        (self.build / "partitions.bin").write_bytes(data + b"\xff" * (0xc00 - len(data)))
        station.write_json(self.build / "project_description.json", {
            "project_name": "obd_ble_node", "target": "esp32c3", "git_revision": "v5.5.5",
            "project_version": "synthetic-test"})
        args = {"extra_esptool_args": {"chip": "esp32c3"}, "flash_settings": station.SETTINGS,
                "flash_files": {"0x0": "bootloader.bin", "0x8000": "partitions.bin", "0x10000": "app.bin"}}
        for role, offset, filename in [("bootloader", "0x0", "bootloader.bin"),
                                       ("partition-table", "0x8000", "partitions.bin"),
                                       ("app", "0x10000", "app.bin")]:
            args[role] = {"offset": offset, "file": filename, "encrypted": "false"}
        station.write_json(self.build / "flasher_args.json", args)
        station.make_package(self.build, self.package, "SYNTHETIC-TEST-NOT-A-RELEASE")

    def edit_manifest(self, edit):
        path = self.package / "manifest.json"
        manifest = station.read_json(path)
        edit(manifest)
        station.write_json(path, manifest)

    def test_valid_package_commands_preserve_sparse_layout(self):
        manifest = station.validate(self.package)
        cmds = station.commands(self.package, manifest, "FAKE_PORT")
        self.assertEqual([stage for stage, _ in cmds], ["identity", "write", "verify"])
        command = cmds[1][1]
        self.assertTrue(all(hex(n) in command for n in (0, 0x8000, 0x10000)))
        self.assertFalse(any(word in command for word in ("erase_flash", "--force", "--encrypt")))
        self.assertNotIn("0x9000", command)

    def test_tampered_file_rejected_before_port_open(self):
        app = self.package / "app.bin"
        data = bytearray(app.read_bytes())
        data[-1] ^= 1
        app.write_bytes(data)
        calls = []
        with self.assertRaisesRegex(ValueError, "SHA256"):
            station.flash(self.package, "FAKE", "SN", "ST", self.reports,
                          runner=lambda command: calls.append(command))
        self.assertEqual(calls, [])

    def test_wrong_chip(self):
        self.edit_manifest(lambda m: m.update(chip="esp32s3"))
        with self.assertRaisesRegex(ValueError, "Wrong chip"):
            station.validate(self.package)

    def test_other_project_build(self):
        station.write_json(self.build / "project_description.json", {
            "project_name": "display", "target": "esp32c3", "git_revision": "v5.5.5"})
        with self.assertRaisesRegex(ValueError, "not the OBDNode"):
            station.make_package(self.build, self.root / "wrong-project", "test")

    def test_nvs_overlap(self):
        self.edit_manifest(lambda m: m["images"][2].update(offset=0x9000))
        with self.assertRaisesRegex(ValueError, "offset"):
            station.validate(self.package)

    def test_app_erase_crosses_capture_boundary(self):
        app = self.package / "app.bin"
        app.write_bytes(b"\0" * (0x200000 + 1))
        self.edit_manifest(lambda m: m["images"][2].update(size=app.stat().st_size,
                                                          sha256=station.digest(app)))
        with self.assertRaisesRegex(ValueError, "erase region"):
            station.validate(self.package)

    def test_file_escape(self):
        (self.root / "outside.bin").write_bytes(b"secret")
        self.edit_manifest(lambda m: m["images"][2].update(file="../outside.bin"))
        with self.assertRaisesRegex(ValueError, "outside"):
            station.validate(self.package)

    def test_bad_inner_image_checksum_even_if_package_hash_updated(self):
        app = self.package / "app.bin"
        data = bytearray(app.read_bytes())
        data[40] ^= 1
        app.write_bytes(data)
        self.edit_manifest(lambda m: m["images"][2].update(sha256=station.digest(app)))
        with self.assertRaisesRegex(ValueError, "checksum|digest"):
            station.validate(self.package)

    def test_partition_layout_drift_even_if_checksums_updated(self):
        table = self.package / "partition-table.bin"
        data = bytearray(table.read_bytes())
        struct.pack_into("<I", data, 2 * 32 + 8, 0x210000)  # grow factory into capture
        data[144:160] = hashlib.md5(data[:128]).digest()
        table.write_bytes(data)
        self.edit_manifest(lambda m: m["images"][1].update(sha256=station.digest(table)))
        with self.assertRaisesRegex(ValueError, "Partition layout"):
            station.validate(self.package)

    def simulated_flash(self, outputs, expect_error=False):
        calls = []
        def runner(command):
            calls.append(command)
            returncode, text = outputs[len(calls) - 1]
            return subprocess.CompletedProcess(command, returncode, text, "")
        with contextlib.redirect_stdout(io.StringIO()):
            if expect_error:
                with self.assertRaises(ValueError):
                    station.flash(self.package, "FAKE_PORT", "TEST-001", "HOST-TEST", self.reports, runner)
            else:
                station.flash(self.package, "FAKE_PORT", "TEST-001", "HOST-TEST", self.reports, runner)
        paths = list(self.reports.glob("*.json"))
        self.assertEqual(len(paths), 1)
        report = json.loads(paths[0].read_text(encoding="utf-8"))
        self.assertEqual(report["fct_status"], "NOT_RUN")
        return calls, report

    def test_identity_missing_stops_before_write(self):
        calls, report = self.simulated_flash([(0, "connected without identity")], True)
        self.assertEqual(len(calls), 1)
        self.assertEqual(report["status"], "FAILED")

    def test_write_failure_stops_before_verify(self):
        calls, report = self.simulated_flash([(0, "MAC: 02:00:00:00:00:01"), (1, "write failure")], True)
        self.assertEqual(len(calls), 2)
        self.assertEqual(report["status"], "FAILED")

    def test_verify_failure_cannot_pass(self):
        _, report = self.simulated_flash([(0, "MAC: 02:00:00:00:00:01"), (0, "wrote"),
                                          (1, "verify mismatch")], True)
        self.assertEqual(report["status"], "FAILED")

    def test_success_is_only_flash_verified(self):
        calls, report = self.simulated_flash([(0, "MAC: 02:00:00:00:00:01"), (0, "wrote"),
                                             (0, "verified")])
        self.assertEqual(len(calls), 3)
        self.assertEqual(report["status"], "FLASH_VERIFIED")
        self.assertEqual(report["rom_mac"], "02:00:00:00:00:01")

    def test_cli_requires_explicit_port(self):
        result = subprocess.run([sys.executable, str(Path(station.__file__)), "flash", str(self.package)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--port", result.stderr)


if __name__ == "__main__":
    unittest.main()
