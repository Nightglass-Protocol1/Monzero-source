"""Read-only stand-in for the few Bitcoin Core RPCs the live driver uses, answered by
public Electrum servers, for the Bitcoin mainnet stage where no node is run
(BUYER_LIGHT_AND_MAINNET.md).

Every answer must agree across at least two servers (byte-identical transactions,
identical block hashes); a disagreement stops the run. The traders themselves never
use this: they reach the chain through their own light helpers. This only lets the
driver check and record what happened, so it trusts nothing it cannot cross-check.

    node = ElectrumNode([("electrum.acinq.co", 50002), ("electrum2.bluewallet.io", 443)])
    node("getrawtransaction", txid, True)   # Core-style verbose dict
"""
import decimal
import hashlib
import json
import socket
import ssl
import threading

BECH32_CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"


class ElectrumError(RuntimeError):
    pass


def _bech32_polymod(values):
    generator = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    check = 1
    for value in values:
        top = check >> 25
        check = (check & 0x1FFFFFF) << 5 ^ value
        for i in range(5):
            check ^= generator[i] if (top >> i) & 1 else 0
    return check


def segwit_script(address, hrp="bc"):
    """scriptPubKey hex of a bech32/bech32m segwit address (BIP173/350), checked."""
    address = address.lower()
    prefix, _, data_part = address.rpartition("1")
    if prefix != hrp or not data_part or any(c not in BECH32_CHARSET for c in data_part):
        raise ElectrumError("not a " + hrp + " segwit address")
    data = [BECH32_CHARSET.index(c) for c in data_part]
    expanded = [ord(c) >> 5 for c in prefix] + [0] + [ord(c) & 31 for c in prefix]
    constant = _bech32_polymod(expanded + data)
    version = data[0]
    if constant != (1 if version == 0 else 0x2BC830A3):
        raise ElectrumError("bad address checksum")
    acc, bits, program = 0, 0, []
    for value in data[1:-6]:
        acc = (acc << 5) | value
        bits += 5
        while bits >= 8:
            bits -= 8
            program.append((acc >> bits) & 0xFF)
    if bits >= 5 or (acc << (8 - bits)) & 0xFF or not 2 <= len(program) <= 40 or version > 16:
        raise ElectrumError("bad address program")
    if version == 0 and len(program) not in (20, 32):
        raise ElectrumError("bad v0 program length")
    opcode = 0 if version == 0 else 0x50 + version
    return bytes([opcode, len(program)] + program).hex()


def scripthash(script_hex):
    return hashlib.sha256(bytes.fromhex(script_hex)).digest()[::-1].hex()


class _Session:
    def __init__(self, host, port, timeout):
        context = ssl.create_default_context()  # certificate authorities only
        self.name = f"{host}:{port}"
        self.socket = context.wrap_socket(socket.create_connection((host, port), timeout=timeout), server_hostname=host)
        self.file = self.socket.makefile("rwb")
        self.next_id = 0
        self.lock = threading.Lock()
        self.call("server.version", "monzero-driver", "1.4")

    def call(self, method, *params):
        with self.lock:
            self.next_id += 1
            self.file.write((json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": list(params)}) + "\n").encode())
            self.file.flush()
            while True:
                line = self.file.readline()
                if not line:
                    raise ElectrumError("server closed the connection")
                reply = json.loads(line, parse_float=decimal.Decimal)  # exact amounts, as Core RPC callers expect
                # After blockchain.headers.subscribe the server pushes a notification for every
                # new block (no id); it is not the reply to this request.
                if reply.get("id") == self.next_id:
                    break
                if "id" in reply and reply["id"] is not None:
                    raise ElectrumError(f"{self.name} replied out of order")
            if reply.get("error"):
                raise ElectrumError(str(reply["error"]))
            if reply.get("result") is None:
                # None of the calls used here can legitimately answer null (seen once on
                # mainnet for listunspent): treat it as a failed answer, never as "nothing".
                raise ElectrumError(f"{self.name} answered null to {method}")
            return reply["result"]

    def close(self):
        try:
            self.socket.close()
        except OSError:
            pass


class MempoolView:
    """`txid in node("getrawmempool")`: the transaction is known to a server but unconfirmed."""
    def __init__(self, node):
        self.node = node

    def __contains__(self, txid):
        try:
            return int(self.node("getrawtransaction", txid, True).get("confirmations", 0)) == 0
        except ElectrumError:
            return False

    def __iter__(self):
        raise ElectrumError("Electrum servers cannot list the whole mempool")


class ElectrumNode:
    """Callable like the driver's LtcRpc. Needs >= 2 servers that serve verbose transactions."""
    port = None

    def __init__(self, servers, hrp="bc", chain="main", timeout=30):
        if len(servers) < 2 or len(set(servers)) != len(servers):
            raise ElectrumError("need at least two distinct servers")
        self.servers, self.hrp, self.chain, self.timeout = list(servers), hrp, chain, timeout
        self.sessions = {}

    def _session(self, server):
        if server not in self.sessions:
            self.sessions[server] = _Session(*server, self.timeout)
        return self.sessions[server]

    def _ask(self, server, method, *params):
        """Public servers drop idle connections; a broken connection is retried once on a
        fresh one. A server's own error reply is never retried."""
        for attempt in (1, 2):
            try:
                return self._session(server).call(method, *params)
            except (OSError, ConnectionError, ValueError) as error:
                broken = error
            except ElectrumError as error:
                if str(error) != "server closed the connection":
                    raise
                broken = error
            stale = self.sessions.pop(server, None)
            if stale:
                stale.close()
        raise ElectrumError(f"{server[0]}:{server[1]} unreachable: {broken}")

    def _all(self, method, *params):
        return [self._ask(server, method, *params) for server in self.servers]

    def _agreed(self, method, *params, key=lambda a: a):
        answers = self._all(method, *params)
        if any(key(a) != key(answers[0]) for a in answers[1:]):
            raise ElectrumError(f"servers disagree on {method}")
        return answers[0]

    def close(self):
        for session in self.sessions.values():
            session.close()
        self.sessions.clear()

    def tip_height(self):
        # Servers may be a block apart for a moment; the lowest common tip is safe.
        return min(answer["height"] for answer in self._all("blockchain.headers.subscribe"))

    def block_hash(self, height):
        header = self._agreed("blockchain.block.header", height)
        return hashlib.sha256(hashlib.sha256(bytes.fromhex(header)).digest()).digest()[::-1].hex()

    def __call__(self, method, *params, wallet=None):
        if wallet is not None:
            raise ElectrumError("no node wallet on the mainnet stage")
        if method == "getblockcount":
            return self.tip_height()
        if method == "getblockhash":
            return self.block_hash(params[0])
        if method == "getblockchaininfo":
            height = self.tip_height()
            return {"chain": self.chain, "blocks": height, "headers": height, "initialblockdownload": False}
        if method == "getrawtransaction":
            txid, verbose = params[0], bool(params[1]) if len(params) > 1 else False
            if not verbose:
                return self._agreed("blockchain.transaction.get", txid)
            # Byte-identical transaction, same block, on every server.
            answers = self._all("blockchain.transaction.get", txid, True)
            first = answers[0]
            for other in answers[1:]:
                if other["hex"] != first["hex"] or other.get("blockhash") != first.get("blockhash"):
                    raise ElectrumError("servers disagree on transaction " + txid)
            result = dict(first)
            result["confirmations"] = min(int(a.get("confirmations", 0) or 0) for a in answers)
            return result
        if method == "getrawmempool":
            return MempoolView(self)
        if method == "validateaddress":
            return {"isvalid": True, "address": params[0], "scriptPubKey": segwit_script(params[0], self.hrp)}
        if method == "gettxout":
            txid, index = params[0], params[1]
            transaction = self("getrawtransaction", txid, True)
            script = transaction["vout"][index]["scriptPubKey"]["hex"]
            unspent = self._agreed("blockchain.scripthash.listunspent", scripthash(script),
                key=lambda rows: sorted((r["tx_hash"], r["tx_pos"]) for r in rows))
            hit = [r for r in unspent if r["tx_hash"] == txid and r["tx_pos"] == index]
            return {"value": transaction["vout"][index]["value"], "scriptPubKey": {"hex": script}} if hit else None
        if method == "sendrawtransaction":
            raise ElectrumError("the driver never broadcasts on the mainnet stage")
        raise ElectrumError("unsupported on the mainnet stage: " + method)
