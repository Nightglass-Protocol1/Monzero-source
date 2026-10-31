#!/usr/bin/env python3
"""Light wallet end to end against an isolated actual node of each UTXO chain.

Usage: test_light_wallet_chain.py CHAIN HELPER NODE REPORT [--p2sh32]
The optional BCH-only flag pays a node-encoded P2SH32 destination.
CHAIN is btc (Bitcoin Core), bch (Bitcoin Cash Node) or doge (Dogecoin Core).

The shared light-wallet helper is driven with the chain's schemas through a
local TLS Electrum peer whose every answer is computed from the real regtest
node (headers, histories, unspent outputs, raw transactions, Merkle branches and
broadcast through `sendrawtransaction`). Flow: create an encrypted wallet,
check its first receive address independently where the node can derive it,
receive a real payment, refresh, prepare, sign, broadcast, confirm, reconcile
and refresh again. The node's own script interpreter judges every signature.
No public peers, user wallet data, live funds or XMZ mining are involved.
"""
import base64, decimal, hashlib, json, os, socket, ssl, subprocess, sys, tempfile, threading, time, urllib.request, uuid
from pathlib import Path
from cryptography.hazmat.primitives.serialization import load_der_private_key, Encoding, PrivateFormat, NoEncryption
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_litecoin_tls_fixture import generate as generate_tls

CHAINS = {
    'btc': {'genesis': '0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206', 'prefix': 'bcrt1q',
            'descriptor': 'wpkh', 'mature': 101, 'credit': '0.01', 'pay': 400_000, 'fee': 2_000, 'min_change': 1_000},
    'bch': {'genesis': '0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206', 'prefix': 'bchreg:q',
            'descriptor': None, 'mature': 101, 'credit': '0.01', 'pay': 400_000, 'fee': 2_000, 'min_change': 1_000},
    'doge': {'genesis': '3d2160a3b5dc4a9d62e7e66a295f70313ac808440ef7400d6c0772171ce973a5', 'prefix': None,
             'descriptor': None, 'mature': 160, 'credit': '100', 'pay': 4_000_000_000, 'fee': 10_000_000, 'min_change': 100_000_000},
}
CHAIN = None
PASSWORD = 'disposable regtest-only light wallet password'


def sha256d(data): return hashlib.sha256(hashlib.sha256(data).digest()).digest()
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def scripthash(script_hex): return hashlib.sha256(bytes.fromhex(script_hex)).digest()[::-1].hex()
def sats(value): return int((decimal.Decimal(str(value)) * 100_000_000).to_integral_exact())


class Node:
    """Owned regtest Bitcoin Core with no peers, cookie RPC and a transaction index."""
    def __init__(self, executable, directory):
        self.executable, self.directory = executable, directory
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0)); self.port = sock.getsockname()[1]
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.process = None; self.counter = 0

    def rpc(self, method, *params, wallet=None):
        self.counter += 1
        cookie = (self.directory / 'regtest/.cookie').read_bytes().strip()
        url = f'http://127.0.0.1:{self.port}' + (f'/wallet/{wallet}' if wallet else '')
        request = urllib.request.Request(url, data=json.dumps({'jsonrpc': '1.0', 'id': self.counter,
            'method': method, 'params': params}).encode(), headers={
            'Authorization': 'Basic ' + base64.b64encode(cookie).decode(), 'Content-Type': 'application/json'})
        try: response = self.opener.open(request, timeout=60)
        except urllib.error.HTTPError as error: response = error
        with response: value = json.loads(response.read(), parse_float=decimal.Decimal)
        if value.get('error'): raise RuntimeError(f"{method}: {value['error']['message']}")
        return value['result']

    def start(self):
        self.log = open(self.directory / 'daemon.log', 'ab')
        extra = ['-fallbackfee=0.0002'] if CHAIN in ('btc', 'bch', 'ltc') else []
        self.process = subprocess.Popen([str(self.executable), f'-datadir={self.directory}', '-regtest', '-server=1',
            '-listen=0', '-connect=0', '-dnsseed=0', '-discover=0',
            '-rpcbind=127.0.0.1', '-rpcallowip=127.0.0.1', f'-rpcport={self.port}', '-txindex=1',
            '-printtoconsole=1', *extra], stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            assert self.process.poll() is None, 'node exited during startup'
            try:
                assert self.rpc('getblockchaininfo')['chain'] == 'regtest'
                assert self.rpc('getblockhash', 0) == CHAINS[CHAIN]['genesis'], 'unexpected regtest genesis'
                assert self.rpc('getconnectioncount') == 0, 'fixture node has peers'
                return
            except (FileNotFoundError, urllib.error.URLError, ConnectionError, RuntimeError):
                time.sleep(0.2)
        raise RuntimeError('node startup timed out')

    def stop(self):
        if self.process and self.process.poll() is None:
            try: self.rpc('stop')
            except Exception: self.process.terminate()
            try: self.process.wait(timeout=30)
            except subprocess.TimeoutExpired: self.process.kill(); self.process.wait(timeout=5)
        if getattr(self, 'log', None): self.log.close()

    def mine(self, count, address):
        try: return self.rpc('generatetoaddress', count, address)
        except RuntimeError as error:
            if 'not found' not in str(error).lower(): raise
            return self.rpc('generate', count)  # older Dogecoin RPC mines to the node wallet
    def header(self, height): return self.rpc('getblockheader', self.rpc('getblockhash', height), False)


class ElectrumPeer:
    """Local TLS Electrum 1.4 peer computed live from the node. Not a trusted server."""
    def __init__(self, node, fixture, workdir):
        self.node, self.requests, self.errors, self.stop = node, [], [], threading.Event()
        self.cache, self.blocks = {}, {}
        self.hold_next_broadcast_reply = False
        self.broadcast_held = threading.Event()
        self.release_broadcast_reply = threading.Event()
        self.drop_next_broadcast_reply = False
        self.dropped_broadcast_replies = 0
        self.accepted_broadcast_digests = []
        cert, key = workdir / 'server.pem', workdir / 'server-key.pem'
        cert.write_text(ssl.DER_cert_to_PEM_cert((fixture / 'server.der').read_bytes()))
        key.write_bytes(load_der_private_key((fixture / 'server-key.der').read_bytes(), None)
            .private_bytes(Encoding.PEM, PrivateFormat.PKCS8, NoEncryption())); key.chmod(0o600)
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); self.context.load_cert_chain(cert, key)
        self.listener = socket.socket(); self.listener.bind(('127.0.0.1', 0)); self.port = self.listener.getsockname()[1]
        self.listener.listen(); self.listener.settimeout(0.2)

    def __enter__(self):
        self.thread = threading.Thread(target=self.serve, daemon=True); self.thread.start(); return self

    def __exit__(self, *args):
        self.stop.set(); self.release_broadcast_reply.set(); self.listener.close(); self.thread.join(10)
        assert not self.errors, self.errors

    # ---- chain views computed from the node
    def decoded(self, txid):
        if txid not in self.cache: self.cache[txid] = self.node.rpc('getrawtransaction', txid, 1)
        return self.cache[txid]

    def transactions(self):
        """(height, tx) for every confirmed block transaction and mempool entry (height 0).
        Uses only RPCs every supported node has (Dogecoin 1.14 lacks getblock verbosity)."""
        tip = self.node.rpc('getblockcount')
        for height in range(1, tip + 1):
            block = self.node.rpc('getblockhash', height)
            if block not in self.blocks: self.blocks[block] = self.node.rpc('getblock', block, True)['tx']
            for txid in self.blocks[block]: yield height, self.decoded(txid)
        for txid in self.node.rpc('getrawmempool'):
            yield 0, self.decoded(txid)

    def history(self, target):
        entries, scripts = [], {}
        for height, tx in self.transactions():
            touched = False
            for out in tx['vout']:
                scripts[(tx['txid'], out['n'])] = out['scriptPubKey']['hex']
                touched |= scripthash(out['scriptPubKey']['hex']) == target
            touched |= any(scripthash(scripts.get((vin.get('txid'), vin.get('vout')), '')) == target for vin in tx['vin'] if 'txid' in vin)
            if touched: entries.append({'tx_hash': tx['txid'], 'height': height})
        return entries

    def unspent(self, target):
        spent, outputs = set(), []
        for height, tx in self.transactions():
            for vin in tx['vin']:
                if 'txid' in vin: spent.add((vin['txid'], vin['vout']))
            for out in tx['vout']:
                if scripthash(out['scriptPubKey']['hex']) == target:
                    outputs.append({'tx_hash': tx['txid'], 'tx_pos': out['n'], 'height': height, 'value': sats(out['value'])})
        return [o for o in outputs if (o['tx_hash'], o['tx_pos']) not in spent]

    def merkle(self, txid, height):
        txids = self.node.rpc('getblock', self.node.rpc('getblockhash', height), True)['tx']
        index = txids.index(txid); level = [bytes.fromhex(t)[::-1] for t in txids]; branch = []
        while len(level) > 1:
            if len(level) % 2: level.append(level[-1])
            branch.append(level[index ^ 1][::-1].hex())
            level = [sha256d(level[i] + level[i + 1]) for i in range(0, len(level), 2)]; index //= 2
        return {'block_height': height, 'pos': txids.index(txid), 'merkle': branch}

    def answer(self, method, params):
        node = self.node
        if method == 'server.version': return [f'monzero-{CHAIN}-fixture', '1.4']
        if method == 'server.features': return {'genesis_hash': node.rpc('getblockhash', 0), 'hash_function': 'sha256'}
        if method == 'blockchain.headers.subscribe':
            tip = node.rpc('getblockcount'); return {'height': tip, 'hex': node.header(tip)}
        if method == 'blockchain.block.header': return node.header(params[0])
        if method == 'blockchain.block.headers':
            start, count = params; tip = node.rpc('getblockcount')
            headers = [node.header(h) for h in range(start, min(tip, start + count - 1) + 1)]
            return {'count': len(headers), 'hex': ''.join(headers), 'max': 2016}
        if method == 'blockchain.scripthash.get_history': return self.history(params[0])
        if method == 'blockchain.scripthash.listunspent': return self.unspent(params[0])
        if method == 'blockchain.transaction.get':
            assert params[1] is False; return node.rpc('getrawtransaction', params[0], 0)
        if method == 'blockchain.transaction.get_merkle': return self.merkle(params[0], params[1])
        if method == 'blockchain.transaction.broadcast': return node.rpc('sendrawtransaction', params[0])
        if method == 'blockchain.estimatefee': return 0.0002 if CHAIN != 'doge' else 0.01
        raise AssertionError('unexpected Electrum method ' + method)

    def serve(self):
        while not self.stop.is_set():
            try: connection, _ = self.listener.accept()
            except socket.timeout: continue
            except OSError:
                if self.stop.is_set(): return
                raise
            try:
                connection.settimeout(60)
                with self.context.wrap_socket(connection, server_side=True) as tls, tls.makefile('rwb') as stream:
                    while True:
                        line = stream.readline(1 << 20)
                        if not line: break
                        request = json.loads(line); self.requests.append(request['method'])
                        result = self.answer(request['method'], request['params'])
                        if request['method'] == 'blockchain.transaction.broadcast':
                            self.accepted_broadcast_digests.append(hashlib.sha256(bytes.fromhex(request['params'][0])).hexdigest())
                            if self.hold_next_broadcast_reply:
                                self.hold_next_broadcast_reply = False
                                self.broadcast_held.set()
                                if not self.release_broadcast_reply.wait(180):
                                    raise RuntimeError('Owned broadcast barrier timed out')
                                self.dropped_broadcast_replies += 1
                                break
                            if self.drop_next_broadcast_reply:
                                self.drop_next_broadcast_reply = False
                                self.dropped_broadcast_replies += 1
                                break  # Node accepted; intentionally lose the TLS reply once.
                        stream.write(json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': result},
                            default=str).encode() + b'\n'); stream.flush()
            except ssl.SSLError: pass
            except Exception as error: self.errors.append(repr(error)); return


class Helper:
    def __init__(self, program, fixture): self.program, self.fixture = program, fixture

    def call(self, mode, config, extra=None, scan=True):
        identity = (extra or {}).get('requestId', str(uuid.uuid4()))
        request = {'schema': f'monzero-{CHAIN}-light-{mode}-request-v1', 'requestId': identity}
        if scan and mode in ['wallet-refresh', 'wallet-prepare', 'wallet-reopen', 'wallet-estimate', 'wallet-sign', 'wallet-broadcast']:
            request['walletScan'] = {'gap': 2, 'addressesPerBranch': 3, 'historyEntriesPerBranch': 50}
        request.update(extra or {})
        env = os.environ.copy(); env['SSL_CERT_FILE'] = str(self.fixture / 'root.pem')
        result = subprocess.run([str(self.program), f'--{mode}-stdio-v1', str(config)], input=json.dumps(request).encode(),
            capture_output=True, env=env, timeout=600)
        assert result.returncode == 0, (mode, result.returncode, result.stderr.decode())
        value = json.loads(result.stdout)
        assert value['schema'] == f'monzero-{CHAIN}-light-{mode}-response-v1' and value['requestId'] == identity
        assert value['executable'] is False and PASSWORD.encode() not in result.stdout
        return value


def write_private(path, value): path.write_text(json.dumps(value)); path.chmod(0o600)


def atomic_text(value): return str(value)


def run():
    global CHAIN
    CHAIN = sys.argv[1]
    assert CHAIN in CHAINS, 'chain must be btc, bch or doge'
    spec = CHAINS[CHAIN]
    program, node_binary, report = Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(), Path(sys.argv[4]).resolve()
    assert not report.exists(), 'report already exists'
    p2sh32 = sys.argv[5:] == ['--p2sh32']
    assert not sys.argv[5:] or (p2sh32 and CHAIN == 'bch'), 'only BCH supports --p2sh32'
    started = time.time()
    credit_atomic = sats(spec['credit']); pay, fee, minimum_change = spec['pay'], spec['fee'], spec['min_change']
    change = credit_atomic - pay - fee
    with tempfile.TemporaryDirectory(prefix=f'monzero-{CHAIN}-light-') as temp:
        root = Path(temp).resolve(); fixture = root / 'tls'; generate_tls(fixture)
        helper = Helper(program, fixture)
        dirs = {}
        for key, name in [('setupDirectory', 'setup'), ('vaultDirectory', 'vault'), ('addressBookDirectory', 'book'),
                          ('backupDirectory', 'backup'), ('spendDirectory', 'spends'), ('cacheDirectory', 'cache')]:
            (root / name).mkdir(mode=0o700); dirs[key] = str(root / name)
        setup = root / 'setup.json'
        write_private(setup, dict(dirs, schema=f'monzero-private-{CHAIN}-setup-v1', network='regtest',
            walletId=str(uuid.uuid4()), accountIndex=0, maximumHeight=1000))

        # 1. create the encrypted wallet through the explicit approval gate
        created = helper.call('wallet-create', setup, {'createApproval': {
            'approvedConfigDigest': digest(setup), 'password': PASSWORD}})['wallet']
        published = Path(created['configurationFile']); assert digest(published) == created['configurationDigest']
        config_value = json.loads(published.read_bytes())
        assert config_value['schema'] == f'monzero-private-{CHAIN}-light-v1' and config_value['network'] == 'regtest'
        assert config_value['wallet']['account']['schema'] == f'monzero-{CHAIN}-public-account-v1'
        runtime = root / 'runtime.json'; write_private(runtime, config_value)

        node = Node(node_binary, root / 'node'); (root / 'node').mkdir(mode=0o700); node.start()
        try:
            # 2. first receive address; derived independently by the node where it supports descriptors
            receive = helper.call('wallet-receive', runtime)['wallet']['address']
            validated = node.rpc('validateaddress', receive)
            assert validated['isvalid'], validated
            independent = None
            if spec['descriptor']:
                xpub = config_value['wallet']['account']['accountXpub']
                described = node.rpc('getdescriptorinfo', f"{spec['descriptor']}({xpub}/0/0)")['descriptor']
                independent = node.rpc('deriveaddresses', described)
                assert independent == [receive], ('node derives a different address', independent, receive)
            if spec['prefix']: assert receive.startswith(spec['prefix']), receive
            else: assert receive[0] in 'mn', receive

            # 3. a real payment from the node's own wallet
            if CHAIN == 'btc': node.rpc('createwallet', 'fixture'); wallet = 'fixture'
            else: wallet = None
            anchor = node.rpc('getnewaddress', '', 'bech32', wallet=wallet) if CHAIN == 'btc' else node.rpc('getnewaddress')
            node.mine(spec['mature'], anchor)
            credit = node.rpc('sendtoaddress', receive, spec['credit'], wallet=wallet); node.mine(1, anchor)
            with ElectrumPeer(node, fixture, root) as peer:
                config_value['endpoint'] = {'host': 'localhost', 'port': peer.port}; write_private(runtime, config_value)
                synced = helper.call('sync', runtime, {'maximumDownloadHeaders': 1000}, scan=False)
                assert synced['chain']['height'] == node.rpc('getblockcount')
                assert synced['chain']['tip'] == node.rpc('getbestblockhash') and synced['chain']['network'] == 'regtest'
                funded = helper.call('wallet-refresh', runtime)['wallet']
                assert funded['observedConfirmedAtomic'] == atomic_text(credit_atomic), funded
                assert funded['confirmedCoinCount'] == 1 and funded['pendingReceivedAtomic'] == '0'

                # 4. prepare, sign and broadcast a payment back to the node wallet
                destination = node.rpc('getnewaddress', '', 'bech32', wallet=wallet) if CHAIN == 'btc' else node.rpc('getnewaddress')
                contract_script = None
                if p2sh32:
                    # Node independently encodes the exact HASH256 contract output.
                    contract_script = 'aa20' + sha256d(bytes.fromhex('51')).hex() + '87'
                    decoded_contract = node.rpc('decodescript', contract_script)
                    destination = decoded_contract['addresses'][0]
                    assert node.rpc('validateaddress', destination)['scriptPubKey'] == contract_script
                payment = {'address': destination, 'amountAtomic': atomic_text(pay), 'feeAtomic': atomic_text(fee),
                           'minimumChangeAtomic': atomic_text(minimum_change), 'minimumConfirmations': 1}
                operation = str(uuid.uuid4())
                review = helper.call('wallet-prepare', runtime, {'requestId': operation, 'payment': payment})['wallet']
                assert review['paymentAtomic'] == atomic_text(pay) and review['feeAtomic'] == atomic_text(fee)
                assert review['changeAtomic'] == atomic_text(change), review
                assert node.rpc('validateaddress', review['changeAddress'])['isvalid']
                retained = {'signingCommitment': review['signingCommitment'], 'changeAddress': review['changeAddress']}
                signed = helper.call('wallet-sign', runtime, {'requestId': operation, 'payment': payment, 'retainedReview': retained,
                    'signingApproval': {'approvedCommitment': review['signingCommitment'], 'password': PASSWORD}})['wallet']
                raw = (Path(dirs['spendDirectory']) / f'ltc-signed-{signed["signingCommitment"]}.tx').read_bytes()
                if CHAIN == 'doge':
                    decoded_raw = node.rpc('decoderawtransaction', raw.hex())
                    assert decoded_raw['txid'] == signed['txid']
                else:
                    accepted = node.rpc('testmempoolaccept', [raw.hex()])[0]
                    assert accepted['allowed'], accepted
                sent = helper.call('wallet-broadcast', runtime, {'requestId': operation, 'payment': payment, 'retainedReview': retained,
                    'broadcastApproval': {'approvedCommitment': signed['signingCommitment'], 'approvedTxid': signed['txid'],
                                          **config_value['endpoint']}})['wallet']
                assert sent['broadcastStatus'] == 'server-acknowledged' and signed['txid'] in node.rpc('getrawmempool')

                # 5. confirm and reconcile against the selected header chain
                confirmed_block = node.mine(1, anchor)[0]
                status = {'requestId': operation, 'payment': payment, 'retainedReview': retained}
                reconciled = helper.call('wallet-reconcile', runtime, dict(status, maximumDownloadHeaders=1000,
                    maximumHistoryEntries=10), scan=False)['wallet']
                assert reconciled['confirmationStatus'] == 'included', reconciled
                assert reconciled['paymentObservation']['blockHash'] == confirmed_block
                decoded = node.rpc('getrawtransaction', signed['txid'], 1)
                def paid_to(out):
                    key = out['scriptPubKey']
                    if contract_script is not None: return key['hex'] == contract_script
                    return key.get('address') == destination or destination in key.get('addresses', [])
                paid = [o for o in decoded['vout'] if paid_to(o)]
                assert len(paid) == 1 and sats(paid[0]['value']) == pay and decoded['confirmations'] == 1, decoded

                after = helper.call('wallet-refresh', runtime)['wallet']
                assert after['observedConfirmedAtomic'] == atomic_text(change), after
                methods = sorted(set(peer.requests))
            report.write_text(json.dumps({'status': 'passed', 'fixture': f'actual-isolated-{CHAIN}-light-wallet-v1',
                'chain': CHAIN, 'network': 'regtest', 'p2sh32Destination': p2sh32, 'nodeVersion': node.rpc('getnetworkinfo')['subversion'],
                'receiveAddress': receive, 'independentDerivation': independent, 'creditTxid': credit, 'paymentTxid': signed['txid'],
                'paymentBlock': confirmed_block, 'paidAtomic': pay, 'feeAtomic': fee, 'changeAtomic': change,
                'balancesAtomic': {'afterCredit': credit_atomic, 'afterPayment': change},
                'electrumMethods': methods, 'helperSha256': digest(program), 'testSha256': digest(__file__),
                'elapsedSeconds': round(time.time() - started, 1)}, indent=2) + '\n')
            report.chmod(0o600)
            print(f'PASS: {CHAIN} light wallet created, received, signed, broadcast, confirmed and reconciled on an actual node')
        finally:
            node.stop()


if __name__ == '__main__':
    run()
