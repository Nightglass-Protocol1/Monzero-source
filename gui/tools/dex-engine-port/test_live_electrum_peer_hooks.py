#!/usr/bin/env python3
"""Focused tests for the staged LiveElectrumPeer broadcast hooks.

Real TLS on loopback with disposable fixture material (generate_litecoin_tls_fixture),
real per-connection handler threads, and a fake `ltc` RPC callable in place of a
node. No node, network, wallet or live test is touched.

Run from anywhere (bytecode is not written next to the engine-port sources):
  python3 -B test_live_electrum_peer_hooks.py [-v]
MONZERO_DEX_ENGINE_PORT overrides the engine-port tools directory.
"""
import hashlib
import json
import os
import socket
import ssl
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
STAGING = Path(__file__).resolve().parent
ENGINE_PORT = Path(os.environ.get("MONZERO_DEX_ENGINE_PORT", str(STAGING)))
sys.path.insert(0, str(ENGINE_PORT))
sys.path.insert(0, str(STAGING))  # the staged driver copy shadows the worktree one

import test_litecoin_live_swap as driver  # noqa: E402
from generate_litecoin_tls_fixture import generate as generate_tls  # noqa: E402

assert Path(driver.__file__).resolve().parent == STAGING, "imported the worktree driver, not the staged copy"
BROADCAST = "blockchain.transaction.broadcast"
WAIT = 10


def raw_tx(tag):
    return (b"monzero-test-tx-" + tag.encode()).hex()


def digest(raw):
    return hashlib.sha256(bytes.fromhex(raw)).hexdigest()


class FakeLtc:
    """Stands in for LtcRpc. sendrawtransaction may rendezvous on a barrier so that
    two connections reach the hook decision at the same moment."""
    def __init__(self):
        self.lock, self.accepted, self.rejected, self.barrier = threading.Lock(), [], set(), None

    def __call__(self, method, *params, wallet=None):
        if method == "sendrawtransaction":
            raw = params[0]
            if self.barrier is not None:
                self.barrier.wait(WAIT)
            if raw in self.rejected:
                raise driver.RpcError(-26, "txn-mempool-conflict")
            with self.lock:
                self.accepted.append(raw)
            return hashlib.sha256(hashlib.sha256(bytes.fromhex(raw)).digest()).digest()[::-1].hex()
        if method == "getblockhash":
            return "00"*32
        raise AssertionError("unexpected fake RPC " + method)


class Client:
    """Electrum-over-TLS client that verifies the fixture certificate like the helper."""
    def __init__(self, port, root):
        context = ssl.create_default_context(cafile=str(root))
        raw = socket.create_connection(("127.0.0.1", port), timeout=WAIT*3)
        self.tls = context.wrap_socket(raw, server_hostname="localhost")
        self.stream, self.counter = self.tls.makefile("rwb"), 0

    def send(self, method, *params):
        self.counter += 1
        self.stream.write(json.dumps({"jsonrpc":"2.0","id":self.counter,"method":method,"params":list(params)}).encode()+b"\n")
        self.stream.flush()
        return self.counter

    def reply(self):
        """Next reply, or None once the peer closed the connection without one."""
        try:
            line = self.stream.readline()
        except (ssl.SSLEOFError, ConnectionResetError):
            return None
        return json.loads(line) if line else None

    def call(self, method, *params):
        identity = self.send(method, *params)
        value = self.reply()
        assert value is not None and value["id"] == identity, value
        return value["result"]

    def close(self):
        for item in (self.stream, self.tls):
            try:
                item.close()
            except OSError:
                pass


class Reader(threading.Thread):
    """Reads one reply in the background so a held connection never blocks the test."""
    def __init__(self, client):
        super().__init__(daemon=True); self.client, self.value, self.error = client, "unread", None
        self.start()

    def run(self):
        try:
            self.value = self.client.reply()
        except Exception as error:  # noqa: BLE001
            self.error = error

    def done(self, timeout=WAIT):
        self.join(timeout)
        return not self.is_alive()


class LiveElectrumPeerHooks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="live-electrum-hooks-")
        base = Path(self.temp.name)
        generate_tls(base/"tls")
        (base/"peer").mkdir(mode=0o700)
        self.root, self.ltc, self.clients = base/"tls/root.pem", FakeLtc(), []
        self.peer = driver.LiveElectrumPeer(self.ltc, base/"tls", base/"peer", 0)
        self.peer.__enter__()
        self.exited = False

    def tearDown(self):
        for client in self.clients:
            client.close()
        if not self.exited:
            self.exit_peer()
        self.temp.cleanup()

    def exit_peer(self):
        self.exited = True
        self.peer.__exit__(None, None, None)  # asserts the peer recorded no errors

    def client(self):
        value = Client(self.peer.port, self.root); self.clients.append(value)
        return value

    def broadcast_async(self, client, raw):
        identity = client.send(BROADCAST, raw)
        return identity, Reader(client)

    def wait_for(self, predicate, timeout=WAIT):
        deadline = time.monotonic()+timeout
        while not predicate():
            self.assertLess(time.monotonic(), deadline, "condition not reached")
            time.sleep(0.01)

    # ---- unarmed behaviour and acceptance counting

    def test_unarmed_broadcast_replies_and_records_digest(self):
        raw = raw_tx("plain")
        txid = self.client().call(BROADCAST, raw)
        self.assertEqual(len(txid), 64)
        self.assertEqual(self.peer.accepted_broadcast_digests, [digest(raw)])
        self.assertEqual(self.peer.dropped_broadcast_replies, 0)

    def test_rejected_broadcast_is_not_counted_and_consumes_no_hook(self):
        rejected, accepted = raw_tx("rejected"), raw_tx("accepted")
        self.ltc.rejected.add(rejected)
        self.peer.hold_next_broadcast(); self.peer.drop_next_broadcast()
        first = self.client()
        first.send(BROADCAST, rejected)
        self.assertIsNone(first.reply())  # existing behaviour: RPC error closes without a reply
        self.assertEqual(self.peer.accepted_broadcast_digests, [])
        self.assertTrue(self.peer.hold_next_broadcast_reply and self.peer.drop_next_broadcast_reply)
        self.assertFalse(self.peer.broadcast_held.is_set())
        self.wait_for(lambda: self.peer.errors)  # appended after the handler closed the connection
        self.assertEqual(len(self.peer.errors), 1); self.assertTrue(self.peer.errors[0].startswith("RpcError("))
        self.peer.errors.clear()
        # The next accepted broadcast takes the hold (hold wins over drop, as in the base peer).
        _, reader = self.broadcast_async(self.client(), accepted)
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.assertEqual(self.peer.accepted_broadcast_digests, [digest(accepted)])
        self.assertEqual(self.peer.held_broadcast_digests, [digest(accepted)])
        self.assertTrue(self.peer.drop_next_broadcast_reply)
        self.peer.release_broadcast_reply.set()
        self.assertTrue(reader.done()); self.assertIsNone(reader.value)

    def test_other_requests_do_not_consume_hooks(self):
        self.peer.hold_next_broadcast(); self.peer.drop_next_broadcast()
        connection = self.client()
        self.assertEqual(connection.call("server.version", "test", "1.4")[1], "1.4")
        self.assertIn("genesis_hash", connection.call("server.features"))
        self.assertTrue(self.peer.hold_next_broadcast_reply and self.peer.drop_next_broadcast_reply)
        self.assertEqual(self.peer.accepted_broadcast_digests, [])

    # ---- drop

    def test_drop_suppresses_exactly_one_reply_after_acceptance(self):
        self.peer.drop_next_broadcast()
        dropped, kept = raw_tx("dropped"), raw_tx("kept")
        first = self.client()
        first.send(BROADCAST, dropped)
        self.assertIsNone(first.reply())
        self.assertEqual(self.ltc.accepted, [dropped])  # the node accepted it before the reply was lost
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)
        self.assertFalse(self.peer.drop_next_broadcast_reply)
        self.assertEqual(len(self.client().call(BROADCAST, kept)), 64)
        self.assertEqual(self.peer.accepted_broadcast_digests, [digest(dropped), digest(kept)])
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)

    def test_attribute_style_drop_matches_base_peer_contract(self):
        self.peer.drop_next_broadcast_reply = True  # as litecoin_app_light_fixture arms it
        first = self.client()
        first.send(BROADCAST, raw_tx("attribute"))
        self.assertIsNone(first.reply())
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)

    # ---- hold

    def test_hold_suppresses_reply_while_other_connections_are_served(self):
        held_raw, other_raw = raw_tx("held"), raw_tx("other")
        self.peer.hold_next_broadcast()
        _, held = self.broadcast_async(self.client(), held_raw)
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.assertEqual(self.peer.accepted_broadcast_digests, [digest(held_raw)])
        # A second connection opened while the first is held is fully served,
        # including a broadcast (the hold was consumed once).
        other = self.client()
        self.assertEqual(other.call("server.version", "test", "1.4")[1], "1.4")
        self.assertEqual(len(other.call(BROADCAST, other_raw)), 64)
        self.assertTrue(held.is_alive(), "held connection answered before release")
        self.assertEqual(self.peer.dropped_broadcast_replies, 0)
        self.peer.release_broadcast_reply.set()
        self.assertTrue(held.done()); self.assertIsNone(held.value)  # EOF, never the reply line
        self.wait_for(lambda: self.peer.holding == 0)
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)
        self.assertEqual(self.peer.accepted_broadcast_digests, [digest(held_raw), digest(other_raw)])
        self.assertEqual(self.peer.held_broadcast_digests, [digest(held_raw)])

    def test_attribute_style_hold_matches_app_fixture_arming(self):
        peer = self.peer  # litecoin_app_fixture.interrupt_relay arms the hold this way
        peer.broadcast_held.clear(); peer.release_broadcast_reply.clear(); peer.hold_next_broadcast_reply = True
        _, held = self.broadcast_async(self.client(), raw_tx("fixture-style"))
        self.assertTrue(peer.broadcast_held.wait(WAIT))
        self.assertTrue(held.is_alive())
        peer.release_broadcast_reply.set()
        self.assertTrue(held.done()); self.assertIsNone(held.value)

    def test_rearming_after_release_holds_again(self):
        for round_number in range(2):
            self.peer.hold_next_broadcast()  # clears the previous round's set events
            _, held = self.broadcast_async(self.client(), raw_tx(f"round-{round_number}"))
            self.assertTrue(self.peer.broadcast_held.wait(WAIT))
            time.sleep(0.2)
            self.assertTrue(held.is_alive(), "stale release event let the reply through")
            self.peer.release_broadcast_reply.set()
            self.assertTrue(held.done()); self.assertIsNone(held.value)
        self.wait_for(lambda: self.peer.dropped_broadcast_replies == 2)

    def test_second_hold_cannot_be_armed_while_one_is_pending(self):
        self.peer.hold_next_broadcast()
        with self.assertRaises(Exception):
            self.peer.hold_next_broadcast()
        _, held = self.broadcast_async(self.client(), raw_tx("pending"))
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        with self.assertRaises(Exception):
            self.peer.hold_next_broadcast()  # would clear the barrier under the held reply
        self.peer.release_broadcast_reply.set()
        self.assertTrue(held.done())

    def test_hold_timeout_closes_without_reply_and_records_error(self):
        self.peer.broadcast_hold_seconds = 0.3
        self.peer.hold_next_broadcast()
        _, held = self.broadcast_async(self.client(), raw_tx("timeout"))
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.assertTrue(held.done()); self.assertIsNone(held.value)
        self.wait_for(lambda: self.peer.errors)
        self.assertEqual(self.peer.errors, ["RuntimeError('Owned broadcast barrier timed out')"])
        self.assertEqual(self.peer.dropped_broadcast_replies, 0)  # base peer parity: timeout is an error
        self.assertEqual(self.peer.holding, 0)
        self.peer.errors.clear()

    def test_peer_exit_releases_held_reply_without_sending_it(self):
        self.peer.hold_next_broadcast()
        _, held = self.broadcast_async(self.client(), raw_tx("closing"))
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.exit_peer()  # sets stop and release_broadcast_reply, closes the listener
        self.assertTrue(held.done()); self.assertIsNone(held.value)
        self.wait_for(lambda: self.peer.dropped_broadcast_replies == 1)
        self.assertEqual(self.peer.errors, [])
        with self.assertRaises(OSError):
            Client(self.peer.port, self.root)

    # ---- concurrency: an armed hook is consumed once

    def concurrent_broadcasts(self, count=2):
        self.ltc.barrier = threading.Barrier(count)
        raws = [raw_tx(f"concurrent-{n}") for n in range(count)]
        readers = [self.broadcast_async(self.client(), raw)[1] for raw in raws]
        return raws, readers

    def test_concurrent_broadcasts_take_one_hold(self):
        self.peer.hold_next_broadcast()
        raws, readers = self.concurrent_broadcasts()
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.wait_for(lambda: sum(not r.is_alive() for r in readers) == 1)
        time.sleep(0.2)
        answered = [r for r in readers if not r.is_alive()]
        pending = [r for r in readers if r.is_alive()]
        self.assertEqual((len(answered), len(pending)), (1, 1), "hold consumed twice or not at all")
        self.assertEqual(len(answered[0].value["result"]), 64)
        self.assertEqual(sorted(self.peer.accepted_broadcast_digests), sorted(map(digest, raws)))
        self.assertEqual(len(self.peer.held_broadcast_digests), 1)
        self.assertFalse(self.peer.hold_next_broadcast_reply)
        self.peer.release_broadcast_reply.set()
        self.assertTrue(pending[0].done()); self.assertIsNone(pending[0].value)
        self.wait_for(lambda: self.peer.dropped_broadcast_replies == 1)

    def test_concurrent_broadcasts_take_one_drop(self):
        self.peer.drop_next_broadcast()
        raws, readers = self.concurrent_broadcasts()
        for reader in readers:
            self.assertTrue(reader.done())
        values = [r.value for r in readers]
        self.assertEqual(sum(v is None for v in values), 1, values)
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)
        self.assertFalse(self.peer.drop_next_broadcast_reply)
        self.assertEqual(sorted(self.peer.accepted_broadcast_digests), sorted(map(digest, raws)))

    def test_concurrent_hold_and_drop_each_taken_once(self):
        self.peer.hold_next_broadcast(); self.peer.drop_next_broadcast()
        raws, readers = self.concurrent_broadcasts(3)
        self.assertTrue(self.peer.broadcast_held.wait(WAIT))
        self.wait_for(lambda: sum(not r.is_alive() for r in readers) == 2)
        time.sleep(0.2)
        finished = sorted((r.value is None) for r in readers if not r.is_alive())
        self.assertEqual(finished, [False, True], "expected one dropped and one answered reply")
        self.assertEqual(self.peer.dropped_broadcast_replies, 1)
        self.assertFalse(self.peer.hold_next_broadcast_reply or self.peer.drop_next_broadcast_reply)
        held = [r for r in readers if r.is_alive()]
        self.assertEqual(len(held), 1)
        self.peer.release_broadcast_reply.set()
        self.assertTrue(held[0].done()); self.assertIsNone(held[0].value)
        self.wait_for(lambda: self.peer.dropped_broadcast_replies == 2)
        self.assertEqual(sorted(self.peer.accepted_broadcast_digests), sorted(map(digest, raws)))

    def test_repeated_concurrent_drops_never_consume_twice(self):
        for round_number in range(25):
            self.ltc.barrier = threading.Barrier(4)
            self.peer.drop_next_broadcast()
            readers = [self.broadcast_async(self.client(), raw_tx(f"stress-{round_number}-{n}"))[1] for n in range(4)]
            for reader in readers:
                self.assertTrue(reader.done())
            self.assertEqual(sum(r.value is None for r in readers), 1, f"round {round_number}")
            self.assertEqual(self.peer.dropped_broadcast_replies, round_number+1)
        self.assertEqual(len(self.peer.accepted_broadcast_digests), 100)


if __name__ == "__main__":
    unittest.main()
