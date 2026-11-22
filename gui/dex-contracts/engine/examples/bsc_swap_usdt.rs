//! Swaps a test account's BNB for USDT (BEP-20) through PancakeSwap's v2 router, for the
//! owner's real-funds USDT-BSC/XMR test. The account is rebuilt from the phrase file (never printed).
//!   ACCOUNTS_FILE=<…-accounts.json> INDEX=<i> SWAP_WEI=<wei> [GO=1] cargo run --release --example bsc_swap_usdt
//! Without GO=1 it only shows the balances and the quote (nothing is sent). It refuses to leave
//! the account with less than KEEP_WEI (default 0.001 BNB) for gas, and accepts at most 2% less
//! USDT than quoted.
use alloy::network::EthereumWallet;
use alloy::primitives::{address, Address, U256};
use alloy::providers::ProviderBuilder;
use alloy::sol;
use hmac::{Hmac, Mac};
use monzero_dex_evm::evm_key::evm_account;
use serde_json::Value;
use sha2::Sha512;

const ROUTER: Address = address!("10ED43C718714eb63d5aA57B78B54704E256024E");
const WBNB: Address = address!("bb4CdB9CBd36B01bD1cBaEBF2De08d9173bc095c");
const USDT: Address = address!("55d398326f99059fF775485246999027B3197955");

sol! {
    #[sol(rpc)]
    interface IRouter {
        function getAmountsOut(uint amountIn, address[] calldata path) external view returns (uint[] memory amounts);
        function swapExactETHForTokens(uint amountOutMin, address[] calldata path, address to, uint deadline) external payable returns (uint[] memory amounts);
    }
    #[sol(rpc)]
    interface IErc20 {
        function balanceOf(address who) external view returns (uint256);
    }
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

fn show(v: U256) -> String {
    format!("{:.6}", v.to_string().parse::<f64>().unwrap() / 1e18)
}

#[tokio::main]
async fn main() {
    let env = |k: &str, d: &str| std::env::var(k).unwrap_or_else(|_| d.into());
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE", "")).expect("ACCOUNTS_FILE")).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().expect("mnemonic"));
    let key = evm_account(&seed, env("INDEX", "").parse().expect("INDEX")).unwrap();
    let me = key.address();
    let rpc = env("RPC", "https://bsc-rpc.publicnode.com");
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(key)).connect_http(rpc.parse().unwrap());
    use alloy::providers::Provider;
    assert_eq!(provider.get_chain_id().await.unwrap(), 56, "not BNB Smart Chain");
    let router = IRouter::new(ROUTER, &provider);
    let usdt = IErc20::new(USDT, &provider);
    let bnb = provider.get_balance(me).await.unwrap();
    let amount: U256 = env("SWAP_WEI", "").parse().expect("SWAP_WEI");
    let keep: U256 = env("KEEP_WEI", "1000000000000000").parse().unwrap();
    println!("account {me}: {} BNB, {} USDT", show(bnb), show(usdt.balanceOf(me).call().await.unwrap()));
    assert!(amount + keep <= bnb, "SWAP_WEI would leave less than KEEP_WEI for gas");
    let path = vec![WBNB, USDT];
    let quote = router.getAmountsOut(amount, path.clone()).call().await.unwrap()[1];
    let min = quote * U256::from(98) / U256::from(100);
    println!("quote: {} BNB -> {} USDT (accepting at least {})", show(amount), show(quote), show(min));
    if env("GO", "") != "1" {
        println!("dry run: nothing sent (GO=1 to swap)");
        return;
    }
    let deadline = U256::from(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs() + 600);
    let pending = router.swapExactETHForTokens(min, path, me, deadline).value(amount).send().await.expect("send");
    let hash = *pending.tx_hash();
    println!("sent {hash}");
    // Poll the receipt itself (publicnode refuses the filter queries get_receipt uses)
    let receipt = loop {
        if let Ok(Some(r)) = provider.get_transaction_receipt(hash).await {
            break r;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    };
    println!("mined in block {:?}, status {}", receipt.block_number, receipt.status());
    println!("now: {} BNB, {} USDT", show(provider.get_balance(me).await.unwrap()), show(usdt.balanceOf(me).call().await.unwrap()));
}
