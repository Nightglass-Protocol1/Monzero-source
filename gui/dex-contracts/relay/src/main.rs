//! monzero-dex-relay <config.json>
//!
//! {
//!   "listen": "127.0.0.1:8790",
//!   "dataDir": "/var/lib/monzero-dex-relay",
//!   "trustProxy": true,
//!   "chains": [
//!     { "chainId": 1,  "rpc": "https://ethereum-rpc.publicnode.com", "contract": "0xF2b4…D187", "maxValueWei": "20000000000000000" },
//!     { "chainId": 56, "rpc": "https://bsc-rpc.publicnode.com",      "contract": "0x9073…0feb", "maxValueWei": "100000000000000000" }
//!   ],
//!   "peers": ["https://relay2.example.org"]
//! }
//!
//! Each contract must hold the XmzSwap runtime code the app pins. State is saved to
//! `dataDir/relay.json` every minute and on shutdown.

use alloy::primitives::{keccak256, Address, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use monzero_dex_evm::listing::{Rules, SignedListing, SignedSolAccount};
use monzero_dex_evm::Chain;
use monzero_dex_relay::chain::{AlloyView, View};
use monzero_dex_relay::http::{now, router, Api};
use monzero_dex_relay::mesh::{Announcer, Fetcher, Mesh};
use monzero_dex_relay::{Print, Relay, Snapshot, ARCHIVE_SECS};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

/// keccak256 of the XmzSwap runtime code deployed on BNB and Ethereum
const RUNTIME_KECCAK: &str = "0xe2368d8f82649ecd46a19e882e70c9535752dbc9f4c9499a22af141877f21f1a";

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct ChainConfig {
    chain_id: u64,
    rpc: String,
    /// EVM chains: the XmzSwap contract
    #[serde(default)]
    contract: Option<Address>,
    /// Solana clusters: the swap program (base58). It must be the one the app pins for the cluster.
    #[serde(default)]
    sol_program: Option<String>,
    max_value_wei: Option<U256>,
    /// A cap for offers in the chain's token (USDT; base units: 18 places on BNB Smart Chain, 6 on
    /// Solana). maxValueWei is in the chain's own coin and never applies to tokens.
    #[serde(default)]
    max_token_value: Option<U256>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Config {
    listen: SocketAddr,
    data_dir: PathBuf,
    #[serde(default)]
    trust_proxy: bool,
    chains: Vec<ChainConfig>,
    #[serde(default)]
    peers: Vec<String>,
    /// BTC/XMZ and LTC/XMZ offers for these networks ("btc-mainnet", "ltc-mainnet", "btc-signet", ...)
    #[serde(default)]
    utxo_networks: Vec<String>,
    /// Accept Monero (XMR) offers on the main networks. Leave off until XMR trading is opened;
    /// test networks accept XMR offers either way.
    #[serde(default)]
    xmr_mainnet: bool,
    /// Accept direct (coin <-> coin: ETH, BNB, SOL) offers with real coins. Leave off until direct
    /// pairs are opened; test networks accept them either way.
    #[serde(default)]
    direct_mainnet: bool,
    /// Pay USDT traders' network fees, paid back in USDT (absent: this relay does not)
    #[serde(default)]
    sponsor: Option<SponsorConfig>,
    /// Tor's SOCKS port, to read relays with .onion addresses (peers or announced)
    #[serde(default)]
    tor_socks: Option<SocketAddr>,
    /// Hub: accept relay announcements, list them at /v1/relays and pull their offers
    #[serde(default)]
    mesh: bool,
    /// Box: announce this relay to hubs (absent: it does not)
    #[serde(default)]
    announce: Option<AnnounceConfig>,
    /// The operator's control key (hex): this relay mirrors the signed control documents it
    /// verifies with it (wallets still find a current one when the website is down)
    #[serde(default = "default_control_key")]
    control_key: String,
    /// Where the documents are fetched from every 10 minutes ("": only those posted here)
    #[serde(default = "default_control_source")]
    control_source: String,
}

fn default_control_key() -> String {
    "9eba2b1784ddfc23f028c03c78f862fecc81af81616722b56e671e6662e0a6fb".into()
}

fn default_control_source() -> String {
    "https://monzero.org/dex/control/".into()
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct AnnounceConfig {
    /// A file holding this relay's public address (the onion name, or http://<public ip>:<port>);
    /// absent or empty = nothing announced (public relay off)
    url_file: PathBuf,
    /// This relay's own Ed25519 key (made on the first start)
    key_file: PathBuf,
    /// Hubs to announce to
    to: Vec<String>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct SponsorConfig {
    /// The relay's Solana fee-payer key (solana-keygen JSON, or 32 bytes hex); chain: the
    /// configured Solana cluster
    #[serde(default)]
    sol_key_file: Option<PathBuf>,
    /// The relay's BNB Smart Chain key (hex); chain: the configured BNB Smart Chain (or local)
    #[serde(default)]
    evm_key_file: Option<PathBuf>,
    /// Tests only: the USDT mint of a local Solana validator, the USDT token of a local EVM chain
    #[serde(default)]
    local_sol_usdt_mint: Option<String>,
    #[serde(default)]
    local_usdt_token: Option<Address>,
}

fn fail(msg: impl std::fmt::Display) -> ! {
    eprintln!("monzero-dex-relay: {msg}");
    std::process::exit(1)
}

fn save(relay: &Relay<View>, dir: &Path) {
    let tmp = dir.join("relay.json.tmp");
    let data = serde_json::to_vec(&relay.snapshot()).expect("snapshot serializes");
    if let Err(e) = std::fs::write(&tmp, data).and_then(|_| std::fs::rename(&tmp, dir.join("relay.json"))) {
        eprintln!("saving state: {e}");
    }
}

async fn fetch<T: serde::de::DeserializeOwned>(client: &Fetcher, url: &str) -> Result<T, String> {
    client.get_json(url).await
}

#[derive(Deserialize)]
struct Listings {
    /// One by one: a peer may hold offers of a kind this relay does not know
    listings: Vec<serde_json::Value>,
}

#[derive(Deserialize)]
struct UtxoListings {
    listings: Vec<monzero_dex_relay::utxo::SignedUtxoListing>,
}

#[derive(Deserialize)]
struct SolAccounts {
    accounts: Vec<SignedSolAccount>,
}

#[derive(Deserialize)]
struct Prints {
    prints: Vec<Print>,
}

/// Copies offers and trades from one peer relay, checking each again (signatures; claims on chain)
async fn sync(relay: &Relay<View>, client: &Fetcher, peer: &str) -> Result<(usize, usize), String> {
    let base = peer.trim_end_matches('/');
    let now = now();
    let mut offers = 0;
    // Offers on Solana come with their makers' signed Solana accounts (a peer that predates them has none)
    let mut accounts: std::collections::HashMap<B256, SignedSolAccount> =
        fetch::<SolAccounts>(client, &format!("{base}/v1/sol-accounts")).await.map(|a| a.accounts).unwrap_or_default().into_iter().take(10000).map(|a| (a.note.id, a)).collect();
    let listed = fetch::<Listings>(client, &format!("{base}/v1/listings?partial=1")).await?.listings;
    for signed in listed.into_iter().take(2000).filter_map(|v| serde_json::from_value::<SignedListing>(v).ok()) {
        let account = accounts.get(&signed.listing.id).cloned();
        if relay.listing(signed.listing.id).is_none() && relay.post_with(signed, account, now).await == Ok(true) {
            offers += 1;
        }
    }
    // Direct offers: a peer from before them has none (the request fails quietly)
    if let Ok(d) = fetch::<Listings>(client, &format!("{base}/v1/direct/listings")).await {
        for signed in d.listings.into_iter().take(2000).filter_map(|v| serde_json::from_value::<monzero_dex_evm::direct_listing::SignedDirectListing>(v).ok()) {
            if relay.direct_listing(signed.listing.id).is_none() && relay.post_direct(signed, now).await == Ok(true) {
                offers += 1;
            }
        }
    }
    if !relay.utxo_networks().is_empty() {
        if let Ok(b) = fetch::<UtxoListings>(client, &format!("{base}/v1/utxo/listings")).await {
            for signed in b.listings.into_iter().take(2000) {
                if relay.post_utxo(signed, now) == Ok(true) {
                    offers += 1;
                }
            }
        }
    }
    let mut trades = 0;
    let since = now.saturating_sub(ARCHIVE_SECS);
    for p in fetch::<Prints>(client, &format!("{base}/v1/prints?since={since}")).await?.prints.into_iter().take(2000) {
        if relay.known_print(&p.tx) {
            continue;
        }
        if relay.listing(p.listing).is_none() {
            match fetch::<SignedListing>(client, &format!("{base}/v1/listings/{}", p.listing)).await {
                Ok(l) if l.listing.id == p.listing => {
                    if relay.remember(l, accounts.remove(&p.listing)).is_err() {
                        continue;
                    }
                }
                _ => continue,
            }
        }
        if relay.report(p.chain_id, p.tx.clone(), now).await.is_ok() && relay.known_print(&p.tx) {
            trades += 1;
        }
    }
    Ok((offers, trades))
}

#[tokio::main]
async fn main() {
    let path = std::env::args().nth(1).unwrap_or_else(|| fail("usage: monzero-dex-relay <config.json>"));
    let config: Config = serde_json::from_slice(&std::fs::read(&path).unwrap_or_else(|e| fail(format!("{path}: {e}")))).unwrap_or_else(|e| fail(format!("{path}: {e}")));
    std::fs::create_dir_all(&config.data_dir).unwrap_or_else(|e| fail(format!("{}: {e}", config.data_dir.display())));

    let mut rules = Rules { xmr_mainnet: config.xmr_mainnet, ..Rules::default() };
    if config.xmr_mainnet {
        eprintln!("Monero (XMR) offers on the main networks: accepted");
    }
    let mut chains = BTreeMap::new();
    let expected: B256 = RUNTIME_KECCAK.parse().expect("constant");
    for c in &config.chains {
        let chain = Chain::from_id(c.chain_id).unwrap_or_else(|| fail(format!("unknown chain {}", c.chain_id)));
        if chain.is_solana() {
            use monzero_dex_evm::sol;
            let text = c.sol_program.as_deref().unwrap_or_else(|| fail(format!("chain {}: solProgram is missing", c.chain_id)));
            let program = sol::from_base58(text).unwrap_or_else(|e| fail(format!("chain {}: {e}", c.chain_id)));
            // A real cluster serves the program the app pins, nothing else; a local validator, the given one
            match chain {
                Chain::SolLocal => sol::set_local_program(program),
                _ if sol::program_of(chain) != Some(program) => fail(format!("chain {}: {text} is not the pinned swap program", c.chain_id)),
                _ => {}
            }
            sol::check_program(&c.rpc, &program).await.unwrap_or_else(|e| fail(format!("chain {}: {e}", c.chain_id)));
            let contract = sol::contract_address(&program);
            rules.contracts.insert(c.chain_id, contract);
            if let Some(max) = c.max_value_wei {
                rules.max_value.insert(c.chain_id, max);
            }
            if let Some(max) = c.max_token_value {
                rules.max_token_value.insert(c.chain_id, max);
            }
            chains.insert(c.chain_id, View::Solana(monzero_dex_relay::chain::SolView::new(&c.rpc, program)));
            eprintln!("chain {} ({}): program {text} ok (offers stand for it as {contract})", c.chain_id, chain.pair());
            continue;
        }
        let contract = c.contract.unwrap_or_else(|| fail(format!("chain {}: contract is missing", c.chain_id)));
        let view = AlloyView::new(&c.rpc, chain, contract).unwrap_or_else(|e| fail(e));
        view.check().await.unwrap_or_else(|e| fail(format!("chain {}: {e}", c.chain_id)));
        if chain != Chain::Local {
            let code = ProviderBuilder::new()
                .connect_http(c.rpc.parse().unwrap_or_else(|e| fail(format!("{}: {e}", c.rpc))))
                .get_code_at(contract)
                .await
                .unwrap_or_else(|e| fail(format!("chain {}: {e}", c.chain_id)));
            if keccak256(&code) != expected {
                fail(format!("chain {}: {} is not the pinned XmzSwap code", c.chain_id, contract));
            }
        }
        rules.contracts.insert(c.chain_id, contract);
        if let Some(max) = c.max_value_wei {
            rules.max_value.insert(c.chain_id, max);
        }
        if let Some(max) = c.max_token_value {
            rules.max_token_value.insert(c.chain_id, max);
        }
        chains.insert(c.chain_id, View::Evm(view));
        eprintln!("chain {} ({}): contract {} ok", c.chain_id, chain.pair(), contract);
    }

    for n in &config.utxo_networks {
        if monzero_dex_relay::utxo::pair_of(n).is_none() {
            fail(format!("unknown BTC/LTC network {n}"));
        }
    }
    if config.direct_mainnet {
        eprintln!("direct (coin <-> coin) offers on the main networks: accepted");
    }
    let relay = Arc::new(Relay::new(rules, chains).with_utxo_networks(config.utxo_networks.clone()).with_direct_mainnet(config.direct_mainnet));
    if let Ok(bytes) = std::fs::read(config.data_dir.join("relay.json")) {
        match serde_json::from_slice::<Snapshot>(&bytes) {
            Ok(snap) => relay.restore(snap),
            Err(e) => eprintln!("ignoring saved state: {e}"),
        }
    }

    // Upkeep: close expired offers, check pending claims again, save
    {
        let (relay, dir) = (relay.clone(), config.data_dir.clone());
        tokio::spawn(async move {
            let mut tick = tokio::time::interval(Duration::from_secs(60));
            loop {
                tick.tick().await;
                relay.prune(now());
                relay.recheck(now()).await;
                save(&relay, &dir);
            }
        });
    }
    let fetcher = Fetcher::new(config.tor_socks);
    // The swap contracts this relay serves: an announced relay must serve the same ones
    let contracts: BTreeMap<u64, String> = relay.rules().contracts.iter().map(|(id, a)| (*id, format!("{a:#x}"))).collect();
    let mesh = config.mesh.then(|| Arc::new(Mesh::new(fetcher.clone(), contracts, config.peers.clone(), config.trust_proxy)));
    if mesh.is_some() {
        eprintln!("mesh: accepting relay announcements{}", if config.tor_socks.is_some() { " (onions through Tor)" } else { " (no Tor: onions are refused)" });
    }
    if !config.peers.is_empty() || mesh.is_some() {
        let (relay, peers, mesh, client) = (relay.clone(), config.peers.clone(), mesh.clone(), fetcher.clone());
        tokio::spawn(async move {
            let mut tick = tokio::time::interval(Duration::from_secs(30));
            let mut round = 0usize;
            loop {
                tick.tick().await;
                for peer in &peers {
                    match sync(&relay, &client, peer).await {
                        Ok((0, 0)) => {}
                        Ok((o, t)) => eprintln!("{peer}: {o} new offers, {t} new trades"),
                        Err(e) => eprintln!("{peer}: {e}"),
                    }
                }
                // Announced relays: a few per round, each about every five minutes
                if let Some(mesh) = &mesh {
                    mesh.prune(now());
                    let listed = mesh.listed(now());
                    let per_round = listed.len().div_ceil(10).max(1);
                    for r in listed.iter().skip(round % 10 * per_round).take(per_round) {
                        match sync(&relay, &client, &r.url).await {
                            Ok((0, 0)) => {}
                            Ok((o, t)) => eprintln!("mesh {}: {o} new offers, {t} new trades", r.url),
                            Err(e) => eprintln!("mesh {}: {e}", r.url),
                        }
                    }
                    round += 1;
                }
            }
        });
    }
    if let Some(a) = &config.announce {
        let announcer = Announcer::load(&a.key_file, a.url_file.clone(), a.to.clone()).unwrap_or_else(|e| fail(e));
        eprintln!("announce: relay key {}", hex::encode(announcer.key.verifying_key().to_bytes()));
        tokio::spawn(async move {
            let client = reqwest::Client::builder().timeout(Duration::from_secs(90)).user_agent("monzero-dex-relay").build().expect("client");
            // Look every minute; announce as soon as there is an address (Tor makes the onion a
            // little after start) or it changed, then every ten minutes
            let mut tick = tokio::time::interval(Duration::from_secs(60));
            let mut last = Vec::new();
            let (mut last_url, mut last_time) = (None, 0u64);
            loop {
                tick.tick().await;
                let url = announcer.url();
                if url.is_none() || (url == last_url && now() < last_time + 600) {
                    continue;
                }
                last_url = url;
                last_time = now();
                let results = announcer.announce_once(&client).await;
                if results != last {
                    for r in &results {
                        eprintln!("announce: {r}");
                    }
                    last = results;
                }
            }
        });
    }

    let api = Arc::new(Api::new(relay.clone(), config.peers.clone(), config.trust_proxy));
    let mut app = router(api);
    // The signed control documents, mirrored (see monzero_dex_relay::control)
    let control = monzero_dex_relay::control::ControlMirror::new(&config.control_key, config.data_dir.join("control")).unwrap_or_else(|e| fail(e));
    if !config.control_source.is_empty() {
        let (keep, source) = (control.clone(), config.control_source.clone());
        tokio::spawn(async move {
            let client = reqwest::Client::builder().timeout(Duration::from_secs(20)).user_agent("monzero-dex-relay").build().expect("client");
            let mut tick = tokio::time::interval(Duration::from_secs(600));
            loop {
                tick.tick().await;
                keep.refresh_from(&client, &source).await;
            }
        });
    }
    app = app.merge(monzero_dex_relay::control::router(control));
    if let Some(mesh) = &mesh {
        app = app.merge(monzero_dex_relay::mesh::router(mesh.clone()));
    }
    if let Some(sc) = &config.sponsor {
        use monzero_dex_evm::sol;
        use monzero_dex_relay::sponsor::{EvmSponsor, SolSponsor, Sponsor};
        if let Some(m) = &sc.local_sol_usdt_mint {
            sol::set_local_usdt_mint(sol::from_base58(m).unwrap_or_else(|e| fail(e)));
        }
        if let Some(t) = sc.local_usdt_token {
            monzero_dex_evm::chain::set_local_usdt_token(t);
        }
        let read = |p: &PathBuf| std::fs::read_to_string(p).unwrap_or_else(|e| fail(format!("{}: {e}", p.display())));
        // A key file not created yet: this relay does not sponsor that chain (it still relays)
        let present = |p: &PathBuf| p.exists() || { eprintln!("sponsor: {} not there; not sponsoring with it", p.display()); false };
        let mut sol_sponsor = None;
        if let Some(path) = sc.sol_key_file.as_ref().filter(|p| present(p)) {
            let c = config.chains.iter().find(|c| Chain::from_id(c.chain_id).is_some_and(|k| k.is_solana())).unwrap_or_else(|| fail("sponsor: no Solana chain configured"));
            let chain = Chain::from_id(c.chain_id).expect("checked");
            let program = sol::from_base58(c.sol_program.as_deref().unwrap_or_default()).unwrap_or_else(|e| fail(e));
            let mint = sol::usdt_mint(chain).unwrap_or_else(|| fail("sponsor: no USDT mint on this cluster"));
            let key = sol::relayer_key(&read(path)).unwrap_or_else(|e| fail(format!("{}: {e}", path.display())));
            sol::open_own_token_account(&c.rpc, &key, &mint).await.unwrap_or_else(|e| eprintln!("sponsor: own USDT account not opened yet: {e}"));
            eprintln!("sponsor: Solana fee payer {}", sol::to_base58(&key.verifying_key().to_bytes()));
            sol_sponsor = Some(SolSponsor { chain, rpc: c.rpc.clone(), program, mint, key });
        }
        let mut evm_sponsor = None;
        if let Some(path) = sc.evm_key_file.as_ref().filter(|p| present(p)) {
            let c = config.chains.iter().find(|c| matches!(Chain::from_id(c.chain_id), Some(Chain::BscMainnet | Chain::BscTestnet | Chain::Local)))
                .unwrap_or_else(|| fail("sponsor: no BNB Smart Chain configured"));
            let chain = Chain::from_id(c.chain_id).expect("checked");
            let token = monzero_dex_evm::chain::usdt_token(chain).unwrap_or_else(|| fail("sponsor: no USDT token on this chain"));
            let signer: alloy::signers::local::PrivateKeySigner = read(path).trim().parse().unwrap_or_else(|e| fail(format!("{}: {e}", path.display())));
            let provider = ProviderBuilder::new().connect_http(c.rpc.parse().unwrap_or_else(|e| fail(format!("{}: {e}", c.rpc)))).erased();
            eprintln!("sponsor: BNB Smart Chain account {}", signer.address());
            evm_sponsor = Some(EvmSponsor { chain, contract: c.contract.unwrap_or_else(|| fail("sponsor: contract missing")), token, provider, signer });
        }
        let s = Sponsor::new(relay.clone(), sol_sponsor, evm_sponsor, config.trust_proxy);
        if s.enabled() {
            let keep = s.clone();
            tokio::spawn(async move {
                let mut tick = tokio::time::interval(Duration::from_secs(300));
                loop {
                    tick.tick().await;
                    keep.refresh().await;
                }
            });
            app = app.merge(monzero_dex_relay::sponsor::router(s));
        }
    }
    let listener = tokio::net::TcpListener::bind(config.listen).await.unwrap_or_else(|e| fail(format!("{}: {e}", config.listen)));
    eprintln!("listening on {}", config.listen);
    let shutdown = async {
        let _ = tokio::signal::ctrl_c().await;
    };
    #[cfg(unix)]
    let shutdown = async {
        let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()).expect("signal handler");
        tokio::select! { _ = shutdown => {}, _ = term.recv() => {} }
    };
    axum::serve(listener, app.into_make_service_with_connect_info::<SocketAddr>())
        .with_graceful_shutdown(shutdown)
        .await
        .unwrap_or_else(|e| fail(e));
    save(&relay, &config.data_dir);
}
