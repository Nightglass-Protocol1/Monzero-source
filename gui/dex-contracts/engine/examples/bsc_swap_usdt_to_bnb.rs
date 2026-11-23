//! Swaps a test account's USDT (BEP-20) for BNB through PancakeSwap's v2 router and pays the BNB
//! straight to TO (the owner's address), to return test funds. The account is rebuilt from the
//! phrase file (never printed).
//!   ACCOUNTS_FILE=<…-accounts.json> INDEX=<i> USDT_WEI=<units> TO=<0x…> [GO=1] cargo run --release --example bsc_swap_usdt_to_bnb
//! Without GO=1 it only shows the balances and the quote (nothing is sent). Accepts at most 2%
//! less BNB than quoted. Approves the router for exactly USDT_WEI first when needed.
use alloy::network::EthereumWallet;
use alloy::primitives::{address, Address, U256};
use alloy::providers::{Provider, ProviderBuilder};
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
        function swapExactTokensForETH(uint amountIn, uint amountOutMin, address[] calldata path, address to, uint deadline) external returns (uint[] memory amounts);
    }
    #[sol(rpc)]
    interface IErc20 {
        function balanceOf(address who) external view returns (uint256);
        function allowance(address owner, address spender) external view returns (uint256);
        function approve(address spender, uint256 value) external returns (bool);
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
    format!("{:.8}", v.to_string().parse::<f64>().unwrap() / 1e18)
}

/// Polls the receipt itself (publicnode refuses the filter queries get_receipt uses)
async fn mined<P: Provider>(provider: &P, hash: alloy::primitives::B256) -> bool {
    loop {
        if let Ok(Some(r)) = provider.get_transaction_receipt(hash).await {
            println!("  mined in block {:?}, status {}", r.block_number, r.status());
            return r.status();
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
}

#[tokio::main]
async fn main() {
    let env = |k: &str| std::env::var(k).unwrap_or_default();
    let accounts: Value = serde_json::from_str(&std::fs::read_to_string(env("ACCOUNTS_FILE")).expect("ACCOUNTS_FILE")).unwrap();
    let seed = bip39_seed(accounts["mnemonic"].as_str().expect("mnemonic"));
    let key = evm_account(&seed, env("INDEX").parse().expect("INDEX")).unwrap();
    let me = key.address();
    let to: Address = env("TO").parse().expect("TO");
    let rpc = { let r = env("RPC"); if r.is_empty() { "https://bsc-rpc.publicnode.com".to_string() } else { r } };
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(key)).connect_http(rpc.parse().unwrap());
    assert_eq!(provider.get_chain_id().await.unwrap(), 56, "not BNB Smart Chain");
    let router = IRouter::new(ROUTER, &provider);
    let usdt = IErc20::new(USDT, &provider);
    let amount: U256 = env("USDT_WEI").parse().expect("USDT_WEI");
    let held = usdt.balanceOf(me).call().await.unwrap();
    let gas = provider.get_balance(me).await.unwrap();
    println!("account {me}: {} BNB (gas), {} USDT", show(gas), show(held));
    assert!(amount <= held, "USDT_WEI is more than the account holds");
    let path = vec![USDT, WBNB];
    let quote = router.getAmountsOut(amount, path.clone()).call().await.unwrap()[1];
    let min = quote * U256::from(98) / U256::from(100);
    println!("quote: {} USDT -> {} BNB to {to} (accepting at least {})", show(amount), show(quote), show(min));
    if env("GO") != "1" {
        println!("dry run: nothing sent (GO=1 to swap)");
        return;
    }
    if usdt.allowance(me, ROUTER).call().await.unwrap() < amount {
        let p = usdt.approve(ROUTER, amount).send().await.expect("approve");
        println!("approve sent {}", p.tx_hash());
        assert!(mined(&provider, *p.tx_hash()).await, "approve failed");
    }
    let deadline = U256::from(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs() + 600);
    let before = provider.get_balance(to).await.unwrap();
    let p = router.swapExactTokensForETH(amount, min, path, to, deadline).send().await.expect("swap");
    println!("swap sent {}", p.tx_hash());
    assert!(mined(&provider, *p.tx_hash()).await, "swap failed");
    let after = provider.get_balance(to).await.unwrap();
    println!("{to} received {} BNB; account now {} USDT, {} BNB gas", show(after.saturating_sub(before)), show(usdt.balanceOf(me).call().await.unwrap()), show(provider.get_balance(me).await.unwrap()));
}
