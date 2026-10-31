//! XMZ <-> BNB swaps on public BNB testnet (chain 97) against the private XMZ testnet.
//!
//!   cargo run --example bsc_testnet -- swap|refund <run-dir>     (new trade)
//!   cargo run --example bsc_testnet -- resume <run-dir>          (after a stop or crash)
//!
//!  - BNB side: the deployed XmzSwap (deployments/bsc-testnet.json) and the two test accounts,
//!    rebuilt from their phrase with `evm_account` (owner index 0, claimer index 1). The
//!    owner reads BNB testnet through one public RPC, the claimer through another.
//!  - Keys: each trader's `SwapSeed`, exchanged over the libp2p key-exchange protocol between
//!    two nodes on localhost.
//!  - XMZ side: fresh wallets on the XMZ testnet node 127.0.0.1:16175; the claimer's funds
//!    wallet is paid by the testnet funds wallet (wallet RPC 127.0.0.1:16183). The node mines
//!    while the run lasts.
//!
//! `swap`: the whole trade. `refund`: the claimer goes offline after the owner's setReady;
//! the owner refunds after timeout2 and the claimer then sweeps its XMZ back.
//! Prints state names only: no keys, frames or secrets.

use alloy::primitives::{keccak256, Address, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::signers::local::PrivateKeySigner;
use futures::StreamExt;
use hmac::{Hmac, Mac};
use libp2p::request_response::{Event, Message};
use libp2p::swarm::SwarmEvent;
use libp2p::{noise, tcp, yamux, Swarm, SwarmBuilder};
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::driver::{Driver, FileJournal, Journal, Record, RoleState};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::exchange::{p2p, SavedTrade, Session};
use monzero_dex_evm::machine::{ClaimerState, OwnerState, Timing};
use monzero_dex_evm::negotiation::{Agreed, Offer, Role};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::xmz::{WalletRpc, WalletRpcConfig, WalletRpcXmz, XmzNetwork};
use monzero_dex_evm::Chain;
use rand::RngCore;
use serde_json::{json, Value};
use sha2::Sha512;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::Duration;

const OWNER_RPC: &str = "https://bsc-testnet-rpc.publicnode.com";
// The claimer's endpoint (CLAIMER_RPC overrides). BNB Chain's own testnet seeds refuse log
// queries, drpc's free tier serves only recent ones, onfinality's rate-limits a trader polling
// every 15 s; publicnode has served both traders reliably.
const CLAIMER_RPC_DEFAULT: &str = "https://bsc-testnet-rpc.publicnode.com";
const OWNER_LOG_CHUNK: u64 = 2_000;
const CLAIMER_LOG_CHUNK: u64 = 1_000;

fn claimer_rpc() -> String {
    std::env::var("CLAIMER_RPC").unwrap_or_else(|_| CLAIMER_RPC_DEFAULT.into())
}
const XMZ_NODE: &str = "127.0.0.1:16175";
const XMZ_FUNDS_RPC: &str = "http://127.0.0.1:16183";
const WALLET_RPC_BIN: &str = "/home/tripo/projects/Monzero-App/testnet/bin/monzero-v1.0.0-linux-x86_64/monzero-wallet-rpc";
const ACCOUNTS: &str = ".config/monzero-dex/bsc-testnet-accounts.json";
const VALUE_WEI: u64 = 20_000_000_000_000_000; // 0.02 tBNB
const XMZ_AMOUNT: u64 = 2_000_000_000_000; // 20 tXMZ (11 decimals)
const TICK: Duration = Duration::from_secs(15);

type Real = Driver<AlloyChain, WalletRpcXmz, FileJournal>;

fn log(msg: impl std::fmt::Display) {
    let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    println!("{:02}:{:02}:{:02} {msg}", t / 3600 % 24, t / 60 % 60, t % 60);
}

/// State name only (some states hold a revealed share; it is public on chain, but not logged)
fn label(state: &RoleState) -> String {
    let debug = match state {
        RoleState::Owner(OwnerState::Done(o)) | RoleState::Claimer(ClaimerState::Done(o)) => return format!("Done({o:?})"),
        RoleState::Owner(s) => format!("{s:?}"),
        RoleState::Claimer(s) => format!("{s:?}"),
    };
    debug.split([' ', '{', '(']).next().unwrap().to_string()
}

/// BIP-39 seed of a phrase (PBKDF2-HMAC-SHA512, 2048 rounds, empty passphrase)
fn bip39_seed(phrase: &str) -> [u8; 64] {
    let mac = || Hmac::<Sha512>::new_from_slice(phrase.as_bytes()).unwrap();
    let mut m = mac();
    m.update(b"mnemonic");
    m.update(&1u32.to_be_bytes());
    let mut u: [u8; 64] = m.finalize().into_bytes().into();
    let mut out = u;
    for _ in 1..2048 {
        let mut m = mac();
        m.update(&u);
        u = m.finalize().into_bytes().into();
        out.iter_mut().zip(u).for_each(|(o, x)| *o ^= x);
    }
    out
}

struct Procs(Vec<Child>);

impl Drop for Procs {
    fn drop(&mut self) {
        for c in &mut self.0 {
            let _ = c.kill();
            let _ = c.wait();
        }
    }
}

fn free_port() -> u16 {
    std::net::TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}

async fn wallet_rpc(procs: &mut Procs, dir: &Path, name: &str) -> WalletRpc {
    let port = free_port();
    let wallets = dir.join(name);
    std::fs::create_dir_all(&wallets).unwrap();
    procs.0.push(
        Command::new(WALLET_RPC_BIN)
            .args(["--testnet", "--wallet-dir", wallets.to_str().unwrap(), "--daemon-address", XMZ_NODE])
            .args(["--trusted-daemon", "--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", &port.to_string(), "--non-interactive"])
            .args(["--log-file", dir.join(format!("{name}.log")).to_str().unwrap()])
            .current_dir(dir)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap(),
    );
    let login = dir.join(format!("monzero-wallet-rpc.{port}.login"));
    for _ in 0..300 {
        if login.exists() {
            break;
        }
        tokio::time::sleep(Duration::from_millis(200)).await;
    }
    let rpc = WalletRpc::with_login_file(&format!("http://127.0.0.1:{port}"), &login).unwrap();
    for _ in 0..300 {
        if rpc.call("get_version", json!({})).await.is_ok() {
            return rpc;
        }
        tokio::time::sleep(Duration::from_millis(200)).await;
    }
    panic!("wallet RPC {name} never answered");
}

async fn node(path: &str, body: Value) -> Value {
    let client = alloy::transports::http::reqwest::Client::new();
    client.post(format!("http://{XMZ_NODE}/{path}")).json(&body).send().await.unwrap().json().await.unwrap()
}

async fn xmz_height() -> u64 {
    node("json_rpc", json!({"jsonrpc": "2.0", "id": 0, "method": "get_info"})).await["result"]["height"].as_u64().unwrap()
}

async fn bnb(url: &str, who: Address) -> U256 {
    ProviderBuilder::new().connect_http(url.parse().unwrap()).get_balance(who).await.unwrap()
}

async fn chain_time(url: &str) -> u64 {
    let p = ProviderBuilder::new().connect_http(url.parse().unwrap());
    p.get_block_by_number(alloy::eips::BlockNumberOrTag::Latest).await.unwrap().unwrap().header.timestamp
}

fn swarm() -> Swarm<p2p::Behaviour> {
    SwarmBuilder::with_new_identity()
        .with_tokio()
        .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)
        .unwrap()
        .with_behaviour(|_| p2p::behaviour())
        .unwrap()
        .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(30)))
        .build()
}

/// The claimer (taker) asks, the owner (maker) answers: two libp2p nodes over TCP.
async fn exchange_keys(mut maker_session: Session, mut taker_session: Session, now: u64) -> (Session, Session) {
    let (mut maker, mut taker) = (swarm(), swarm());
    let maker_id = *maker.local_peer_id();
    maker.listen_on("/ip4/127.0.0.1/tcp/0".parse().unwrap()).unwrap();
    let address = loop {
        if let SwarmEvent::NewListenAddr { address, .. } = maker.select_next_some().await {
            break address;
        }
    };
    taker.dial(address).unwrap();
    loop {
        tokio::select! {
            e = taker.select_next_some() => if let SwarmEvent::ConnectionEstablished { peer_id, .. } = e { if peer_id == maker_id { break; } },
            _ = maker.select_next_some() => {}
        }
    }
    taker.behaviour_mut().send_request(&maker_id, taker_session.request());
    loop {
        tokio::select! {
            e = maker.select_next_some() => if let SwarmEvent::Behaviour(Event::Message { message: Message::Request { request, channel, .. }, .. }) = e {
                let response = maker_session.respond(&request, now);
                maker.behaviour_mut().send_response(channel, response).unwrap();
            },
            e = taker.select_next_some() => match e {
                SwarmEvent::Behaviour(Event::Message { message: Message::Response { response, .. }, .. }) =>
                { taker_session.on_response(&response, now).expect("the owner's answer is accepted"); break; }
                SwarmEvent::Behaviour(Event::OutboundFailure { error, .. }) => panic!("key exchange failed: {error}"),
                _ => {}
            }
        }
    }
    (maker_session, taker_session)
}

fn tx_hashes(journal: &FileJournal) -> BTreeMap<String, Vec<String>> {
    let record = journal.load().unwrap();
    record.retained.iter().map(|(k, v)| (k.clone(), v.iter().map(|s| keccak256(&s.raw).to_string()).collect())).collect()
}

/// The run's own notes (mode, balances before). The trade itself lives in each trader's
/// journal (`Driver::start`), as in the app.
#[derive(serde::Serialize, serde::Deserialize)]
struct RunInfo {
    mode: String,
    owner_bnb_before: String,
    claimer_bnb_before: String,
}

#[tokio::main]
async fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (mode, dir) = match args.as_slice() {
        [_, m, d] if ["swap", "refund", "resume"].contains(&m.as_str()) => (m.clone(), PathBuf::from(d)),
        _ => panic!("usage: bsc_testnet swap|refund|resume <run-dir>"),
    };
    let resume = mode == "resume";
    std::fs::create_dir_all(&dir).unwrap();
    if !resume {
        assert!(std::fs::read_dir(&dir).unwrap().next().is_none(), "run dir must be empty");
    }

    // Test accounts, rebuilt from the phrase by the app's derivation
    let home = std::env::var("HOME").unwrap();
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(Path::new(&home).join(ACCOUNTS)).unwrap()).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().unwrap());
    // ACCOUNTS_BASE=n uses accounts n (owner) and n+1 (claimer), so runs can overlap safely
    let base: u32 = std::env::var("ACCOUNTS_BASE").map_or(0, |v| v.parse().unwrap());
    let (owner_key, claimer_key): (PrivateKeySigner, PrivateKeySigner) = (evm_account(&seed, base).unwrap(), evm_account(&seed, base + 1).unwrap());
    if base == 0 {
        assert_eq!(owner_key.address().to_string(), accounts["owner"]["address"].as_str().unwrap(), "owner account derivation");
        assert_eq!(claimer_key.address().to_string(), accounts["claimer"]["address"].as_str().unwrap(), "claimer account derivation");
    }
    let deployment: Value =
        serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../deployments/bsc-testnet.json")).unwrap()).unwrap();
    let contract: Address = deployment["contract"].as_str().unwrap().parse().unwrap();
    // Each trader's own wallet seed (test: two labels on one test phrase)
    let owner_seed = SwapSeed::from_wallet_seed(&[&seed[..], b"owner wallet"].concat());
    let claimer_seed = SwapSeed::from_wallet_seed(&[&seed[..], b"claimer wallet"].concat());
    log(format!("{mode}; contract {contract}; owner {}, claimer {}", owner_key.address(), claimer_key.address()));

    // Both endpoints must serve contract logs before anything moves
    for (url, chunk) in [(OWNER_RPC, OWNER_LOG_CHUNK), (claimer_rpc().as_str(), CLAIMER_LOG_CHUNK)] {
        AlloyChain::new(url, Chain::BscTestnet, contract, 0, owner_key.clone()).unwrap().with_log_chunk(chunk).check_logs().await.unwrap();
    }
    log("both BNB endpoints serve contract logs");

    let mut procs = Procs(vec![]);
    let owner_main = wallet_rpc(&mut procs, &dir, "owner-main").await;
    let owner_swap = wallet_rpc(&mut procs, &dir, "owner-swap").await;
    let claimer_main = wallet_rpc(&mut procs, &dir, "claimer-main").await;
    let claimer_swap = wallet_rpc(&mut procs, &dir, "claimer-swap").await;
    let funds = WalletRpc::new(XMZ_FUNDS_RPC, None);
    let addr = |v: Value| v["address"].as_str().unwrap().to_string();
    let funds_address = addr(funds.call("get_address", json!({})).await.unwrap());
    let mine = || node("start_mining", json!({"miner_address": funds_address, "threads_count": 8, "do_background_mining": false, "ignore_battery": true}));

    let run: RunInfo = if resume {
        for (rpc, name) in [(&owner_main, "owner"), (&claimer_main, "claimer")] {
            rpc.call("open_wallet", json!({"filename": name, "password": ""})).await.unwrap();
        }
        log(format!("XMZ testnet mining: {}", mine().await["status"]));
        serde_json::from_str(&std::fs::read_to_string(dir.join("run.json")).unwrap()).unwrap()
    } else {
        let refund = mode == "refund";
        // The claimer pays its own gas (no relayer): give it a little tBNB if it has none
        if bnb(OWNER_RPC, claimer_key.address()).await < U256::from(5_000_000_000_000_000u64) {
            let p = ProviderBuilder::new().wallet(owner_key.clone()).connect_http(OWNER_RPC.parse().unwrap());
            let tx = alloy::rpc::types::TransactionRequest::default().to(claimer_key.address()).value(U256::from(20_000_000_000_000_000u64));
            let receipt = p.send_transaction(tx).await.unwrap().get_receipt().await.unwrap();
            log(format!("gas money for the claimer: 0.02 tBNB in {}", receipt.transaction_hash));
        }
        let owner_bnb_before = bnb(OWNER_RPC, owner_key.address()).await.to_string();
        let claimer_bnb_before = bnb(OWNER_RPC, claimer_key.address()).await.to_string();
        for (rpc, name) in [(&owner_main, "owner"), (&claimer_main, "claimer")] {
            rpc.call("create_wallet", json!({"filename": name, "password": "", "language": "English"})).await.unwrap();
        }
        let claimer_xmz = addr(claimer_main.call("get_address", json!({})).await.unwrap());
        let paid = funds
            .call("transfer", json!({"destinations": [{"amount": 3_000_000_000_000u64, "address": claimer_xmz}], "priority": 0}))
            .await
            .unwrap();
        log(format!("claimer's XMZ wallet funded with 30 tXMZ: {}", paid["tx_hash"].as_str().unwrap()));
        log(format!("XMZ testnet mining: {}", mine().await["status"]));
        loop {
            claimer_main.call("refresh", json!({})).await.unwrap();
            let unlocked = claimer_main.call("get_balance", json!({})).await.unwrap()["unlocked_balance"].as_u64().unwrap();
            if unlocked >= 3_000_000_000_000 {
                break;
            }
            tokio::time::sleep(Duration::from_secs(20)).await;
        }
        log(format!("claimer's tXMZ unlocked at XMZ height {}", xmz_height().await));

        let now = chain_time(OWNER_RPC).await;
        let mut trade_id = [0u8; 32];
        rand::thread_rng().fill_bytes(&mut trade_id);
        let (duration1, duration2) = if refund { (40 * 60, 20 * 60) } else { (2 * 3600, 2 * 3600) };
        let offer = Offer {
            trade_id: B256::from(trade_id),
            chain_id: Chain::BscTestnet.id(),
            contract,
            owner: owner_key.address(),
            claimer: claimer_key.address(),
            asset: Address::ZERO,
            value: U256::from(VALUE_WEI),
            duration1,
            duration2,
            xmz_amount: XMZ_AMOUNT,
            xmz_network: XmzNetwork::Testnet,
            expires_at: now + 3600,
        };
        let maker = Session::start(&owner_seed, offer.clone(), Role::Owner, &owner_key, now).unwrap();
        let taker = Session::start(&claimer_seed, offer.clone(), Role::Claimer, &claimer_key, now).unwrap();
        let (maker, taker) = exchange_keys(maker, taker, now).await;
        assert_eq!(maker.agreed().unwrap().transcript, taker.agreed().unwrap().transcript);
        log(format!("keys agreed over libp2p; shared XMZ address {}", maker.agreed().unwrap().shared.address()));
        let timing = if refund {
            Timing { xmz_confirmations: 10, xmz_lock_budget: 25 * 60, owner_refund_margin: 5 * 60, claim_margin: 5 * 60 }
        } else {
            Timing::BSC
        };
        let from_block = ProviderBuilder::new().connect_http(OWNER_RPC.parse().unwrap()).get_block_number().await.unwrap();
        let restore_height = xmz_height().await - 1;
        // Each trader saves the agreed trade in its journal before anything is locked
        for (session, who) in [(&maker, "owner"), (&taker, "claimer")] {
            let saved = session.saved(timing, now + 1800, from_block, restore_height).unwrap();
            let mut journal = FileJournal(dir.join(format!("{who}-journal.json")));
            let state = if who == "owner" { RoleState::Owner(OwnerState::Start) } else { RoleState::Claimer(ClaimerState::WaitingForEvmLock) };
            journal.save(&Record { trade: Some(saved), ..Record::new(state) }).unwrap();
        }
        let run = RunInfo { mode: mode.clone(), owner_bnb_before, claimer_bnb_before };
        std::fs::write(dir.join("run.json"), serde_json::to_string_pretty(&run).unwrap()).unwrap();
        run
    };
    let refund = run.mode == "refund";
    let owner_xmz = addr(owner_main.call("get_address", json!({})).await.unwrap());

    // Both traders are rebuilt from their journal and seed alone, as the app does
    let saved = |who: &str| -> SavedTrade { FileJournal(dir.join(format!("{who}-journal.json"))).load().unwrap().trade.expect("saved trade") };
    let (owner_saved, claimer_saved) = (saved("owner"), saved("claimer"));
    let offer = owner_saved.offer.clone();
    let at_owner = owner_saved.agreed(&owner_seed, &owner_key).unwrap();
    let cfg = |saved: &SavedTrade, agreed: &Agreed, who: &str, seed: &SwapSeed| WalletRpcConfig {
        shared: agreed.shared.clone(),
        restore_height: saved.xmz_restore_height,
        wallet_name: format!("{who}-shared"),
        wallet_password: seed.wallet_password(&saved.offer, saved.role).to_string(),
        sweep_confirmations: 10,
        record: dir.join(format!("{who}-xmz.json")),
    };
    let mut owner: Real = Driver::resume(
        &owner_seed,
        &owner_key,
        AlloyChain::new(OWNER_RPC, Chain::BscTestnet, contract, owner_saved.evm_from_block, owner_key.clone()).unwrap().with_log_chunk(OWNER_LOG_CHUNK),
        WalletRpcXmz::new(cfg(&owner_saved, &at_owner, "owner", &owner_seed), owner_main, owner_swap),
        FileJournal(dir.join("owner-journal.json")),
    )
    .unwrap();
    let at_claimer = claimer_saved.agreed(&claimer_seed, &claimer_key).unwrap();
    let mut claimer: Real = Driver::resume(
        &claimer_seed,
        &claimer_key,
        AlloyChain::new(&claimer_rpc(), Chain::BscTestnet, contract, claimer_saved.evm_from_block, claimer_key.clone()).unwrap().with_log_chunk(CLAIMER_LOG_CHUNK),
        WalletRpcXmz::new(cfg(&claimer_saved, &at_claimer, "claimer", &claimer_seed), claimer_main, claimer_swap),
        FileJournal(dir.join("claimer-journal.json")),
    )
    .unwrap();

    // The runner plays the user: approve locking funds for exactly this trade (idempotent)
    owner.approve(owner_saved.transcript).unwrap();
    claimer.approve(claimer_saved.transcript).unwrap();

    // Run both traders
    let (mut o_last, mut c_last) = (String::new(), String::new());
    let (mut o_done, mut c_done, mut errors) = (false, false, 0);
    let mut claimer_offline = false;
    while !(o_done && c_done) {
        if !o_done {
            match owner.tick().await {
                Ok(s) => {
                    o_done = s.is_done();
                    let l = label(&s);
                    if l != o_last {
                        log(format!("owner   -> {l}"));
                        o_last = l;
                    }
                    if refund && matches!(s, RoleState::Owner(OwnerState::Ready { .. })) && !claimer_offline {
                        claimer_offline = true;
                        log("claimer goes OFFLINE until the owner has refunded");
                    }
                }
                Err(e) => {
                    errors += 1;
                    log(format!("owner tick error ({errors}): {e}"));
                }
            }
        }
        if !c_done && (!claimer_offline || o_done) {
            match claimer.tick().await {
                Ok(s) => {
                    c_done = s.is_done();
                    let l = label(&s);
                    if l != c_last {
                        log(format!("claimer -> {l}"));
                        c_last = l;
                    }
                }
                Err(e) => {
                    errors += 1;
                    log(format!("claimer tick error ({errors}): {e}"));
                }
            }
        }
        // Errors are retried; a long run of them stops the runner, which can be resumed
        assert!(errors < 200, "too many errors: resume later with `resume {}`", dir.display());
        tokio::time::sleep(TICK).await;
    }
    node("stop_mining", json!({})).await;
    log("XMZ testnet mining stopped");

    // Results
    let record = |who: &str| -> Value { serde_json::from_str(&std::fs::read_to_string(dir.join(format!("{who}-xmz.json"))).unwrap_or("null".into())).unwrap() };
    let summary = json!({
        "mode": run.mode,
        "contract": contract.to_string(),
        "tradeId": offer.trade_id.to_string(),
        "sharedXmzAddress": at_owner.shared.address(),
        "ownerXmzAddress": owner_xmz,
        "owner": { "final": o_last, "bnbTxs": tx_hashes(owner.journal()), "xmz": record("owner")["sweep"].clone() },
        "claimer": { "final": c_last, "bnbTxs": tx_hashes(claimer.journal()), "xmzLock": record("claimer")["lock"]["tx_hash"].clone(), "xmzSweep": record("claimer")["sweep"].clone() },
        "bnb": {
            "ownerBefore": run.owner_bnb_before, "ownerAfter": bnb(OWNER_RPC, owner_key.address()).await.to_string(),
            "claimerBefore": run.claimer_bnb_before, "claimerAfter": bnb(OWNER_RPC, claimer_key.address()).await.to_string(),
        },
    });
    std::fs::write(dir.join("summary.json"), serde_json::to_string_pretty(&strip_metadata(summary.clone())).unwrap()).unwrap();
    println!("{}", serde_json::to_string_pretty(&strip_metadata(summary)).unwrap());
}

/// XMZ records hold full transaction metadata: keep only the hashes in the summary
fn strip_metadata(v: Value) -> Value {
    match v {
        Value::Object(m) => Value::Object(m.into_iter().filter(|(k, _)| k != "metadata").map(|(k, v)| (k, strip_metadata(v))).collect()),
        Value::Array(a) => Value::Array(a.into_iter().map(strip_metadata).collect()),
        other => other,
    }
}
