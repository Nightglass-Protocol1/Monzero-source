#!/usr/bin/env python3
"""Owned Litecoin regtest/XMZ fakechain success path through production controllers."""
import argparse
import json
import os
import signal
import pathlib
import secrets
import shutil
import subprocess
import tempfile
import time
import uuid
from nacl.signing import SigningKey
from nacl.pwhash import argon2id
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_encrypt
from test_litecoin_settlement import Node, write_private, require, atoms
from test_wallet_recovery import GENESIS, port, ready, rpc
from litecoin_app_fixture import LitecoinAppFixture

TEST = "litecoin_negotiation::paired_chain_fixture::actual_paired_chain_execution"


def install_identity(root, role):
    # Disposable known seeds encrypted in the actual Qt identity-store format.
    seed = bytes([1 if role == "litecoin-funder" else 2])*32
    password = secrets.token_hex(24).encode()
    salt, nonce = secrets.token_bytes(16), secrets.token_bytes(24)
    header = b"MONZERO-DEX-IDENTITY-ARGON2ID13-3-64M-XCHACHA20-V1\n" + salt + nonce
    key = argon2id.kdf(32, password, salt, opslimit=3, memlimit=64*1024*1024)
    blob = header + crypto_aead_xchacha20poly1305_ietf_encrypt(seed, header, nonce, key)
    path = root / (role+"-identity.bin")
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as file:
        file.write(blob)
    return path, password, SigningKey(seed).verify_key.encode().hex()


def run(args):
    require(not args.report.exists(), "Report already exists")
    require(not args.gui_test_binary or args.journal_helper, "GUI journal fixture requires journal helper")
    require(not args.gui_submit_refund or (args.gui_test_binary and args.journal_helper and args.cancel_refund), "GUI submission requires an isolated refund fixture and both helpers")
    require(not args.gui_submit_payout or (args.gui_test_binary and args.journal_helper and not args.cancel_refund), "GUI payout submission requires a success fixture and both helpers")
    require(not args.gui_check_funding or (args.helper_check_funding and args.gui_test_binary), "GUI funding confirmation requires helper checks and Qt")
    require(not args.helper_check_funding or args.helper_submit_funding, "Funding confirmation fixture requires configured helper funding mode")
    require(not args.gui_submit_funding or (args.helper_submit_funding and args.gui_test_binary), "GUI funding submission requires funding helper mode and Qt binary")
    require(not args.gui_app_setup or (args.indexed_settlement and args.gui_peer_startup and args.gui_reserve_litecoin
        and args.helper_submit_funding), "Shared app setup requires the indexed paired-chain GUI fixture")
    require(not args.indexed_settlement or (args.gui_restore_wallet and args.gui_prepare_payments
        and (args.gui_submit_refund if args.cancel_refund else args.gui_submit_payout)),
        "Indexed settlement requires Qt wallet recovery, preparation and submission checks")
    require(not args.gui_submit_ltc_redemption or args.gui_redemption_session, "Qt redemption submission requires the private redemption session")
    require(not args.gui_redemption_session or (args.gui_peer_startup and args.gui_recovery_session and args.helper_submit_funding and not args.cancel_refund and not args.gui_redeem_signing), "Private redemption requires the peer/recovery/funding success fixture and replaces direct signing")
    require(not args.gui_redeem_signing or (args.gui_peer_startup and not args.cancel_refund), "Qt redemption signing requires the private peer success fixture")
    require(not args.gui_check_ltc_recovery or args.gui_submit_ltc_recovery, "Qt recovery confirmation requires app-driven recovery submission")
    require(not args.gui_submit_ltc_recovery or (args.gui_reserve_litecoin and args.cancel_refund), "Qt Litecoin recovery requires reservation and the refund path")
    require(not args.gui_restore_wallet or args.gui_prepare_payments, "Wallet restoration requires Qt payment preparation")
    require(not args.gui_prepare_payments or (args.gui_submit_refund if args.cancel_refund else args.gui_submit_payout), "Payment preparation requires matching Qt submission")
    require(not args.gui_submit_ltc_lock or args.gui_reserve_litecoin, "Qt Litecoin submission requires Qt reservation")
    require(not args.gui_reserve_litecoin or args.gui_recovery_session, "Qt reservation requires the private recovery session")
    require(not args.gui_recovery_session or (args.gui_peer_startup and not args.gui_recovery_signing), "Recovery session requires peer startup without fixture-driven signing")
    require(not args.gui_recovery_signing or args.gui_peer_startup, "Recovery signing fixture requires configured Qt peer startup")
    require(not args.gui_prepare_funding or (args.helper_submit_funding and args.gui_test_binary), "GUI preparation requires funding helper and Qt binary")
    require(not args.helper_submit_funding or args.journal_helper, "Funding submission requires the journal helper")
    require(not args.gui_start or (args.helper_start and args.gui_test_binary), "GUI startup requires both helpers and startup mode")
    require(not args.gui_peer_startup or (args.helper_start and args.gui_test_binary and not args.gui_negotiate_session and not args.gui_sign_negotiation), "Wallet peer startup requires helper startup and Qt binary without another negotiation driver")
    require(not args.gui_negotiate_session or (args.helper_start and args.gui_test_binary and not args.gui_sign_negotiation), "Private session requires helper startup and Qt binary, without the fixture signer")
    require(not args.gui_sign_negotiation or (args.helper_negotiate and args.gui_test_binary), "Qt signing requires peer negotiation and Qt fixture binary")
    require(not args.helper_negotiate or args.helper_start, "Peer admission fixture requires helper startup")
    require(not args.helper_start or args.journal_helper, "Startup fixture requires journal helper")
    require(not args.gui_resident_funding or (args.gui_app_setup and args.gui_prepare_funding and args.gui_submit_funding
        and args.gui_check_funding and args.gui_submit_ltc_lock), "Resident funding requires the complete shared startup/funding fixture")
    require(not args.gui_resident_settlement or (args.gui_resident_funding
        and (args.gui_check_ltc_recovery if args.cancel_refund else args.gui_submit_ltc_redemption)),
        "Continuous settlement requires resident funding and the complete Qt settlement path")
    require(not args.gui_native_cancel or (args.cancel_refund and args.gui_resident_settlement),
        "Native-funder cancellation requires the continuous app refund fixture")
    require(not args.gui_native_punish or args.gui_native_cancel,
        "Native punishment requires native cancellation and its continuous app prerequisites")
    require(not args.gui_light_recovery_helper or (args.gui_resident_funding and args.light_wallet_tls),
        "Light recovery requires resident app funding and owned Electrum TLS certificates")
    require(not args.gui_light_settlement or (args.gui_light_recovery_helper and args.gui_resident_settlement),
        "Light settlement requires the continuous app fixture")
    require(not args.gui_light_restart or args.gui_light_settlement, "Active trade restart requires local settlement")
    require(not args.gui_light_lost_ack or (args.gui_light_restart and args.gui_light_restart_stage=="sent"),
        "Lost acknowledgement fixture requires restart after attempted sending")
    require(not args.gui_light_inflight or (args.gui_light_restart and args.gui_light_restart_stage=="sent" and not args.gui_light_lost_ack),
        "In-flight interruption requires its own sent-stage restart fixture")
    light_peer = None
    resident = None
    root = pathlib.Path(tempfile.mkdtemp(prefix="ltc-xmz-paired-", dir=args.temp_parent)).resolve()
    processes = []
    ltc = None
    try:
        (root / "ltc").mkdir(mode=0o700)
        ltc = Node(args.litecoind, root / "ltc")
        ltc.start()
        ltc.rpc("createwallet", "fixture")
        anchor = ltc.rpc("getnewaddress", "", "bech32", wallet="fixture")
        ltc.mine(440, anchor)
        ltc_payout = ltc.rpc("getnewaddress", "", "bech32", wallet="fixture")
        if args.gui_light_recovery_helper:
            from litecoin_app_light_fixture import create_light_wallet
            light_peer,light_setup=create_light_wallet(args,root,ltc,anchor)
            ltc_refund=light_setup['address']
        else:
            ltc_refund = ltc.rpc("getnewaddress", "", "bech32", wallet="fixture")
        daemon_port, source_port, claim_port, observer_port, p2p_port = port(), port(), port(), port(), port()
        require(len({daemon_port, source_port, claim_port, observer_port, p2p_port, ltc.port}) == 6, "Fixture port collision")
        daemon = subprocess.Popen([str(args.daemon), "--regtest", "--keep-fakechain", "--fixed-difficulty", "1",
            "--offline", "--no-igd", "--hide-my-port", "--no-zmq", "--non-interactive",
            "--data-dir", str(root / "native-chain"), "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(daemon_port),
            "--p2p-bind-ip", "127.0.0.1", "--p2p-bind-port", str(p2p_port), "--log-file", str(root / "native-node.log")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(daemon)
        info = ready(daemon, daemon_port, "get_info")
        require(info["nettype"] == "fakechain" and info["offline"]
            and info["incoming_connections_count"] == info["outgoing_connections_count"] == 0, "Native node not isolated")
        require(rpc(daemon_port, "get_block", {"height":0})["block_header"]["hash"] == GENESIS, "Wrong native genesis")
        wallet_processes={}
        for name, rpc_port in (("source-wallets", source_port), ("claim-wallets", claim_port), ("observer-wallets", observer_port)):
            (root / name).mkdir(mode=0o700)
            process = subprocess.Popen([str(args.wallet_rpc), "--wallet-dir", str(root / name),
                "--daemon-address", f"127.0.0.1:{daemon_port}", "--trusted-daemon", "--allow-mismatched-daemon-version",
                "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", str(rpc_port), "--non-interactive",
                "--max-concurrency", "2", "--log-file", str(root / (name+".log"))], cwd=root,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            processes.append(process)
            wallet_processes[rpc_port]=process
            ready(process, rpc_port, "get_version", root / f"monzero-wallet-rpc.{rpc_port}.login")

        def wallet(method, params=None, claim=False, observer=False):
            rpc_port = observer_port if observer else (claim_port if claim else source_port)
            return rpc(rpc_port, method, params, root / f"monzero-wallet-rpc.{rpc_port}.login")

        addresses = {}
        for name in ("mining", "payout", "reserve", "source"):
            wallet("create_wallet", {"filename":name,"password":"","language":"English"})
            addresses[name] = wallet("get_address")["address"]
            wallet("close_wallet")
        wallet("open_wallet", {"filename":"source","password":""})
        rpc(daemon_port, "generateblocks", {"wallet_address":addresses["source"],"amount_of_blocks":131})
        wallet("refresh", {"start_height":0})
        policy = {"ltc_network":"ltc-regtest","xmz_network":"xmz-mainnet","xmz_genesis":GENESIS,
            "ltc_confirmations":2,"xmz_confirmations":10,"cancel_blocks":20,"punish_blocks":10,
            "maximum_ltc_atoms":1000000000,"maximum_xmz_atoms":200000000000,
            "maximum_ltc_funding_fee":100000,"maximum_ltc_recovery_fee":20000,
            "maximum_xmz_fee":10000000000,"minimum_ltc_output":100000,"minimum_relay_fee_atoms_per_kb":1000}
        terms = {"schema":"monzero-native-ltc-xmz-quote-v2","quoteId":str(uuid.uuid4()),"swapId":str(uuid.uuid4()),
            "ltcFunder":SigningKey(bytes([1])*32).verify_key.encode().hex(),
            "xmzFunder":SigningKey(bytes([2])*32).verify_key.encode().hex(),
            "ltcGenesis":ltc.rpc("getblockhash", 0),"xmzGenesis":GENESIS,"ltcNetwork":"ltc-regtest","xmzNetwork":"xmz-mainnet",
            "ltcAtoms":"100000000","xmzAtoms":"200000000000","xmzPayoutAtoms":"100000000000",
            "xmzClaimFeeMaxAtoms":"10000000000","xmzRefundFeeMaxAtoms":"10000000000",
            "ltcPayout":ltc_payout,"ltcRefund":ltc_refund,"xmzPayout":addresses["payout"],"xmzRefund":addresses["reserve"],
            "ltcFundingFeeMaxAtoms":"100000","xmzNativeFeeMaxAtoms":"10000000000",
            "ltcCancelFeeAtoms":"10000","ltcRefundFeeAtoms":"10000","ltcRedeemFeeAtoms":"10000",
            "ltcPunishFeeAtoms":"10000","ltcEarlyRefundFeeAtoms":"10000","ltcConfirmations":"2","xmzConfirmations":"10",
            "cancelBlocks":"20","punishBlocks":"10","expiresAt":str(int(time.time())+3500),"appFeeXmzAtoms":"0",
            "ltcMinimumOutputAtoms":"100000","ltcMinimumRelayFeeAtomsPerKb":"1000"}
        request = {"policy":policy,"terms":terms,"ltc_port":ltc.port,"native_port":daemon_port,
            "wallet_port":source_port,"claim_wallet_port":claim_port,"observer_wallet_port":observer_port,"anchor":anchor,"native_source":addresses["source"],
            "password":secrets.token_hex(16)}

        def invoke(action, reject=False):
            request["action"] = action
            path = root / "request.json"
            if path.exists():
                path.unlink()  # Disposable input; never controller state.
            write_private(path, request)
            print("Paired-chain stage: " + action, flush=True)
            started = time.monotonic()
            completed = subprocess.run([str(args.engine_test_binary), "--exact", TEST, "--ignored"],
                env=dict(os.environ, MONZERO_PAIRED_CHAIN_FIXTURE=str(root)), capture_output=True, timeout=1800)
            print(f"Stage {action}: exit={completed.returncode}, elapsed={time.monotonic()-started:.1f}s", flush=True)
            if reject:
                require(completed.returncode != 0 and not (root / (action+"-result.json")).exists(),
                        "Premature recovery was accepted")
                return
            if completed.returncode:
                diagnostic = pathlib.Path(str(args.report)+"."+action+".failure.log")
                with os.fdopen(os.open(diagnostic, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as out:
                    out.write(completed.stdout + completed.stderr)
            require(completed.returncode == 0 and b"1 passed" in completed.stdout, "Paired-chain stage failed: "+action)
            result = json.loads((root / (action+"-result.json")).read_text())
            require(result["schema"] == "monzero-paired-chain-fixture-v1" and result["executable"] is False,
                    "Unexpected paired-chain result")
            return result

        def install_journal(payout=False, label="journal"):
            import hashlib
            # Cargo's build artifact may have another hard link in deps/.
            # The native launcher requires an owned single-link installation.
            require(args.journal_helper.stat().st_size <= 512*1024*1024, "Oversized journal helper")
            executable = args.journal_helper.read_bytes()
            helper = root / (label+"-helper")
            with os.fdopen(os.open(helper, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o700), "wb") as out:
                out.write(executable)
                out.flush()
                os.fsync(out.fileno())
            require(helper.stat().st_nlink == 1 and hashlib.sha256(helper.read_bytes()).digest()
                == hashlib.sha256(executable).digest(), "Journal helper installation changed bytes")
            config = {"schema":"monzero-private-ltc-journal-v1","helper":str(helper),
                "helperDigest":hashlib.sha256(helper.read_bytes()).hexdigest(),
                "metadataDirectory":str(root / ("bob-meta" if payout else "alice-meta")),"snapshotDirectory":str(root / ("bob-secrets" if payout else "alice-secrets")),
                "tradeId":terms["swapId"],"role":"litecoin-funder" if payout else "native-funder",
                "pins":[list(bytes.fromhex(terms["ltcFunder"])),list(bytes.fromhex(terms["xmzFunder"]))],"policy":policy}
            config_path = root / (label+"-config.json")
            write_private(config_path, json.loads(json.dumps(config, sort_keys=True)))
            return helper,config,config_path

        def prepare_app_setup():
            # Provision every phase before creation, without future block hashes.
            app_directory=root/"app-setup";app_directory.mkdir(mode=0o700)
            app_request=dict(session_request,appDirectory=str(app_directory))
            funding_rpc=root/"app-funding-rpc.json"
            write_private(funding_rpc,{"schema":"monzero-private-ltc-funding-rpc-v2","ltcPort":ltc.port,
                "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                "nativeDaemonPort":daemon_port,"nativeWalletPort":source_port,
                "nativeLoginFile":str(root/f"monzero-wallet-rpc.{source_port}.login"),
                "source":{"wallet":addresses["source"],"account":0,"priority":1}})
            funding=dict(startup_configs["native-funder"][1]);funding.pop("startQuoteFile")
            funding.update(schema="monzero-private-ltc-journal-v4",fundingRpcFile=str(funding_rpc))
            funding_path=root/"app-funding.json";write_private(funding_path,json.loads(json.dumps(funding,sort_keys=True)))
            app_request["aliceSettlementConfig"]=str(funding_path)
            reservation_rpc=root/"app-reservation-rpc.json"
            write_private(reservation_rpc,{"schema":"monzero-private-ltc-reservation-rpc-v1","ltcPort":ltc.port,
                "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                "reservationDirectory":str(root/"reservations"),"feeRateAtomsPerKb":1000})
            reservation=root/"app-reservation.json"
            write_private(reservation,{"journalConfigFile":str(startup_configs["litecoin-funder"][2]),
                "rpcFile":str(reservation_rpc),"schema":"monzero-private-ltc-reservation-v1"})
            app_request["reservationConfig"]=str(reservation)
            for role,label,purpose in (("native-funder","alice","refund"),("litecoin-funder","bob","payout")):
                password_path=root/("app-"+label+"-wallet-password")
                with os.fdopen(os.open(password_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),"wb") as out:
                    out.write(request["password"].encode())
                observer=source_port if purpose=="refund" else observer_port
                rpc_path=root/("app-"+label+"-payment-rpc.json")
                write_private(rpc_path,{"schema":"monzero-private-ltc-"+purpose+"-rpc-v4","ltcPort":ltc.port,
                    "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                    "nativeDaemonPort":daemon_port,"nativeWalletPort":claim_port,"nativeLoginFile":str(root/f"monzero-wallet-rpc.{claim_port}.login"),
                    "preparationObserver":{"walletPort":observer,"loginFile":str(root/f"monzero-wallet-rpc.{observer}.login")},
                    "walletPasswordFile":str(password_path)})
                config=dict(startup_configs[role][1]);config.pop("startQuoteFile")
                config["schema"]="monzero-private-ltc-journal-v2" if purpose=="refund" else "monzero-private-ltc-journal-v3"
                config[purpose+"RpcFile"]=str(rpc_path)
                payment_path=root/("app-"+label+"-payment.json")
                write_private(payment_path,json.loads(json.dumps(config,sort_keys=True)))
                app_request[label+"PaymentConfig"]=str(payment_path)
            redeem_rpc=root/"app-redeem-rpc.json"
            write_private(redeem_rpc,{"schema":"monzero-private-ltc-redeem-rpc-v1","ltcPort":ltc.port,
                "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                "nativeDaemonPort":daemon_port,"observerWalletPort":observer_port,
                "observerLoginFile":str(root/f"monzero-wallet-rpc.{observer_port}.login"),"safetyBlocks":2})
            wrapper=root/"app-redeem.json"
            write_private(wrapper,{"journalConfigFile":str(startup_configs["litecoin-funder"][2]),"rpcFile":str(redeem_rpc),"schema":"monzero-private-ltc-redeem-v1"})
            app_request["bobSettlementConfig"]=str(wrapper)
            return app_request

        if args.helper_start:
            invoke("prepare_start")
            startup_reviews = []
            startup_configs = {}
            for payout in (False, True):
                helper, config, config_path = install_journal(payout=payout,
                    label="start-bob" if payout else "start-alice")
                config["schema"] = "monzero-private-ltc-journal-v5"
                config["startQuoteFile"] = str(root / "start-quote.json")
                config_path.unlink()
                write_private(config_path, json.loads(json.dumps(config, sort_keys=True)))

                def startup(submit=False, commitment=None, reject=False):
                    operation = "submit" if submit else "review"
                    payload = {"schema":"monzero-ltc-start-"+operation+"-request-v1", "requestId":str(uuid.uuid4())}
                    if commitment is not None:
                        payload["commitment"] = commitment
                    completed = subprocess.run([str(helper), "--start-"+operation+"-stdio-v1",
                        "--config-file", str(config_path)], input=json.dumps(payload).encode(),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
                    if reject:
                        require(completed.returncode != 0 and not completed.stdout,
                                "Invalid or repeated trade start was accepted")
                        return
                    require(completed.returncode == 0 and len(completed.stdout) <= 8192,
                            "Startup helper failed: "+completed.stderr.decode(errors="replace")[-1024:])
                    response = json.loads(completed.stdout)
                    require(response["schema"] == "monzero-ltc-start-"+operation+"-response-v1"
                        and response["requestId"] == payload["requestId"], "Unexpected startup response")
                    return response

                meta, snapshots = pathlib.Path(config["metadataDirectory"]), pathlib.Path(config["snapshotDirectory"])
                review = startup()["review"]
                require(review["details"]["terms"] == terms and review["details"]["policy"] == policy
                    and review["details"]["role"] == config["role"]
                    and review["details"]["chainObserved"] is False and review["details"]["executable"] is False,
                    "Startup helper changed role, signed terms or policy")
                require(not list(meta.iterdir()) and not list(snapshots.iterdir()), "Review wrote trade state")
                startup(True, "0"*64, reject=True)
                # Native configuration is trusted context but still part of the
                # approval boundary: a changed policy/role cannot reuse consent.
                for field, value in (("role", "native-funder" if payout else "litecoin-funder"),
                        ("policy", dict(policy, maximum_xmz_atoms=policy["maximum_xmz_atoms"]+1)),
                        ("tradeId", str(uuid.uuid4())), ("pins", list(reversed(config["pins"])))):
                    changed = dict(config)
                    changed[field] = value
                    config_path.unlink()
                    write_private(config_path, json.loads(json.dumps(changed, sort_keys=True)))
                    startup(True, review["commitment"], reject=True)
                    config_path.unlink()
                    write_private(config_path, json.loads(json.dumps(config, sort_keys=True)))
                if startup_reviews:
                    startup(True, startup_reviews[0]["commitment"], reject=True)
                require(not list(meta.iterdir()) and not list(snapshots.iterdir()), "Rejected consent wrote trade state")
                if args.gui_peer_startup:
                    startup_reviews.append(review)
                    startup_configs[config["role"]] = (helper, config, config_path)
                    continue  # The native startup owner must observe creation, not just resume it.
                if args.gui_start:
                    gui_result = root / ("start-bob-qt.json" if payout else "start-alice-qt.json")
                    gui = subprocess.run([str(args.gui_test_binary), "actualLitecoinStartup"],
                        env=dict(os.environ, MONZERO_LTC_START_FIXTURE=str(config_path),
                            MONZERO_LTC_START_RESULT=str(gui_result), QT_QPA_PLATFORM="offscreen"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=780)
                    require(gui.returncode == 0 and gui_result.is_file(),
                            "GUI startup failed: "+gui.stdout.decode(errors="replace")[-4096:])
                    started = json.loads(gui_result.read_text())
                else:
                    started = startup(True, review["commitment"])
                require(started["review"] == review and started["snapshot"]["tradeId"] == terms["swapId"]
                    and started["snapshot"]["role"] == config["role"]
                    and started["snapshot"]["keysReady"] is False
                    and started["snapshot"]["nativeFunding"]["prepared"] is False,
                    "Startup did not retain the approved unfunded trade")
                retained = {str(path):path.read_bytes() for directory in (meta,snapshots) for path in directory.iterdir() if path.is_file()}
                startup(True, review["commitment"], reject=True)
                require(retained == {str(path):path.read_bytes() for directory in (meta,snapshots) for path in directory.iterdir() if path.is_file()},
                        "Repeated startup changed retained keys or journal")
                startup_reviews.append(review)
                startup_configs[config["role"]] = (helper, config, config_path)
            require(startup_reviews[0]["commitment"] != startup_reviews[1]["commitment"], "Startup consent was not role bound")
            if args.gui_negotiate_session or args.gui_peer_startup:
                session_request = {}
                for label, role in (("alice", "native-funder"), ("bob", "litecoin-funder")):
                    identity, password, _ = install_identity(root, role)
                    session_request[label+"Config"] = str(startup_configs[role][2])
                    session_request[label+"Identity"] = str(identity)
                    session_request[label+"Passphrase"] = password.decode()
                if args.gui_app_setup:
                    shared_app_request=prepare_app_setup()
                fixture, output = root / "session.json", root / "session-result.json"
                startup_request=dict(shared_app_request,initialStartup=True) if args.gui_app_setup else session_request
                if args.gui_resident_funding:
                    resident_directory=root/"resident-app";resident_directory.mkdir(mode=0o700)
                    startup_request["residentDirectory"]=str(resident_directory)
                write_private(fixture,startup_request)
                slot = "actualLitecoinPeerStartup" if args.gui_peer_startup else "actualLitecoinNegotiationSession"
                fixture_variable = "MONZERO_LTC_PEER_STARTUP_FIXTURE" if args.gui_peer_startup else "MONZERO_LTC_SESSION_FIXTURE"
                result_variable = "MONZERO_LTC_PEER_STARTUP_RESULT" if args.gui_peer_startup else "MONZERO_LTC_SESSION_RESULT"
                if args.gui_app_setup:
                    slot,fixture_variable,result_variable="actualLitecoinAppSetup","MONZERO_LTC_APP_FIXTURE","MONZERO_LTC_APP_RESULT"
                pools=(ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
                if args.gui_resident_funding:
                    resident=LitecoinAppFixture(args.gui_test_binary,fixture,resident_directory,processes, args.light_wallet_tls/"root.pem" if args.gui_light_recovery_helper else None)
                    session_result=resident.start()
                else:
                    session = subprocess.run([str(args.gui_test_binary), slot],
                        env=dict(os.environ, **{fixture_variable:str(fixture), result_variable:str(output)}, QT_QPA_PLATFORM="offscreen"),
                        capture_output=True, timeout=900)
                    require(session.returncode == 0 and output.is_file(),
                        "Private negotiation startup failed: "+session.stdout.decode(errors="replace")[-4096:]+session.stderr.decode(errors="replace")[-1024:])
                    session_result = json.loads(output.read_text())
                expected = {"schema":"monzero-ltc-peer-startup-fixture-v1", "deferredUntilSaved":True,
                    "startedAfterApproval":True,"changedCredentialsRejected":True,"restartResumeVerified":True,"executable":False} if args.gui_peer_startup else {
                    "schema":"monzero-ltc-session-fixture-v1", "mutualTlsVerified":True,
                    "wrongPinRejected":True,"restartRedeliveryVerified":True,"redemptionSessionVerified":False,"executable":False}
                if args.gui_app_setup:
                    expected={"schema":"monzero-ltc-app-startup-fixture-v1","deferredUntilSaved":True,
                        "startedAfterApproval":True,"prematurePaymentsRejected":True,"restartVerified":True,"executable":False}
                if args.gui_resident_funding:
                    expected={"schema":"monzero-ltc-resident-startup-fixture-v1","startedAfterApproval":True,"executable":False}
                require(session_result == expected, "Incomplete private session evidence")
                require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])==pools,"Startup sent funds")
                if args.gui_app_setup:
                    app_sources=list(p for p in root.glob("app-*") if p.is_file())
                    app_sources += [p for p in (root/"app-setup").iterdir() if p.name.startswith(("alice","bob")) or p.name=="ca.pem"]
                    shared_app_bytes={p:p.read_bytes() for p in app_sources}
                negotiated = invoke("begin_negotiated")
            elif args.helper_negotiate:
                def retained_state():
                    return {str(path):path.read_bytes()
                        for _, config, _ in startup_configs.values()
                        for field in ("metadataDirectory", "snapshotDirectory")
                        for path in pathlib.Path(config[field]).iterdir() if path.is_file()}

                def accept_peer(role, frame, reject=False, **changes):
                    helper, config, config_path = startup_configs[role]
                    payload = {"schema":"monzero-ltc-peer-accept-request-v1",
                        "requestId":str(uuid.uuid4()), "frameHex":frame["frameHex"],
                        "signatureHex":frame["signatureHex"]}
                    payload.update(changes)
                    before = retained_state()
                    completed = subprocess.run([str(helper), "--peer-accept-stdio-v1",
                        "--config-file", str(config_path)], input=json.dumps(payload).encode(),
                        capture_output=True, timeout=180)
                    if reject:
                        require(completed.returncode != 0 and not completed.stdout,
                            "Invalid peer negotiation message was accepted")
                        require(retained_state() == before, "Rejected peer message changed trade state")
                        return
                    require(completed.returncode == 0 and len(completed.stdout) <= 8192,
                        "Peer admission helper failed: "+completed.stderr.decode(errors="replace")[-1024:])
                    response = json.loads(completed.stdout)
                    require(response["schema"] == "monzero-ltc-peer-accept-response-v1"
                        and response["requestId"] == payload["requestId"]
                        and response["snapshot"]["tradeId"] == terms["swapId"]
                        and response["snapshot"]["role"] == role
                        and response["snapshot"]["chainObserved"] is False
                        and response["snapshot"]["executable"] is False,
                        "Peer helper changed trade identity or granted payment authority")
                    return response

                def outgoing(role):
                    helper, config, config_path = startup_configs[role]

                    def command(operation, frame=None, signature=None, reject=False):
                        payload = {"schema":"monzero-ltc-outgoing-"+operation+"-request-v1", "requestId":str(uuid.uuid4())}
                        if frame is not None:
                            payload.update(frameHex=frame, signatureHex=signature)
                        before = retained_state()
                        completed = subprocess.run([str(helper), "--outgoing-"+operation+"-stdio-v1",
                            "--config-file", str(config_path)], input=json.dumps(payload).encode(),
                            capture_output=True, timeout=180)
                        if reject:
                            require(completed.returncode != 0 and not completed.stdout and retained_state() == before,
                                "Rejected outgoing signature changed saved negotiation")
                            return
                        require(completed.returncode == 0 and len(completed.stdout) <= 410000,
                            "Outgoing helper failed: "+completed.stderr.decode(errors="replace")[-1024:])
                        response = json.loads(completed.stdout)
                        require(response["schema"] == "monzero-ltc-outgoing-"+operation+"-response-v1"
                            and response["requestId"] == payload["requestId"]
                            and response["snapshot"]["tradeId"] == terms["swapId"]
                            and response["snapshot"]["role"] == role and response["snapshot"]["executable"] is False,
                            "Outgoing helper response changed trade context")
                        if operation == "review":
                            require(retained_state() == before, "Outgoing review changed trade state")
                        return response

                    review = command("review")
                    require(command("review")["frameHex"] == review["frameHex"], "Outgoing review changed local keys")
                    command("submit", review["frameHex"], "00"*64, reject=True)
                    identity_path, password, public_key = install_identity(root, role)
                    fixture, output = root / (role+"-sign.json"), root / (role+"-signature.hex")
                    write_private(fixture, {"identityFile":str(identity_path), "passphrase":password.decode(),
                        "publicKey":public_key, "frameHex":review["frameHex"]})
                    signed = subprocess.run([str(args.gui_test_binary), "actualLitecoinOutgoingSignature"],
                        env=dict(os.environ, MONZERO_LTC_SIGN_FIXTURE=str(fixture), MONZERO_LTC_SIGN_RESULT=str(output), QT_QPA_PLATFORM="offscreen"),
                        capture_output=True, timeout=180)
                    require(signed.returncode == 0 and output.is_file(), "Qt identity could not sign the local negotiation frame")
                    signature = output.read_text()
                    submitted = command("submit", review["frameHex"], signature)
                    require(submitted["frameHex"] == review["frameHex"] and submitted["signatureHex"] == signature,
                        "Retained outgoing message differs from Qt signature")
                    saved = retained_state()
                    repeated = command("submit", review["frameHex"], signature)
                    require(repeated["snapshot"] == submitted["snapshot"] and retained_state() == saved,
                        "Repeated outgoing submission changed the saved negotiation")
                    require(command("review")["frameHex"] == review["frameHex"], "Saved outgoing review changed bytes")
                    command("submit", review["frameHex"], "00"*64, reject=True)
                    return submitted

                for action, receiver, sender in (("outgoing_bob", "native-funder", "litecoin-funder"),
                        ("outgoing_alice", "litecoin-funder", "native-funder")):
                    frame = outgoing(sender) if args.gui_sign_negotiation else invoke(action)
                    accept_peer(sender, frame, reject=True)
                    for changes in ({"signatureHex":"00"*64}, {"frameHex":"00"},
                            {"schema":"monzero-ltc-start-submit-request-v1"},
                            {"schema":"monzero-ltc-outgoing-submit-request-v1"},
                            {"requestId":str(uuid.UUID(int=0))}, {"action":"send"},
                            {"frameHex":"00"*200001}):
                        accept_peer(receiver, frame, reject=True, **changes)
                    # These messages have valid signatures from the pinned test
                    # peer, but a different trade context or invalid key share.
                    original = json.loads(bytes.fromhex(frame["frameHex"]))
                    for field, value in (("swapId", str(uuid.uuid4())),
                            ("quoteCommitment", "00"*32), ("previous", "ff"*32),
                            ("share", dict(original["share"], contract_key=original["share"]["adaptor_key"]))):
                        changed = dict(original)
                        changed[field] = value
                        encoded = json.dumps(changed, sort_keys=True, separators=(",", ":")).encode()
                        domain = b"monzero-ltc-xmz-key-exchange-v1"
                        signing = b"MONZERO-DEX-OFFER-V1\n" + str(len(domain)).encode() + b":" + domain + str(len(encoded)).encode() + b":" + encoded
                        identity = SigningKey(bytes([1 if sender == "litecoin-funder" else 2])*32)
                        accept_peer(receiver, frame, reject=True, frameHex=encoded.hex(),
                            signatureHex=identity.sign(signing).signature.hex())
                    first = accept_peer(receiver, frame)
                    saved = retained_state()
                    repeated = accept_peer(receiver, frame)
                    require(first["checkpoint"] == repeated["checkpoint"]
                        and first["snapshot"] == repeated["snapshot"] and retained_state() == saved,
                        "Repeated peer delivery changed the persisted negotiation")
                    accept_peer(receiver, frame, reject=True, signatureHex="00"*64)
                negotiated = invoke("begin_negotiated")
            else:
                negotiated = invoke("begin_prepared")
        else:
            negotiated = invoke("begin")
        require(negotiated["startApprovalBound"] is True, "Trade setup did not use role-bound start approval")
        if args.gui_recovery_session:
            session_input = dict(session_request)
            if args.gui_reserve_litecoin:
                rpc_path, reservation_path = root/"reservation-rpc.json", root/"reservation.json"
                write_private(rpc_path,{"schema":"monzero-private-ltc-reservation-rpc-v1","ltcPort":ltc.port,
                    "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc"/"regtest"/".cookie"),
                    "reservationDirectory":str(root/"reservations"),"feeRateAtomsPerKb":1000})
                write_private(reservation_path,{"journalConfigFile":str(startup_configs["litecoin-funder"][2]),
                    "rpcFile":str(rpc_path),"schema":"monzero-private-ltc-reservation-v1"})
                session_input["reservationConfig"] = str(reservation_path)
            else:
                reserved = invoke("ltc_reserve")
                session_input["recoveryPsbt"] = reserved["psbt"]
            fixture, output = root/"recovery-session.json", root/"recovery-session-result.json"
            write_private(fixture,session_input)
            if args.gui_light_recovery_helper:
                from litecoin_app_light_fixture import attach_light_wallet
                attach_light_wallet(args,root,resident,light_setup)
            pools = (ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            if resident:
                if args.gui_light_recovery_helper:
                    resident.call("bob","light-prepare",feeAtomic="1000")
                else:
                    resident.call("bob","reserve")
                resident.call("bob","wait-recovery")
            else:
                session = subprocess.run([str(args.gui_test_binary),"actualLitecoinNegotiationSession"],
                    env=dict(os.environ,MONZERO_LTC_SESSION_FIXTURE=str(fixture),MONZERO_LTC_SESSION_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,timeout=900)
                require(session.returncode == 0 and output.is_file(),"Private recovery session failed: "+session.stdout.decode(errors="replace")[-6000:]+session.stderr.decode(errors="replace")[-1024:])
            if args.gui_light_recovery_helper:
                state=resident.call("bob","light-review")["lightFunding"]
                require(state["state"]=="complete" and state["reviewOperation"]=="review"
                    and state["review"], "Actual peer recovery did not authorize local signature review")
                require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"]) == pools,
                    "Local wallet recovery or signature review broadcast a payment")
                resident.call("bob","reserve",accepted=False)
                resident.call("bob","lock-review",accepted=False)
                resident.call("bob","light-reopen")
                resident.call("bob","wait-recovery")
                require(resident.last["bob"]["lightFunding"]["retainedReview"] == state["retainedReview"],
                    "Reopening local funding changed the saved reservation")
                require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"]) == pools,
                    "Reopening local funding broadcast a payment")
                if args.gui_light_settlement:
                    from litecoin_app_light_fixture import submit_light_funding
                    restart=(lambda:resident.crash_resume(args.gui_light_recovery_helper,light_setup['helperSha256'],light_setup['launcher'])) if args.gui_light_restart else None
                    inflight=(light_peer,lambda commitment:resident.interrupt_relay(args.gui_light_recovery_helper,light_setup['helperSha256'],light_setup['launcher'],light_peer,commitment)) if args.gui_light_inflight else None
                    lock=submit_light_funding(resident,ltc,anchor,restart,args.gui_light_restart_stage,light_peer if args.gui_light_lost_ack else None,inflight)
                else:
                    resident.stop()
                    write_private(args.report,dict(schema="monzero-ltc-app-light-recovery-results-v1",
                        localWalletPeerRecoveryVerified=True, signatureReviewVerified=True, reopenVerified=True,
                        coreFundingInterlockVerified=True, noBroadcastVerified=True,
                        fullCrossChainSettlement=False, executable=False, **light_setup))
                    return
            if args.gui_reserve_litecoin and not args.gui_light_recovery_helper:
                # Corrupt or conflicting saved intent must never select replacement inputs.
                intent = root/"reservations"/(terms["swapId"]+".intent")
                original = intent.read_bytes()
                saved_files = {p.name:p.read_bytes() for p in (root/"reservations").iterdir() if p.is_file()}
                locked = ltc.rpc("listlockunspent",wallet="fixture")
                for bad in (b"{",json.dumps(dict(json.loads(original),amount=1)).encode()):
                    intent.write_bytes(bad)
                    try:
                        rejected = subprocess.run([str(startup_configs["litecoin-funder"][0]),"--reservation-prepare-stdio-v1","--config-file",str(reservation_path)],
                            input=json.dumps({"schema":"monzero-ltc-reservation-prepare-request-v1","requestId":str(uuid.uuid4())}).encode(),
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
                        require(rejected.returncode != 0 and not rejected.stdout,"Changed reservation intent accepted")
                        require(ltc.rpc("listlockunspent",wallet="fixture") == locked,"Rejected reservation changed wallet coin locks")
                    finally:
                        intent.write_bytes(original)
                    require({p.name:p.read_bytes() for p in (root/"reservations").iterdir() if p.is_file()} == saved_files,
                        "Rejected reservation selected or retained replacement inputs")
            if not args.gui_light_settlement:
                require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"]) == pools,
                    "Private recovery session broadcast a payment")
        if args.gui_recovery_signing:
            reserved = invoke("ltc_reserve")
            def recovery_request(role, operation, fields, reject=False):
                helper, _, config_path = startup_configs[role]
                request_id = str(uuid.uuid4())
                payload = dict(schema="monzero-ltc-recovery-"+operation+"-request-v1",requestId=request_id,**fields)
                response = subprocess.run([str(helper),"--recovery-"+operation+"-stdio-v1","--config-file",str(config_path)],
                    input=json.dumps(payload).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=300)
                if reject:
                    require(response.returncode != 0 and not response.stdout,"Unsafe recovery request accepted")
                    return
                require(response.returncode == 0 and not response.stderr,"Recovery helper failed: "+response.stderr.decode(errors="replace")[-1024:])
                result = json.loads(response.stdout)
                require(result["schema"] == "monzero-ltc-recovery-"+operation+"-response-v1"
                    and result["requestId"] == request_id and result["snapshot"]["tradeId"] == terms["swapId"]
                    and result["snapshot"]["role"] == role and result["snapshot"]["executable"] is False,
                    "Recovery helper changed operation/trade context")
                return result
            def recovery_state():
                return {str(p):p.read_bytes() for folder in ("alice-meta","alice-secrets","bob-meta","bob-secrets")
                    for p in (root/folder).iterdir() if p.is_file()}
            signed_recovery = {}
            ltc_pool_before = ltc.rpc("getrawmempool")
            for label, role, public_key in (("bob","litecoin-funder",terms["ltcFunder"]),("alice","native-funder",terms["xmzFunder"])):
                before = recovery_state()
                recovery_request(role,"review",{"psbt":reserved["psbt"],"commitment":"00"*32},reject=True)
                recovery_request(role,"submit",{"frameHex":"00","signatureHex":"00"*64},reject=True)
                require(recovery_state() == before,"Invalid recovery request changed unprepared trade")
                review = recovery_request(role,"review",{"psbt":reserved["psbt"]})
                before = recovery_state()
                require(recovery_request(role,"review",{"psbt":reserved["psbt"]})["frameHex"] == review["frameHex"],
                    "Repeated recovery preparation changed contract signatures")
                recovery_request(role,"submit",{"frameHex":review["frameHex"],"signatureHex":"00"*64},reject=True)
                require(recovery_state() == before,"Rejected recovery identity changed retained state")
                fixture, output = root/(label+"-recovery-sign.json"), root/(label+"-recovery-signature.hex")
                write_private(fixture,{"identityFile":session_request[label+"Identity"],"passphrase":session_request[label+"Passphrase"],
                    "publicKey":public_key,"frameHex":review["frameHex"],"protocol":"recovery"})
                signed = subprocess.run([str(args.gui_test_binary),"actualLitecoinOutgoingSignature"],
                    env=dict(os.environ,MONZERO_LTC_SIGN_FIXTURE=str(fixture),MONZERO_LTC_SIGN_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,timeout=180)
                require(signed.returncode == 0 and output.is_file(),"Qt identity recovery signing failed")
                message = {"frameHex":review["frameHex"],"signatureHex":output.read_text()}
                saved = recovery_request(role,"submit",message)
                require(saved["frameHex"]==message["frameHex"] and saved["signatureHex"]==message["signatureHex"],
                    "Recovery helper replaced externally signed message")
                before = recovery_state()
                recovery_request(role,"submit",message)
                require(recovery_state() == before,"Recovery retry changed retained message")
                signed_recovery[role] = message
            for role, peer in (("native-funder","litecoin-funder"),("litecoin-funder","native-funder")):
                before = recovery_state()
                recovery_request(role,"accept",signed_recovery[role],reject=True)
                bad = dict(signed_recovery[peer],signatureHex="00"*64)
                recovery_request(role,"accept",bad,reject=True)
                require(recovery_state() == before,"Rejected recovery peer changed trade")
                recovery_request(role,"accept",signed_recovery[peer])
                before = recovery_state()
                recovery_request(role,"accept",signed_recovery[peer])
                require(recovery_state() == before,"Repeated peer recovery changed saved agreement")
            require(ltc.rpc("getrawmempool") == ltc_pool_before and rpc(daemon_port,"get_info")["tx_pool_size"] == 0,
                "Recovery agreement broadcast a payment")
        if args.gui_light_settlement:
            pass  # Already approved and sent through the local wallet controller.
        elif args.gui_submit_ltc_lock:
            require(args.gui_reserve_litecoin, "Qt Litecoin submission requires Qt reservation")
            def lock_helper(mode, extra=None, reject=False):
                request = dict(schema="monzero-ltc-lock-funding-"+mode+"-request-v1", requestId=str(uuid.uuid4()))
                request.update(extra or {})
                result = subprocess.run([str(startup_configs["litecoin-funder"][0]),"--lock-funding-"+mode+"-stdio-v1","--config-file",str(reservation_path)],
                    input=json.dumps(request,separators=(",",":")),text=True,capture_output=True,timeout=180)
                if reject:
                    require(result.returncode != 0,"Invalid lock approval accepted")
                    return None
                require(result.returncode == 0,"Lock helper failed: "+result.stderr)
                return json.loads(result.stdout)
            initial = lock_helper("review")["review"]
            before_pool = ltc.rpc("getrawmempool")
            before_records = {p.name:p.read_bytes() for p in (root/"reservations").iterdir() if p.is_file()}
            lock_helper("submit",{"commitment":"00"*32},reject=True)
            require(ltc.rpc("getrawmempool") == before_pool,"Rejected lock approval sent a payment")
            require({p.name:p.read_bytes() for p in (root/"reservations").iterdir() if p.is_file()} == before_records,
                "Rejected lock approval changed reservation or signed transaction")
            for retry in (False,True):
                output = root/("lock-retry.json" if retry else "lock-send.json")
                if resident:
                    state=resident.call("bob","lock-review")["reservation"]
                    review=state["fundingReview"]
                    require(review["details"]["action"]==("retransmit" if retry else "send"),"Wrong resident lock approval action")
                    resident.call("bob","lock-submit",accepted=False,commitment="0"*64)
                    sent=resident.call("bob","lock-submit",commitment=review["commitment"])["reservation"]
                    result={"outcome":sent["fundingSubmission"]}
                else:
                    gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinLockFunding"],
                        env=dict(os.environ,MONZERO_LTC_LOCK_CONFIG=str(reservation_path),
                            MONZERO_LTC_LOCK_JOURNAL=str(startup_configs["litecoin-funder"][2]),
                            MONZERO_LTC_LOCK_RESULT=str(output),MONZERO_LTC_LOCK_RETRY="1" if retry else "0",QT_QPA_PLATFORM="offscreen"),
                        capture_output=True,text=True,timeout=240)
                    require(gui.returncode == 0 and output.is_file(),"Qt Litecoin funding failed: "+gui.stdout+gui.stderr)
                    result = json.loads(output.read_text())
                require(result["outcome"]["txid"] == initial["details"]["txid"],"Retry changed Litecoin transaction")
                lock_helper("submit",{"commitment":initial["commitment"]},reject=True)
                require(ltc.rpc("getrawmempool") == [initial["details"]["txid"]],"Funding retry created another transaction")
            lock = {"txid":initial["details"]["txid"]}
        else:
            lock = invoke("ltc_lock_recovered" if args.gui_recovery_signing or args.gui_recovery_session else "ltc_lock")
        require(args.gui_light_settlement or lock["txid"] in ltc.rpc("getrawmempool"), "Litecoin funding absent")
        # Local-wallet observation already mined the first confirmation.
        ltc.mine(1 if args.gui_light_settlement else 2, anchor)
        native_source_before = wallet("get_balance")["balance"]
        if args.gui_light_settlement:
            decoded=ltc.rpc("getrawtransaction",lock["txid"],True)
            inputs=sum(atoms(ltc.rpc("getrawtransaction",v["txid"],True)["vout"][v["vout"]]["value"]) for v in decoded["vin"])
            ltc_funding_fee=inputs-sum(atoms(v["value"]) for v in decoded["vout"])
        else:
            funding_info = ltc.rpc("gettransaction", lock["txid"], wallet="fixture")
            ltc_funding_fee = atoms(-funding_info["fee"])
        require(0 < ltc_funding_fee <= int(terms["ltcFundingFeeMaxAtoms"]), "Litecoin funding fee exceeds signed ceiling")
        native_lock = None if args.gui_prepare_funding else invoke("native_prepare" if args.helper_submit_funding else "native_lock")
        if args.helper_submit_funding:
            helper, config, config_path = install_journal(label="funding-submit")
            rpc_path = root / "funding-rpc.json"
            write_private(rpc_path, {"schema":"monzero-private-ltc-funding-rpc-v1",
                "ltcPort":ltc.port,"ltcWallet":"fixture","ltcAnchor":anchor,
                "ltcCookieFile":str(root / "ltc" / "regtest" / ".cookie"),
                "nativeDaemonPort":daemon_port,"nativeWalletPort":source_port,
                "nativeLoginFile":str(root / f"monzero-wallet-rpc.{source_port}.login")})
            config.update(schema="monzero-private-ltc-journal-v4",fundingRpcFile=str(rpc_path))
            config_path.unlink()  # Disposable fixture config, before launching its helper.
            write_private(config_path, json.loads(json.dumps(config,sort_keys=True)))
            funding_exchange_config=config_path

            def funding_request(mode, fields, reject=False):
                request_id = str(uuid.uuid4())
                response = subprocess.run([str(helper),"--"+mode+"-stdio-v1","--config-file",str(config_path)],
                    input=json.dumps(dict(schema="monzero-ltc-"+mode+"-request-v1",requestId=request_id,**fields)).encode(),
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=300)
                if reject:
                    require(response.returncode != 0 and not response.stdout, "Unsafe funding request was accepted")
                    return
                if response.returncode != 0:
                    write_private(args.report.with_name(args.report.name+".funding-helper-failure.json"), {
                        "mode":mode,"exitCode":response.returncode,"stderr":response.stderr.decode(errors="replace")[:32768]})
                require(response.returncode == 0 and not response.stderr, "Funding helper operation failed: "+mode)
                result = json.loads(response.stdout)
                require(result["schema"] == "monzero-ltc-"+mode+"-response-v1" and result["requestId"] == request_id,
                    "Funding helper response context differs")
                return result

            if args.gui_prepare_funding:
                rpc_config = json.loads(rpc_path.read_text())
                rpc_config.update(schema="monzero-private-ltc-funding-rpc-v2",
                    source={"wallet":addresses["source"],"account":0,"priority":1})
                def replace_rpc(value):
                    rpc_path.unlink()
                    write_private(rpc_path, value)
                def retained_files():
                    return {str(p):p.read_bytes() for folder in (root / "alice-meta", root / "alice-secrets")
                        for p in folder.rglob("*") if p.is_file()}
                for bad_source in ({"wallet":addresses["payout"],"account":0,"priority":1},
                        {"wallet":addresses["source"],"account":999,"priority":1}):
                    replace_rpc(dict(rpc_config, source=bad_source))
                    before = retained_files()
                    funding_request("funding-prepare", {"safetyBlocks":2}, reject=True)
                    require(retained_files() == before, "Invalid funding source changed retained trade")
                replace_rpc(rpc_config)
                tip = ltc.rpc("getbestblockhash")
                ltc.rpc("invalidateblock", tip)
                before = retained_files()
                funding_request("funding-prepare", {"safetyBlocks":2}, reject=True)
                require(retained_files() == before, "Shallow LTC lock changed retained trade")
                ltc.rpc("reconsiderblock", tip)
                output = root / "gui-funding-preparation-result.json"
                if resident:
                    resident.call("alice","funding-refresh")
                    state=resident.call("alice","funding-prepare")["funding"]
                    review=state["fundingReview"]
                    prepared={"review":{key:review[key] for key in ("details","commitment")}}
                else:
                    gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinFundingPreparation"],
                        env=dict(os.environ,MONZERO_LTC_FUNDING_PREPARE_FIXTURE=str(config_path),
                            MONZERO_LTC_FUNDING_PREPARE_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
                    require(gui.returncode == 0, "Qt funding preparation failed: " + gui.stdout.decode(errors="replace")[-12000:])
                    prepared = json.loads(output.read_text())
                repeated = funding_request("funding-prepare", {"safetyBlocks":2})
                require(repeated["review"] == prepared["review"], "Preparation retry replaced saved transaction")
                require(not repeated["snapshot"]["nativeFunding"]["attempted"], "Preparation attempted broadcast")
                require(rpc(daemon_port,"get_info")["tx_pool_size"] == 0
                    and wallet("get_balance")["balance"] == native_source_before, "Preparation spent source funds")
                native_lock = {"txid":prepared["review"]["details"]["txid"],"review":prepared["review"]["details"]}

            approval = funding_request("funding-review", {"action":"send","safetyBlocks":2})["review"]
            require(approval["details"] == native_lock["review"], "Helper changed prepared funding review")
            consent = {"action":"send","safetyBlocks":2,"commitment":approval["commitment"]}
            funding_request("funding-submit", dict(consent,commitment=("0" if consent["commitment"][0]!="0" else "1")+consent["commitment"][1:]), reject=True)
            funding_request("funding-submit", dict(consent,safetyBlocks=3), reject=True)
            unchanged = funding_request("funding-review", {"action":"send","safetyBlocks":2})
            require(unchanged["review"] == approval and not unchanged["snapshot"]["nativeFunding"]["attempted"],
                "Rejected funding approval changed retained dispatch state")
            for action in ("send","retransmit"):
                if action == "retransmit":
                    funding_request("funding-submit", consent, reject=True)
                    retry = funding_request("funding-review", {"action":"retransmit","safetyBlocks":2})["review"]
                    consent = {"action":"retransmit","safetyBlocks":2,"commitment":retry["commitment"]}
                if args.gui_submit_funding:
                    if resident:
                        state=resident.call("alice","funding-review",retry=action=="retransmit")["funding"]
                        require(state["fundingReview"]["commitment"]==consent["commitment"],"Resident funding approval differs")
                        resident.call("alice","funding-submit",accepted=False,commitment="0"*64)
                        sent=resident.call("alice","funding-submit",commitment=consent["commitment"])["funding"]
                        require(sent["snapshot"]["nativeFunding"]["acknowledged"],"Resident funding not acknowledged")
                    else:
                        gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinFundingSubmission"],
                            env=dict(os.environ, MONZERO_LTC_FUNDING_SUBMIT_FIXTURE=str(config_path),
                                MONZERO_LTC_FUNDING_RETRY="1" if action=="retransmit" else "0", QT_QPA_PLATFORM="offscreen"),
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=780)
                        if gui.returncode != 0:
                            write_private(args.report.with_name(args.report.name+".funding-submit-gui-failure.json"), {
                                "action":action,"exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                                "stderr":gui.stderr.decode(errors="replace")[:32768]})
                        require(gui.returncode == 0, "GUI funding submission failed")
                    checked = funding_request("funding-review", {"action":"retransmit","safetyBlocks":2})
                    require(checked["snapshot"]["nativeFunding"]["acknowledged"]
                        and checked["review"]["details"]["txid"] == native_lock["txid"]
                        and checked["review"]["details"]["feeAtoms"] == native_lock["review"]["feeAtoms"],
                        "GUI funding submission changed retained payment")
                else:
                    sent = funding_request("funding-submit", consent)
                    require(sent["outcome"] == {"state":"acknowledged","txid":native_lock["txid"],
                        "feeAtoms":native_lock["review"]["feeAtoms"],"chainObserved":False,"executable":False}
                        and sent["snapshot"]["nativeFunding"]["acknowledged"], "Funding helper changed retained transaction")
        def gui_check_funding(reject=False):
            if not args.gui_check_funding:
                return
            if resident:
                resident.call("alice","funding-refresh")
                state=resident.call("alice","funding-check")["funding"]
                if reject:
                    require(state["fundingCheckState"]!="confirmed" and not state["fundingConfirmation"],"Resident retained rejected funding confirmation")
                else:
                    require(state["fundingCheckState"]=="confirmed" and state["fundingConfirmation"]["txid"]==native_lock["txid"],"Resident funding not confirmed")
                return
            gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinFundingConfirmation"],
                env=dict(os.environ, MONZERO_LTC_FUNDING_CONFIRM_FIXTURE=str(config_path),
                    MONZERO_LTC_FUNDING_CONFIRM_EXPECT="reject" if reject else "confirm", QT_QPA_PLATFORM="offscreen"),
                stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=540)
            if gui.returncode != 0:
                write_private(args.report.with_name(args.report.name+".funding-confirm-gui-failure.json"), {
                    "expectRejection":reject,"exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                    "stderr":gui.stderr.decode(errors="replace")[:32768]})
            require(gui.returncode == 0, "GUI funding confirmation check failed")
        if args.helper_check_funding:
            funding_request("funding-check", {"safetyBlocks":2}, reject=True)
            gui_check_funding(reject=True)
        rpc(daemon_port, "generateblocks", {"wallet_address":addresses["mining"],"amount_of_blocks":10})
        wallet("refresh")
        if args.helper_check_funding:
            def check_confirmed_funding():
                observed = funding_request("funding-check", {"safetyBlocks":2})
                confirmation = observed["confirmation"]
                require(set(confirmation) == {"txid","confirmations","chainObserved","executable"}
                    and confirmation["txid"] == native_lock["txid"] and int(confirmation["confirmations"]) >= 10
                    and confirmation["chainObserved"] is True and confirmation["executable"] is False,
                    "Funding helper claimed invalid native confirmation")
                require(observed["review"]["details"]["txid"] == native_lock["txid"], "Confirmation review changed funding")
                gui_check_funding()
            check_confirmed_funding()
            balance_before_checks = wallet("get_balance")["balance"]
            # One owned empty block is removed: native depth falls below the
            # signed ten-block floor, while the funding transaction remains mined.
            import requests
            height_before = rpc(daemon_port, "get_info")["height"]
            with requests.Session() as client:
                client.trust_env = False
                popped = client.post(f"http://127.0.0.1:{daemon_port}/pop_blocks",json={"nblocks":1},timeout=15,allow_redirects=False)
                require(popped.status_code == 200 and popped.json().get("status") == "OK", "Owned native block removal failed")
            require(rpc(daemon_port, "get_info")["height"] == height_before-1, "Native block removal did not change depth")
            wallet("refresh")
            funding_request("funding-check", {"safetyBlocks":2}, reject=True)
            gui_check_funding(reject=True)
            rpc(daemon_port, "generateblocks", {"wallet_address":addresses["mining"],"amount_of_blocks":1})
            wallet("refresh")
            check_confirmed_funding()
            # Opening another owned source account cannot reuse a past result.
            wallet("close_wallet")
            wallet("open_wallet", {"filename":"mining","password":""})
            funding_request("funding-check", {"safetyBlocks":2}, reject=True)
            gui_check_funding(reject=True)
            wallet("close_wallet")
            wallet("open_wallet", {"filename":"source","password":""})
            wallet("refresh")
            check_confirmed_funding()
            require(wallet("get_balance")["balance"] == balance_before_checks, "Funding confirmation check changed source balance")
        if resident and not args.gui_resident_settlement:
            resident.stop()
        continuous=resident if args.gui_resident_settlement else None
        native_funding = wallet("get_transfer_by_txid", {"txid":native_lock["txid"]})["transfer"]
        native_funding_fee = native_funding["fee"]
        require(native_funding["type"] == "out" and native_funding["amount"] == 200000000000
            and 0 < native_funding_fee <= int(terms["xmzNativeFeeMaxAtoms"]), "Native funding amount or fee differs")
        require(native_source_before - wallet("get_balance")["balance"] == 200000000000 + native_funding_fee,
                "Native source balance does not account for funding and fee")
        funding_review = native_lock["review"]
        require(funding_review["action"] == "send" and funding_review["wallet"] == addresses["source"]
            and funding_review["destination"] == negotiated["nativeAddress"] and funding_review["amountAtoms"] == terms["xmzAtoms"]
            and funding_review["txid"] == native_lock["txid"] and funding_review["feeAtoms"] == str(native_funding_fee)
            and funding_review["maximumFeeAtoms"] == terms["xmzNativeFeeMaxAtoms"]
            and funding_review["totalDebitAtoms"] == str(200000000000 + native_funding_fee)
            and funding_review["safetyBlocks"] == "2" and funding_review["expiresAt"] == terms["expiresAt"]
            and funding_review["chainObserved"] is False, "Native funding differs from approved source, amount or safety window")
        funding_helper_checked = False
        funding_gui_checked = False
        if args.journal_helper:
            import hashlib
            funding_helper,_,funding_config_path = install_journal(label="funding-journal")
            request_id = str(uuid.uuid4())
            response = subprocess.run([str(funding_helper),"--funding-review-stdio-v1","--config-file",str(funding_config_path)],
                input=json.dumps({"schema":"monzero-ltc-funding-review-request-v1","requestId":request_id,
                    "action":"retransmit","safetyBlocks":2}).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=240)
            require(response.returncode == 0 and not response.stderr, "Native funding review helper failed")
            response = json.loads(response.stdout)
            require(response["schema"] == "monzero-ltc-funding-review-response-v1" and response["requestId"] == request_id,
                "Funding review response context differs")
            details = response["review"]["details"]
            require(details["action"] == "retransmit" and details["dispatchCommitment"] is not None
                and details["chainObserved"] is False, "Funding review changed saved authority")
            for key in ("tradeId","quoteCommitment","txid","wallet","account","priority","destination","amountAtoms",
                    "feeAtoms","maximumFeeAtoms","totalDebitAtoms","ltcContract","ltcAtoms","safetyBlocks","expiresAt"):
                require(details[key] == funding_review[key], "Helper changed native funding field: "+key)
            binding = b"MONZERO-LTC-NATIVE-FUNDING-APPROVAL-V1\0" + json.dumps(details,sort_keys=True,separators=(",",":")).encode()
            require(hashlib.sha256(binding).hexdigest() == response["review"]["commitment"], "Funding review commitment differs")
            funding_helper_checked = True
            if args.gui_test_binary:
                env = dict(os.environ, MONZERO_LTC_FUNDING_REVIEW_FIXTURE=str(funding_config_path), QT_QPA_PLATFORM="offscreen")
                gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinFundingReview"],env=env,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=420)
                if gui.returncode != 0:
                    write_private(args.report.with_name(args.report.name+".funding-gui-failure.json"), {
                        "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                        "stderr":gui.stderr.decode(errors="replace")[:32768]})
                require(gui.returncode == 0, "Native GUI rejected actual funding review")
                funding_gui_checked = True
        wallet("refresh", {"start_height":0}, observer=True)
        recovery_counter=0
        def recovery_context(role):
            require(role in ("alice","bob"), "Unknown recovery participant")
            native=role=="alice"
            require(not native or args.gui_app_setup, "Native recovery requires bound shared app configuration")
            owner="native-funder" if native else "litecoin-funder"
            config=pathlib.Path(shared_app_request["aliceSettlementConfig"]) if native else reservation_path
            return owner,config,"native-recovery-" if native else "recovery-"

        def gui_recovery_spend(kind,reject=False,role="bob"):
            owner,config,prefix=recovery_context(role)
            def records():
                return {(folder,p.name):p.read_bytes() for folder in ("alice-meta","alice-secrets","bob-meta","bob-secrets") for p in (root/folder).iterdir() if p.is_file()}
            before=records();pool=ltc.rpc("getrawmempool")
            def submit(retry):
                nonlocal recovery_counter
                recovery_counter+=1
                output=root/("recovery-spend-"+str(recovery_counter)+".json")
                if continuous:
                    state=continuous.call(role,"recovery-review",kind=kind)["reservation"]
                    review=state["recoveryReview"]
                    require(review["details"]["action"]==("retransmit" if retry else "send"),"Wrong continuous recovery approval")
                    continuous.call(role,"recovery-submit",accepted=False,commitment="0"*64)
                    state=continuous.call(role,"recovery-submit",commitment=review["commitment"])["reservation"]
                    require(state["recoveryState"]==("unknown" if reject else "mempool" if retry else "acknowledged"),"Unexpected continuous recovery outcome")
                    require(not state["recoverySubmission"] if reject else state["recoverySubmission"]["txid"]==review["details"]["txid"],"Wrong continuous recovery identity")
                    return {"review":review,"outcome":state["recoverySubmission"]}
                gui=subprocess.run([str(args.gui_test_binary),"actualLitecoinRecoverySpend"],
                    env=dict(os.environ,MONZERO_LTC_RECOVERY_SPEND_CONFIG=str(config),
                        MONZERO_LTC_RECOVERY_SPEND_JOURNAL=str(startup_configs[owner][2]),
                        MONZERO_LTC_RECOVERY_SPEND_KIND=kind,MONZERO_LTC_RECOVERY_SPEND_RETRY="1" if retry else "0",
                        MONZERO_LTC_RECOVERY_SPEND_REJECT="1" if reject else "0",MONZERO_LTC_RECOVERY_SPEND_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,text=True,timeout=300)
                require(gui.returncode==0 and output.is_file(),"Qt Litecoin recovery failed: "+gui.stdout+gui.stderr)
                return json.loads(output.read_text())
            first=submit(False)
            if reject:
                require(records()==before and ltc.rpc("getrawmempool")==pool,"Premature recovery changed dispatch or sent funds")
                return None
            reviewed=first["review"]["details"];txid=reviewed["txid"]
            require(txid in ltc.rpc("getrawmempool"),"Approved recovery was not submitted")
            sent_records=records();sent_pool=ltc.rpc("getrawmempool")
            # Consent for first submission cannot authorize a later retry.
            stale=subprocess.run([str(startup_configs[owner][0]),"--"+prefix+kind+"-submit-stdio-v1","--config-file",str(config)],
                input=json.dumps({"schema":"monzero-ltc-"+prefix+kind+"-submit-request-v1","requestId":str(uuid.uuid4()),
                    "commitment":first["review"]["commitment"]}),text=True,capture_output=True,timeout=180)
            require(stale.returncode!=0,"Stale first-submission consent authorized recovery retry")
            repeated=submit(True)
            require(repeated["outcome"]["state"]=="mempool" and repeated["outcome"]["txid"]==txid,
                "Recovery retry did not reconcile the same signed transaction")
            require(records()==sent_records and ltc.rpc("getrawmempool")==sent_pool,"Recovery retry changed saved bytes or mempool")
            return {"txid":txid}

        def continuous_ltc_check(kind,txid,reject=False,reorg_block=None,role="bob"):
            redeem=kind=="redeem";role="alice" if redeem else role
            section="redemption" if redeem else "reservation"
            status_key="checkState" if redeem else "recoveryCheckState"
            evidence_key="observation" if redeem else "recoveryObservation"
            def check(rejected):
                state=continuous.call(role,"redemption-check" if redeem else "recovery-check",kind=kind)[section]
                evidence=state[evidence_key]
                require(state[status_key]==("unknown" if rejected else "confirmed"),"Wrong continuous Litecoin confirmation state")
                if rejected: require(not evidence,"Stale continuous Litecoin evidence")
                else:
                    require(evidence["txid"]==txid and evidence["block"]==ltc.rpc("getrawtransaction",txid,True)["blockhash"],"Wrong continuous Litecoin confirmation identity")
                return evidence
            evidence=check(reject)
            if reorg_block:
                try:
                    ltc.rpc("invalidateblock",reorg_block)
                    require(ltc.rpc("getblockheader",reorg_block)["confirmations"]==-1,"Continuous reorg failed")
                    check(True)
                finally: ltc.rpc("reconsiderblock",reorg_block)
            return evidence

        def gui_recovery_confirmation(kind,txid,reject=False,reorg_block=None,role="bob"):
            nonlocal recovery_counter
            owner,config,_=recovery_context(role)
            recovery_counter+=1
            output=root/("recovery-confirmation-"+str(recovery_counter)+".json")
            gate=root/("recovery-reorg-"+str(recovery_counter))
            def records():
                return {(folder,p.name):p.read_bytes() for folder in ("alice-meta","alice-secrets","bob-meta","bob-secrets") for p in (root/folder).iterdir() if p.is_file()}
            before=records();pool=ltc.rpc("getrawmempool")
            if continuous:
                continuous_ltc_check(kind,txid,reject,reorg_block,role)
                require(records()==before and ltc.rpc("getrawmempool")==pool,"Continuous confirmation changed saved state or sent funds")
                return
            env=dict(os.environ,MONZERO_LTC_RECOVERY_CHECK_CONFIG=str(config),
                MONZERO_LTC_RECOVERY_CHECK_JOURNAL=str(startup_configs[owner][2]),
                MONZERO_LTC_RECOVERY_CHECK_KIND=kind,MONZERO_LTC_RECOVERY_CHECK_TXID=txid,
                MONZERO_LTC_RECOVERY_CHECK_REJECT="1" if reject else "0",MONZERO_LTC_RECOVERY_CHECK_RESULT=str(output),
                MONZERO_LTC_RECOVERY_CHECK_REORG_GATE=str(gate) if reorg_block else "",QT_QPA_PLATFORM="offscreen")
            gui=subprocess.Popen([str(args.gui_test_binary),"actualLitecoinRecoveryConfirmation"],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            invalidated=False
            try:
                if reorg_block:
                    deadline=time.monotonic()+180
                    while not output.exists() and gui.poll() is None and time.monotonic()<deadline: time.sleep(0.05)
                    require(output.exists(),"Qt confirmation did not reach reorg gate")
                    ltc.rpc("invalidateblock",reorg_block);invalidated=True
                    require(ltc.rpc("getblockheader",reorg_block)["confirmations"]==-1,"Recovery block remained active")
                    write_private(gate,{"reorg":True})
                stdout,stderr=gui.communicate(timeout=300)
                require(gui.returncode==0 and output.is_file(),"Qt recovery confirmation failed: "+stdout+stderr)
                if not reject:
                    observed=json.loads(output.read_text())["recoveryObservation"]
                    require(observed["txid"]==txid,"Wrong observed recovery identity")
                    expected_block=reorg_block if reorg_block else ltc.rpc("getrawtransaction",txid,True)["blockhash"]
                    require(observed["block"]==expected_block,"Wrong observed recovery block")
            finally:
                if gui.poll() is None: gui.kill();gui.communicate()
                if invalidated: ltc.rpc("reconsiderblock",reorg_block)
            require(records()==before and ltc.rpc("getrawmempool")==pool,"Recovery confirmation changed saved state or sent funds")

        def gui_restore_wallet(name):
            helper, config, review_config_path = install_journal(payout=name=="payout",label=name+"-wallet")
            password_path=root/(name+"-wallet-password")
            with os.fdopen(os.open(password_path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),"w") as out:
                out.write(request["password"])
            rpc_path=root/(name+"-wallet-rpc.json")
            observation_port=observer_port if name=="payout" else source_port
            services={"schema":"monzero-private-ltc-"+name+"-rpc-v3","ltcPort":ltc.port,"ltcWallet":"fixture",
                "ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),"nativeDaemonPort":daemon_port,
                "nativeWalletPort":claim_port,"nativeLoginFile":str(root/f"monzero-wallet-rpc.{claim_port}.login"),
                "preparationObserver":{"walletPort":observation_port,"loginFile":str(root/f"monzero-wallet-rpc.{observation_port}.login")},
                "walletPasswordFile":str(password_path),"ltcRedeemBlock" if name=="payout" else "ltcRefundBlock":request["block"]}
            if args.indexed_settlement:
                services["schema"]="monzero-private-ltc-"+name+"-rpc-v4"
                del services["ltcRedeemBlock" if name=="payout" else "ltcRefundBlock"]
            write_private(rpc_path,services)
            config["schema"]="monzero-private-ltc-journal-v3" if name=="payout" else "monzero-private-ltc-journal-v2"
            config[name+"RpcFile"]=str(rpc_path)
            config_path=root/(name+"-wallet-execution.json")
            write_private(config_path,json.loads(json.dumps(config,sort_keys=True)))
            counter=0
            def operate(operation,reject=False):
                nonlocal counter
                counter+=1
                output=root/(name+"-wallet-"+str(counter)+".json")
                if continuous:
                    role="bob" if name=="payout" else "alice"
                    continuous.call(role,"payment-refresh")
                    state=continuous.call(role,"wallet-"+operation,accepted=None if reject else True)["payment"]
                    if reject:
                        require(not state["recoveredWallet"],"Rejected continuous wallet operation retained identity")
                        return None
                    require(state["walletRecoveryState"]=="verified", "Continuous wallet identity not verified")
                    return state["recoveredWallet"]
                completed=subprocess.run([str(args.gui_test_binary),"actualLitecoinRecoveredWallet"],
                    env=dict(os.environ,MONZERO_LTC_RECOVERED_WALLET_CONFIG=str(config_path),
                        MONZERO_LTC_RECOVERED_WALLET_OPERATION=operation,MONZERO_LTC_RECOVERED_WALLET_REJECT="1" if reject else "0",
                        MONZERO_LTC_RECOVERED_WALLET_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,text=True,timeout=420)
                require(completed.returncode==0,"Qt recovered wallet failed: "+completed.stdout+completed.stderr)
                if reject:
                    require(not output.exists(),"Rejected wallet operation returned verified identity")
                    return None
                require(output.is_file(),"Missing recovered wallet result")
                return json.loads(output.read_text())
            pools=(ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            first=operate("restore")
            require(first["address"]==negotiated["nativeAddress"],"Restored wallet differs from negotiation")
            require(first["filename"]=="swap-"+terms["swapId"].replace("-",""),"Restored wallet filename changed")
            # A fresh Qt object verifies the already-open wallet, as after a lost reply.
            checked=operate("check")
            require(checked["address"]==first["address"],"Wallet check changed identity")
            operate("restore",reject=True)
            wallet("close_wallet",claim=True)
            process=wallet_processes[claim_port]
            process.terminate();process.wait(timeout=20)
            process=subprocess.Popen(process.args,cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            processes.append(process);wallet_processes[claim_port]=process
            ready(process,claim_port,"get_version",root/f"monzero-wallet-rpc.{claim_port}.login")
            reopened=operate("open")
            require(reopened["address"]==first["address"] and reopened["filename"]==first["filename"],"Reopen replaced wallet")
            # A different open wallet must remain open, never silently closed/replaced.
            wallet("close_wallet",claim=True)
            wallet("create_wallet",{"filename":"unrelated-wallet","password":"","language":"English"},claim=True)
            unrelated=wallet("get_address",claim=True)["address"]
            operate("check",reject=True)
            require(wallet("get_address",claim=True)["address"]==unrelated,"Rejected check replaced unrelated wallet")
            wallet("close_wallet",claim=True)
            # Address alone is insufficient: a matching watch-only wallet has no spend capability.
            view=wallet("query_key",{"key_type":"view_key"},observer=True)["key"]
            wallet("generate_from_keys",{"filename":"matching-watch-wallet","address":first["address"],"viewkey":view,
                "spendkey":"","restore_height":0,"password":"","autosave_current":True},claim=True)
            operate("check",reject=True)
            require(wallet("get_address",claim=True)["address"]==first["address"],"Rejected watch-wallet check changed identity")
            wallet("close_wallet",claim=True)
            operate("open")
            require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])==pools,"Wallet recovery broadcast a payment")
            return {"nativeAddress":first["address"],"wallet":first["filename"],"helper":helper,"configPath":config_path,"reviewConfigPath":review_config_path}

        def gui_prepare_payment(helper, config_path, name):
            output = root/(name+"-prepared-review.json")
            pools = (ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            if continuous:
                role="bob" if name=="payout" else "alice"
                continuous.call(role,"payment-refresh")
                state=continuous.call(role,name+"-prepare")["payment"]
                reviewed=state[name+"Review"]
                require(reviewed,"Continuous payment was not prepared")
            else:
                gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinPaymentPreparation"],
                    env=dict(os.environ,MONZERO_LTC_PAYMENT_PREPARE_CONFIG=str(config_path),
                        MONZERO_LTC_PAYMENT_PREPARE_ROLE=name,MONZERO_LTC_PAYMENT_PREPARE_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,text=True,timeout=420)
                require(gui.returncode == 0 and output.is_file(),"Qt payment preparation failed: "+gui.stdout+gui.stderr)
                reviewed = json.loads(output.read_text())
            for priority in (1,2):
                request = {"schema":"monzero-ltc-"+name+"-prepare-request-v1","requestId":str(uuid.uuid4()),"priority":priority}
                result = subprocess.run([str(helper),"--"+name+"-prepare-stdio-v1","--config-file",str(config_path)],
                    input=json.dumps(request),text=True,capture_output=True,timeout=240)
                if priority==2:
                    require(result.returncode != 0,"Preparation changed retained priority")
                else:
                    require(result.returncode == 0,"Exact preparation restart failed")
                    repeated=json.loads(result.stdout)["review"]
                    require(repeated["commitment"] == reviewed["commitment"] and repeated["details"] == reviewed["details"],
                        "Preparation restart changed saved payment")
            require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"]) == pools,
                "Payment preparation broadcast a transaction")
            return {"review":reviewed["details"],"txid":reviewed["details"]["txid"]}

        def verify_app_setup(refund_path):
            if continuous: continuous.stop()
            require(all(p.read_bytes()==b for p,b in shared_app_bytes.items()),"Upfront app configuration changed during settlement")
            app_request=dict(shared_app_request,refundPath=refund_path)
            fixture,output=root/"app-fixture.json",root/"app-result.json"
            write_private(fixture,app_request)
            pools=(ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            completed=subprocess.run([str(args.gui_test_binary),"actualLitecoinAppSetup"],
                env=dict(os.environ,QT_QPA_PLATFORM="offscreen",MONZERO_LTC_APP_FIXTURE=str(fixture),MONZERO_LTC_APP_RESULT=str(output)),
                capture_output=True,text=True,timeout=1200)
            require(completed.returncode==0 and output.is_file(),"Shared Qt app setup failed: "+completed.stdout[-20000:]+completed.stderr[-2000:])
            evidence=json.loads(output.read_text())
            require(evidence=={"schema":"monzero-ltc-app-fixture-v1","bothRolesVerified":True,"phaseIsolationVerified":True,
                "changedSetupRejected":True,"restartVerified":True,"executable":False},"Incomplete shared app setup evidence")
            require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])==pools,"Shared app setup checks sent funds")
            require(all(p.read_bytes()==b for p,b in shared_app_bytes.items()),"Resumed app changed upfront configuration")

        if args.cancel_refund:
            cancellation_role="alice" if args.gui_native_cancel else "bob"
            if args.gui_native_cancel:
                continuous.call("alice","recovery-review",accepted=False,kind="refund")
                continuous.call("bob","recovery-review",accepted=False,kind="punish")
            if args.gui_submit_ltc_recovery: gui_recovery_spend("cancel",reject=True,role=cancellation_role)
            else: invoke("cancel", reject=True)
            require(not ltc.rpc("getrawmempool"), "Premature cancellation was broadcast")
            ltc.mine(policy["cancel_blocks"] - policy["ltc_confirmations"], anchor)
            cancellation = gui_recovery_spend("cancel",role=cancellation_role) if args.gui_submit_ltc_recovery else invoke("cancel")
            if args.gui_check_ltc_recovery: gui_recovery_confirmation("cancel",cancellation["txid"],reject=True,role=cancellation_role)
            cancel_block=ltc.mine(1, anchor)[0]
            if args.gui_check_ltc_recovery: gui_recovery_confirmation("cancel",cancellation["txid"],reject=True,role=cancellation_role)
            ltc.mine(1, anchor)
            if args.gui_check_ltc_recovery:
                gui_recovery_confirmation("cancel",cancellation["txid"],reorg_block=cancel_block,role=cancellation_role)
                gui_recovery_confirmation("cancel",cancellation["txid"],role=cancellation_role)
            if args.gui_native_cancel:
                # The shared cancellation may be published by either participant.
                gui_recovery_confirmation("cancel",cancellation["txid"],role="bob",reorg_block=cancel_block)
                gui_recovery_confirmation("cancel",cancellation["txid"],role="bob")
                gui_recovery_spend("punish",reject=True,role="alice")
            if args.gui_native_punish:
                # This branch withholds the cooperative refund. It must not use
                # the completed two-chain-refund report or claim a native payout.
                native_before=rpc(daemon_port,"get_info")
                require(native_before["tx_pool_size"]==0, "Unexpected native pending payment before punishment")
                ltc.mine(policy["punish_blocks"]-policy["ltc_confirmations"],anchor)
                punishment=gui_recovery_spend("punish",role="alice")
                gui_recovery_confirmation("punish",punishment["txid"],reject=True,role="alice")
                punish_block=ltc.mine(1,anchor)[0]
                gui_recovery_confirmation("punish",punishment["txid"],reject=True,role="alice")
                ltc.mine(1,anchor)
                gui_recovery_confirmation("punish",punishment["txid"],reorg_block=punish_block,role="alice")
                gui_recovery_confirmation("punish",punishment["txid"],role="alice")
                transaction=ltc.rpc("getrawtransaction",punishment["txid"],True)
                expected=int(terms["ltcAtoms"])-int(terms["ltcCancelFeeAtoms"])-int(terms["ltcPunishFeeAtoms"])
                require(transaction["confirmations"]>=policy["ltc_confirmations"]
                    and len(transaction["vin"])==1 and len(transaction["vout"])==1,
                    "Punishment transaction shape or confirmation differs")
                require(transaction["vin"][0]["txid"]==cancellation["txid"] and transaction["vin"][0]["vout"]==0
                    and atoms(transaction["vout"][0]["value"])==expected
                    and transaction["vout"][0]["scriptPubKey"]["hex"]==ltc.rpc("validateaddress",ltc_payout)["scriptPubKey"],
                    "Punishment consumed a different output or changed the agreed amount/destination")
                require(ltc.rpc("gettxout",cancellation["txid"],0) is None,
                    "Punishment did not consume the cancellation output")
                require(atoms(ltc.rpc("getreceivedbyaddress",ltc_payout,policy["ltc_confirmations"],wallet="fixture"))==expected,
                    "Punishment recipient balance differs")
                native_after=rpc(daemon_port,"get_info")
                require(native_after["height"]==native_before["height"] and native_after["tx_count"]==native_before["tx_count"]
                    and native_after["tx_pool_size"]==0,"Punishment unexpectedly sent a native payment")
                continuous.stop()
                continuous=None
                # A fresh Qt object reopens the same role-bound configuration.
                gui_recovery_confirmation("punish",punishment["txid"],role="alice")
                require(all(p.read_bytes()==b for p,b in shared_app_bytes.items()),
                    "Punishment recovery changed upfront app configuration")
                write_private(args.report,{"schema":"monzero-ltc-xmz-punishment-results-v1",
                    "nativeCancellationVerified":True,"nativePrematurePunishmentRejected":True,
                    "nativePunishmentVerified":True,"nativePunishmentRetryVerified":True,
                    "nativePunishmentReorgRejected":True,"nativePunishmentRestartVerified":True,
                    "ltcCancelTxid":cancellation["txid"],"ltcPunishTxid":punishment["txid"],
                    "ltcPunishAtoms":str(expected),"ltcCancelFeeAtoms":terms["ltcCancelFeeAtoms"],
                    "ltcPunishFeeAtoms":terms["ltcPunishFeeAtoms"],"nativeFundingTxid":native_lock["txid"],
                    "nativePayoutOrRefundExecuted":False,"fullCrossChainSettlement":False,
                    "recoveryPathsVerified":False,"executable":False})
                return
            refund = gui_recovery_spend("refund") if args.gui_submit_ltc_recovery else invoke("refund")
            if args.gui_check_ltc_recovery: gui_recovery_confirmation("refund",refund["txid"],reject=True)
            request["block"] = ltc.mine(1, anchor)[0]
            if args.gui_check_ltc_recovery: gui_recovery_confirmation("refund",refund["txid"],reject=True)
            ltc.mine(1, anchor)
            if args.gui_check_ltc_recovery:
                gui_recovery_confirmation("refund",refund["txid"],reorg_block=request["block"])
                gui_recovery_confirmation("refund",refund["txid"])
                gui_recovery_confirmation("cancel",cancellation["txid"])
            if args.gui_native_cancel:
                gui_recovery_confirmation("cancel",cancellation["txid"],role="alice")
            transaction = ltc.rpc("getrawtransaction", refund["txid"], True)
            require(transaction["confirmations"] >= 2 and len(transaction["vout"]) == 1,
                    "Unconfirmed Litecoin refund")
            require(atoms(transaction["vout"][0]["value"]) == 99980000
                and transaction["vout"][0]["scriptPubKey"]["hex"] == ltc.rpc("validateaddress", ltc_refund)["scriptPubKey"],
                "Wrong Litecoin refund amount or destination")
            light_refund_evidence={}
            if args.gui_light_settlement:
                from litecoin_app_light_fixture import verify_light_refund
                light_refund_evidence=verify_light_refund(args,ltc,light_setup,refund['txid'])
            restored = gui_restore_wallet("refund") if args.gui_restore_wallet else invoke("refund_restore")
            require(restored["nativeAddress"] == negotiated["nativeAddress"], "Refund wallet differs from negotiated address")
            wallet("refresh", claim=True)
            require(wallet("get_balance", claim=True)["balance"] == 200000000000, "Refund wallet failed to recover exact lock")
            payout = None if args.gui_prepare_payments else invoke("refund_prepare" if args.gui_submit_refund else "refund_payout")
            if args.gui_submit_refund:
                if args.gui_restore_wallet:
                    helper,submit_path,config_path=restored["helper"],restored["configPath"],restored["reviewConfigPath"]
                else:
                    helper, config, config_path = install_journal()
                    rpc_path = root / "refund-rpc.json"
                    write_private(rpc_path, {"schema":"monzero-private-ltc-refund-rpc-v1", "ltcPort":ltc.port,
                        "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root / "ltc/regtest/.cookie"),
                        "nativeDaemonPort":daemon_port,"nativeWalletPort":claim_port,
                        "nativeLoginFile":str(root / f"monzero-wallet-rpc.{claim_port}.login"),"ltcRefundBlock":request["block"]})
                    if args.gui_prepare_payments:
                        preparation_rpc=json.loads(rpc_path.read_text())
                        preparation_rpc["schema"]="monzero-private-ltc-refund-rpc-v2"
                        preparation_rpc["preparationObserver"]={"walletPort":source_port,
                            "loginFile":str(root / f"monzero-wallet-rpc.{source_port}.login")}
                        rpc_path=root/"refund-preparation-rpc.json"
                        write_private(rpc_path,preparation_rpc)
                    submit_config = dict(config, schema="monzero-private-ltc-journal-v2",refundRpcFile=str(rpc_path))
                    submit_path = root / "refund-submit-config.json"
                    write_private(submit_path, json.loads(json.dumps(submit_config, sort_keys=True)))
                if args.gui_prepare_payments: payout=gui_prepare_payment(helper,submit_path,"refund")
                env = dict(os.environ, MONZERO_LTC_REFUND_SUBMIT_FIXTURE=str(submit_path),QT_QPA_PLATFORM="offscreen")
                if continuous:
                    continuous.submit_payment("refund")
                else:
                    gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinRefundSubmission"],env=env,
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=720)
                    if gui.returncode != 0:
                        write_private(args.report.with_name(args.report.name+".gui-submit-failure.json"), {
                            "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                            "stderr":gui.stderr.decode(errors="replace")[:32768]})
                    require(gui.returncode == 0, "Actual Qt refund submission failed")
            rpc(daemon_port, "generateblocks", {"wallet_address":addresses["mining"],"amount_of_blocks":10})
            wallet("refresh", claim=True)
            confirmed = invoke("confirm_refund_payout")
            require(confirmed["nativeDepth"] >= 10 and (args.gui_prepare_payments or confirmed["payout"] == payout["payout"]), "Refund payment confirmation differs")
            accounting = confirmed["payout"]
            fee = int(accounting["feeAtoms"])
            received = int(accounting["payoutAtoms"]) + int(accounting["reserveReturnAtoms"])
            review = payout["review"]
            require(review["action"] == "send" and review["tradeId"] == terms["swapId"]
                and review["destination"] == terms["xmzRefund"] and review["txid"] == payout["txid"]
                and review["refundAtoms"] == str(received) and review["feeAtoms"] == str(fee)
                and review["maximumFeeAtoms"] == terms["xmzRefundFeeMaxAtoms"] and review["chainObserved"] is False,
                "Native refund approval review differs from the actual payment")
            require(0 < fee <= int(terms["xmzRefundFeeMaxAtoms"]) and received + fee == 200000000000,
                    "Refund payment fee or net amount differs")
            require(accounting["destination"] == terms["xmzRefund"] and accounting["reserveReturn"] == terms["xmzRefund"],
                    "Refund payment destination differs from signed quote")
            require(wallet("get_balance", claim=True)["balance"] == 0, "Refund wallet retains unexplained balance")
            wallet("close_wallet")
            wallet("open_wallet", {"filename":"reserve","password":""})
            wallet("refresh", {"start_height":0})
            require(wallet("get_balance")["balance"] == received, "Wrong native refund recipient balance")
            incoming = wallet("get_transfer_by_txid", {"txid":payout["txid"]})["transfer"]
            require(incoming["type"] == "in" and incoming["amount"] == received, "Wrong native refund incoming payment")
            wallet("close_wallet")
            journal_checked = False
            gui_journal_checked = False
            if args.journal_helper:
                if not args.gui_submit_refund:
                    helper, config, config_path = install_journal()
                request_id = str(uuid.uuid4())
                payload = json.dumps({"schema":"monzero-ltc-journal-request-v1","requestId":request_id}).encode()
                checked = subprocess.run([str(helper),"--snapshot-stdio-v1","--config-file",str(config_path)],
                    input=payload, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
                require(checked.returncode == 0 and not checked.stderr and len(checked.stdout) <= 8192,
                        "Journal helper rejected completed refund")
                response = json.loads(checked.stdout)
                snapshot = response["snapshot"]
                require(response["requestId"] == request_id and snapshot["tradeId"] == terms["swapId"]
                    and snapshot["role"] == "native-funder" and snapshot["journalValidated"] is True
                    and snapshot["chainObserved"] is False and snapshot["executable"] is False,
                    "Journal helper context or authority differs")
                require(snapshot["nativeRefundPayout"] == {"prepared":True,"attempted":True,"acknowledged":True},
                    "Journal helper omitted retained refund payment")
                require(snapshot["xmzRefund"] == terms["xmzRefund"] and snapshot["xmzAtoms"] == terms["xmzAtoms"],
                    "Journal helper changed signed refund terms")
                journal_checked = True
                if args.gui_test_binary:
                    env = dict(os.environ, MONZERO_LTC_JOURNAL_FIXTURE=str(config_path), QT_QPA_PLATFORM="offscreen")
                    gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinJournalHelper"],env=env,
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=420)
                    if gui.returncode != 0:
                        write_private(args.report.with_name(args.report.name+".gui-failure.json"), {
                            "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                            "stderr":gui.stderr.decode(errors="replace")[:32768]})
                    require(gui.returncode == 0, "Native GUI rejected the actual journal helper response")
                    gui_journal_checked = True
            if args.gui_submit_refund:
                def gui_confirmation(label, rejected=False):
                    if continuous:
                        continuous.check_payment("refund",payout["txid"],rejected)
                        return
                    env = dict(os.environ, MONZERO_LTC_REFUND_SUBMIT_FIXTURE=str(submit_path),
                        MONZERO_LTC_REFUND_EXPECT_REJECTED="1" if rejected else "0", QT_QPA_PLATFORM="offscreen")
                    gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinRefundConfirmation"],env=env,
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=540)
                    if gui.returncode != 0:
                        write_private(args.report.with_name(args.report.name+"."+label+"-failure.json"), {
                            "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                            "stderr":gui.stderr.decode(errors="replace")[:32768]})
                    require(gui.returncode == 0, "Actual Qt refund confirmation failed: "+label)
                gui_confirmation("confirmed")
                ltc.rpc("invalidateblock", request["block"])
                require(ltc.rpc("getblockheader", request["block"])["confirmations"] == -1, "Refund block was not invalidated")
                gui_confirmation("reorg", rejected=True)
                ltc.rpc("reconsiderblock", request["block"])
                require(ltc.rpc("getblockheader", request["block"])["confirmations"] >= 2, "Refund block was not restored")
                gui_confirmation("reconfirmed")
                if args.indexed_settlement:
                    previous=request["block"]
                    ltc.rpc("invalidateblock",previous)
                    require(refund["txid"] in ltc.rpc("getrawmempool"),"Refund was not returned to the mempool")
                    request["block"]=ltc.mine(2,ltc.rpc("getnewaddress","replacement-refund-block"))[0]
                    require(request["block"]!=previous and ltc.rpc("getrawtransaction",refund["txid"],True)["blockhash"]==request["block"],
                        "Refund was not included in a different block")
                    gui_confirmation("remined")

                require(wallet("get_balance", claim=True)["balance"] == 0, "Confirmation checks changed refund balance")
            if args.gui_app_setup: verify_app_setup(True)
            write_private(args.report, {"schema":"monzero-ltc-xmz-refund-recovery-results-v1",
                "ltcCancellationVerified":True,"prematureCancellationRejected":True,"ltcRefundVerified":True,
                "journalHelperVerified":journal_checked,"guiJournalHelperVerified":gui_journal_checked,"guiRefundReviewVerified":gui_journal_checked,"guiRefundSubmissionVerified":args.gui_submit_refund,"guiRefundConfirmationVerified":args.gui_submit_refund,"guiRefundReorgRejected":args.gui_submit_refund,
                "nativeRefundWalletRecoveryVerified":True,"nativeRefundPaid":True,"refundApprovalBound":True,"nativeRefundTxid":payout["txid"],"nativeRefundAtoms":str(received),"nativeRefundFeeAtoms":str(fee),
                "fullCrossChainSettlement":True,"localLightWalletFunding":args.gui_light_settlement,"localFundingProcessRestart":args.gui_light_restart,"localFundingLostAcknowledgement":args.gui_light_lost_ack,"localFundingInFlightInterruption":args.gui_light_inflight,"localFundingRestartStage":args.gui_light_restart_stage if args.gui_light_restart else None,"indexedSettlementBlockVerified":args.indexed_settlement,"sharedAppSetupVerified":args.gui_app_setup,"sharedAppStartupVerified":args.gui_app_setup,"residentAppFundingVerified":args.gui_resident_funding,"residentAppSettlementVerified":args.gui_resident_settlement,"tradeStartApprovalBound":True,"startupHelperVerified":args.helper_start,"guiStartupVerified":args.gui_start or args.gui_peer_startup,"peerAdmissionHelperVerified":args.helper_negotiate or args.gui_negotiate_session or args.gui_peer_startup,"guiOutgoingSignatureVerified":args.gui_sign_negotiation or args.gui_negotiate_session or args.gui_peer_startup,"privateNegotiationSessionVerified":args.gui_negotiate_session or args.gui_peer_startup,"walletPeerStartupVerified":args.gui_peer_startup,"refundPathVerified":True,"recoveryPathsVerified":False,"executable":False,
                "ltcCancelTxid":cancellation["txid"],"ltcRefundTxid":refund["txid"],**light_refund_evidence,"nativeCancellationVerified":args.gui_native_cancel,"nativePrematurePunishmentRejected":args.gui_native_cancel,
                "fundingApprovalBound":True,"fundingHelperSubmissionVerified":args.helper_submit_funding,"fundingHelperRetryVerified":args.helper_submit_funding,"privateRedemptionSessionVerified":args.gui_redemption_session,"guiRedeemSigningVerified":args.gui_redeem_signing or args.gui_redemption_session,"redeemHelperAdmissionVerified":args.gui_redeem_signing or args.gui_redemption_session,"redeemReleaseReorgRejected":args.gui_redeem_signing,"guiLitecoinRecoveryConfirmationVerified":args.gui_check_ltc_recovery,"guiLitecoinRecoveryReorgRejected":args.gui_check_ltc_recovery,"guiLitecoinRecoverySubmissionVerified":args.gui_submit_ltc_recovery,"guiLitecoinRecoveryRetryVerified":args.gui_submit_ltc_recovery,"guiRecoveredWalletVerified":args.gui_restore_wallet,"guiRecoveredWalletRestartVerified":args.gui_restore_wallet,"guiPaymentPreparationVerified":args.gui_prepare_payments,"paymentPreparationNoBroadcastVerified":args.gui_prepare_payments,"guiLitecoinFundingSubmissionVerified":args.gui_submit_ltc_lock,"guiLitecoinFundingRetryVerified":args.gui_submit_ltc_lock,"guiLitecoinReservationVerified":args.gui_reserve_litecoin and not args.gui_light_settlement,"privateRecoverySessionVerified":args.gui_recovery_session,"guiRecoverySigningVerified":args.gui_recovery_signing or args.gui_recovery_session,"recoveryHelperAdmissionVerified":args.gui_recovery_signing or args.gui_recovery_session,"guiFundingPreparationVerified":args.gui_prepare_funding,"fundingPreparationNoBroadcastVerified":args.gui_prepare_funding,"guiFundingSubmissionVerified":args.gui_submit_funding,"fundingHelperConfirmationVerified":args.helper_check_funding,"fundingConfirmationReorgRejected":args.helper_check_funding,"guiFundingConfirmationVerified":args.gui_check_funding,"fundingHelperReviewVerified":funding_helper_checked,"guiFundingReviewVerified":funding_gui_checked,"nativeFundingTxid":native_lock["txid"],"recoveredNativeAtoms":"200000000000",
                "ltcFundingFeeAtoms":str(ltc_funding_fee),"nativeFundingFeeAtoms":str(native_funding_fee)})
            return
        def gui_redemption(operation,label,txid,retry=False,reject=False,reorg_block=None):
            output=root/("gui-redemption-"+label+".json");gate=root/("gui-redemption-"+label+"-gate.json")
            def records():
                return {(folder,p.name):p.read_bytes() for folder in ("alice-meta","alice-secrets") for p in (root/folder).iterdir() if p.is_file()}
            before=records();pool=ltc.rpc("getrawmempool")
            if continuous:
                if operation=="check":
                    result={"observation":continuous_ltc_check("redeem",txid,reject,reorg_block)}
                else:
                    review=continuous.call("alice","redemption-review")["redemption"]["review"]
                    require(review["details"]["txid"]==txid and review["details"]["action"]==("retransmit" if retry else "send"),"Wrong continuous redemption review")
                    continuous.call("alice","redemption-submit",accepted=False,commitment="0"*64)
                    state=continuous.call("alice","redemption-submit",commitment=review["commitment"])["redemption"]
                    result={"review":review,"outcome":state["submission"]}
                    require(result["outcome"]["txid"]==txid,"Wrong continuous redemption submission")
                if operation=="check" or retry or reject:
                    require(records()==before and ltc.rpc("getrawmempool")==pool,"Continuous redemption check/retry changed retained state")
                return result
            env=dict(os.environ,QT_QPA_PLATFORM="offscreen",MONZERO_LTC_REDEMPTION_CONFIG=str(funding_exchange_config),
                MONZERO_LTC_REDEMPTION_OPERATION=operation,MONZERO_LTC_REDEMPTION_TXID=txid,
                MONZERO_LTC_REDEMPTION_RETRY="1" if retry else "0",MONZERO_LTC_REDEMPTION_REJECT="1" if reject else "0",
                MONZERO_LTC_REDEMPTION_RESULT=str(output),MONZERO_LTC_REDEMPTION_REORG_GATE=str(gate) if reorg_block else "")
            gui=subprocess.Popen([str(args.gui_test_binary),"actualLitecoinRedemptionTransaction"],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            invalidated=False
            try:
                if reorg_block:
                    deadline=time.monotonic()+180
                    while not output.exists() and gui.poll() is None and time.monotonic()<deadline: time.sleep(0.05)
                    require(output.exists(),"Qt redemption confirmation did not reach reorg gate")
                    ltc.rpc("invalidateblock",reorg_block);invalidated=True
                    require(ltc.rpc("getblockheader",reorg_block)["confirmations"]==-1,"Redemption block remained active")
                    write_private(gate,{"reorg":True})
                stdout,stderr=gui.communicate(timeout=540)
                require(gui.returncode==0 and output.is_file(),"Qt redemption failed ("+label+"): "+stdout+stderr)
                result=json.loads(output.read_text())
                if operation=="check" and not reject:
                    require(result["observation"]["txid"]==txid,"Wrong confirmed redemption identity")
            finally:
                if gui.poll() is None: gui.kill();gui.communicate()
                if invalidated: ltc.rpc("reconsiderblock",reorg_block)
            if operation=="check" or retry or reject:
                require(records()==before and ltc.rpc("getrawmempool")==pool,"Read-only/retry redemption changed saved bytes or sent funds")
            return result

        if args.gui_redemption_session:
            redeem_rpc=root/"redemption-session-rpc.json"
            write_private(redeem_rpc,{"schema":"monzero-private-ltc-redeem-rpc-v1","ltcPort":ltc.port,
                "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                "nativeDaemonPort":daemon_port,"observerWalletPort":observer_port,
                "observerLoginFile":str(root/f"monzero-wallet-rpc.{observer_port}.login"),"safetyBlocks":2})
            redeem_config=root/"redemption-session-config.json"
            write_private(redeem_config,{"journalConfigFile":str(startup_configs["litecoin-funder"][2]),
                "rpcFile":str(redeem_rpc),"schema":"monzero-private-ltc-redeem-v1"})
            psbt=json.loads((root/"bob-meta"/(terms["swapId"]+".ltc-recovery-intent")).read_text())["psbt"]
            session_input=dict(session_request,aliceSettlementConfig=str(funding_exchange_config),
                bobSettlementConfig=str(redeem_config),recoveryPsbt=psbt)
            fixture,output=root/"redemption-session.json",root/"redemption-session-result.json"
            write_private(fixture,session_input)
            pools=(ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            if continuous:
                continuous.call("alice","share-proof")
                state=continuous.call("bob","wait-release")["peer"]
                require(state["nativeFundingTxid"]==native_lock["txid"],"Continuous proof changed native funding identity")
                continuous.call("bob","release-redemption",accepted=False,txid="0"*64)
                continuous.call("bob","release-redemption",txid=native_lock["txid"])
                continuous.call("alice","wait-settlement")
            else:
                session=subprocess.run([str(args.gui_test_binary),"actualLitecoinNegotiationSession"],
                    env=dict(os.environ,MONZERO_LTC_SESSION_FIXTURE=str(fixture),MONZERO_LTC_SESSION_RESULT=str(output),QT_QPA_PLATFORM="offscreen"),
                    capture_output=True,text=True,timeout=1200)
                require(session.returncode==0 and output.is_file(),"Private redemption session failed: "+session.stdout[-12000:]+session.stderr[-2000:])
                evidence=json.loads(output.read_text())
                require(evidence["redemptionSessionVerified"] and evidence["mutualTlsVerified"] and evidence["wrongPinRejected"]
                    and evidence["restartRedeliveryVerified"],"Private redemption evidence incomplete")
            require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])==pools,"Private redemption exchange broadcast funds")
            if args.gui_submit_ltc_redemption:
                cfg=json.loads(funding_exchange_config.read_text())
                request_id=str(uuid.uuid4())
                reviewed=subprocess.run([cfg["helper"],"--redemption-review-stdio-v1","--config-file",str(funding_exchange_config)],
                    input=json.dumps({"schema":"monzero-ltc-redemption-review-request-v1","requestId":request_id}),capture_output=True,text=True,timeout=180)
                require(reviewed.returncode==0 and not reviewed.stderr,"Redemption helper review failed")
                response=json.loads(reviewed.stdout)
                require(response["requestId"]==request_id and response["schema"]=="monzero-ltc-redemption-review-response-v1","Wrong redemption review response")
                txid=response["review"]["details"]["txid"]
                require(response["review"]["details"]["action"]=="send" and ltc.rpc("getrawmempool")==pools[0],"Review sent Litecoin")
                gui_redemption("submit","first",txid)
                gui_redemption("submit","retry",txid,retry=True)
                gui_redemption("check","unconfirmed",txid,reject=True)
                redemption={"txid":txid,"redemptionApprovalBound":True}
            else:
                redemption=invoke("redeem_retained")
        elif args.gui_redeem_signing:
            helper=startup_configs["litecoin-funder"][0]
            redeem_rpc=root/"redeem-rpc.json"
            write_private(redeem_rpc,{"schema":"monzero-private-ltc-redeem-rpc-v1","ltcPort":ltc.port,
                "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root/"ltc/regtest/.cookie"),
                "nativeDaemonPort":daemon_port,"observerWalletPort":observer_port,
                "observerLoginFile":str(root/f"monzero-wallet-rpc.{observer_port}.login"),"safetyBlocks":2})
            redeem_config=root/"redeem-config.json"
            write_private(redeem_config,{"schema":"monzero-private-ltc-redeem-v1",
                "journalConfigFile":str(startup_configs["litecoin-funder"][2]),"rpcFile":str(redeem_rpc)})
            def records():
                return {(folder,p.name):p.read_bytes() for folder in ("bob-meta","bob-secrets","alice-meta","alice-secrets") for p in (root/folder).iterdir() if p.is_file()}
            def redeem_command(operation,message=None,reject=False):
                payload={"schema":"monzero-ltc-redeem-"+operation+"-request-v1","requestId":str(uuid.uuid4())}
                if message: payload.update(message)
                config=startup_configs["native-funder"][2] if operation=="accept" else redeem_config
                program=startup_configs["native-funder"][0] if operation=="accept" else helper
                before=records()
                completed=subprocess.run([str(program),"--redeem-"+operation+"-stdio-v1","--config-file",str(config)],
                    input=json.dumps(payload),capture_output=True,text=True,timeout=180)
                if reject:
                    require(completed.returncode!=0,"Unsafe redemption release accepted: "+operation)
                    require(records()==before,"Rejected redemption operation changed retained state")
                    return None
                require(completed.returncode==0,"Redemption helper failed: "+operation+" "+completed.stderr[-1024:])
                response=json.loads(completed.stdout)
                require(response["schema"]=="monzero-ltc-redeem-"+operation+"-response-v1" and response["requestId"]==payload["requestId"],"Wrong redemption response")
                return response
            # No peer proof has yet been admitted by the Litecoin funder.
            redeem_command("review",reject=True)
            invoke("admit_native")
            pools=(ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])
            head_path=root/"bob-meta"/(terms["swapId"]+".ltc-execution-head")
            before_review=head_path.read_bytes()
            reviewed=redeem_command("review");retained=records()
            require(redeem_command("review")["frameHex"]==reviewed["frameHex"] and records()==retained,"Restart changed retained redeem frame")
            pending=root/"bob-meta"/(terms["swapId"]+".ltc-redeem-unsigned")
            hidden=root/"hidden-redeem-unsigned"
            pending.rename(hidden)
            redeem_command("review",reject=True)
            require(not pending.exists(),"Missing committed frame was regenerated")
            hidden.rename(pending)
            # Retention-before-head-publication recovery uses the same frame.
            head_path.write_bytes(before_review)
            require(redeem_command("review")["frameHex"]==reviewed["frameHex"] and records()==retained,"Interrupted publication replaced redeem frame")
            fixture,signature_path=root/"redeem-sign.json",root/"redeem-signature.hex"
            write_private(fixture,{"identityFile":session_request["bobIdentity"],"passphrase":session_request["bobPassphrase"],
                "publicKey":SigningKey(bytes([1])*32).verify_key.encode().hex(),"frameHex":reviewed["frameHex"],"protocol":"redeem"})
            signed=subprocess.run([str(args.gui_test_binary),"actualLitecoinOutgoingSignature"],
                env=dict(os.environ,MONZERO_LTC_SIGN_FIXTURE=str(fixture),MONZERO_LTC_SIGN_RESULT=str(signature_path),QT_QPA_PLATFORM="offscreen"),
                capture_output=True,text=True,timeout=180)
            require(signed.returncode==0 and signature_path.is_file(),"Qt redemption signing failed: "+signed.stdout+signed.stderr)
            message={"frameHex":reviewed["frameHex"],"signatureHex":signature_path.read_text()}
            redeem_command("submit",dict(message,signatureHex="00"*64),reject=True)
            frame=bytes.fromhex(message["frameHex"])
            wrong_domain=b"monzero-ltc-xmz-recovery-v1\0"+len(frame).to_bytes(8,"big")+frame
            redeem_command("submit",dict(message,signatureHex=SigningKey(bytes([1])*32).sign(wrong_domain).signature.hex()),reject=True)
            redeem_command("submit",dict(message,frameHex="00"),reject=True)
            # Revalidation is mandatory even after the unsigned frame is saved.
            lock_block=ltc.rpc("getrawtransaction",lock["txid"],True)["blockhash"]
            ltc.rpc("invalidateblock",lock_block)
            redeem_command("review",reject=True);redeem_command("submit",message,reject=True)
            ltc.rpc("reconsiderblock",lock_block)
            require(redeem_command("review")["frameHex"]==message["frameHex"],"Restored chain changed redeem frame")
            saved=redeem_command("submit",message)
            require(saved["frameHex"]==message["frameHex"] and saved["signatureHex"]==message["signatureHex"],"Retained release differs from Qt signature")
            retained=records();redeem_command("submit",message)
            require(records()==retained,"Exact release retry changed saved state")
            redeem_command("submit",dict(message,signatureHex="00"*64),reject=True)
            redeem_command("accept",dict(message,signatureHex="00"*64),reject=True)
            admitted=redeem_command("accept",message);retained=records()
            require(admitted["snapshot"]["litecoinRedemption"]["prepared"] and not admitted["snapshot"]["litecoinRedemption"]["attempted"],"Peer admission broadcast redemption")
            require(redeem_command("accept",message)["txid"]==admitted["txid"] and records()==retained,"Peer redelivery changed completed redemption")
            require((ltc.rpc("getrawmempool"),rpc(daemon_port,"get_info")["tx_pool_size"])==pools,"Signature exchange sent funds")
            redemption=invoke("redeem_retained")
            require(redemption["txid"]==admitted["txid"],"Submission changed completed redemption")
        else:
            redemption = invoke("redeem")
        require(redemption["redemptionApprovalBound"], "Redemption approval/retry was not verified")
        invoke("check_redeem_unconfirmed", reject=True)
        request["block"] = ltc.mine(2, anchor)[0]
        confirmation = invoke("check_redeem")["observation"]
        require(confirmation["chainObserved"] and confirmation["txid"] == redemption["txid"]
            and int(confirmation["confirmations"]) >= int(confirmation["requiredConfirmations"]),
            "Indexed redemption confirmation differs")
        ltc.rpc("invalidateblock", request["block"])
        require(ltc.rpc("getblockheader", request["block"])["confirmations"] == -1, "Redemption block was not invalidated")
        invoke("check_redeem_reorg", reject=True)
        ltc.rpc("reconsiderblock", request["block"])
        (root / "check_redeem-result.json").unlink()
        require(invoke("check_redeem")["observation"]["txid"] == redemption["txid"], "Restored redemption differs")
        if args.gui_submit_ltc_redemption:
            gui_redemption("check","confirmed-and-reorg",redemption["txid"],reorg_block=request["block"])
            gui_redemption("check","restored",redemption["txid"])
        transaction = ltc.rpc("getrawtransaction", redemption["txid"], True)
        require(transaction["confirmations"] >= 2 and len(transaction["vout"]) == 1, "Unconfirmed Litecoin redemption")
        require(atoms(transaction["vout"][0]["value"]) == 99990000
            and transaction["vout"][0]["scriptPubKey"]["hex"] == ltc.rpc("validateaddress", ltc_payout)["scriptPubKey"],
            "Wrong Litecoin principal or recipient")
        restored = gui_restore_wallet("payout") if args.gui_restore_wallet else invoke("claim_restore")
        require(restored["nativeAddress"] == negotiated["nativeAddress"], "Claim wallet differs from negotiation")
        wallet("refresh", claim=True)
        require(wallet("get_balance", claim=True)["balance"] == 200000000000, "Native claim did not import exact lock")
        payout = None if args.gui_prepare_payments else invoke("payout_prepare" if args.gui_submit_payout else "payout")
        if args.gui_submit_payout:
            if args.gui_restore_wallet:
                helper,submit_path,config_path=restored["helper"],restored["configPath"],restored["reviewConfigPath"]
            else:
                helper, config, config_path = install_journal(payout=True)
                rpc_path = root / "payout-rpc.json"
                write_private(rpc_path, {"schema":"monzero-private-ltc-payout-rpc-v1", "ltcPort":ltc.port,
                    "ltcWallet":"fixture","ltcAnchor":anchor,"ltcCookieFile":str(root / "ltc/regtest/.cookie"),
                    "nativeDaemonPort":daemon_port,"nativeWalletPort":claim_port,
                    "nativeLoginFile":str(root / f"monzero-wallet-rpc.{claim_port}.login"),"ltcRedeemBlock":request["block"]})
                if args.gui_prepare_payments:
                    preparation_rpc=json.loads(rpc_path.read_text())
                    preparation_rpc["schema"]="monzero-private-ltc-payout-rpc-v2"
                    preparation_rpc["preparationObserver"]={"walletPort":observer_port,
                        "loginFile":str(root / f"monzero-wallet-rpc.{observer_port}.login")}
                    rpc_path=root/"payout-preparation-rpc.json"
                    write_private(rpc_path,preparation_rpc)
                submit_config = dict(config, schema="monzero-private-ltc-journal-v3",payoutRpcFile=str(rpc_path))
                submit_path = root / "payout-submit-config.json"
                write_private(submit_path, json.loads(json.dumps(submit_config, sort_keys=True)))
            if args.gui_prepare_payments: payout=gui_prepare_payment(helper,submit_path,"payout")
            env = dict(os.environ, MONZERO_LTC_PAYOUT_SUBMIT_FIXTURE=str(submit_path),QT_QPA_PLATFORM="offscreen")
            if continuous:
                continuous.submit_payment("payout")
            else:
                gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinPayoutSubmission"],env=env,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=720)
                if gui.returncode != 0:
                    write_private(args.report.with_name(args.report.name+".gui-submit-failure.json"), {
                        "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                        "stderr":gui.stderr.decode(errors="replace")[:32768]})
                require(gui.returncode == 0, "Actual Qt payout submission failed")
        rpc(daemon_port, "generateblocks", {"wallet_address":addresses["mining"],"amount_of_blocks":10})
        wallet("refresh", claim=True)
        confirmed = invoke("confirm_payout")
        require(confirmed["nativeDepth"] >= 10 and (args.gui_prepare_payments or confirmed["payout"] == payout["payout"]), "Payout confirmation differs")
        require(wallet("get_balance", claim=True)["balance"] == 0, "Claim wallet retains unexplained balance")
        accounting = confirmed["payout"]
        review = payout["review"]
        require(review["action"] == "send" and review["tradeId"] == terms["swapId"]
            and review["destination"] == terms["xmzPayout"] and review["reserveDestination"] == terms["xmzRefund"]
            and review["txid"] == payout["txid"] and review["payoutAtoms"] == accounting["payoutAtoms"]
            and review["reserveReturnAtoms"] == accounting["reserveReturnAtoms"] and review["feeAtoms"] == accounting["feeAtoms"]
            and review["maximumFeeAtoms"] == terms["xmzClaimFeeMaxAtoms"], "Reviewed payout differs from actual signed settlement")
        require(int(accounting["payoutAtoms"]) == 100000000000 and
            sum(int(accounting[key]) for key in ("payoutAtoms","reserveReturnAtoms","feeAtoms")) == 200000000000,
            "Native accounting differs from signed principal")
        if args.gui_submit_payout:
            def gui_payout_confirmation(label, rejected=False):
                if continuous:
                    continuous.check_payment("payout",payout["txid"],rejected)
                    return
                env = dict(os.environ, MONZERO_LTC_PAYOUT_SUBMIT_FIXTURE=str(submit_path),
                    MONZERO_LTC_PAYOUT_EXPECT_REJECTED="1" if rejected else "0", QT_QPA_PLATFORM="offscreen")
                gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinPayoutConfirmation"],env=env,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=540)
                if gui.returncode != 0:
                    write_private(args.report.with_name(args.report.name+"."+label+"-failure.json"), {
                        "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                        "stderr":gui.stderr.decode(errors="replace")[:32768]})
                require(gui.returncode == 0, "Actual Qt payout confirmation failed: "+label)
            gui_payout_confirmation("confirmed")
            ltc.rpc("invalidateblock", request["block"])
            require(ltc.rpc("getblockheader", request["block"])["confirmations"] == -1, "Redemption block was not invalidated")
            gui_payout_confirmation("reorg", rejected=True)
            ltc.rpc("reconsiderblock", request["block"])
            require(ltc.rpc("getblockheader", request["block"])["confirmations"] >= 2, "Redemption block was not restored")
            gui_payout_confirmation("reconfirmed")
            if args.indexed_settlement:
                previous=request["block"]
                ltc.rpc("invalidateblock",previous)
                require(redemption["txid"] in ltc.rpc("getrawmempool"),"Redemption was not returned to the mempool")
                request["block"]=ltc.mine(2,ltc.rpc("getnewaddress","replacement-redemption-block"))[0]
                require(request["block"]!=previous and ltc.rpc("getrawtransaction",redemption["txid"],True)["blockhash"]==request["block"],
                    "Redemption was not included in a different block")
                gui_payout_confirmation("remined")

            require(wallet("get_balance", claim=True)["balance"] == 0, "Confirmation checks changed payout balance")
        wallet("close_wallet")
        for name, amount in (("payout",100000000000),("reserve",int(accounting["reserveReturnAtoms"]))):
            wallet("open_wallet", {"filename":name,"password":""})
            wallet("refresh", {"start_height":0})
            require(wallet("get_balance")["balance"] == amount, "Wrong native recipient balance")
            incoming = wallet("get_transfer_by_txid", {"txid":payout["txid"]})["transfer"]
            require(incoming["type"] == "in" and incoming["amount"] == amount, "Wrong native incoming transaction")
            wallet("close_wallet")
        payout_helper_checked = False
        payout_gui_checked = False
        if args.journal_helper:
            import hashlib
            if not args.gui_submit_payout:
                helper,config,config_path = install_journal(payout=True)
            request_id = str(uuid.uuid4())
            reviewed = subprocess.run([str(helper),"--payout-review-stdio-v1","--config-file",str(config_path)],
                input=json.dumps({"schema":"monzero-ltc-payout-review-request-v1","requestId":request_id,
                    "action":"retransmit"}).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
            require(reviewed.returncode == 0 and not reviewed.stderr, "Payout review helper failed")
            response = json.loads(reviewed.stdout)
            require(response["schema"] == "monzero-ltc-payout-review-response-v1" and response["requestId"] == request_id,
                "Payout helper response context differs")
            details = response["review"]["details"]
            require(details["action"] == "retransmit" and details["dispatchCommitment"] is not None
                and details["chainObserved"] is False, "Payout review changed saved authority")
            for key in ("tradeId","quoteCommitment","txid","destination","payoutAtoms","reserveDestination",
                    "reserveReturnAtoms","feeAtoms","maximumFeeAtoms","lockedAtoms"):
                require(details[key] == review[key], "Helper changed retained payout field: "+key)
            binding = b"MONZERO-LTC-NATIVE-PAYOUT-APPROVAL-V1\0" + json.dumps(details,sort_keys=True,separators=(",",":")).encode()
            require(hashlib.sha256(binding).hexdigest() == response["review"]["commitment"], "Payout review commitment differs")
            payout_helper_checked = True
            if args.gui_test_binary:
                env = dict(os.environ, MONZERO_LTC_PAYOUT_REVIEW_FIXTURE=str(config_path), QT_QPA_PLATFORM="offscreen")
                gui = subprocess.run([str(args.gui_test_binary),"actualLitecoinPayoutReview"],env=env,
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=420)
                if gui.returncode != 0:
                    write_private(args.report.with_name(args.report.name+".gui-failure.json"), {
                        "exitCode":gui.returncode,"stdout":gui.stdout.decode(errors="replace")[:32768],
                        "stderr":gui.stderr.decode(errors="replace")[:32768]})
                require(gui.returncode == 0, "Native GUI rejected actual payout review")
                payout_gui_checked = True
        if args.gui_app_setup: verify_app_setup(False)
        write_private(args.report, {"schema":"monzero-ltc-xmz-paired-results-v1","fullCrossChainSettlement":True,"localLightWalletFunding":args.gui_light_settlement,"localFundingProcessRestart":args.gui_light_restart,"localFundingLostAcknowledgement":args.gui_light_lost_ack,"localFundingInFlightInterruption":args.gui_light_inflight,"localFundingRestartStage":args.gui_light_restart_stage if args.gui_light_restart else None,"indexedSettlementBlockVerified":args.indexed_settlement,"sharedAppSetupVerified":args.gui_app_setup,"sharedAppStartupVerified":args.gui_app_setup,"residentAppFundingVerified":args.gui_resident_funding,"residentAppSettlementVerified":args.gui_resident_settlement,"tradeStartApprovalBound":True,"startupHelperVerified":args.helper_start,"guiStartupVerified":args.gui_start or args.gui_peer_startup,"peerAdmissionHelperVerified":args.helper_negotiate or args.gui_negotiate_session or args.gui_peer_startup,"guiOutgoingSignatureVerified":args.gui_sign_negotiation or args.gui_negotiate_session or args.gui_peer_startup,"privateNegotiationSessionVerified":args.gui_negotiate_session or args.gui_peer_startup,"walletPeerStartupVerified":args.gui_peer_startup,
            "successPathVerified":True,"payoutApprovalBound":True,"payoutHelperReviewVerified":payout_helper_checked,"guiPayoutReviewVerified":payout_gui_checked,"guiPayoutSubmissionVerified":args.gui_submit_payout,"guiPayoutConfirmationVerified":args.gui_submit_payout,"guiPayoutReorgRejected":args.gui_submit_payout,"recoveryPathsVerified":False,"executable":False,
            "ltcFundingTxid":lock["txid"],"fundingApprovalBound":True,"fundingHelperSubmissionVerified":args.helper_submit_funding,"fundingHelperRetryVerified":args.helper_submit_funding,"privateRedemptionSessionVerified":args.gui_redemption_session,"guiRedeemSigningVerified":args.gui_redeem_signing or args.gui_redemption_session,"redeemHelperAdmissionVerified":args.gui_redeem_signing or args.gui_redemption_session,"redeemReleaseReorgRejected":args.gui_redeem_signing,"guiLitecoinRecoveryConfirmationVerified":args.gui_check_ltc_recovery,"guiLitecoinRecoveryReorgRejected":args.gui_check_ltc_recovery,"guiLitecoinRecoverySubmissionVerified":args.gui_submit_ltc_recovery,"guiLitecoinRecoveryRetryVerified":args.gui_submit_ltc_recovery,"guiRecoveredWalletVerified":args.gui_restore_wallet,"guiRecoveredWalletRestartVerified":args.gui_restore_wallet,"guiPaymentPreparationVerified":args.gui_prepare_payments,"paymentPreparationNoBroadcastVerified":args.gui_prepare_payments,"guiLitecoinFundingSubmissionVerified":args.gui_submit_ltc_lock,"guiLitecoinFundingRetryVerified":args.gui_submit_ltc_lock,"guiLitecoinReservationVerified":args.gui_reserve_litecoin and not args.gui_light_settlement,"privateRecoverySessionVerified":args.gui_recovery_session,"guiRecoverySigningVerified":args.gui_recovery_signing or args.gui_recovery_session,"recoveryHelperAdmissionVerified":args.gui_recovery_signing or args.gui_recovery_session,"guiFundingPreparationVerified":args.gui_prepare_funding,"fundingPreparationNoBroadcastVerified":args.gui_prepare_funding,"guiFundingSubmissionVerified":args.gui_submit_funding,"fundingHelperConfirmationVerified":args.helper_check_funding,"fundingConfirmationReorgRejected":args.helper_check_funding,"guiFundingConfirmationVerified":args.gui_check_funding,"fundingHelperReviewVerified":funding_helper_checked,"guiFundingReviewVerified":funding_gui_checked,"nativeFundingTxid":native_lock["txid"],"ltcRedeemTxid":redemption["txid"],"redemptionApprovalBound":True,"redemptionRetryVerified":True,"redemptionConfirmationVerified":True,"redemptionReorgRejected":True,"guiRedemptionSubmissionVerified":args.gui_submit_ltc_redemption,"guiRedemptionRetryVerified":args.gui_submit_ltc_redemption,"guiRedemptionConfirmationVerified":args.gui_submit_ltc_redemption,"guiRedemptionReorgRejected":args.gui_submit_ltc_redemption,
            "nativePayoutTxid":payout["txid"],"nativePayout":accounting,
            "fundingFeeAccountingVerified":True,"ltcFundingFeeAtoms":str(ltc_funding_fee),
            "nativeFundingFeeAtoms":str(native_funding_fee)})
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                if getattr(process,"monzero_owned_group",False):
                    require(os.getpgid(process.pid)==process.pid,"Owned Qt process group changed during cleanup")
                    os.killpg(process.pid,signal.SIGTERM)
                else:
                    process.terminate()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
        try:
            if light_peer:
                light_peer.__exit__(None,None,None)
        finally:
            try:
                if ltc:
                    ltc.stop()
            finally:
                shutil.rmtree(root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("litecoind","daemon","wallet-rpc","engine-test-binary","temp-parent","report"):
        parser.add_argument("--"+flag, required=True, type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--gui-light-inflight", action="store_true", help="Kill owned app/helper group while broadcast is accepted but its reply is held")
    parser.add_argument("--gui-light-lost-ack", action="store_true", help="Drop one TLS broadcast reply after node acceptance, then restart and reconcile")
    parser.add_argument("--gui-light-restart-stage", choices=("signed","sent"), default="sent", help="Owned idle-operation interruption boundary")
    parser.add_argument("--gui-light-restart", action="store_true", help="Interrupt and resume the resident app at the selected local LTC funding boundary")
    parser.add_argument("--gui-light-settlement", action="store_true", help="Continue actual local-wallet funding through complete success or refund settlement")
    parser.add_argument("--gui-light-recovery-helper", type=lambda value:pathlib.Path(value).resolve(),
        help="Stop after actual app peer recovery with a disposable local light wallet; no settlement claim")
    parser.add_argument("--light-wallet-tls", type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--gui-test-binary", type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--journal-helper", type=lambda value:pathlib.Path(value).resolve())
    parser.add_argument("--gui-start", action="store_true", help="Review, approve and resume both startup roles through the actual Qt process bridge")
    parser.add_argument("--gui-peer-startup", action="store_true", help="Configure native wallet peer startup before approving either trade, then connect and resume saved negotiation")
    parser.add_argument("--gui-negotiate-session", action="store_true", help="Run both native Qt coordinators over mutually pinned TLS, including reconnect redelivery")
    parser.add_argument("--gui-sign-negotiation", action="store_true", help="Sign engine-prepared negotiation frames with encrypted Qt identities and retain them through the helper")
    parser.add_argument("--helper-negotiate", action="store_true", help="Authenticate and durably accept both peer frames through separate journal helper processes")
    parser.add_argument("--helper-start", action="store_true", help="Review and approve both isolated trade roles through the native startup helper")
    parser.add_argument("--gui-check-funding", action="store_true", help="Check native confirmation, depth loss and source substitution through Qt without sending")
    parser.add_argument("--helper-check-funding", action="store_true", help="Observe native funding without sending, reject shallow/reorged confirmation and wrong source wallet")
    parser.add_argument("--gui-native-punish", action="store_true", help="After native cancellation, withhold refund and verify mature punishment, retry, reorg and fresh Qt confirmation; no native payout/refund")
    parser.add_argument("--gui-native-cancel", action="store_true", help="Cancel from the native-funder app, check both roles and reject premature punishment before the ordinary refund")
    parser.add_argument("--gui-resident-settlement", action="store_true", help="Keep both engines alive through redemption/recovery, wallet restoration and final native payment")
    parser.add_argument("--gui-resident-funding", action="store_true", help="Keep both app engines alive from initial approval through reservation, recovery agreement and confirmed native funding")
    parser.add_argument("--gui-app-setup", action="store_true", help="Resume both roles through one Qt app setup and verify phase routing, binding and restart")
    parser.add_argument("--indexed-settlement", action="store_true", help="Resolve settlement blocks from retained transaction identities and recheck after different-block mining")
    parser.add_argument("--gui-submit-ltc-redemption", action="store_true", help="Review, approve, submit, retry and check the retained redemption through Qt")
    parser.add_argument("--gui-redemption-session", action="store_true", help="Exchange verified funding proof and approved redemption with both Qt peers over pinned mutual TLS")
    parser.add_argument("--gui-redeem-signing", action="store_true", help="Release and accept redemption through the helper with the encrypted Qt identity signer")
    parser.add_argument("--gui-check-ltc-recovery", action="store_true", help="Check cancellation/refund depth and reject reorganised evidence in the same Qt object")
    parser.add_argument("--gui-submit-ltc-recovery", action="store_true", help="Approve saved Litecoin cancellation/refund and reconcile exact-byte retries through Qt")
    parser.add_argument("--gui-restore-wallet", action="store_true", help="Restore, verify and reopen claim/refund wallet through Qt, with no replacement on failure")
    parser.add_argument("--gui-prepare-payments", action="store_true", help="Prepare recovered XMZ payout/refund through Qt without broadcasting")
    parser.add_argument("--gui-submit-ltc-lock", action="store_true", help="Approve Litecoin funding and review an exact transaction retry through Qt")
    parser.add_argument("--gui-reserve-litecoin", action="store_true", help="Prepare or restore Litecoin reservation through Qt, then automatically start recovery exchange")
    parser.add_argument("--gui-recovery-session", action="store_true", help="Exchange recovery through the production Qt session over pinned mutual TLS")
    parser.add_argument("--gui-recovery-signing", action="store_true", help="Prepare, sign with Qt identity, retain and admit both recovery messages before funding")
    parser.add_argument("--gui-prepare-funding", action="store_true", help="Prepare XMZ funding through Qt without broadcast and recover identical review after restart")
    parser.add_argument("--gui-submit-funding", action="store_true", help="Explicitly submit and retry prepared test funding through the Qt process bridge")
    parser.add_argument("--helper-submit-funding", action="store_true", help="Submit approved initial funding and an explicit same-transaction retry through the isolated-chain journal helper")
    parser.add_argument("--gui-submit-payout", action="store_true", help="Explicitly submit and confirm the prepared fakechain payout through the actual Qt process bridge")
    parser.add_argument("--gui-submit-refund", action="store_true", help="Explicitly submit the prepared fakechain refund through the actual Qt process bridge")
    parser.add_argument("--cancel-refund", action="store_true", help="Cancel/refund Litecoin and pay the recovered native lock to the signed refund destination")
    run(parser.parse_args())
