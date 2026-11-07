//! Read-only check of the connection a swap would use, with the same client the trader uses:
//!
//!   cargo run --example evm_preflight -- <eth|eth-sepolia|bsc> [rpc-url]
//!
//! Holds no key: it makes a throwaway one because the client needs a signer, signs nothing and
//! sends nothing. Exits non-zero if anything a trade depends on is missing.

use alloy::eips::BlockNumberOrTag;
use alloy::primitives::{Address, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::Chain;
use serde_json::Value;

// Measured in test/gas.test.js
const GAS: [(&str, u128); 5] = [("deploy contract (once)", 1_141_500), ("buyer: lock", 51_400), ("buyer: set ready", 32_000), ("seller: claim", 44_000), ("refund (only if a trade fails)", 43_900)];

#[tokio::main]
async fn main() {
    let args: Vec<String> = std::env::args().collect();
    let (chain, record, coin, default_rpc) = match args.get(1).map(String::as_str) {
        Some("eth") => (Chain::EthMainnet, "eth-mainnet", "ETH", "https://ethereum-rpc.publicnode.com"),
        Some("eth-sepolia") => (Chain::EthSepolia, "eth-sepolia", "SepoliaETH", "https://ethereum-sepolia-rpc.publicnode.com"),
        Some("bsc") => (Chain::BscMainnet, "bsc-mainnet", "BNB", "https://bsc-rpc.publicnode.com"),
        _ => {
            eprintln!("usage: evm_preflight <eth|eth-sepolia|bsc> [rpc-url]");
            std::process::exit(2);
        }
    };
    let url = args.get(2).map_or(default_rpc, String::as_str);
    let file = format!("{}/../deployments/{record}.json", env!("CARGO_MANIFEST_DIR"));
    let deployment: Option<Value> = std::fs::read_to_string(&file).ok().map(|s| serde_json::from_str(&s).expect("a valid deployment record"));
    let contract: Address = deployment.as_ref().map_or(Address::ZERO, |d| d["contract"].as_str().unwrap().parse().unwrap());

    println!("endpoint       {url}");
    let client = AlloyChain::new(url, chain, contract, 0, PrivateKeySigner::random()).unwrap();
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let mut failed = false;
    let mut report = |what: &str, result: Result<String, String>| match result {
        Ok(detail) => println!("ok    {what}: {detail}"),
        Err(e) => {
            failed = true;
            println!("FAIL  {what}: {e}");
        }
    };

    report("network", client.check_network().await.map(|id| format!("chain {id}")).map_err(|e| e.to_string()));
    let head = provider.get_block_number().await.unwrap_or(0);
    report("head", if head > 0 { Ok(format!("block {head}")) } else { Err("no answer".into()) });
    let block = provider.get_block_by_number(BlockNumberOrTag::Latest).await.ok().flatten();
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    report("freshness", match block {
        Some(b) if now.saturating_sub(b.header.timestamp) < 180 => Ok(format!("newest block is {} s old", now.saturating_sub(b.header.timestamp))),
        Some(b) => Err(format!("newest block is {} s old", now.saturating_sub(b.header.timestamp))),
        None => Err("no newest block".into()),
    });
    let finalized = provider.get_block_by_number(BlockNumberOrTag::Finalized).await.ok().flatten();
    report("finality", match finalized {
        Some(b) if head.saturating_sub(b.header.number) <= chain.confirmations() => Ok(format!("finalized block is {} behind; a trade waits {} blocks", head.saturating_sub(b.header.number), chain.confirmations())),
        Some(b) => Err(format!("finalized block is {} behind, more than the {} blocks a trade waits", head.saturating_sub(b.header.number), chain.confirmations())),
        None => Err("no finalized block".into()),
    });
    report("logs", client.check_logs().await.map(|_| "serves log queries 1,000+ blocks back".to_string()).map_err(|e| e.to_string()));
    let past = head.saturating_sub(chain.confirmations());
    report("past state", provider.get_balance(contract).number(past).await.map(|_| format!("reads state at block {past}")).map_err(|e| e.to_string()));
    let fees = provider.estimate_eip1559_fees().await;
    report("fees", fees.as_ref().map(|f| format!("max {:.3} gwei, tip {:.3} gwei", f.max_fee_per_gas as f64 / 1e9, f.max_priority_fee_per_gas as f64 / 1e9)).map_err(|e| e.to_string()));
    report("contract", match &deployment {
        None => Ok(format!("not deployed yet (no deployments/{record}.json)")),
        Some(d) => match provider.get_code_at(contract).await {
            Ok(code) if alloy::primitives::keccak256(&code).to_string() == d["runtimeCodeKeccak"].as_str().unwrap_or_default() => Ok(format!("{contract}: code matches the record")),
            Ok(_) => Err(format!("{contract}: code on chain differs from the record")),
            Err(e) => Err(e.to_string()),
        },
    });

    if let Ok(f) = fees {
        println!("\ncost at today's fees ({coin}), using the max fee, so an upper estimate:");
        for (what, gas) in GAS {
            let wei = U256::from(gas) * U256::from(f.max_fee_per_gas);
            println!("  {:>12.8}  {what}", f64::from(wei) / 1e18);
        }
    }
    println!("\nnothing was signed or sent");
    std::process::exit(failed as i32);
}
