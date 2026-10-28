import importlib.util
import io
import json
import pathlib
import contextlib
import os
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
import unittest
import urllib.error
import urllib.request
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "live_lab", pathlib.Path(__file__).with_name("verify-live-lab.py"))
live_lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live_lab)


class Reply(io.BytesIO):
    status = 200


class LiveLabTransportTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "Linux lab process-group policy")
    def test_whole_probe_deadline_and_exit_status(self):
        self.assertEqual(live_lab.run_bounded([sys.executable, "-c", "raise SystemExit(7)"]), 7)
        start = time.monotonic()
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(live_lab.run_bounded(
                [sys.executable, "-c", "import time; time.sleep(30)"], timeout=0.2), 124)
        self.assertLess(time.monotonic() - start, 3)

    def test_timeout_cleans_probe_process_group(self):
        with patch.object(live_lab.subprocess, "Popen") as popen, \
                patch.object(live_lab.os, "killpg") as kill_group, \
                contextlib.redirect_stderr(io.StringIO()):
            process = popen.return_value.__enter__.return_value
            process.pid = 12345
            process.wait.side_effect = [subprocess.TimeoutExpired("probe", 20), 0]
            self.assertEqual(live_lab.run_bounded(["probe"]), 124)
            popen.assert_called_once_with(["probe"], start_new_session=True)
            kill_group.assert_called_once_with(12345, signal.SIGKILL)
            self.assertEqual(process.wait.call_count, 2)

    def test_real_loopback_http_boundaries(self):
        class Handler(BaseHTTPRequestHandler):
            mode = "valid"
            requests = []

            def log_message(self, *args):
                pass

            def do_POST(self):
                self.requests.append((self.command, self.path))
                body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
                if self.mode == "redirect":
                    self.send_response(302)
                    self.send_header("Location", "/redirect-target")
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                if self.mode == "oversized":
                    self.wfile.write(b" " * (live_lab.MAX_RPC_BYTES + 1))
                else:
                    parsed = json.loads(body)
                    self.wfile.write(json.dumps({"jsonrpc": "2.0", "id": parsed["id"],
                                                "result": {"height": 7}}).encode())

            def do_GET(self):
                self.requests.append((self.command, self.path))
                self.send_response(500)
                self.end_headers()

        with HTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever,
                                      kwargs={"poll_interval": 0.01}, daemon=True)
            thread.start()
            try:
                endpoint = f"http://127.0.0.1:{server.server_port}/json_rpc"
                with patch.object(live_lab, "XMZ_RPC_URL", endpoint):
                    self.assertEqual(live_lab.xmz("get_info"), {"height": 7})
                    Handler.mode = "redirect"
                    with self.assertRaises(urllib.error.HTTPError) as caught:
                        live_lab.xmz("get_info")
                    self.assertEqual(caught.exception.code, 302)
                    caught.exception.close()
                    self.assertNotIn(("GET", "/redirect-target"), Handler.requests)
                    self.assertNotIn(("POST", "/redirect-target"), Handler.requests)
                    Handler.mode = "oversized"
                    with self.assertRaisesRegex(RuntimeError, "size limit"):
                        live_lab.xmz("get_info")
                    self.assertEqual(len(Handler.requests), 3)
            finally:
                server.shutdown()
                thread.join(timeout=2)
                self.assertFalse(thread.is_alive())

    def reply(self, body):
        with patch.object(live_lab.RPC_OPENER, "open", return_value=Reply(body)) as request:
            result = live_lab.xmz("get_info")
            self.assertEqual(request.call_args.kwargs["timeout"], 5)
            return result

    def test_exact_response_identity(self):
        self.assertEqual(self.reply(b'{"jsonrpc":"2.0","id":"health","result":{"height":7}}'),
                         {"height": 7})
        for body in [b'[]', b'{"id":"health","result":{}}',
                     b'{"jsonrpc":"2.0","id":"other","result":{}}',
                     b'{"jsonrpc":"2.0","id":"health","error":null,"result":{}}',
                     b'{"jsonrpc":"2.0","id":"health"}']:
            with self.subTest(body=body), self.assertRaises(RuntimeError):
                self.reply(body)

    def test_bounded_and_unambiguous_json(self):
        with self.assertRaises(RuntimeError):
            self.reply(b' ' * (live_lab.MAX_RPC_BYTES + 1))
        with self.assertRaises(ValueError):
            self.reply(b'{"jsonrpc":"2.0","id":"other","id":"health","result":{}}')
        with self.assertRaises(ValueError):
            self.reply(b'{"jsonrpc":"2.0","id":"health","result":{"height":1,"height":2}}')
        with self.assertRaises(json.JSONDecodeError):
            self.reply(b'{')

    def test_redirect_is_refused(self):
        request = urllib.request.Request("http://127.0.0.1:6175/json_rpc")
        with self.assertRaises(urllib.error.HTTPError) as caught:
            live_lab.NoRedirect().redirect_request(request, None, 302, "Found", {},
                                                  "https://example.invalid/")
        caught.exception.close()

    def test_nonfinite_numbers_are_rejected_in_nested_fields(self):
        for number in (b"NaN", b"Infinity", b"-Infinity", b"1e999", b"-1e999"):
            with self.subTest(number=number), self.assertRaises(ValueError):
                self.reply(b'{"jsonrpc":"2.0","id":"health","result":{"extra":['
                           + number + b']}}')
        self.assertEqual(live_lab.decode_rpc('{"progress":0.999,"height":7}'),
                         {"progress": 0.999, "height": 7})

    def test_bitcoin_health_uses_strict_decoder(self):
        for response in ('{"chain":"main","chain":"regtest"}',
                         '{"verificationprogress":NaN}',
                         '{"verificationprogress":1e999}'):
            with self.subTest(response=response), \
                    patch.object(live_lab, "bitcoin", return_value=response) as bitcoin, \
                    patch.object(live_lab, "xmz") as xmz, \
                    self.assertRaises(ValueError):
                try:
                    live_lab.main()
                finally:
                    bitcoin.assert_called_once_with("getblockchaininfo")
                    xmz.assert_not_called()

    def test_bitcoin_command_has_deadline(self):
        with patch.object(live_lab.subprocess, "check_output", return_value="0\n") as command:
            self.assertEqual(live_lab.bitcoin("getblockcount"), "0")
            self.assertEqual(command.call_args.kwargs["timeout"], 10)


if __name__ == "__main__":
    unittest.main()
