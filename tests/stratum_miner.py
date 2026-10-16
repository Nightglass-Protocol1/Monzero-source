"""A slow test miner: speaks the same stratum as XMRig, hashes through the
daemon's calc_pow RPC (a few hundred hashes per second). For tests only."""
import json, queue, socket, threading, time, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from xmzpool import rpc, meets, with_nonce  # noqa: E402


def target_diff(target):
    t = int.from_bytes(bytes.fromhex(target), "little")
    return (0xFFFFFFFF if len(target) == 8 else 0xFFFFFFFFFFFFFFFF) // t


class StratumMiner:
    def __init__(self, host, port, daemon):
        self.daemon = daemon.rstrip("/") + "/json_rpc"
        self.sock = socket.create_connection((host, port), timeout=30)
        self.file = self.sock.makefile("r")
        self.replies, self.jobs = {}, queue.Queue()
        self.lock, self.next_id = threading.Lock(), 1
        self.cond = threading.Condition()
        self.session, self.job = None, None
        self.closed = False
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            for line in self.file:
                msg = json.loads(line)
                if msg.get("method") == "job":
                    self.jobs.put(msg["params"])
                else:
                    with self.cond:
                        self.replies[msg["id"]] = msg
                        self.cond.notify_all()
        except (OSError, ValueError):
            pass
        self.closed = True
        with self.cond:
            self.cond.notify_all()

    def call(self, method, params, timeout=60):
        with self.lock:
            mid, self.next_id = self.next_id, self.next_id + 1
        self.sock.sendall((json.dumps({"id": mid, "jsonrpc": "2.0", "method": method, "params": params}) + "\n").encode())
        end = time.time() + timeout
        with self.cond:
            while mid not in self.replies:
                if self.closed or time.time() > end:
                    return None
                self.cond.wait(1)
            return self.replies.pop(mid)

    def login(self, login, password="w1"):
        r = self.call("login", {"login": login, "pass": password, "agent": "xmzpool-test/1", "algo": ["rx/0"]})
        if r and r.get("result"):
            self.session, self.job = r["result"]["id"], r["result"]["job"]
        return r

    def submit(self, job_id, nonce, result):
        return self.call("submit", {"id": self.session, "job_id": job_id, "nonce": nonce, "result": result, "algo": "rx/0"})

    def pow(self, job, nonce):
        blob = with_nonce(job["blob"], nonce)
        return rpc(self.daemon, "calc_pow", {"major_version": int(blob[:2], 16), "height": job["height"],
                                              "block_blob": blob, "seed_hash": job["seed_hash"]})

    def mine(self, stop, start_nonce=0, on_share=None):
        """Mine until stop() is true. Returns (accepted, rejected)."""
        nonce, ok, bad = start_nonce, 0, 0
        while not stop() and not self.closed:
            try:
                while True:
                    self.job = self.jobs.get_nowait()
            except queue.Empty:
                pass
            job = self.job
            n = nonce.to_bytes(4, "little").hex()
            nonce = (nonce + 1) & 0xFFFFFFFF
            h = self.pow(job, n)
            if meets(h, target_diff(job["target"])):
                r = self.submit(job["job_id"], n, h)
                if r and r.get("result", {}) and r["result"].get("status") == "OK":
                    ok += 1
                else:
                    bad += 1
                if on_share:
                    on_share(r)
        return ok, bad

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass
