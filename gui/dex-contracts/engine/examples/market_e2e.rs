//! A whole public-offer trade through the helper's JSON commands, as the app runs them:
//! the maker posts an offer to a relay, the taker finds it and takes it through sealed relay
//! mail, both step the swap to the end, the claimer reports the claim and the relay's chart
//! gets its first candle. Test networks only (Sepolia + XMZ testnet).
//!
//!   RELAY=http://127.0.0.1:18790 cargo run --example market_e2e -- <run dir>
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
            "evmRpc": env("EVM_RPC", "https://ethereum-sepolia-rpc.publicnode.com"),
            "swapKeys": {"swapSeedHex": hex::encode(*swap_seed), "evmKeyHex": hex::encode(key.to_bytes())},
            "mainWallet": main_ep,
            "swapWallet": swap_ep,
        });
        log(format!("{name}: account {}", key.address()));
        (Side { name, dir: dir.join("trade"), tick, _procs: procs }, main)
    }

    async fn call(&self, command: &str, extra: Value) -> Value {
        let mut req = json!({"schema": "monzero-xmz-evm-helper-request-v1", "command": command, "tradeDir": self.dir, "tick": self.tick, "relays": [env("RELAY", "http://127.0.0.1:18790")]});
        if command == "listings" || command == "report" || command == "status" || command == "approve" {
            req.as_object_mut().unwrap().remove("tick");
        }
        if command != "listings" && command != "post" && command != "serve" && command != "take" && command != "cancel" && command != "report" {
            req.as_object_mut().unwrap().remove("relays");
        }
        for (k, v) in extra.as_object().unwrap() {
            req[k] = v.clone();
        }
        handle(&serde_json::to_vec(&req).unwrap()).await
    }
}

#[tokio::main]
async fn main() {
    let run = PathBuf::from(std::env::args().nth(1).expect("run dir"));
    std::fs::create_dir_all(&run).unwrap();
    let home = env("HOME", "/root");
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE", &format!("{home}/.config/monzero-dex/eth-sepolia-accounts.json"))).unwrap()).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().unwrap());
    let (maker, _) = Side::new("maker", &run, &seed, env("OWNER_INDEX", "1").parse().unwrap()).await;
    let (taker, taker_main) = Side::new("taker", &run, &seed, env("CLAIMER_INDEX", "0").parse().unwrap()).await;

    // The taker sells XMZ: fund its wallet from the testnet funder
    let address = taker_main.call("get_address", json!({})).await.unwrap()["address"].as_str().unwrap().to_string();
    let funder = WalletRpc::new(&format!("http://{}", env("FUNDER_RPC", "127.0.0.1:16183")), None);
    if !run.join("funded").exists() {
        let r = funder.call("transfer", json!({"destinations": [{"amount": 2_000_000_000_000u64, "address": address}], "ring_size": 16})).await.unwrap();
        std::fs::write(run.join("funded"), r.to_string()).unwrap();
        log(format!("taker funded 20 tXMZ: {}", r["tx_hash"]));
    }

    // 1. The maker posts a buy-XMZ offer
    let posted = if maker.dir.join("listing.json").exists() {
        json!({"ok": true})
    } else {
        let offer = json!({"offer": {"chainId": 11155111, "side": "buy-xmz", "valueWei": env("VALUE_WEI", "10000000000000000"), "xmzAtoms": env("XMZ_ATOMS", "1000000000000"), "lifetimeSecs": 6 * 3600}});
        maker.call("post", offer).await
    };
    assert_eq!(posted["ok"], true, "post: {posted}");
    log(format!("maker posted: {}", posted.get("posted").unwrap_or(&Value::Null)));

    // 2. The taker finds it among the relay's checked offers
    let found = taker.call("listings", json!({"pair": "ETH/XMZ"})).await;
    let id = serde_json::from_str::<Value>(&std::fs::read_to_string(maker.dir.join("listing.json")).unwrap()).unwrap()["listing"]["listing"]["id"].clone();
    let listing = found["listings"].as_array().unwrap().iter().find(|l| l["signed"]["listing"]["id"] == id).expect("our offer on the relay")["signed"].clone();
    log(format!("taker found offer {id} among {}", found["listings"].as_array().unwrap().len()));

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

    // 5. The claimer reports its claim; the relay records it once final and charts it
    let r = taker.call("report", json!({})).await;
    log(format!("taker reported: {r}"));
    let relay = env("RELAY", "http://127.0.0.1:18790");
    let client = reqwest::Client::new();
    for _ in 0..180 {
        let c: Value = client.get(format!("{relay}/v1/candles?pair=ETH/XMZ&interval=3600")).send().await.unwrap().json().await.unwrap();
        if c["candles"].as_array().is_some_and(|a| !a.is_empty()) {
            log(format!("relay candles: {}", c["candles"]));
            let p: Value = client.get(format!("{relay}/v1/prints?pair=ETH/XMZ")).send().await.unwrap().json().await.unwrap();
            log(format!("relay prints: {}", p["prints"]));
            println!("PASS: {} / {}", last[0], last[1]);
            return;
        }
        tokio::time::sleep(Duration::from_secs(30)).await;
    }
    panic!("the relay never recorded the trade");
}
