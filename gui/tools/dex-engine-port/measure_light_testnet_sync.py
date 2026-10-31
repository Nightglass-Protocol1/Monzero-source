#!/usr/bin/env python3
"""Measure a full Litecoin light-wallet header sync on testnet.

Serves an exported header file (fetch_ltc_headers.py) over the same local
TLS Electrum fixture the helper tests use, then runs the release helper's
sync from genesis to the file's tip, and once more to show the cached restart.

    measure_light_testnet_sync.py HELPER TLS_FIXTURE_DIR HEADERS.bin REPORT.json
"""
import hashlib, json, mmap, os, socket, ssl, subprocess, sys, tempfile, threading, time, uuid
from pathlib import Path
from cryptography.hazmat.primitives.serialization import load_der_private_key, Encoding, PrivateFormat, NoEncryption

TESTNET_GENESIS = "4966625a4b2851d9fdee139e56211a0d88575f59ed816ff5e6a63deb4e3e29a0"


class Headers:
    """Read-only view of the export: index i is the header at height i + 1."""
    def __init__(self, path):
        self.file = open(path, "rb")
        self.map = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        self.count = len(self.map) // 80 - 1
        assert hashlib.sha256(hashlib.sha256(self.map[:80]).digest()).digest()[::-1].hex() == TESTNET_GENESIS

    def __len__(self):
        return self.count

    def __getitem__(self, i):
        if isinstance(i, slice):
            start, stop, _ = i.indices(self.count)
            return [self.map[80 * (h + 1):80 * (h + 2)] for h in range(start, stop)]
        if i < 0:
            i += self.count
        return self.map[80 * (i + 1):80 * (i + 2)]


class Peer(threading.Thread):
    def __init__(self, path, fixture, headers):
        super().__init__(daemon=True)
        self.headers, self.stop, self.errors, self.requests = headers, threading.Event(), [], 0
        cert, key = path / "server.pem", path / "key.pem"
        cert.write_text(ssl.DER_cert_to_PEM_cert((fixture / "server.der").read_bytes()))
        key.write_bytes(load_der_private_key((fixture / "server-key.der").read_bytes(), None)
                        .private_bytes(Encoding.PEM, PrivateFormat.PKCS8, NoEncryption()))
        key.chmod(0o600)
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(cert, key)
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.port = self.listener.getsockname()[1]
        self.listener.listen()
        self.listener.settimeout(0.2)

    def run(self):
        while not self.stop.is_set():
            try:
                connection, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            try:
                connection.settimeout(60)
                with self.context.wrap_socket(connection, server_side=True) as tls, tls.makefile("rwb") as stream:
                    while True:
                        line = stream.readline(4097)
                        if not line:
                            break
                        request = json.loads(line)
                        method, params = request["method"], request["params"]
                        self.requests += 1
                        if method == "server.version":
                            result = ["fixture", "1.4"]
                        elif method == "server.features":
                            result = {"genesis_hash": TESTNET_GENESIS, "hash_function": "sha256"}
                        elif method == "blockchain.headers.subscribe":
                            result = {"height": len(self.headers), "hex": self.headers[-1].hex()}
                        elif method == "blockchain.block.header":
                            result = self.headers[params[0] - 1].hex()
                        elif method == "blockchain.block.headers":
                            start, count = params
                            chunk = self.headers[start - 1:start - 1 + count]
                            result = {"count": len(chunk), "hex": b"".join(chunk).hex(), "max": 2016}
                        else:
                            raise AssertionError(method)
                        stream.write(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}).encode() + b"\n")
                        stream.flush()
            except ssl.SSLError:
                pass
            except Exception as error:  # noqa: BLE001 — reported by the caller
                self.errors.append(repr(error))
                return


def sync(helper, fixture, config, budget):
    identity = str(uuid.uuid4())
    request = {"schema": "monzero-ltc-light-sync-request-v1", "requestId": identity, "maximumDownloadHeaders": budget}
    env = dict(os.environ, SSL_CERT_FILE=str(fixture / "root.pem"))
    started = time.time()
    done = subprocess.run([str(helper), "--sync-stdio-v1", str(config)], input=json.dumps(request).encode(),
                          capture_output=True, env=env, timeout=4 * 3600)
    seconds = time.time() - started
    if done.returncode:
        raise SystemExit(f"helper failed after {seconds:.0f}s: {done.stderr.decode()[-2000:]}")
    return json.loads(done.stdout), seconds


def main():
    helper, fixture, headers_file, report = map(Path, sys.argv[1:5])
    headers = Headers(headers_file)
    with tempfile.TemporaryDirectory(prefix="ltc-light-testnet-sync-") as temp:
        path = Path(temp)
        cache = path / "cache"
        cache.mkdir(mode=0o700)
        peer = Peer(path, fixture, headers)
        peer.start()
        config = path / "config.json"
        config.write_text(json.dumps({"schema": "monzero-private-ltc-light-v1", "network": "test",
                                      "cacheDirectory": str(cache), "maximumHeight": 10_000_000,
                                      "endpoint": {"host": "localhost", "port": peer.port}}))
        config.chmod(0o600)
        first, first_seconds = sync(helper, fixture, config, 10_000_000)
        cache_bytes = sum(f.stat().st_size for f in cache.rglob("*") if f.is_file())
        second, second_seconds = sync(helper, fixture, config, 10_000_000)
        peer.stop.set()
        result = {
            "network": "litecoin-testnet", "headers": len(headers), "tip": first["chain"]["tip"],
            "expectedTip": hashlib.sha256(hashlib.sha256(headers[-1]).digest()).digest()[::-1].hex(),
            "firstSyncSeconds": round(first_seconds, 1), "downloadedHeaders": first["synchronization"]["downloadedHeaders"],
            "cacheBytes": cache_bytes, "restartSyncSeconds": round(second_seconds, 1),
            "restartDownloadedHeaders": second["synchronization"]["downloadedHeaders"],
            "serverRequests": peer.requests, "serverErrors": peer.errors,
        }
        result["tipMatches"] = result["tip"] == result["expectedTip"]
        report.write_text(json.dumps(result, indent=1))
        print(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()
