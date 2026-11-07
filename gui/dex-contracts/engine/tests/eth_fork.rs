//! A whole swap on a local copy of Ethereum mainnet, through the client and transactions a real
//! trade uses: chain id 1, real base fee, Ethereum's 96-block wait. Nothing reaches the real chain.
//!
//!   ETH_FORK_RPC=https://ethereum-rpc.publicnode.com npx hardhat node     (in dex-contracts)
//!   MONZERO_ETH_FORK=http://127.0.0.1:8545 cargo test --test eth_fork -- --test-threads=1
//!
//! Skipped when MONZERO_ETH_FORK is not set.

use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, Bytes, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::driver::{EvmCall, EvmChain};
use monzero_dex_evm::*;
use std::str::FromStr;

// Hardhat's public development account 0. On the real chains it holds EIP-7702 delegated code,
// which this copy of mainnet shows too.
const DELEGATED: &str = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266";

fn fork() -> Option<String> {
    let url = std::env::var("MONZERO_ETH_FORK").ok()?;
    assert!(url.starts_with("http://127.0.0.1:"), "this test uses a local node's test methods: local nodes only, not {url}");
    Some(url)
}

/// A new account holding 100 ETH on the local node only, with no code
async fn funded(url: &str) -> PrivateKeySigner {
    let key = PrivateKeySigner::random();
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let hundred = format!("{:#x}", U256::from(100u64) * U256::from(10u64.pow(18)));
    let _: serde_json::Value = provider.raw_request("hardhat_setBalance".into(), (key.address(), hundred)).await.unwrap();
    key
}

fn secret() -> B256 {
    loop {
        let candidate = B256::from(rand::random::<[u8; 32]>());
        if commitment(&candidate).is_ok() {
            return candidate;
        }
    }
}

async fn deploy(url: &str, key: &PrivateKeySigner) -> Address {
    let artifact: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json")).expect("compiled contract")).unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(key.clone())).connect_http(url.parse().unwrap());
    let receipt = provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap();
    receipt.contract_address.unwrap()
}

/// Mine `blocks` blocks 12 seconds apart, as on Ethereum
async fn mine(url: &str, blocks: u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    for _ in 0..blocks {
        let _: serde_json::Value = provider.raw_request("evm_increaseTime".into(), [12]).await.unwrap();
        let _: serde_json::Value = provider.raw_request("evm_mine".into(), Vec::<serde_json::Value>::new()).await.unwrap();
    }
}

async fn send(chain: &AlloyChain, call: &EvmCall, url: &str) {
    let raw = chain.sign(call).await.expect("the call to be accepted and signed");
    chain.broadcast(&raw).await.unwrap();
    mine(url, 1).await;
}

async fn balance(url: &str, who: Address) -> U256 {
    ProviderBuilder::new().connect_http(url.parse().unwrap()).get_balance(who).await.unwrap()
}

struct World {
    url: String,
    owner: AlloyChain,
    owner_key: PrivateKeySigner,
    claimer: AlloyChain,
    terms: AgreedTerms,
    claim_secret: B256,
    refund_secret: B256,
}

async fn world(url: String) -> World {
    let (owner_key, claimer_key) = (funded(&url).await, funded(&url).await);
    let contract = deploy(&url, &owner_key).await;
    mine(&url, 1).await;
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let from_block = provider.get_block_number().await.unwrap();
    let (claim_secret, refund_secret) = (secret(), secret());
    let terms = AgreedTerms {
        chain: Chain::EthMainnet,
        contract,
        owner: owner_key.address(),
        claimer: claimer_key.address(),
        claim_commitment: commitment(&claim_secret).unwrap(),
        refund_commitment: commitment(&refund_secret).unwrap(),
        asset: Address::ZERO,
        value: U256::from(5_000_000_000_000_000u64),
        duration1: 2 * 3600,
        duration2: 2 * 3600,
    };
    let chain = |key: &PrivateKeySigner| AlloyChain::new(&url, Chain::EthMainnet, contract, from_block, key.clone()).unwrap();
    let (owner, claimer) = (chain(&owner_key), chain(&claimer_key));
    World { url, owner, owner_key, claimer, terms, claim_secret, refund_secret }
}

impl World {
    /// The owner locks, and the claimer finds and checks the lock after the wait
    async fn locked(&self) -> Swap {
        send(&self.owner, &EvmCall::NewSwap { terms: self.terms.clone(), nonce: U256::from(rand::random::<u64>()) }, &self.url).await;
        assert!(self.claimer.find_lock(&self.terms).await.unwrap().is_none(), "not visible before the wait");
        mine(&self.url, Chain::EthMainnet.confirmations()).await;
        let lock = self.claimer.find_lock(&self.terms).await.unwrap().expect("the lock after the wait");
        assert_eq!(lock.chain_id, 1);
        check_lock(&self.terms, &lock).expect("the lock to match the terms")
    }
}

#[tokio::test]
async fn swap_completes_on_chain_one() {
    let Some(url) = fork() else { return };
    let w = world(url).await;
    assert_eq!(w.owner.check_network().await.unwrap(), 1);

    let swap = w.locked().await;
    let id = swap_id(&swap);
    assert_eq!(w.claimer.stage(id).await.unwrap(), Stage::Pending);

    send(&w.owner, &EvmCall::SetReady(swap.clone()), &w.url).await;
    assert_eq!(w.claimer.stage(id).await.unwrap(), Stage::Pending, "ready is not trusted before the wait");
    mine(&w.url, Chain::EthMainnet.confirmations()).await;
    assert_eq!(w.claimer.stage(id).await.unwrap(), Stage::Ready);

    let before = balance(&w.url, w.terms.claimer).await;
    send(&w.claimer, &EvmCall::Claim(swap.clone(), w.claim_secret), &w.url).await;
    let gained = balance(&w.url, w.terms.claimer).await - before;
    assert!(gained > w.terms.value - U256::from(1_000_000_000_000_000u64) && gained <= w.terms.value, "claimer received the value less gas: {gained}");

    // The owner reads the secret without the finality wait, two blocks later, and can sweep the XMZ
    mine(&w.url, Chain::EthMainnet.search_lag()).await;
    assert_eq!(w.owner.revealed(&swap).await.unwrap(), Some(Revealed::Claimed(w.claim_secret)));
}

#[tokio::test]
async fn owner_gets_a_refund_back_on_chain_one() {
    let Some(url) = fork() else { return };
    let w = world(url).await;
    let swap = w.locked().await;
    let before = balance(&w.url, w.terms.owner).await;
    send(&w.owner, &EvmCall::Refund(swap.clone(), w.refund_secret), &w.url).await;
    assert!(balance(&w.url, w.terms.owner).await > before, "owner received the value back");
    mine(&w.url, Chain::EthMainnet.search_lag()).await;
    assert_eq!(w.claimer.revealed(&swap).await.unwrap(), Some(Revealed::Refunded(w.refund_secret)));
}

#[tokio::test]
async fn nothing_is_signed_when_the_endpoint_is_on_another_chain() {
    let Some(url) = fork() else { return };
    let w = world(url).await;
    let wrong = AlloyChain::new(&w.url, Chain::BscMainnet, w.terms.contract, 0, w.owner_key.clone()).unwrap();
    let call = EvmCall::NewSwap { terms: w.terms.clone(), nonce: U256::from(1) };
    let refusal = wrong.sign(&call).await.unwrap_err().to_string();
    assert!(refusal.contains("endpoint is on chain 1"), "{refusal}");
    assert!(wrong.check_network().await.is_err());
}

#[tokio::test]
async fn an_account_with_code_on_chain_is_refused() {
    let Some(url) = fork() else { return };
    let w = world(url).await;
    w.owner.check_plain_accounts(&[w.terms.owner, w.terms.claimer]).await.expect("new accounts are plain");
    let delegated: Address = DELEGATED.parse().unwrap();
    let refusal = w.owner.check_plain_accounts(&[w.terms.owner, delegated]).await.unwrap_err().to_string();
    assert!(refusal.contains("has code on chain"), "{refusal}");
}
