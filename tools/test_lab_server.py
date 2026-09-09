import hashlib
import http.client
import json
from pathlib import Path
import ssl
import tempfile
import threading
import unittest
import base64
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding

from lab_server import BOARD, LabServer, initialize, strict_json, publish
from replay_recording import transactions


class LabTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        initialize(cls.root, [])
        cls.server = LabServer(("127.0.0.1", 0), cls.root)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.context = ssl.create_default_context(cafile=str(cls.root / "trust" / "service_ca.pem"))
        cls.device = "a" * 32
        cls.report = "veh-" + "b" * 16 + "-00000001.jsonl"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.temp.cleanup()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPSConnection("localhost", self.server.server_port, context=self.context, timeout=5)
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        result = response.status, strict_json(response.read())
        conn.close()
        return result

    def body(self, extra=None):
        rows = [{"kind": "header", "schema": 1, "device": self.device, "boot": "b" * 16, "board": BOARD}]
        rows.extend(extra or [])
        rows.append({"kind": "end", "recovered": False, "dropped_boot_total": 0})
        return b"".join(json.dumps(row).encode() + b"\n" for row in rows)

    def headers(self, body):
        return {"Authorization": f"Bearer {self.server.token}", "X-Device-Id": self.device,
                "X-Report-Id": self.report, "X-Content-SHA256": hashlib.sha256(body).hexdigest()}

    def test_idempotent_and_conflicting_upload(self):
        body = self.body()
        for _ in range(2):
            status, ack = self.request("POST", "/v1/reports", body, self.headers(body))
            self.assertEqual(status, 200)
            self.assertEqual(ack["sha256"], hashlib.sha256(body).hexdigest())
            self.assertEqual(ack["size"], len(body))
        changed = self.body([{"kind": "gap", "dropped": 1}])
        self.assertEqual(self.request("POST", "/v1/reports", changed, self.headers(changed))[0], 409)

    def test_auth_required(self):
        self.assertEqual(self.request("GET", "/v1/ota/manifest")[0], 401)
        self.assertEqual(self.request("POST", "/v1/reports", self.body())[0], 401)

    def test_digest_and_path_rejected(self):
        body = self.body()
        headers = self.headers(body)
        headers["X-Content-SHA256"] = "0" * 64
        self.assertEqual(self.request("POST", "/v1/reports", body, headers)[0], 400)
        headers = self.headers(body)
        headers["X-Report-Id"] = "../../private/token.txt"
        self.assertEqual(self.request("POST", "/v1/reports", body, headers)[0], 400)

    def test_wrong_schema_and_truncated_tail(self):
        body = self.body().replace(BOARD.encode(), b"wrong-board")
        self.assertEqual(self.request("POST", "/v1/reports", body, self.headers(body))[0], 400)
        body = self.body()[:-2]
        self.assertEqual(self.request("POST", "/v1/reports", body, self.headers(body))[0], 400)

    def test_private_files_not_served(self):
        auth = {"Authorization": f"Bearer {self.server.token}"}
        for path in ("/private/token.txt", "/v1/ota/../private/token.txt", "/v1/ota/manifest?token=x"):
            self.assertEqual(self.request("GET", path, headers=auth)[0], 404)

    def test_no_implicit_key_rotation(self):
        with self.assertRaises(ValueError):
            initialize(self.root, [])

    def test_quota(self):
        original = self.server.quota
        self.server.quota = 1
        try:
            body = self.body()
            headers = self.headers(body)
            headers["X-Report-Id"] = self.report.replace("00000001", "00000002")
            self.assertEqual(self.request("POST", "/v1/reports", body, headers)[0], 507)
        finally:
            self.server.quota = original

    def test_replay_fragment_gap(self):
        rows = [{"kind": "tx", "seq": 1, "generation": 1, "transaction": 1, "hex": "303130440d"},
                {"kind": "rx", "seq": 2, "generation": 1, "transaction": 1, "notification": 1,
                 "offset": 0, "total": 12, "hex": b"41 0D 56\r>".hex()},
                {"kind": "result", "seq": 3, "generation": 1, "transaction": 1, "result": 0}]
        path = self.root / "replay.jsonl"
        path.write_bytes(self.body(rows))
        output, _ = transactions([path])
        self.assertFalse(output[0]["complete"])
        rows[1]["total"] = 10
        path.write_bytes(self.body(rows))
        output, _ = transactions([path])
        self.assertTrue(output[0]["complete"])

    def test_duplicate_json_keys(self):
        with self.assertRaises(ValueError):
            strict_json('{"size":1,"size":2}')

    @unittest.skipUnless((Path(__file__).resolve().parents[1] / "build" / "odb2_dash.bin").exists(), "Build P4 firmware first")
    def test_real_firmware_signed_release(self):
        binary = Path(__file__).resolve().parents[1] / "build" / "odb2_dash.bin"
        manifest = publish(self.root, binary, f"https://localhost:{self.server.server_port}")
        auth = {"Authorization": f"Bearer {self.server.token}"}
        status, envelope = self.request("GET", "/v1/ota/manifest", headers=auth)
        self.assertEqual(status, 200)
        payload, sig = base64.b64decode(envelope["payload"]), base64.b64decode(envelope["signature"])
        public = serialization.load_pem_public_key((self.root / "trust" / "ota_public.pem").read_bytes())
        public.verify(sig, payload, padding.PKCS1v15(), hashes.SHA256())
        with self.assertRaises(InvalidSignature):
            public.verify(sig, payload + b" ", padding.PKCS1v15(), hashes.SHA256())
        self.assertEqual(strict_json(payload), manifest)
        conn = http.client.HTTPSConnection("localhost", self.server.server_port, context=self.context, timeout=5)
        conn.request("GET", f"/v1/ota/firmware/{manifest['sha256']}.bin", headers=auth)
        response = conn.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(hashlib.sha256(response.read()).hexdigest(), manifest["sha256"])
        conn.close()


if __name__ == "__main__":
    unittest.main()
