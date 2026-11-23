//! Swaps a test account's SOL for USDT (Tether's SPL mint) on Solana through Jupiter, for the
//! owner's real-funds USDT (Solana)/XMR test. The account is the Solana key the wallet derives from
//! test account INDEX (phrase file never printed); only its public address goes to Jupiter.
//!   ACCOUNTS_FILE=<…-accounts.json> INDEX=<i> LAMPORTS=<n> [GO=1] cargo run --release --example sol_jupiter_swap
//! Without GO=1 it only shows the balance and Jupiter's quote. Slippage at most 1%. Refuses to leave
//! less than KEEP_LAMPORTS (default 0.02 SOL) for fees and rent.
use base64::Engine as _;
use ed25519_dalek::Signer;
use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::sol;
use serde_json::{json, Value};
use sha2::Sha512;

const WSOL: &str = "So11111111111111111111111111111111111111112";
const USDT: &str = "Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB";
const JUP: &str = "https://lite-api.jup.ag/swap/v1";

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

async fn rpc(url: &str, method: &str, params: Value) -> Value {
    let r: Value = reqwest::Client::new().post(url).json(&json!({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}))
        .send().await.expect("Solana RPC").json().await.expect("json");
    if let Some(e) = r.get("error") {
        panic!("{method}: {e}");
    }
    r["result"].clone()
}

/// Solana's compact-u16 length prefix: (value, bytes used)
fn compact_u16(b: &[u8]) -> (usize, usize) {
    let (mut v, mut i) = (0usize, 0usize);
    loop {
        let x = b[i] as usize;
        v |= (x & 0x7f) << (7 * i);
        i += 1;
        if x & 0x80 == 0 {
            return (v, i);
        }
    }
}

#[tokio::main]
async fn main() {
    let env = |k: &str| std::env::var(k).unwrap_or_default();
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE")).expect("ACCOUNTS_FILE")).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().expect("mnemonic"));
    let evm = evm_account(&seed, env("INDEX").parse().expect("INDEX")).unwrap();
    let key = sol::account_key(&evm.to_bytes().0);
    let me = key.verifying_key().to_bytes();
    let address = sol::to_base58(&me);
    let url = { let r = env("RPC"); if r.is_empty() { "https://solana-rpc.publicnode.com".to_string() } else { r } };
    let lamports: u64 = env("LAMPORTS").parse().expect("LAMPORTS");
    let keep: u64 = { let k = env("KEEP_LAMPORTS"); if k.is_empty() { 20_000_000 } else { k.parse().unwrap() } };
    let usdt = sol::from_base58(USDT).unwrap();
    let bal = sol::balance(&url, &me).await.unwrap();
    println!("account {address}: {:.9} SOL, {:.6} USDT", bal as f64 / 1e9, sol::token_balance(&url, &me, &usdt).await.unwrap() as f64 / 1e6);
    assert!(lamports + keep <= bal, "LAMPORTS would leave less than KEEP_LAMPORTS");
    let http = reqwest::Client::new();
    let quote: Value = http.get(format!("{JUP}/quote?inputMint={WSOL}&outputMint={USDT}&amount={lamports}&slippageBps=100"))
        .send().await.expect("Jupiter quote").json().await.expect("quote json");
    let out: f64 = quote["outAmount"].as_str().expect("outAmount").parse().unwrap();
    let min: f64 = quote["otherAmountThreshold"].as_str().unwrap_or("0").parse().unwrap();
    println!("quote: {:.9} SOL -> {:.6} USDT (at least {:.6}), route {}", lamports as f64 / 1e9, out / 1e6, min / 1e6,
        quote["routePlan"].as_array().map(|r| r.iter().map(|s| s["swapInfo"]["label"].as_str().unwrap_or("?")).collect::<Vec<_>>().join(" > ")).unwrap_or_default());
    if env("GO") != "1" {
        println!("dry run: nothing sent (GO=1 to swap)");
        return;
    }
    let swap: Value = http.post(format!("{JUP}/swap"))
        .json(&json!({"quoteResponse": quote, "userPublicKey": address, "wrapAndUnwrapSol": true, "dynamicComputeUnitLimit": true}))
        .send().await.expect("Jupiter swap").json().await.expect("swap json");
    let mut tx = base64::engine::general_purpose::STANDARD.decode(swap["swapTransaction"].as_str().expect("swapTransaction")).unwrap();
    let (sigs, at) = compact_u16(&tx);
    assert_eq!(sigs, 1, "one signer expected");
    let message = tx[at + 64..].to_vec();
    // A v0 message: 0x80 prefix, then the header; the first account key is the fee payer
    let body = if message[0] & 0x80 != 0 { &message[1..] } else { &message[..] };
    assert_eq!(body[0], 1, "one required signature");
    let (_, n) = compact_u16(&body[3..]);
    assert_eq!(&body[3 + n..3 + n + 32], &me, "the fee payer is this account");
    tx[at..at + 64].copy_from_slice(&key.sign(&message).to_bytes());
    let id = rpc(&url, "sendTransaction", json!([base64::engine::general_purpose::STANDARD.encode(&tx), {"encoding": "base64", "preflightCommitment": "confirmed"}])).await;
    let id = id.as_str().expect("signature").to_string();
    println!("sent {id}");
    for _ in 0..60 {
        let s = rpc(&url, "getSignatureStatuses", json!([[id]])).await;
        let st = &s["value"][0];
        if !st.is_null() && st["confirmationStatus"].as_str().is_some_and(|c| c == "confirmed" || c == "finalized") {
            println!("confirmed, error: {}", st["err"]);
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    println!("now: {:.9} SOL, {:.6} USDT", sol::balance(&url, &me).await.unwrap() as f64 / 1e9, sol::token_balance(&url, &me, &usdt).await.unwrap() as f64 / 1e6);
}
