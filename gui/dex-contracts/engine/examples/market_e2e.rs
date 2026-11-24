//! A whole public-offer trade through the helper's JSON commands, as the app runs them:
//! the maker posts an offer to a relay, the taker finds it and takes it through sealed relay
//! mail, both step the swap to the end, the claimer reports the claim and the relay's chart
//! gets its first candle. Test networks only (Sepolia + XMZ testnet).
//!
//! With COIN=xmr the private coin is Monero on its stagenet (pair ETH/XMR): WALLET_RPC_BIN is
//! the official `monero-wallet-rpc`, XMZ_NODE a Monero stagenet node (host:port), FUNDER_RPC a
//! stagenet wallet RPC holding test XMR, XMZ_ATOMS in XMR atoms (default 0.01 XMR), and
//! WITNESSES optional extra stagenet nodes (comma-separated daemon RPC URLs).
//!
//!   RELAY=http://127.0.0.1:18790 cargo run --example market_e2e -- <run dir>
//!
//! Real funds (owner's test, real-funds/run-xmr-bnb.sh): NET=mainnet with COIN=xmr trades on
//! the main networks (CHAIN_ID 56: BNB/XMR), sends xmrMainnet in every request, and
//! FUNDER_RPC=none: the run prints the addresses to fund and waits for the coins instead of a
//! funding wallet. TRUSTED_DAEMON=0 when the wallets use a public node.
//!
//! Env: ACCOUNTS_FILE (test phrase; default ~/.config/monzero-dex/eth-sepolia-accounts.json),
//! OWNER_INDEX (1) / CLAIMER_INDEX (0), EVM_RPC (Sepolia publicnode), XMZ_NODE (127.0.0.1:16175),
//! FUNDER_RPC (a testnet wallet RPC that pays the claimer 20 tXMZ; 127.0.0.1:16183),
//! WALLET_RPC_BIN, VALUE_WEI (10^16), XMZ_ATOMS (10 tXMZ).

use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::helper::handle;
use monzero_dex_evm::xmz::WalletRpc;
use serde_json::{json, Value};
use sha2::Sha512;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::Duration;

fn env(name: &str, default: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| default.into())
}

/// The private coin of this run: XMR (Monero stagenet) or XMZ (testnet)
fn monero() -> bool {
    env("COIN", "xmz") == "xmr"
}

/// The private coin, as the helper names it
fn private_coin() -> monzero_dex_evm::xmz::PrivateCoin {
    if monero() { monzero_dex_evm::xmz::PrivateCoin::Xmr } else { monzero_dex_evm::xmz::PrivateCoin::Xmz }
}

/// Real coins on the main networks (the owner's real-funds test only)
fn mainnet() -> bool {
    env("NET", "test") == "mainnet"
}

fn chain_id() -> u64 {
    env("CHAIN_ID", if mainnet() { "56" } else { "11155111" }).parse().expect("CHAIN_ID")
}

/// A Solana cluster (CHAIN_ID 501000101 mainnet, or devnet): amounts in lamports, the trade
/// accounts' Solana addresses, balances read from the cluster
fn solana() -> bool {
    monzero_dex_evm::Chain::from_id(chain_id()).is_some_and(|c| c.is_solana())
}

/// Base units per coin of the chain's coin: 10^9 lamports per SOL, 10^18 wei otherwise
fn coin_unit() -> f64 {
    if solana() { 1e9 } else { 1e18 }
}

/// The chain's own coin: SOL on Solana, BNB on BNB Smart Chain (56, 97), ETH otherwise
fn evm_coin() -> &'static str {
    if solana() {
        "SOL"
    } else if matches!(chain_id(), 56 | 97) {
        "BNB"
    } else {
        "ETH"
    }
}

fn evm_rpc() -> String {
    env("EVM_RPC", match chain_id() {
        _ if solana() && mainnet() => "https://solana-rpc.publicnode.com",
        _ if solana() => "https://api.devnet.solana.com",
        56 => "https://bsc-rpc.publicnode.com",
        97 => "https://bsc-testnet-rpc.publicnode.com",
        1 => "https://ethereum-rpc.publicnode.com",
        _ => "https://ethereum-sepolia-rpc.publicnode.com",
    })
}

/// A trade account's balance of the chain's coin: wei, or lamports on Solana
async fn coin_balance(side: &Side) -> u128 {
    if solana() {
        let pk = monzero_dex_evm::sol::from_base58(&side.address).expect("Solana address");
        return monzero_dex_evm::sol::balance(&evm_rpc(), &pk).await.expect("Solana RPC") as u128;
    }
    evm_balance(&side.address).await
}

/// An EVM account's balance in wei
async fn evm_balance(address: &str) -> u128 {
    let r: Value = reqwest::Client::new()
        .post(evm_rpc())
        .json(&json!({"jsonrpc": "2.0", "id": 1, "method": "eth_getBalance", "params": [address, "latest"]}))
        .send()
        .await
        .and_then(|r| r.error_for_status())
        .expect("EVM RPC")
        .json()
        .await
        .expect("EVM RPC answer");
    u128::from_str_radix(r["result"].as_str().unwrap_or("0x0").trim_start_matches("0x"), 16).unwrap_or(0)
}

fn log(msg: impl AsRef<str>) {
    let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    println!("{:02}:{:02}:{:02} {}", (t / 3600) % 24, (t / 60) % 60, t % 60, msg.as_ref());
}

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

/// SIDE=sell-xmz: the maker sells the private coin for the chain's coin (the taker locks BNB/ETH
/// and the maker claims it); default buy-xmz: the maker locks BNB/ETH and buys the private coin
fn sell() -> bool {
    env("SIDE", "buy-xmz") == "sell-xmz"
}

/// PAIR=USDT-BSC/XMR: the payer locks USDT (BEP-20) instead of the chain's coin
fn token() -> bool {
    env("PAIR", "").starts_with("USDT")
}

/// The payer's USDT in base units: the SPL token account on Solana (6 decimals), else BEP-20
async fn usdt_of(side: &Side) -> u128 {
    if solana() {
        let chain = monzero_dex_evm::Chain::from_id(chain_id()).unwrap();
        let mint = monzero_dex_evm::sol::usdt_mint(chain).expect("USDT mint on this cluster");
        let pk = monzero_dex_evm::sol::from_base58(&side.address).expect("Solana address");
        return monzero_dex_evm::sol::token_balance(&evm_rpc(), &pk, &mint).await.expect("Solana RPC") as u128;
    }
    usdt_balance(&side.address).await
}

/// Base units per USDT: 10^6 for Tether's SPL mint, 10^18 for BSC-USD
fn usdt_unit() -> f64 {
    if solana() { 1e6 } else { 1e18 }
}

/// A BEP-20 balance (USDT on BNB Smart Chain) in base units
async fn usdt_balance(address: &str) -> u128 {
    let data = format!("0x70a08231{:0>64}", address.trim_start_matches("0x").to_lowercase());
    let r: Value = reqwest::Client::new()
        .post(evm_rpc())
        .json(&json!({"jsonrpc": "2.0", "id": 1, "method": "eth_call", "params": [{"to": "0x55d398326f99059fF775485246999027B3197955", "data": data}, "latest"]}))
        .send()
        .await
        .and_then(|r| r.error_for_status())
        .expect("EVM RPC")
        .json()
        .await
        .expect("EVM RPC answer");
    u128::from_str_radix(r["result"].as_str().unwrap_or("0x0").trim_start_matches("0x"), 16).unwrap_or(0)
}

fn pair() -> String {
    if let Ok(p) = std::env::var("PAIR") {
        return p;
    }
    format!("{}/{}", evm_coin(), if monero() { "XMR" } else { "XMZ" })
}

/// The private-coin amount of the trade, in atoms: 10 tXMZ, or 0.01 stagenet XMR
fn xmz_atoms() -> String {
    env("XMZ_ATOMS", if monero() { "10000000000" } else { "1000000000000" })
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

/// A wallet RPC for `dir/name`, and the endpoint the helper is given for it
async fn wallet_rpc(procs: &mut Procs, dir: &Path, name: &str) -> (WalletRpc, Value) {
    let port = free_port();
    let wallets = dir.join(name);
    std::fs::create_dir_all(&wallets).unwrap();
    procs.0.push(
        Command::new(env("WALLET_RPC_BIN", if monero() { "monero-wallet-rpc" } else { "monzero-wallet-rpc" }))
            .args(match (monero(), mainnet()) {
                (true, true) => vec![],
                (true, false) => vec!["--stagenet"],
                (false, true) => vec![],
                (false, false) => vec!["--testnet"],
            })
            .args(["--wallet-dir", wallets.to_str().unwrap()])
            .args(["--daemon-address", &env("XMZ_NODE", if monero() { "node.monerodevs.org:38089" } else { "127.0.0.1:16175" })])
            // A public node is not trusted with the wallet's view of its own outputs
            .arg(if env("TRUSTED_DAEMON", "1") == "0" { "--untrusted-daemon" } else { "--trusted-daemon" })
            .args(["--rpc-bind-ip", "127.0.0.1", "--rpc-bind-port", &port.to_string(), "--non-interactive"])
            .args(["--log-file", dir.join(format!("{name}.log")).to_str().unwrap()])
            .current_dir(dir)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap(),
    );
    let login = dir.join(format!("{}-wallet-rpc.{port}.login", if monero() { "monero" } else { "monzero" }));
    for _ in 0..300 {
        if login.exists() {
            break;
        }
        tokio::time::sleep(Duration::from_millis(200)).await;
    }
    let url = format!("http://127.0.0.1:{port}");
    let rpc = WalletRpc::with_login_file(&url, &login).unwrap();
    for _ in 0..300 {
        if rpc.call("get_version", json!({})).await.is_ok() {
            return (rpc, json!({"url": url, "loginFile": login}));
        }
        tokio::time::sleep(Duration::from_millis(200)).await;
    }
    panic!("wallet RPC {name} never answered");
}

struct Side {
    name: &'static str,
    dir: PathBuf,
    tick: Value,
    address: String,
    _procs: Procs,
}

impl Side {
    async fn new(name: &'static str, run: &Path, seed: &[u8], index: u32) -> (Side, WalletRpc) {
        let dir = run.join(name);
        std::fs::create_dir_all(&dir).unwrap();
        let mut procs = Procs(vec![]);
        let (main, main_ep) = wallet_rpc(&mut procs, &dir, "main").await;
        let (_, swap_ep) = wallet_rpc(&mut procs, &dir, "swap").await;
        if main.call("create_wallet", json!({"filename": name, "password": "", "language": "English"})).await.is_err() {
            main.call("open_wallet", json!({"filename": name, "password": ""})).await.unwrap();
        }
        // Each trader's own swap seed, as two separate apps would hold
        let own = [seed, format!("{name} app").as_bytes()].concat();
        let swap_seed = monzero_evm_keys::swap_seed(&own);
        let key = evm_account(seed, index).unwrap();
        let tick = json!({
            "evmRpc": evm_rpc(),
            "swapKeys": {"swapSeedHex": hex::encode(*swap_seed), "evmKeyHex": hex::encode(key.to_bytes())},
            "mainWallet": main_ep,
            "swapWallet": swap_ep,
            "witnessDaemons": env("WITNESSES", "").split(',').filter(|w| !w.is_empty()).collect::<Vec<_>>(),
        });
        // On Solana the trade account is the Solana key the wallet derives from the same EVM key
        let address = if solana() {
            monzero_dex_evm::sol::to_base58(&monzero_dex_evm::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes())
        } else {
            key.address().to_string()
        };
        log(format!("{name}: account {address}"));
        (Side { name, dir: dir.join("trade"), tick, address, _procs: procs }, main)
    }

    async fn call(&self, command: &str, extra: Value) -> Value {
        let mut req = json!({"schema": "monzero-xmz-evm-helper-request-v1", "command": command, "tradeDir": self.dir, "tick": self.tick, "relays": [env("RELAY", "http://127.0.0.1:18790")]});
        if command == "listings" || command == "report" || command == "status" || command == "approve" {
            req.as_object_mut().unwrap().remove("tick");
        }
        if command != "listings" && command != "post" && command != "serve" && command != "take" && command != "cancel" && command != "report" {
            req.as_object_mut().unwrap().remove("relays");
        }
        if mainnet() && monero() {
            req["xmrMainnet"] = json!(true);
        }
        for (k, v) in extra.as_object().unwrap() {
            req[k] = v.clone();
        }
        handle(&serde_json::to_vec(&req).unwrap()).await
    }
}

/// Real funds: the owner sends the coins; wait until every side holds what it needs.
/// The payer (maker, or taker with SIDE=sell-xmz): the chain's coin plus gas. The seller: the XMR
/// to sell plus its fee, and a little of the chain's coin for the claim's gas.
async fn wait_for_funds(run: &Path, payer: &Side, seller: &Side, seller_main: &WalletRpc, xmr_address: &str) {
    if run.join("funded").exists() {
        return;
    }
    let value: u128 = env("VALUE_WEI", "10000000000000000").parse().unwrap();
    let payer_gas: u128 = env("MAKER_GAS_WEI", "1000000000000000").parse().unwrap();
    // The payer's coin: the chain's own (value plus gas), or USDT (value) and the chain's coin for gas
    let payer_needs = if token() { payer_gas } else { value + payer_gas };
    let seller_gas: u128 = env("TAKER_GAS_WEI", "500000000000000").parse().unwrap();
    let atoms: u64 = xmz_atoms().parse().unwrap();
    // The helper's own lock margin (1%, at least 1 XMZ / 0.0005 XMR) plus room for the fee
    let xmr_needs = monzero_dex_evm::helper_market::xmz_needed(atoms, private_coin()) + env("XMR_FEE_ATOMS", "1000000000").parse::<u64>().unwrap();
    let coin = evm_coin();
    let unit = coin_unit();
    let show = |wei: u128| format!("{:.6} {coin}", wei as f64 / unit);
    let network = if solana() { "Solana" } else { "BNB Smart Chain (BEP20)" };
    let (ticker, scale) = if monero() { ("XMR", 1e12) } else { ("XMZ", 1e11) };
    let showx = |a: u64| format!("{:.6} {ticker}", a as f64 / scale);
    println!("\n== Send the test coins (only these amounts or a little more):");
    let (p, s) = (payer.name, seller.name);
    if token() {
        println!("   {:.6} USDT ({}) to the {p} (pays for {ticker}): {}", value as f64 / usdt_unit(), if solana() { "SPL" } else { "BEP20" }, payer.address);
        println!("   {} to the {p} (gas):           {}", show(payer_needs), payer.address);
    } else {
        println!("   {} to the {p} (pays for {ticker}):     {}", show(payer_needs), payer.address);
    }
    println!("   {} to the {s} (claim gas):        {}", show(seller_gas), seller.address);
    println!("   {} to the {s}'s {ticker} wallet:      {xmr_address}", showx(xmr_needs));
    println!("   on {network} for {coin}; {ticker} needs 10 blocks before it can be spent.\n");
    for round in 0u64.. {
        let _ = seller_main.call("refresh", json!({})).await;
        let b = seller_main.call("get_balance", json!({"account_index": 0})).await.unwrap_or(json!({}));
        let (total, unlocked) = (b["balance"].as_u64().unwrap_or(0), b["unlocked_balance"].as_u64().unwrap_or(0));
        let (m, t) = (coin_balance(payer).await, coin_balance(seller).await);
        let usdt = if token() { usdt_of(payer).await } else { 0 };
        let ready = m >= payer_needs && t >= seller_gas && unlocked >= xmr_needs && (!token() || usdt >= value);
        if ready || round % 4 == 0 {
            if token() {
                log(format!("{p} USDT {:.6} / {:.6}", usdt as f64 / usdt_unit(), value as f64 / usdt_unit()));
            }
            log(format!("{p} {} / {}, {s} {} / {}, {s} {ticker} {} spendable ({} arrived) / {}",
                show(m), show(payer_needs), show(t), show(seller_gas), showx(unlocked), showx(total), showx(xmr_needs)));
        }
        if ready {
            std::fs::write(run.join("funded"), "owner funded\n").unwrap();
            return;
        }
        tokio::time::sleep(Duration::from_secs(30)).await;
    }
}

#[tokio::main]
async fn main() {
    let run = PathBuf::from(std::env::args().nth(1).expect("run dir"));
    std::fs::create_dir_all(&run).unwrap();
    let home = env("HOME", "/root");
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE", &format!("{home}/.config/monzero-dex/eth-sepolia-accounts.json"))).unwrap()).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().unwrap());
    let (maker, maker_main) = Side::new("maker", &run, &seed, env("OWNER_INDEX", "1").parse().unwrap()).await;
    let (taker, taker_main) = Side::new("taker", &run, &seed, env("CLAIMER_INDEX", "0").parse().unwrap()).await;
    // Who locks the chain's coin (payer) and who sells the private coin and claims (seller)
    let (payer, seller, seller_main) = if sell() { (&taker, &maker, &maker_main) } else { (&maker, &taker, &taker_main) };

    // The seller's wallet holds the private coin: the owner's, or from the testnet funder
    let address = seller_main.call("get_address", json!({})).await.unwrap()["address"].as_str().unwrap().to_string();
    if env("FUNDER_RPC", "") == "none" {
        wait_for_funds(&run, payer, seller, seller_main, &address).await;
    }
    let funder = WalletRpc::new(&format!("http://{}", env("FUNDER_RPC", "127.0.0.1:16183")), None);
    if env("FUNDER_RPC", "") != "none" && !run.join("funded").exists() {
        // Twice the trade amount (XMZ default: 20 tXMZ for a 10 tXMZ trade), so fees are covered
        let amount: u64 = env("FUND_ATOMS", &(2 * xmz_atoms().parse::<u64>().unwrap()).to_string()).parse().unwrap();
        let r = funder.call("transfer", json!({"destinations": [{"amount": amount, "address": address}], "ring_size": 16})).await.unwrap();
        std::fs::write(run.join("funded"), r.to_string()).unwrap();
        log(format!("{} funded {amount} atoms: {}", seller.name, r["tx_hash"]));
    }

    // 1. The maker posts its offer (buy-xmz, or sell-xmz)
    let posted = if maker.dir.join("listing.json").exists() {
        json!({"ok": true})
    } else {
        let mut offer = json!({"offer": {"chainId": chain_id(), "side": if sell() { "sell-xmz" } else { "buy-xmz" }, "valueWei": env("VALUE_WEI", "10000000000000000"), "xmzAtoms": xmz_atoms(), "lifetimeSecs": 6 * 3600}});
        if monero() {
            offer["offer"]["coin"] = json!("xmr");
        }
        // A token pair names itself; the chain's own coin is the offer's default
        if token() {
            offer["offer"]["pair"] = json!(pair());
        }
        maker.call("post", offer).await
    };
    assert_eq!(posted["ok"], true, "post: {posted}");
    log(format!("maker posted: {}", posted.get("posted").unwrap_or(&Value::Null)));

    // 2. The taker finds it among the relay's checked offers
    let id = serde_json::from_str::<Value>(&std::fs::read_to_string(maker.dir.join("listing.json")).unwrap()).unwrap()["listing"]["listing"]["id"].clone();
    // A run that is being resumed already took the offer: it is no longer on the relay's board
    let listing = if taker.dir.join("take.json").exists() {
        log("taker: resuming the take it already made");
        serde_json::from_str::<Value>(&std::fs::read_to_string(taker.dir.join("take.json")).unwrap()).unwrap()["listing"].clone()
    } else {
        let found = taker.call("listings", json!({"pair": pair()})).await;
        let listing = found["listings"].as_array().unwrap().iter().find(|l| l["signed"]["listing"]["id"] == id).expect("our offer on the relay")["signed"].clone();
        log(format!("taker found offer {id} among {}", found["listings"].as_array().unwrap().len()));
        listing
    };

    // 3. Take: the taker waits for its XMZ to unlock, then both sides call until agreed
    let mut agreed = (false, false);
    for round in 0.. {
        if !agreed.1 {
            let t = taker.call("take", json!({"listing": listing})).await;
            if t["ok"] == true && t["agreed"] == true {
                agreed.1 = true;
                log("taker: keys agreed");
            } else if round % 8 == 0 {
                log(format!("taker: {}", t.get("error").or(t.get("refused")).unwrap_or(&json!("waiting"))));
            }
        }
        if !agreed.0 {
            let s = maker.call("serve", json!({})).await;
            if s["ok"] == true && s["taken"] == true {
                agreed.0 = true;
                log(format!("maker: taken by {}", s["takenBy"]));
            } else if s["ok"] != true {
                log(format!("maker: {}", s["error"]));
            }
        }
        if agreed == (true, true) {
            break;
        }
        assert!(round < 600, "no agreement");
        tokio::time::sleep(Duration::from_secs(15)).await;
    }

    // 4. Both approve what they were shown, then step to the end
    for side in [&maker, &taker] {
        let s = side.call("status", json!({})).await;
        let a = side.call("approve", json!({"commitment": s["commitment"]})).await;
        assert_eq!(a["ok"], true, "{}: {a}", side.name);
    }
    let mut last = [String::new(), String::new()];
    let mut done = [false, false];
    for round in 0.. {
        for (i, side) in [&maker, &taker].into_iter().enumerate() {
            if done[i] {
                continue;
            }
            let t = side.call("tick", json!({})).await;
            if t["ok"] != true {
                if round % 20 == 0 {
                    log(format!("{}: {}", side.name, t["error"]));
                }
                continue;
            }
            let stage = t["stage"].as_str().unwrap_or("?").to_string();
            if stage != last[i] {
                log(format!("{} -> {stage}", side.name));
                last[i] = stage;
            }
            done[i] = t["done"] == true;
        }
        if done == [true, true] {
            break;
        }
        assert!(round < 2000, "the swap did not finish");
        tokio::time::sleep(Duration::from_secs(15)).await;
    }

    // No swap (a refund or an abort): every coin is back with its owner, nothing to report
    if !last.iter().all(|stage| stage.contains("Swapped")) {
        println!("NO SWAP: {} / {}", last[0], last[1]);
        std::process::exit(3);
    }

    // 5. The claimer reports its claim; the relay records it once final and charts it
    let r = seller.call("report", json!({})).await;
    log(format!("{} reported: {r}", seller.name));
    let relay = env("RELAY", "http://127.0.0.1:18790");
    let client = reqwest::Client::new();
    for _ in 0..180 {
        let c: Value = client.get(format!("{relay}/v1/candles?pair={}&interval=3600", pair())).send().await.unwrap().json().await.unwrap();
        if c["candles"].as_array().is_some_and(|a| !a.is_empty()) {
            log(format!("relay candles: {}", c["candles"]));
            let p: Value = client.get(format!("{relay}/v1/prints?pair={}", pair())).send().await.unwrap().json().await.unwrap();
            log(format!("relay prints: {}", p["prints"]));
            println!("PASS: {} / {}", last[0], last[1]);
            return;
        }
        tokio::time::sleep(Duration::from_secs(30)).await;
    }
    panic!("the relay never recorded the trade");
}
