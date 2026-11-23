//! Prints the Solana addresses (public only) of a test phrase file's first trade accounts, as the
//! wallet derives them from each EVM trade key, with their SOL balances.
//!   ACCOUNTS_FILE=<…-accounts.json> [RPC=<url>] cargo run --example sol_addresses
use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
use monzero_dex_evm::sol;
use serde_json::Value;
use sha2::Sha512;

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

#[tokio::main]
async fn main() {
    let env = |k: &str| std::env::var(k).unwrap_or_default();
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE")).expect("ACCOUNTS_FILE")).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().expect("mnemonic"));
    let rpc = { let r = env("RPC"); if r.is_empty() { "https://solana-rpc.publicnode.com".to_string() } else { r } };
    for i in 0..2 {
        let key = evm_account(&seed, i).unwrap();
        let pk = sol::account_key(&key.to_bytes().0).verifying_key().to_bytes();
        let bal = sol::balance(&rpc, &pk).await.map(|l| format!("{:.9} SOL", l as f64 / 1e9)).unwrap_or_else(|e| e.to_string());
        println!("{i} {} {} {bal}", key.address(), sol::to_base58(&pk));
    }
}
