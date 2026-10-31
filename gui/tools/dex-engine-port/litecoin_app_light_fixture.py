"""Disposable local-wallet setup for the actual two-peer Qt integration fixture."""
import hashlib
import json
import os
import subprocess
import uuid
import test_light_wallet_chain as chain
from test_litecoin_settlement import Node, require, write_private


def create_light_wallet(args, root, node, anchor, network='regtest', maximum_height=10000, peer=None, fund=None, tag='ltc'):
    """Regtest by default. A live network passes its own Electrum peer and a fund(address)
    that pays the new wallet and waits for the payment to confirm."""
    directory = root / 'local-light-wallet'
    directory.mkdir(mode=0o700)
    if peer is None:
        class ElectrumNode(Node):
            def header(self, height):
                return self.rpc('getblockheader', self.rpc('getblockhash', height), False)
        # Keep Electrum's request counter/HTTP opener separate from the main driver.
        observer = ElectrumNode(node.executable, node.directory)
        observer.port = node.port
        peer = chain.ElectrumPeer(observer, args.light_wallet_tls, directory)
    peer = peer.__enter__()
    try:
        helper = args.gui_light_recovery_helper
        digest = hashlib.sha256(helper.read_bytes()).hexdigest()
        password = 'disposable-' + uuid.uuid4().hex
        with os.fdopen(os.open(directory/'light-password', os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600), 'w') as secret:
            secret.write(password)
            secret.flush()
            os.fsync(secret.fileno())
        settings = dict(schema=f'monzero-private-{tag}-setup-v1', network=network,
            walletId=str(uuid.uuid4()), accountIndex=0, maximumHeight=maximum_height,
            seedEncoding='bip39-english-v1', endpoint=dict(host='localhost', port=peer.port))
        for key in ('setupDirectory', 'vaultDirectory', 'addressBookDirectory',
                    'backupDirectory', 'spendDirectory', 'cacheDirectory'):
            folder = directory / key
            folder.mkdir(mode=0o700)
            settings[key] = str(folder)
        setup = directory / 'setup.json'
        write_private(setup, settings)

        def call(mode, config, **fields):
            request = dict(schema=f'monzero-{tag}-light-{mode}-request-v1',
                requestId=str(uuid.uuid4()), **fields)
            result = subprocess.run([str(helper), '--'+mode+'-stdio-v1', str(config)],
                input=json.dumps(request).encode(), capture_output=True, timeout=240,
                env=dict(os.environ, SSL_CERT_FILE=str(args.light_wallet_tls/'root.pem')))
            require(password.encode() not in result.stdout+result.stderr, 'Secret in helper output')
            require(result.returncode == 0, 'Disposable light wallet setup failed: '+mode)
            return json.loads(result.stdout)['wallet']

        created = call('wallet-create', setup, createApproval=dict(
            approvedConfigDigest=hashlib.sha256(setup.read_bytes()).hexdigest(), password=password))
        wallet = created['configurationFile']
        address = call('wallet-receive', wallet)['address']
        if fund is None:
            node.rpc('sendtoaddress', address, '2', wallet='fixture')
            node.mine(1, anchor)
        else:
            fund(address)
        launcher = directory / 'launcher.json'
        write_private(launcher, dict(sorted(dict(schema=f'monzero-private-{tag}-light-launcher-v1',
            configFile=wallet, helper=str(helper), helperDigest=digest).items())))
        return peer, dict(helperSha256=digest, seedEncoding='bip39-english-v1',
            address=address, configurationFile=wallet, launcher=str(launcher))
    except BaseException:
        peer.__exit__(None, None, None)
        raise


def attach_light_wallet(args, root, resident, setup):
    source=root/'local-light-wallet'/'light-password'
    source.rename(resident.directory/'light-password')
    resident.call('bob', 'light-configure', helper=str(args.gui_light_recovery_helper),
        digest=setup['helperSha256'], launcher=setup['launcher'])
    resident.call('bob', 'light-select')
    state=resident.call('bob', 'light-terms')['lightFunding']
    require(state['state']=='complete' and state['terms']['refundAddress']==setup['address'],
        'Negotiated refund differs from the local wallet address')


def verify_light_refund(args, node, setup, refund_txid):
    def call(mode, **fields):
        identity=str(uuid.uuid4())
        request=dict(schema=f'monzero-ltc-light-{mode}-request-v1',requestId=identity,**fields)
        result=subprocess.run([str(args.gui_light_recovery_helper),'--'+mode+'-stdio-v1',setup['configurationFile']],
            input=json.dumps(request).encode(),capture_output=True,timeout=240,
            env=dict(os.environ,SSL_CERT_FILE=str(args.light_wallet_tls/'root.pem')))
        require(result.returncode==0, 'Local refund observation failed: '+mode)
        response=json.loads(result.stdout)
        require(response['requestId']==identity and response['schema']==request['schema'].replace('-request-','-response-')
            and response['executable'] is False, 'Local refund observation context mismatch')
        return response
    call('sync',maximumDownloadHeaders=10000)
    observed=call('wallet-refresh',walletScan=dict(gap=2,addressesPerBranch=3,historyEntriesPerBranch=50))['wallet']
    # Initial 2 LTC minus 1000 funding fee and 20000 cancellation/refund fees.
    require(observed['observedConfirmedAtomic']=='199979000'
        and observed['confirmedCoinCount']==2 and observed['pendingReceivedAtomic']=='0',
        'Local wallet did not recognize the exact confirmed refund and change')
    transaction=node.rpc('getrawtransaction',refund_txid,True)
    require(transaction['confirmations']>=2 and transaction['vout'][0]['scriptPubKey']['hex']
        ==node.rpc('validateaddress',setup['address'])['scriptPubKey'], 'Refund not paid to the local wallet')
    return dict(localWalletRefundVerified=True, localWalletRefundBalanceAtomic=observed['observedConfirmedAtomic'])


def submit_light_funding(resident, node, anchor, restart=None, restart_stage="sent", lost_ack_peer=None, inflight=None):
    def call(operation, **fields):
        value = resident.call('bob', 'light-'+operation, **fields)['lightFunding']
        if fields.get('accepted', True):
            require(value['state'] == 'complete', 'Local app funding failed: '+operation)
        return value
    pool = node.rpc('getrawmempool')
    sign = call('review')['review']['commitment']
    call('sign', accepted=False, commitment='0'*64)
    signed = call('sign', commitment=sign)['result']
    require(signed['signed'] and not signed['broadcast'] and node.rpc('getrawmempool') == pool,
        'Local signing broadcast a transaction')
    txid = signed['txid']
    if restart is not None and restart_stage=='signed':
        restart()
        call('sign', accepted=False, commitment=sign)
        call('send', accepted=False, commitment=sign)
        require(node.rpc('getrawmempool')==pool, 'Restart sent the signed payment without new consent')
    send = call('send-review')['review']['commitment']
    call('send', accepted=False, commitment=sign)
    require(node.rpc('getrawmempool') == pool, 'Signing consent authorized broadcast')
    if inflight is not None:
        peer,interrupt=inflight
        interrupt(send)
        require(node.rpc('getrawmempool')==[txid], 'Interrupted relay was not accepted by the owned node')
    elif lost_ack_peer is not None:
        lost_ack_peer.drop_next_broadcast_reply=True
        uncertain=resident.call('bob','light-send',commitment=send)['lightFunding']
        require(uncertain['state']=='outcome-unconfirmed' and not uncertain['result']
            and lost_ack_peer.dropped_broadcast_replies==1 and node.rpc('getrawmempool')==[txid],
            'Lost reply did not preserve an uncertain app outcome after node acceptance')
    else:
        sent = call('send', commitment=send)['result']
        require(sent['txid'] == txid and node.rpc('getrawmempool') == [txid], 'Local funding not accepted')
    call('send', accepted=False, commitment=send)
    if restart is not None and restart_stage=="sent" and inflight is None:
        restart()
        call('send', accepted=False, commitment=send)
        require(node.rpc('getrawmempool')==[txid], 'App interruption changed pending transaction')
    observed=call('observe')['result']
    require(observed['observation']['position']=='reported-pending', 'Missing pending funding')
    if lost_ack_peer is not None or inflight is not None:
        require(observed['broadcastStatus']=='outcome-unknown', 'Lost acknowledgement was falsely promoted to acknowledged')
    retry = call('retry-review')['review']['commitment']
    call('retry', accepted=False, commitment=send)
    require(call('retry', commitment=retry)['result']['txid']==txid
        and node.rpc('getrawmempool')==[txid], 'Retry changed transaction')
    fault_peer=lost_ack_peer if inflight is None else inflight[0]
    if fault_peer is not None:
        require(fault_peer.dropped_broadcast_replies==1 and len(fault_peer.accepted_broadcast_digests)==2
            and len(set(fault_peer.accepted_broadcast_digests))==1, 'Retry changed the full signed transaction bytes')
    block = node.mine(1, anchor)[0]
    observed = call('observe')['result']['observation']
    require(observed['position']=='included' and observed['blockHash']==block
        and observed['confirmations']>=1, 'Local funding inclusion missing')
    (resident.directory/'light-password').unlink()
    return dict(txid=txid)
