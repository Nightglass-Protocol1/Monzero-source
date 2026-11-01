#!/usr/bin/env python3
"""Local stand-in for wallet.monzero.org (testing only): serves a built page with the same security
headers as deploy/Caddyfile.wallet and the same node gateway (allowlisted node paths at the site root;
Monzero's wallet code cannot reach a node behind a path prefix), to a local node.
    dev-server.py <dist dir> <node url, e.g. http://127.0.0.1:16175> [port]"""
import http.server, sys, urllib.request, os

DIST, NODE = os.path.abspath(sys.argv[1]), sys.argv[2].rstrip("/")
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8099
ALLOWED = {"/json_rpc", "/get_info", "/getinfo", "/get_height", "/getheight", "/getblocks.bin", "/get_blocks.bin",
           "/gethashes.bin", "/get_hashes.bin", "/get_o_indexes.bin", "/get_outs.bin", "/get_output_distribution.bin", "/get_transaction_pool_hashes.bin",
           "/gettransactions", "/get_transactions", "/get_transaction_pool", "/sendrawtransaction", "/send_raw_transaction"}
HEADERS = {
    "Content-Security-Policy": "default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; worker-src 'self' blob:; connect-src 'self'; img-src 'self' data: blob:; style-src 'self'; font-src 'self'; manifest-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'",
    "X-Content-Type-Options": "nosniff", "Referrer-Policy": "no-referrer", "X-Frame-Options": "DENY",
    "Cross-Origin-Opener-Policy": "same-origin",
}

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=DIST, **k)

    def end_headers(self):
        for k, v in HEADERS.items():
            self.send_header(k, v)
        super().end_headers()

    def node(self, method):
        path = self.path.split("?")[0]
        if os.environ.get("LOG_NODE"):
            import time; sys.stderr.write(time.strftime("%H:%M:%S ") + method + " " + path + "\n"); sys.stderr.flush()
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1_000_000:
            self.send_error(413); return
        body = self.rfile.read(length) if length else None
        # Like the live gateway: only the content type is passed on (no Origin, Referer, cookies or auth)
        req = urllib.request.Request(NODE + path, data=body, method=method,
                                     headers={"Content-Type": self.headers.get("Content-Type", "application/json")})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
                self.send_response(r.status)
                self.send_header("Content-Type", r.headers.get("Content-Type", "application/octet-stream"))
                self.send_header("Content-Length", str(len(data)))
                self.end_headers(); self.wfile.write(data)
        except Exception:
            self.send_error(502)

    def do_GET(self):
        return self.node("GET") if self.path.split("?")[0] in ALLOWED else super().do_GET()

    def do_POST(self):
        if self.path.split("?")[0] in ALLOWED:
            return self.node("POST")
        sys.stderr.write("refused POST " + self.path + "\n"); sys.stderr.flush()
        return self.send_error(403)

    def log_message(self, *a):
        pass

http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
