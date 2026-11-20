//! A direct (coin <-> coin) trade end to end, as the apps run it: a relay (in this process), a
//! local node with two swap contracts standing for two chains, and both traders driving the
//! wallet helper's JSON commands only (`directPost`, `directListings`, `directTake`,
//! `directServe`, `directApprove`, `directTick`, `directPayout`).
//!
//!   (cd dex-contracts && npx hardhat compile && npx hardhat node)    # a fresh node
//!   MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --test direct_e2e
//!
//! Skipped when MONZERO_EVM_RPC is not set.

use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, Bytes, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::listing::Rules;
use monzero_dex_evm::Chain;
use monzero_dex_relay::chain::AlloyView;
use monzero_dex_relay::http::{router, Api};
use monzero_dex_relay::Relay;
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::str::FromStr;
use std::sync::Arc;

const FUNDER: &str = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const ETH: u128 = 1_000_000_000_000_000_000;

async fn deploy(url: &str) -> Address {
    let artifact: Value = serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json")).unwrap()).unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let signer: PrivateKeySigner = FUNDER.parse().unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer)).connect_http(url.parse().unwrap());
    provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap().contract_address.unwrap()
}

async fn send(url: &str, to: Address, wei: u128) {
    let signer: PrivateKeySigner = FUNDER.parse().unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer)).connect_http(url.parse().unwrap());
    provider.send_transaction(TransactionRequest::default().with_to(to).with_value(U256::from(wei))).await.unwrap().get_receipt().await.unwrap();
}

async fn advance(url: &str, seconds: u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: Value = provider.raw_request("evm_increaseTime".into(), [seconds]).await.unwrap();
    let _: Value = provider.raw_request("evm_mine".into(), Vec::<Value>::new()).await.unwrap();
}

async fn balance(url: &str, who: Address) -> U256 {
    ProviderBuilder::new().connect_http(url.parse().unwrap()).get_balance(who).await.unwrap()
}

struct Trader {
    seed_hex: String,
    dir: std::path::PathBuf,
}

impl Trader {
    fn address(&self) -> Address {
        monzero_dex_evm::evm_key::evm_account(&hex::decode(&self.seed_hex).unwrap(), 0).unwrap().address()
    }

    async fn call(&self, command: &str, url: &str, relay: &str, local: (Address, Address), extra: Value) -> Value {
        let mut req = json!({
            "schema": "monzero-xmz-evm-helper-request-v1",
            "command": command,
            "tradeDir": self.dir,
            "relays": [relay],
            "localDirectContracts": [local.0, local.1],
            "direct": { "rpcs": { "31337": url }, "walletSeedHex": self.seed_hex, "accountIndex": 0 },
        });
        for (k, v) in extra.as_object().unwrap() {
            req[k] = v.clone();
        }
        monzero_dex_evm::helper::handle(&serde_json::to_vec(&req).unwrap()).await
    }
}

#[tokio::test]
async fn a_direct_trade_through_a_relay_with_the_helper_commands_only() {
    let Ok(url) = std::env::var("MONZERO_EVM_RPC") else { return };
    // "BNB" and "ETH": two contracts on one local node
    let (bnb, eth) = (deploy(&url).await, deploy(&url).await);

    // The relay, in this process
    let mut rules = Rules::default();
    rules.contracts.insert(Chain::Local.id(), eth);
    let relay = Relay::new(rules, BTreeMap::from([(Chain::Local.id(), AlloyView::new(&url, Chain::Local, eth).unwrap())])).with_local_direct_contracts(vec![bnb, eth]);
    let app = router(Arc::new(Api::new(Arc::new(relay), vec![], false)));
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let relay_url = format!("http://127.0.0.1:{}", listener.local_addr().unwrap().port());
    tokio::spawn(async move { axum::serve(listener, app.into_make_service_with_connect_info::<std::net::SocketAddr>()).await.unwrap() });

    let tmp = std::env::temp_dir().join(format!("monzero-direct-e2e-{}", std::process::id()));
    let maker = Trader { seed_hex: "11".repeat(64), dir: tmp.join("maker") };
    let taker = Trader { seed_hex: "22".repeat(64), dir: tmp.join("taker") };
    send(&url, maker.address(), 5 * ETH).await;
    send(&url, taker.address(), 5 * ETH).await;

    // The maker gives 3 "BNB" for 1 "ETH"
    let local = (bnb, eth);
    let posted = maker
        .call("directPost", &url, &relay_url, local, json!({"directOffer": {"givesChainId": 31337, "givesValue": (3 * ETH).to_string(), "wantsChainId": 31337, "wantsValue": ETH.to_string(), "lifetimeSecs": 3600}}))
        .await;
    assert_eq!(posted["ok"], true, "{posted}");
    assert_eq!(posted["posted"][0]["ok"], true, "{posted}");

    let found = taker.call("directListings", &url, &relay_url, local, json!({})).await;
    assert_eq!(found["ok"], true, "{found}");
    let listing = found["listings"][0]["signed"].clone();
    assert_eq!(listing["listing"]["pair"], "ETH/ETH");

    // Take: the taker sends its keys, the maker answers, the taker reads the answer
    let first = taker.call("directTake", &url, &relay_url, local, json!({"directListing": listing})).await;
    assert_eq!(first["agreed"], false, "{first}");
    let served = maker.call("directServe", &url, &relay_url, local, json!({})).await;
    assert_eq!(served["taken"], true, "{served}");
    let agreed = taker.call("directTake", &url, &relay_url, local, json!({})).await;
    assert_eq!(agreed["agreed"], true, "{agreed}");
    assert_eq!(agreed["approved"], false, "the taker approves in the app");
    // Taken: off the board
    let found = taker.call("directListings", &url, &relay_url, local, json!({})).await;
    assert_eq!(found["listings"].as_array().unwrap().len(), 0, "{found}");

    let approved = taker.call("directApprove", &url, &relay_url, local, json!({"commitment": agreed["commitment"]})).await;
    assert_eq!(approved["approved"], true, "{approved}");

    let (mut t, mut m) = (Value::Null, Value::Null);
    for _ in 0..40 {
        t = taker.call("directTick", &url, &relay_url, local, json!({})).await;
        assert_eq!(t["ok"], true, "{t}");
        m = maker.call("directTick", &url, &relay_url, local, json!({})).await;
        assert_eq!(m["ok"], true, "{m}");
        if t["done"] == true && m["done"] == true {
            break;
        }
        advance(&url, 60).await;
    }
    assert_eq!(t["stage"], "Done(Swapped)", "{t}");
    assert_eq!(m["stage"], "Done(Swapped)", "{m}");

    // Pay out what came in: the taker got 3, the maker 1
    let to_taker = fresh_address(0x71);
    let paid = taker.call("directPayout", &url, &relay_url, local, json!({"chainId": 31337, "payoutAddress": to_taker.to_string()})).await;
    assert_eq!(paid["ok"], true, "{paid}");
    let got = balance(&url, to_taker).await;
    assert!(got > U256::from(29 * ETH / 10) && got <= U256::from(3 * ETH), "{got}");
    // Once only
    let again = taker.call("directPayout", &url, &relay_url, local, json!({"chainId": 31337, "payoutAddress": to_taker.to_string()})).await;
    assert_eq!(again["ok"], true, "{again}");
    assert_eq!(balance(&url, to_taker).await, got);
    let _ = std::fs::remove_dir_all(&tmp);
}

async fn deploy_token(url: &str) -> Address {
    let artifact: Value = serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/test/TestTokens.sol/TestToken.json")).unwrap()).unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let signer: PrivateKeySigner = FUNDER.parse().unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer)).connect_http(url.parse().unwrap());
    provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap().contract_address.unwrap()
}

alloy::sol! {
    function mint(address to, uint256 amount) external;
    function balanceOf(address who) external view returns (uint256);
}

async fn mint_tokens(url: &str, token: Address, to: Address, amount: U256) {
    let signer: PrivateKeySigner = FUNDER.parse().unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer)).connect_http(url.parse().unwrap());
    let input = alloy::sol_types::SolCall::abi_encode(&mintCall { to, amount });
    provider.send_transaction(TransactionRequest::default().with_to(token).with_input(input)).await.unwrap().get_receipt().await.unwrap();
}

async fn token_balance(url: &str, token: Address, who: Address) -> U256 {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let input = alloy::sol_types::SolCall::abi_encode(&balanceOfCall { who });
    let out = provider.call(TransactionRequest::default().with_to(token).with_input(input)).await.unwrap();
    U256::from_be_slice(&out[..32])
}

/// A USDT (BEP-20) for coin trade on one chain, as BNB/USDT is on BNB Smart Chain: the maker
/// gives 25 test USDT for 1 "ETH". Its lock first lets the contract take the tokens; the taker's
/// payout sends exactly the 25 tokens, and the maker's other tokens stay where they are.
#[tokio::test]
async fn a_usdt_for_coin_trade_on_one_chain_through_a_relay() {
    let Ok(url) = std::env::var("MONZERO_EVM_RPC") else { return };
    let (swap, usdt) = (deploy(&url).await, deploy_token(&url).await);
    monzero_dex_evm::chain::set_local_usdt_token(usdt);
    let mut rules = Rules::default();
    rules.contracts.insert(Chain::Local.id(), swap);
    let relay = Relay::new(rules, BTreeMap::from([(Chain::Local.id(), AlloyView::new(&url, Chain::Local, swap).unwrap())])).with_local_direct_contracts(vec![swap]);
    let app = router(Arc::new(Api::new(Arc::new(relay), vec![], false)));
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let relay_url = format!("http://127.0.0.1:{}", listener.local_addr().unwrap().port());
    tokio::spawn(async move { axum::serve(listener, app.into_make_service_with_connect_info::<std::net::SocketAddr>()).await.unwrap() });

    let tmp = std::env::temp_dir().join(format!("monzero-direct-e2e-usdt-{}", std::process::id()));
    let maker = Trader { seed_hex: "33".repeat(64), dir: tmp.join("maker") };
    let taker = Trader { seed_hex: "44".repeat(64), dir: tmp.join("taker") };
    send(&url, maker.address(), ETH / 10).await;   // fees only
    send(&url, taker.address(), 2 * ETH).await;
    let units = |n: u64| U256::from(n) * U256::from(ETH);
    mint_tokens(&url, usdt, maker.address(), units(40)).await;
    let local = (swap, swap);
    let token = json!({"localUsdtToken": usdt});
    let with = |mut v: Value| { v["localUsdtToken"] = token["localUsdtToken"].clone(); v };

    // Too few tokens: refused before anything is posted
    let short = maker
        .call("directPost", &url, &relay_url, local, with(json!({"directOffer": {"givesChainId": 31337, "givesValue": units(50).to_string(), "givesToken": "usdt", "wantsChainId": 31337, "wantsValue": ETH.to_string(), "lifetimeSecs": 3600}})))
        .await;
    assert_eq!(short["ok"], false, "{short}");
    assert!(short["error"].as_str().unwrap().contains("USDT"), "{short}");
    let posted = maker
        .call("directPost", &url, &relay_url, local, with(json!({"directOffer": {"givesChainId": 31337, "givesValue": units(25).to_string(), "givesToken": "usdt", "wantsChainId": 31337, "wantsValue": ETH.to_string(), "lifetimeSecs": 3600}})))
        .await;
    assert_eq!(posted["ok"], true, "{posted}");
    assert_eq!(posted["posted"][0]["ok"], true, "{}", posted["posted"]);
    let found = taker.call("directListings", &url, &relay_url, local, with(json!({}))).await;
    let listing = found["listings"][0]["signed"].clone();
    assert_eq!(listing["listing"]["pair"], "ETH/USDT-BSC", "{found}");

    let first = taker.call("directTake", &url, &relay_url, local, with(json!({"directListing": listing}))).await;
    let served = maker.call("directServe", &url, &relay_url, local, with(json!({}))).await;
    assert_eq!(served["taken"], true, "{first}\n{served}");
    let agreed = taker.call("directTake", &url, &relay_url, local, with(json!({}))).await;
    assert_eq!(agreed["agreed"], true, "{agreed}");
    assert_eq!(agreed["gets"]["coin"], "USDT-BSC", "{agreed}");
    assert_eq!(agreed["pair"], "ETH/USDT-BSC", "{agreed}");
    let approved = taker.call("directApprove", &url, &relay_url, local, with(json!({"commitment": agreed["commitment"]}))).await;
    assert_eq!(approved["approved"], true, "{approved}");

    let (mut t, mut m) = (Value::Null, Value::Null);
    for _ in 0..40 {
        t = taker.call("directTick", &url, &relay_url, local, with(json!({}))).await;
        assert_eq!(t["ok"], true, "{t}");
        m = maker.call("directTick", &url, &relay_url, local, with(json!({}))).await;
        assert_eq!(m["ok"], true, "{m}");
        if t["done"] == true && m["done"] == true {
            break;
        }
        advance(&url, 60).await;
    }
    assert_eq!(t["stage"], "Done(Swapped)", "{t}");
    assert_eq!(m["stage"], "Done(Swapped)", "{m}");
    assert_eq!(token_balance(&url, usdt, maker.address()).await, units(15), "the maker's other tokens stayed");

    // The taker's payout: exactly the 25 USDT, once
    let to_taker = fresh_address(0x72);
    let paid = taker.call("directPayout", &url, &relay_url, local, with(json!({"chainId": 31337, "payoutAddress": to_taker.to_string()}))).await;
    assert_eq!(paid["ok"], true, "{paid}");
    assert_eq!(token_balance(&url, usdt, to_taker).await, units(25));
    let again = taker.call("directPayout", &url, &relay_url, local, with(json!({"chainId": 31337, "payoutAddress": to_taker.to_string()}))).await;
    assert_eq!(again["ok"], true, "{again}");
    assert_eq!(token_balance(&url, usdt, to_taker).await, units(25));
    // The maker's ETH
    let to_maker = fresh_address(0x73);
    let paid = maker.call("directPayout", &url, &relay_url, local, with(json!({"chainId": 31337, "payoutAddress": to_maker.to_string()}))).await;
    assert_eq!(paid["ok"], true, "{paid}");
    assert!(balance(&url, to_maker).await > U256::from(ETH * 99 / 100));
    let _ = std::fs::remove_dir_all(&tmp);
}

/// A payout address no earlier run on the same node has used
fn fresh_address(tag: u8) -> Address {
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
    Address::from_slice(&alloy::primitives::keccak256(format!("{tag}-{nanos}-{}", std::process::id()))[12..])
}
