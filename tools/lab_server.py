"""Local HTTPS integration server. Not a production fleet-management service."""
import argparse
import base64
import datetime as dt
import hashlib
import hmac
import http.server
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import ssl
import struct
import tempfile
from urllib.parse import urlsplit

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

BOARD = "esp32-p4-function-ev-board-v1.4"
PROJECT = "odb2_dash"
MAX_REPORT = 2 * 1024 * 1024
MAX_FIRMWARE = 6 * 1024 * 1024
REPORT_NAME = re.compile(r"veh-[0-9a-f]{16}-[0-9a-f]{8}\.jsonl\Z")
HEX_ID = re.compile(r"[0-9a-f]{32}\Z")
HEX_HASH = re.compile(r"[0-9a-f]{64}\Z")
VERSION = re.compile(r"[0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5}\Z")


def strict_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON key")
            result[key] = value
        return result
    def invalid_constant(value):
        raise ValueError("Non-finite JSON number")
    return json.loads(data, object_pairs_hook=pairs, parse_constant=invalid_constant)


def atomic_write(path, data, private=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".pending-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        if private:
            os.chmod(temp, 0o600)
        os.replace(temp, path)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)


def initialize(root, hosts):
    root = Path(root)
    if any((root / "private").glob("*")):
        raise ValueError("Existing identity found; refusing to rotate keys implicitly")
    now = dt.datetime.now(dt.timezone.utc)
    ca_key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    ota_key = rsa.generate_private_key(public_exponent=65537, key_size=3072)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Vehicle Lab CA")])
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(ca_key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(now - dt.timedelta(days=1))
          .not_valid_after(now + dt.timedelta(days=3650))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
          .sign(ca_key, hashes.SHA256()))
    sans = []
    for host in sorted(set(hosts + ["localhost", "127.0.0.1"])):
        try:
            sans.append(x509.IPAddress(ipaddress.ip_address(host)))
        except ValueError:
            if not re.fullmatch(r"[A-Za-z0-9.-]{1,253}", host):
                raise ValueError("Invalid certificate host")
            sans.append(x509.DNSName(host))
    server = (x509.CertificateBuilder()
              .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Vehicle Lab")]))
              .issuer_name(name).public_key(server_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now - dt.timedelta(days=1)).not_valid_after(now + dt.timedelta(days=365))
              .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
              .add_extension(x509.SubjectAlternativeName(sans), critical=False)
              .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
              .sign(ca_key, hashes.SHA256()))
    for filename, key in [("ca_key.pem", ca_key), ("server_key.pem", server_key), ("ota_key.pem", ota_key)]:
        atomic_write(root / "private" / filename, key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8, serialization.NoEncryption()), private=True)
    atomic_write(root / "private" / "token.txt", secrets.token_urlsafe(32).encode("ascii"), private=True)
    atomic_write(root / "private" / "server.pem", server.public_bytes(serialization.Encoding.PEM))
    atomic_write(root / "trust" / "service_ca.pem", ca.public_bytes(serialization.Encoding.PEM))
    atomic_write(root / "trust" / "ota_public.pem", ota_key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    print(f"Public trust directory: {(root / 'trust').resolve()}")
    print(f"Local bearer token file: {(root / 'private' / 'token.txt').resolve()}")


def firmware_description(path):
    from esptool.bin_image import LoadFirmwareImage
    image = LoadFirmwareImage("esp32p4", str(path))
    if image.chip_id != image.ROM_LOADER.IMAGE_CHIP_ID:
        raise ValueError("Firmware is not for ESP32-P4")
    image.verify()
    data = path.read_bytes()
    # ESP image header (24), first segment header (8), then esp_app_desc_t.
    if len(data) < 288 or struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("Not an IDF application image")
    def field(offset):
        return data[32 + offset:32 + offset + 32].split(b"\0", 1)[0].decode("ascii")
    version, project, idf = field(16), field(48), field(112)
    if not VERSION.fullmatch(version) or project != PROJECT or not re.match(r"^v?5\.5(?:\.|$)", idf):
        raise ValueError("Application version, project, or IDF version does not match")
    return version


def publish(root, firmware, base_url):
    root, firmware = Path(root), Path(firmware)
    url = urlsplit(base_url)
    if (url.scheme != "https" or not url.hostname or url.username or url.password or
            url.query or url.fragment or url.path not in ("", "/")):
        raise ValueError("Use an HTTPS origin without a path or credentials")
    if not 1024 <= firmware.stat().st_size <= MAX_FIRMWARE:
        raise ValueError("Firmware does not fit OTA partition")
    version = firmware_description(firmware)
    data = firmware.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    releases = root / "releases"
    binary = releases / f"{digest}.bin"
    if not binary.exists() and len(list(releases.glob("*.bin"))) >= 8:
        raise ValueError("Release limit reached; archive old lab releases explicitly")
    payload = {"schema": 1, "target": "esp32p4", "board": BOARD, "project": PROJECT, "idf": "5.5",
               "version": version, "size": len(data), "sha256": digest,
               "url": f"{base_url.rstrip('/')}/v1/ota/firmware/{digest}.bin"}
    raw = json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
    if len(raw) > 2048 or len(payload["url"]) >= 256:
        raise ValueError("Manifest exceeds device limits")
    key = serialization.load_pem_private_key((root / "private" / "ota_key.pem").read_bytes(), password=None)
    signature = key.sign(raw, padding.PKCS1v15(), hashes.SHA256())
    envelope = {"payload": base64.b64encode(raw).decode("ascii"), "signature": base64.b64encode(signature).decode("ascii")}
    atomic_write(binary, data)
    atomic_write(releases / "manifest.json", json.dumps(envelope, separators=(",", ":")).encode("ascii"))
    print(f"Published actual application version {version}, {len(data)} bytes, SHA256 {digest}")
    return payload


def validate_report(body, device):
    if not body or len(body) > MAX_REPORT or not body.endswith(b"\n"):
        raise ValueError("Incomplete or oversized report")
    lines = body.splitlines()
    if len(lines) < 2 or any(not line or len(line) > 4096 for line in lines):
        raise ValueError("Invalid JSONL framing")
    rows = [strict_json(line) for line in lines]
    if any(not isinstance(row, dict) for row in rows):
        raise ValueError("Expected JSON objects")
    header = rows[0]
    if (header.get("kind") != "header" or header.get("schema") != 1 or
            header.get("device") != device or header.get("board") != BOARD or rows[-1].get("kind") != "end"):
        raise ValueError("Wrong report schema, identity, board, or missing footer")
    if any(row.get("kind") not in ("tx", "rx", "result", "link", "parser_gap", "metrics", "gap") for row in rows[1:-1]):
        raise ValueError("Unsupported record type")
    return rows


class LabServer(http.server.HTTPServer):
    def __init__(self, address, root, quota=256 * 1024 * 1024):
        self.root = Path(root).resolve()
        self.quota = quota
        self.token = (self.root / "private" / "token.txt").read_text("ascii").strip()
        if len(self.token) < 32:
            raise ValueError("Invalid lab credential")
        self.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.tls.minimum_version = ssl.TLSVersion.TLSv1_2
        self.tls.load_cert_chain(self.root / "private" / "server.pem", self.root / "private" / "server_key.pem")
        super().__init__(address, Handler)

    def get_request(self):
        sock, address = super().get_request()
        sock.settimeout(15)
        try:
            return self.tls.wrap_socket(sock, server_side=True), address
        except Exception:
            sock.close()
            raise


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "VehicleLab/1"

    def log_message(self, fmt, *args):
        # Never log untrusted URL/query strings or authorization headers.
        pass

    def reply(self, code, value):
        body = json.dumps(value, separators=(",", ":")).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def authorized(self):
        headers = self.headers.get_all("Authorization", [])
        if len(headers) != 1 or not hmac.compare_digest(headers[0].encode("utf-8"),
                                                       f"Bearer {self.server.token}".encode("ascii")):
            self.reply(401, {"error": "unauthorized"})
            return False
        return True

    def do_POST(self):
        if not self.authorized():
            return
        if self.path != "/v1/reports":
            self.reply(404, {"error": "not found"})
            return
        try:
            for header in ("Content-Length", "X-Device-Id", "X-Report-Id", "X-Content-SHA256"):
                if len(self.headers.get_all(header, [])) != 1:
                    raise ValueError("Missing or duplicate header")
            if self.headers.get("Transfer-Encoding"):
                raise ValueError("Chunked request not supported")
            size = int(self.headers["Content-Length"])
            device, report, digest = (self.headers[key] for key in ("X-Device-Id", "X-Report-Id", "X-Content-SHA256"))
            if (not 0 < size <= MAX_REPORT or not HEX_ID.fullmatch(device) or
                    not REPORT_NAME.fullmatch(report) or not HEX_HASH.fullmatch(digest)):
                raise ValueError("Invalid upload metadata")
            body = self.rfile.read(size)
            if len(body) != size or not hmac.compare_digest(hashlib.sha256(body).hexdigest(), digest):
                raise ValueError("Length or digest mismatch")
            validate_report(body, device)
            reports = self.server.root / "reports"
            target = reports / device / report
            if target.exists():
                if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                    self.reply(409, {"error": "report id conflict"})
                    return
            else:
                used = sum(p.stat().st_size for p in reports.glob("*/*.jsonl"))
                if used + size > self.server.quota:
                    self.reply(507, {"error": "lab disk quota reached"})
                    return
                atomic_write(target, body, private=True)
            self.reply(200, {"ok": True, "report": report, "sha256": digest, "size": size})
        except (ValueError, UnicodeError, RecursionError):
            self.reply(400, {"error": "invalid report"})
        except (OSError, TimeoutError):
            self.reply(503, {"error": "storage or connection unavailable"})

    def do_GET(self):
        if not self.authorized():
            return
        if self.path == "/v1/ota/manifest":
            path, content_type = self.server.root / "releases" / "manifest.json", "application/json"
        else:
            match = re.fullmatch(r"/v1/ota/firmware/([0-9a-f]{64})\.bin", self.path)
            if not match:
                self.reply(404, {"error": "not found"})
                return
            path, content_type = self.server.root / "releases" / f"{match[1]}.bin", "application/octet-stream"
        try:
            with path.open("rb") as source:
                size = os.fstat(source.fileno()).st_size
                if size > MAX_FIRMWARE:
                    raise ValueError("Oversized release")
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(size))
                self.send_header("Connection", "close")
                self.end_headers()
                while chunk := source.read(16384):
                    self.wfile.write(chunk)
                self.close_connection = True
        except FileNotFoundError:
            self.reply(404, {"error": "no release published"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1] / "build" / "lab-server")
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser("init")
    init.add_argument("--host", action="append", default=[], help="LAN IP or DNS name to include in TLS certificate")
    release = commands.add_parser("publish")
    release.add_argument("firmware", type=Path)
    release.add_argument("--base-url", required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--bind", default="127.0.0.1")
    serve.add_argument("--port", type=int, default=8443)
    args = parser.parse_args()
    if args.command == "init":
        initialize(args.root, args.host)
    elif args.command == "publish":
        publish(args.root, args.firmware, args.base_url)
    else:
        with LabServer((args.bind, args.port), args.root) as server:
            print(f"HTTPS lab server listening on {args.bind}:{server.server_port}; Ctrl+C to stop")
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass


if __name__ == "__main__":
    main()
