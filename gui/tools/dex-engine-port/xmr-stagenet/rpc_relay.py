#!/usr/bin/env python3
"""Local HTTP relay to a Monero stagenet node, logging each request line and headers.

  python3 -I rpc_relay.py 127.0.0.1:38999 http://xmr-lux.boldsuck.org:38081

Used to see why the swap's Rust RPC client is reset by public nodes while curl works.
"""
import http.server, sys, urllib.error, urllib.request

LISTEN, UPSTREAM = sys.argv[1], sys.argv[2].rstrip("/")


class Relay(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def relay(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        print(f"{self.command} {self.path} {dict(self.headers)} body={len(body or b'')}", flush=True)
        req = urllib.request.Request(UPSTREAM + self.path, data=body, method=self.command,
                                     headers={"Content-Type": self.headers.get("Content-Type", "application/json")})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                status, data, ctype = r.status, r.read(), r.headers.get("Content-Type", "application/json")
        except urllib.error.HTTPError as e:
            status, data, ctype = e.code, e.read(), e.headers.get("Content-Type", "text/plain")
        except Exception as e:  # noqa: BLE001 - reported to the client as a gateway error
            status, data, ctype = 502, str(e).encode(), "text/plain"
        print(f"  -> {status} {len(data)} bytes", flush=True)
        if "send_raw_transaction" in self.path or "sendrawtransaction" in self.path:
            print(f"  send_raw_transaction reply: {data[:600]!r}", flush=True)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    do_GET = do_POST = relay

    def log_message(self, *args):
        pass


host, port = LISTEN.split(":")
http.server.ThreadingHTTPServer((host, int(port)), Relay).serve_forever()
