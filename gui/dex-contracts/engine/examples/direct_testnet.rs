//! One direct (coin <-> coin) trade on the public test networks, Sepolia ETH for BNB Smart Chain
//! testnet BNB, driven only through the wallet helper's JSON commands, as the apps drive them
//! (`directPost`, `directListings`, `directTake`, `directServe`, `directApprove`, `directTick`,
//! `directPayout`). Test coins only. Real time: no clock tricks, so it takes as long as the
//! chains take (usually 5-15 minutes).
//!
//!   cargo run --example direct_testnet -- accounts             # the test accounts' addresses
//!   RELAY=http://127.0.0.1:18797 cargo run --example direct_testnet -- <work dir>
//!
//! Safe to run again: each trader's folder keeps its trade, and a rerun picks it up.
//!
//! Env: MAKER_PHRASE (file, default ~/.config/monzero-dex/eth-sepolia-accounts.json),
//! MAKER_INDEX (2), TAKER_PHRASE (default ~/.config/monzero-dex/bsc-testnet-accounts.json),
//! TAKER_INDEX (3), GIVES_WEI (maker's SepETH, 0.002), WANTS_WEI (taker's tBNB, 0.01),
//! RPC_SEPOLIA, RPC_BSC_TESTNET. The maker gives ETH on Sepolia and wants BNB; each side also
//! needs a little of the other chain's coin to claim (gas).

use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::helper::handle;
use serde_json::{json, Value};
use sha2::Sha512;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

const SEPOLIA: u64 = 11155111;
const BSC_TESTNET: u64 = 97;

fn env(key: &str, default: &str) -> String {
    std::env::var(key).unwrap_or_else(|_| default.to_string())
}

fn log(s: impl AsRef<str>) {
    let t = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs() % 86400;
    println!("{:02}:{:02}:{:02} {}", t / 3600, t / 60 % 60, t % 60, s.as_ref());
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

fn phrase_seed(file: &str) -> [u8; 64] {
    let v: Value = serde_json::from_str(&std::fs::read_to_string(file).unwrap_or_else(|e| panic!("{file}: {e}"))).unwrap();
    bip39_seed(v["mnemonic"].as_str().expect("mnemonic"))
}

struct Trader {
    name: &'static str,
    seed_hex: String,
    index: u32,
    dir: PathBuf,
    relay: String,
}

impl Trader {
    fn new(name: &'static str, phrase_file: &str, index: u32, work: &Path, relay: &str) -> Trader {
        let seed = phrase_seed(phrase_file);
        let t = Trader { name, seed_hex: hex::encode(seed), index, dir: work.join(name), relay: relay.to_string() };
        log(format!("{name}: account {}", evm_account(&seed, index).unwrap().address()));
        t
    }

    fn payout_address(&self) -> String {
        // Another account of the same test phrase: the trade's coins end up somewhere the
        // owner of the phrase controls, and the payout is a real transfer out of the trade
        evm_account(&hex::decode(&self.seed_hex).unwrap(), self.index + 10).unwrap().address().to_string()
    }

    async fn call(&self, command: &str, extra: Value) -> Value {
        let mut req = json!({
            "schema": "monzero-xmz-evm-helper-request-v1",
            "command": command,
            "tradeDir": self.dir,
            "relays": [self.relay],
            "direct": {
                "rpcs": {
                    SEPOLIA.to_string(): env("RPC_SEPOLIA", "https://sepolia.gateway.tenderly.co"),
                    BSC_TESTNET.to_string(): env("RPC_BSC_TESTNET", "https://bsc-testnet-rpc.publicnode.com"),
                },
                "walletSeedHex": self.seed_hex,
                "accountIndex": self.index,
            },
        });
        for (k, v) in extra.as_object().unwrap() {
            req[k] = v.clone();
        }
        handle(&serde_json::to_vec(&req).unwrap()).await
    }
}

fn short(v: &Value) -> String {
    let s = v.to_string();
    if s.len() > 400 { format!("{}...", &s[..400]) } else { s }
}

#[tokio::main]
async fn main() {
    let arg = std::env::args().nth(1).expect("usage: direct_testnet accounts | <work dir>");
    let home = std::env::var("HOME").unwrap();
    let maker_phrase = env("MAKER_PHRASE", &format!("{home}/.config/monzero-dex/eth-sepolia-accounts.json"));
    let taker_phrase = env("TAKER_PHRASE", &format!("{home}/.config/monzero-dex/bsc-testnet-accounts.json"));
    if arg == "accounts" {
        for (label, file) in [("maker phrase", &maker_phrase), ("taker phrase", &taker_phrase)] {
            let seed = phrase_seed(file);
            for i in 0..6 {
                println!("{label} index {i}: {}", evm_account(&seed, i).unwrap().address());
            }
        }
        return;
    }
    let work = PathBuf::from(arg);
    std::fs::create_dir_all(&work).unwrap();
    let relay = env("RELAY", "http://127.0.0.1:18797");
    let maker = Trader::new("maker", &maker_phrase, env("MAKER_INDEX", "2").parse().unwrap(), &work, &relay);
    let taker = Trader::new("taker", &taker_phrase, env("TAKER_INDEX", "3").parse().unwrap(), &work, &relay);
    let gives = env("GIVES_WEI", "2000000000000000");
    let wants = env("WANTS_WEI", "10000000000000000");

    // 1. The maker posts: gives SepETH, wants tBNB
    let posted_marker = work.join("posted.json");
    if !posted_marker.exists() {
        let posted = maker
            .call("directPost", json!({"directOffer": {"givesChainId": SEPOLIA, "givesValue": gives, "wantsChainId": BSC_TESTNET, "wantsValue": wants, "lifetimeSecs": 3 * 3600}}))
            .await;
        log(format!("maker posted: {}", short(&posted)));
        assert_eq!(posted["ok"], true, "the post was refused");
        assert_eq!(posted["posted"][0]["ok"], true, "the relay refused the offer");
        std::fs::write(&posted_marker, posted.to_string()).unwrap();
    }

    // 2. The taker finds it and takes it; the maker answers
    let agreed_marker = work.join("agreed.json");
    if !agreed_marker.exists() {
        let maker_addr = evm_account(&hex::decode(&maker.seed_hex).unwrap(), maker.index).unwrap().address().to_string().to_lowercase();
        let mut first = taker.call("directTake", json!({})).await;
        if first["ok"] != true || first.get("agreed").is_none() {
            let found = taker.call("directListings", json!({})).await;
            let listing = found["listings"]
                .as_array()
                .and_then(|l| l.iter().find(|x| x["signed"]["listing"]["maker"].as_str().map(str::to_lowercase).as_deref() == Some(&maker_addr) || l.len() == 1))
                .map(|x| x["signed"].clone())
                .unwrap_or_else(|| panic!("the maker's offer is not on the relay: {}", short(&found)));
            log(format!("taker found {} {}", listing["listing"]["pair"], listing["listing"]["id"]));
            first = taker.call("directTake", json!({"directListing": listing})).await;
        }
        log(format!("taker: {}", short(&first)));
        let mut agreed = first;
        for _ in 0..30 {
            if agreed["agreed"] == true {
                break;
            }
            let served = maker.call("directServe", json!({})).await;
            if served["taken"] == true {
                log("maker: taken");
            }
            tokio::time::sleep(Duration::from_secs(5)).await;
            agreed = taker.call("directTake", json!({})).await;
        }
        assert_eq!(agreed["agreed"], true, "no agreement: {}", short(&agreed));
        log(format!("agreed: {} gets {}", agreed["pair"], agreed["gets"]));
        let approved = taker.call("directApprove", json!({"commitment": agreed["commitment"]})).await;
        assert_eq!(approved["approved"], true, "{}", short(&approved));
        std::fs::write(&agreed_marker, agreed.to_string()).unwrap();
    }

    // 3. Both sides tick until both are done (real chains: allow up to 3 hours, the refund path)
    let start = Instant::now();
    let (mut ts, mut ms) = (String::new(), String::new());
    let (mut t, mut m);
    loop {
        t = taker.call("directTick", json!({})).await;
        m = maker.call("directTick", json!({})).await;
        for (who, v, last) in [("taker", &t, &mut ts), ("maker", &m, &mut ms)] {
            let now = if v["ok"] == true { v["stage"].as_str().unwrap_or("?").to_string() } else { format!("error: {}", v["error"]) };
            if &now != last {
                log(format!("{who} -> {now}"));
                *last = now;
            }
        }
        if t["done"] == true && m["done"] == true {
            break;
        }
        if start.elapsed() > Duration::from_secs(3 * 3600 + 1800) {
            log("gave up waiting (3.5 h)");
            std::process::exit(2);
        }
        tokio::time::sleep(Duration::from_secs(20)).await;
    }
    log(format!("taker: {}  maker: {}  ({} min)", t["stage"], m["stage"], start.elapsed().as_secs() / 60));
    if t["stage"] != "Done(Swapped)" || m["stage"] != "Done(Swapped)" {
        println!("NO SWAP: {} / {}", t["stage"], m["stage"]);
        std::process::exit(3);
    }

    // 4. Payouts: each side sends what the trade brought in to another account of its phrase
    for (who, chain) in [(&taker, SEPOLIA), (&maker, BSC_TESTNET)] {
        let paid = who.call("directPayout", json!({"chainId": chain, "payoutAddress": who.payout_address()})).await;
        log(format!("{} payout on {chain}: {}", who.name, short(&paid)));
        assert_eq!(paid["ok"], true);
    }
    println!("SWAPPED");
}
