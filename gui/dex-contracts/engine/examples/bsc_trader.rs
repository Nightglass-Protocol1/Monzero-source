//! One trader of an XMZ <-> BNB swap on BNB testnet, as its own process (one per machine):
//!
//!   bsc_trader owner new <dir> <listen-port>           writes <dir>/offer.json and peer.txt,
//!                                                      answers the key exchange, then trades
//!   bsc_trader claimer new <dir> <offer.json> <peer>   reads the offer, asks for the keys
//!                                                      (peer = /ip4/.../tcp/.../p2p/...), trades
//!   bsc_trader <owner|claimer> resume <dir>            after a crash or power cut
//!
//! Environment: ACCOUNTS_FILE (test phrase), ACCOUNTS_BASE (owner = base, claimer = base + 1),
//! WALLET_RPC_BIN, XMZ_NODE (host:port of an XMZ testnet node), EVM_RPC, SWAP_SEED_PLAIN, SAVE_ONLY. The claimer's XMZ
//! funds wallet must be paid by someone else: its address is written to <dir>/xmz-address.txt
//! and the claimer waits until 30 tXMZ are unlocked there.
//! The trade is saved in <dir>/journal.json before anything is locked (Driver::start); the
//! process plays the user and approves it. Prints state names only.

use alloy::primitives::{keccak256, Address, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::signers::local::PrivateKeySigner;
use futures::StreamExt;
use hmac::{Hmac, Mac};
use libp2p::request_response::{Event, Message};
use libp2p::swarm::SwarmEvent;
use libp2p::{noise, tcp, yamux, Multiaddr, Swarm, SwarmBuilder};
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::driver::{Driver, FileJournal, Journal, RoleState};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::exchange::{p2p, KeyResponse, SavedTrade, Session};
use monzero_dex_evm::machine::{ClaimerState, OwnerState, Timing};
use monzero_dex_evm::negotiation::{Offer, Role};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::xmz::{WalletRpc, WalletRpcConfig, WalletRpcXmz, XmzNetwork};
use monzero_dex_evm::Chain;
use rand::RngCore;
use serde_json::{json, Value};
use sha2::Sha512;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::Duration;

const VALUE_WEI: u64 = 20_000_000_000_000_000; // 0.02 tBNB
const XMZ_AMOUNT: u64 = 2_000_000_000_000; // 20 tXMZ (11 decimals)
const FUNDING: u64 = 3_000_000_000_000; // what the claimer waits for
const TICK: Duration = Duration::from_secs(15);

fn env(name: &str, default: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| default.into())
}

fn log(msg: impl std::fmt::Display) {
    let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    println!("{:02}:{:02}:{:02} {msg}", t / 3600 % 24, t / 60 % 60, t % 60);
}

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
        Command::new(env("WALLET_RPC_BIN", "monzero-wallet-rpc"))
            .args(["--testnet", "--wallet-dir", wallets.to_str().unwrap(), "--daemon-address", &env("XMZ_NODE", "127.0.0.1:16175")])
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

async fn xmz_height() -> u64 {
    let client = alloy::transports::http::reqwest::Client::new();
    let v: Value = client
        .post(format!("http://{}/json_rpc", env("XMZ_NODE", "127.0.0.1:16175")))
        .json(&json!({"jsonrpc": "2.0", "id": 0, "method": "get_info"}))
        .send()
        .await
        .unwrap()
        .json()
        .await
        .unwrap();
    v["result"]["height"].as_u64().unwrap()
}

fn swarm() -> Swarm<p2p::Behaviour> {
    SwarmBuilder::with_new_identity()
        .with_tokio()
        .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)
        .unwrap()
        .with_behaviour(|_| p2p::behaviour())
        .unwrap()
        .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(60)))
        .build()
}

/// Owner: answer key requests for as long as the process runs. Answers repeat the same frame,
/// so a claimer that lost the first answer (or restarted) simply asks again. The first
/// accepted request hands back the trade to save.
fn serve_keys(
    mut session: Session,
    port: u16,
    dir: PathBuf,
    now: u64,
    save: Option<(tokio::sync::oneshot::Sender<SavedTrade>, u64, u64, u64)>,
) {
    let mut save = save;
    tokio::spawn(async move {
        let mut node = swarm();
        node.listen_on(format!("/ip4/0.0.0.0/tcp/{port}").parse().unwrap()).unwrap();
        let id = *node.local_peer_id();
        let mut announced = false;
        loop {
            match node.select_next_some().await {
                SwarmEvent::NewListenAddr { address, .. } if !announced && !address.to_string().contains("/127.") => {
                    std::fs::write(dir.join("peer.txt"), format!("{address}/p2p/{id}\n")).unwrap();
                    log(format!("answering key requests on {address}/p2p/{id}"));
                    announced = true;
                }
                SwarmEvent::Behaviour(Event::Message { peer, message: Message::Request { request, channel, .. }, .. }) => {
                    let response = session.respond(&request, now);
                    let accepted = matches!(response, KeyResponse::Frame(_));
                    log(format!("key request from {peer}: {}", if accepted { "answered" } else { "refused" }));
                    let _ = node.behaviour_mut().send_response(channel, response);
                    if accepted {
                        if let Some((tx, lock_deadline, from_block, restore_height)) = save.take() {
                            let _ = tx.send(session.saved(Timing::BSC, lock_deadline, from_block, restore_height).unwrap());
                        }
                    }
                }
                _ => {}
            }
        }
    });
}

/// Claimer: dial the owner, ask, take the answer.
async fn ask_keys(mut session: Session, peer: &str, now: u64) -> Session {
    let addr: Multiaddr = peer.parse().expect("peer multiaddr with /p2p/");
    let owner = match addr.iter().last() {
        Some(libp2p::multiaddr::Protocol::P2p(id)) => id,
        _ => panic!("peer address must end with /p2p/<id>"),
    };
    let mut node = swarm();
    node.dial(addr).unwrap();
    loop {
        match node.select_next_some().await {
            SwarmEvent::ConnectionEstablished { peer_id, .. } if peer_id == owner => {
                node.behaviour_mut().send_request(&owner, session.request());
            }
            SwarmEvent::Behaviour(Event::Message { message: Message::Response { response, .. }, .. }) => {
                session.on_response(&response, now).expect("the owner's answer is accepted");
                return session;
            }
            SwarmEvent::Behaviour(Event::OutboundFailure { error, .. }) => panic!("key request failed: {error}"),
            SwarmEvent::OutgoingConnectionError { error, .. } => panic!("cannot reach the owner: {error}"),
            _ => {}
        }
    }
}

#[tokio::main]
async fn main() {
    let args: Vec<String> = std::env::args().collect();
    let role = match args.get(1).map(String::as_str) {
        Some("owner") => Role::Owner,
        Some("claimer") => Role::Claimer,
        _ => panic!("usage: see the file header"),
    };
    let new = match args.get(2).map(String::as_str) {
        Some("new") => true,
        Some("resume") => false,
        _ => panic!("usage: see the file header"),
    };
    let dir = PathBuf::from(args.get(3).expect("run dir"));
    std::fs::create_dir_all(&dir).unwrap();
    let who = if role == Role::Owner { "owner" } else { "claimer" };
    let evm_rpc = env("EVM_RPC", "https://bsc-testnet-rpc.publicnode.com");

    let home = std::env::var("HOME").unwrap();
    let accounts_file = env("ACCOUNTS_FILE", &format!("{home}/.config/monzero-dex/bsc-testnet-accounts.json"));
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(accounts_file).unwrap()).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().unwrap());
    let base: u32 = env("ACCOUNTS_BASE", "0").parse().unwrap();
    let owner_address = evm_account(&seed, base).unwrap().address();
    let claimer_address = evm_account(&seed, base + 1).unwrap().address();
    let key: PrivateKeySigner = evm_account(&seed, if role == Role::Owner { base } else { base + 1 }).unwrap();
    // Each trader's own wallet seed (test: one label per role on the test phrase). With
    // SWAP_SEED_PLAIN=1 the swap seed comes from the wallet seed itself, as the app's helper
    // derives it, so the app can take the trade over (see SAVE_ONLY)
    let swap_seed = if env("SWAP_SEED_PLAIN", "") == "1" {
        SwapSeed::from_wallet_seed(&seed)
    } else {
        SwapSeed::from_wallet_seed(&[&seed[..], format!("{who} wallet").as_bytes()].concat())
    };
    let deployment: Value =
        serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../deployments/bsc-testnet.json")).unwrap()).unwrap();
    let contract: Address = deployment["contract"].as_str().unwrap().parse().unwrap();
    log(format!("{who} {}; account {}; contract {contract}", if new { "new" } else { "resume" }, key.address()));
    AlloyChain::new(&evm_rpc, Chain::BscTestnet, contract, 0, key.clone()).unwrap().check_logs().await.expect("EVM endpoint serves logs");

    let mut procs = Procs(vec![]);
    let main_rpc = wallet_rpc(&mut procs, &dir, "main").await;
    let swap_rpc = wallet_rpc(&mut procs, &dir, "swap").await;
    let journal_path = dir.join("journal.json");

    if new {
        assert!(!journal_path.exists(), "{} already holds a trade: use resume", dir.display());
        if main_rpc.call("create_wallet", json!({"filename": who, "password": "", "language": "English"})).await.is_err() {
            // A rerun after a failed key exchange: the wallet is already there
            main_rpc.call("open_wallet", json!({"filename": who, "password": ""})).await.unwrap();
        }
        let address = main_rpc.call("get_address", json!({})).await.unwrap()["address"].as_str().unwrap().to_string();
        std::fs::write(dir.join("xmz-address.txt"), format!("{address}\n")).unwrap();
        log(format!("XMZ wallet {address}"));
        let provider = ProviderBuilder::new().connect_http(evm_rpc.parse().unwrap());
        let block = provider.get_block_by_number(alloy::eips::BlockNumberOrTag::Latest).await.unwrap().unwrap();
        let now = block.header.timestamp;

        let session: Option<SavedTrade> = if role == Role::Owner {
            let mut trade_id = [0u8; 32];
            rand::thread_rng().fill_bytes(&mut trade_id);
            let offer = Offer {
                trade_id: B256::from(trade_id),
                chain_id: Chain::BscTestnet.id(),
                contract,
                owner: owner_address,
                claimer: claimer_address,
                asset: Address::ZERO,
                value: U256::from(VALUE_WEI),
                duration1: 2 * 3600,
                duration2: 2 * 3600,
                xmz_amount: XMZ_AMOUNT,
                xmz_network: XmzNetwork::Testnet,
                expires_at: now + 2 * 3600,
            };
            // The offer as the order book would carry it (no secrets)
            std::fs::write(dir.join("offer.json"), serde_json::to_string_pretty(&offer).unwrap()).unwrap();
            let port: u16 = args.get(4).expect("listen port").parse().unwrap();
            let session = Session::start(&swap_seed, offer, role, &key, now).unwrap();
            let (tx, rx) = tokio::sync::oneshot::channel();
            let from_block = provider.get_block_number().await.unwrap();
            serve_keys(session, port, dir.clone(), now, Some((tx, now + 1800, from_block, xmz_height().await - 1)));
            let saved = rx.await.unwrap();
            log(format!("keys agreed over libp2p; shared XMZ address {}", saved.agreed(&swap_seed, &key).unwrap().shared.address()));
            start_trade(saved, &swap_seed, &key, &evm_rpc, contract, &dir, main_rpc.clone(), swap_rpc.clone());
            None
        } else {
            let offer: Offer = serde_json::from_str(&std::fs::read_to_string(args.get(4).expect("offer.json")).unwrap()).unwrap();
            assert_eq!((offer.claimer, offer.contract), (key.address(), contract), "offer is for us on our contract");
            // Our XMZ must be spendable before we agree to lock it
            loop {
                main_rpc.call("refresh", json!({})).await.unwrap();
                let unlocked = main_rpc.call("get_balance", json!({})).await.unwrap()["unlocked_balance"].as_u64().unwrap();
                if unlocked >= FUNDING {
                    break;
                }
                tokio::time::sleep(Duration::from_secs(20)).await;
            }
            log("our tXMZ is spendable");
            // Our search for the owner's lock starts before our first key request and is kept
            // for retries: the owner may lock as soon as it has answered any of them
            let start = dir.join("start-block.txt");
            if !start.exists() {
                std::fs::write(&start, format!("{}\n", provider.get_block_number().await.unwrap())).unwrap();
            }
            let from_block: u64 = std::fs::read_to_string(&start).unwrap().trim().parse().unwrap();
            let session = Session::start(&swap_seed, offer, role, &key, now).unwrap();
            let session = ask_keys(session, args.get(5).expect("owner peer address"), now).await;
            log(format!("keys agreed over libp2p; shared XMZ address {}", session.agreed().unwrap().shared.address()));
            Some(session.saved(Timing::BSC, now + 1800, from_block, xmz_height().await - 1).unwrap())
        };
        if let Some(saved) = session {
            start_trade(saved, &swap_seed, &key, &evm_rpc, contract, &dir, main_rpc.clone(), swap_rpc.clone());
        }
    } else {
        main_rpc.call("open_wallet", json!({"filename": who, "password": ""})).await.unwrap();
        // The owner keeps answering key requests after a restart, until the offer expires
        if let (Role::Owner, Some(port)) = (role, args.get(4)) {
            let saved: SavedTrade = FileJournal(journal_path.clone()).load().unwrap().trade.expect("saved trade");
            let provider = ProviderBuilder::new().connect_http(evm_rpc.parse().unwrap());
            let now = provider.get_block_by_number(alloy::eips::BlockNumberOrTag::Latest).await.unwrap().unwrap().header.timestamp;
            if now < saved.offer.expires_at {
                let session = Session::start(&swap_seed, saved.offer.clone(), role, &key, now).unwrap();
                serve_keys(session, port.parse().unwrap(), dir.clone(), now, None);
            }
        }
    }

    if env("SAVE_ONLY", "") == "1" {
        // The app takes over: it shows the trade, asks the user to approve and steps it
        log("trade saved; leaving approval and steps to the app");
        if role == Role::Owner {
            // Keep answering key requests while the claimer finishes its exchange
            tokio::time::sleep(Duration::from_secs(120)).await;
        }
        return;
    }
    let saved: SavedTrade = FileJournal(journal_path.clone()).load().unwrap().trade.expect("saved trade");
    let agreed = saved.agreed(&swap_seed, &key).unwrap();
    let chain = AlloyChain::new(&evm_rpc, Chain::BscTestnet, contract, saved.evm_from_block, key.clone()).unwrap();
    let xmz = WalletRpcXmz::new(config(&saved, &agreed.shared, &dir, &swap_seed), main_rpc, swap_rpc);
    let mut driver = Driver::resume(&swap_seed, &key, chain, xmz, FileJournal(journal_path.clone())).unwrap();
    driver.approve(saved.transcript).unwrap();

    let (mut last, mut errors) = (String::new(), 0);
    loop {
        match driver.tick().await {
            Ok(state) => {
                let l = label(&state);
                if l != last {
                    log(format!("{who} -> {l}"));
                    last = l;
                }
                if state.is_done() {
                    break;
                }
            }
            Err(e) => {
                errors += 1;
                log(format!("{who} tick error ({errors}): {e}"));
                assert!(errors < 200, "too many errors: resume later");
            }
        }
        tokio::time::sleep(TICK).await;
    }
    let record = driver.journal().load().unwrap();
    let txs: serde_json::Map<String, Value> = record
        .retained
        .iter()
        .map(|(k, v)| (k.clone(), Value::from(v.iter().map(|s| keccak256(&s.raw).to_string()).collect::<Vec<_>>())))
        .collect();
    let summary = json!({"role": who, "final": last, "tradeId": saved.offer.trade_id.to_string(), "bnbTxs": txs,
        "sharedXmzAddress": agreed.shared.address()});
    std::fs::write(dir.join("summary.json"), serde_json::to_string_pretty(&summary).unwrap()).unwrap();
    println!("{}", serde_json::to_string_pretty(&summary).unwrap());
}

/// Save the agreed trade in the journal before anything is locked.
#[allow(clippy::too_many_arguments)]
fn start_trade(saved: SavedTrade, seed: &SwapSeed, key: &PrivateKeySigner, evm_rpc: &str, contract: Address, dir: &Path, main: WalletRpc, swap: WalletRpc) {
    let agreed = saved.agreed(seed, key).unwrap();
    let chain = AlloyChain::new(evm_rpc, Chain::BscTestnet, contract, saved.evm_from_block, key.clone()).unwrap();
    let xmz = WalletRpcXmz::new(config(&saved, &agreed.shared, dir, seed), main, swap);
    Driver::start(saved, seed, key, chain, xmz, FileJournal(dir.join("journal.json"))).unwrap();
}

fn config(saved: &SavedTrade, shared: &monzero_dex_evm::xmz::SharedXmz, dir: &Path, seed: &SwapSeed) -> WalletRpcConfig {
    WalletRpcConfig {
        shared: shared.clone(),
        restore_height: saved.xmz_restore_height,
        wallet_name: "shared".into(),
        wallet_password: seed.wallet_password(&saved.offer, saved.role).to_string(),
        sweep_confirmations: 10,
        record: dir.join("xmz.json"),
    }
}
