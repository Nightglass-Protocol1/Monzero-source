//! Moves test coins from a test account (rebuilt from a phrase file, never printed) to an address.
//!   ACCOUNTS_FILE=<…-accounts.json> RPC=<url> CHAIN_ID=<id> TO=<0x…> [VALUE_WEI=<wei>] [INDEX=<i>] \
//!     cargo run --example evm_send
//! Without VALUE_WEI it only lists the first accounts and their balances (nothing is sent).
use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
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
    let rpc = env("RPC");
    let provider = ProviderBuilder::new().connect_http(rpc.parse().unwrap());
    if env("VALUE_WEI").is_empty() {
        for i in 0..4 {
            let a = evm_account(&seed, i).unwrap().address();
            println!("{i} {a} {}", provider.get_balance(a).await.unwrap());
        }
        return;
    }
    let index: u32 = env("INDEX").parse().expect("INDEX");
    let key = evm_account(&seed, index).unwrap();
    let from = key.address();
    let to: Address = env("TO").parse().expect("TO");
    let value: U256 = env("VALUE_WEI").parse().expect("VALUE_WEI");
    let chain: u64 = env("CHAIN_ID").parse().expect("CHAIN_ID");
    let signer = ProviderBuilder::new().wallet(EthereumWallet::from(key)).connect_http(rpc.parse().unwrap());
    let tx = TransactionRequest::default().with_from(from).with_to(to).with_value(value).with_chain_id(chain);
    let pending = signer.send_transaction(tx).await.expect("send");
    let hash = *pending.tx_hash();
    println!("sent {hash} from {from}");
    let receipt = pending.get_receipt().await.expect("receipt");
    println!("mined in block {:?}, status {}", receipt.block_number, receipt.status());
}
