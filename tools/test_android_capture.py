"""Analysis fixtures model exports, not real vehicle or BLE validation."""
import json
import tempfile
import unittest
from pathlib import Path
from analyze_android_capture import analyze, export


class AndroidCaptureAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "example.jsonl"

    def rows(self):
        values = [dict(kind="header", schema="vehicle-capture/1", mode="can_monitor"),
                  dict(kind="rx", hex="3745382030200D", notification_id=1),
                  dict(kind="frame", id="7E8", extended=False, dlc=0, data=""),
                  dict(kind="end", frames=1, notifications=1, unparsed=0, transport_complete=True)]
        return [dict(row, sequence=i, monotonic_us=1000+i*100, transaction=1) for i, row in enumerate(values)]

    def write(self, rows, tail=b""):
        self.path.write_bytes(b"".join((json.dumps(row)+"\n").encode() for row in rows) + tail)
        return self.path

    def test_valid_export_preserves_limits(self):
        report = export(self.write(self.rows()), Path(self.temp.name)/"out")
        self.assertTrue(report["recording_intact"])
        self.assertTrue(report["adapter_session_completed"])
        self.assertTrue(report["bus_loss_unknown"])
        self.assertEqual(report["ids"]["7E8"]["frames"], 1)
        self.assertIn("7E8", (Path(self.temp.name)/"out/frames.csv").read_text(encoding="utf-8-sig"))

    def test_partial_tail_recovers_frames(self):
        result = analyze(self.write(self.rows()[:-1], b'{"kind":"end"'))
        self.assertFalse(result["recording_intact"])
        self.assertFalse(result["adapter_session_completed"])
        self.assertEqual(result["parsed_frames"], 1)

    def test_sequence_and_notification_gaps(self):
        rows = self.rows(); rows[1].update(sequence=9, notification_id=3)
        result = analyze(self.write(rows))
        self.assertGreaterEqual(result["issue_count"], 2)

    def test_invalid_frames_rejected_and_footer_mismatch(self):
        for changes in [dict(id="FFF"), dict(dlc=9), dict(data="FF"), dict(extended="false"), dict(id="FFFFFFFF", extended=True)]:
            rows = self.rows(); rows[2].update(changes)
            result = analyze(self.write(rows))
            self.assertEqual(result["parsed_frames"], 0)
            self.assertGreaterEqual(result["issue_count"], 2)

    def test_raw_hex_and_duplicate_footer(self):
        rows = self.rows(); rows[1]["hex"] = "invalid"; rows.append(rows[-1])
        self.assertFalse(analyze(self.write(rows))["recording_intact"])

    def test_interrupted_but_sealed_is_not_completed(self):
        rows = self.rows(); rows[-1]["transport_complete"] = False
        result = analyze(self.write(rows))
        self.assertTrue(result["recording_intact"])
        self.assertFalse(result["adapter_session_completed"])

    def test_wrong_schema_refused(self):
        rows = self.rows(); rows[0]["schema"] = "other"
        with self.assertRaises(ValueError):
            analyze(self.write(rows))

    def test_does_not_overwrite_outputs(self):
        self.write(self.rows()); out = Path(self.temp.name)/"out"
        export(self.path, out)
        with self.assertRaises(ValueError):
            export(self.path, out)


if __name__ == "__main__":
    unittest.main()
