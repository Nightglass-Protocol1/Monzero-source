//! End to end against a local node: both traders driven from Rust through this crate.
//!
//!   (cd dex-contracts && npx hardhat compile && npx hardhat node)
//!   MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --test local_chain
//!
//! Skipped (passes trivially) when MONZERO_EVM_RPC is not set.

use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, Bytes, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::{TransactionReceipt, TransactionRequest};
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::*;
use std::str::FromStr;

// Hardhat's well-known development accounts 0 and 1 (test keys, never funded anywhere real)
const OWNER_KEY: &str = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const CLAIMER_KEY: &str = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d";
const D1: u64 = 3 * 3600;
const D2: u64 = 6 * 3600;

/// The tests share one chain: the same owner account (nonces) and the chain clock
static CHAIN: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

fn rpc() -> Option<String> {
    std::env::var("MONZERO_EVM_RPC").ok()
}

fn signer(key: &str) -> PrivateKeySigner {
    key.parse().expect("the test key to parse")
}

fn random_secret() -> B256 {
    loop {
        let candidate = B256::from(rand::random::<[u8; 32]>());
        if commitment(&candidate).is_ok() {
            return candidate;
        }
    }
}

async fn deploy(url: &str) -> Address {
    let artifact: serde_json::Value = serde_json::from_str(
        &std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json"))
            .expect("the contract to be compiled (npx hardhat compile)"),
    )
    .unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer(OWNER_KEY))).connect_http(url.parse().unwrap());
    let receipt = provider
        .send_transaction(TransactionRequest::default().with_deploy_code(code))
        .await
        .unwrap()
        .get_receipt()
        .await
        .unwrap();
    receipt.contract_address.expect("a deployment to create a contract")
}

async fn advance(url: &str, seconds: u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request("evm_increaseTime".into(), [seconds]).await.unwrap();
    let _: serde_json::Value = provider.raw_request("evm_mine".into(), Vec::<serde_json::Value>::new()).await.unwrap();
}

/// What the claimer observes about the owner's lock
async fn observe(url: &str, contract: Address, receipt: &TransactionReceipt) -> ObservedLock {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let log = receipt
        .inner
        .logs()
        .iter()
        .find_map(|log| log.log_decode::<XmzSwap::New>().ok())
        .expect("newSwap to emit New");
    let block = provider
        .get_block_by_number(receipt.block_number.unwrap().into())
        .await
        .unwrap()
        .expect("the lock block to exist");
    let stage = XmzSwap::new(contract, &provider).swaps(log.inner.data.swapId).call().await.unwrap();
    ObservedLock { chain_id: provider.get_chain_id().await.unwrap(), log, block_timestamp: block.header.timestamp, stage }
}

struct Setup {
    url: String,
    contract: Address,
    owner: Address,
    claimer: Address,
    claim_secret: B256,
    refund_secret: B256,
    terms: AgreedTerms,
}

async fn setup(url: String) -> Setup {
    let contract = deploy(&url).await;
    let claim_secret = random_secret();
    let refund_secret = random_secret();
    let owner = signer(OWNER_KEY).address();
    let claimer = signer(CLAIMER_KEY).address();
    let terms = AgreedTerms {
        chain: Chain::Local,
        contract,
        owner,
        claimer,
        claim_commitment: commitment(&claim_secret).unwrap(),
        refund_commitment: commitment(&refund_secret).unwrap(),
        asset: Address::ZERO,
        value: U256::from(10u64.pow(18)),
        duration1: D1,
        duration2: D2,
    };
    Setup { url, contract, owner, claimer, claim_secret, refund_secret, terms }
}

async fn lock(s: &Setup, value: U256) -> TransactionReceipt {
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer(OWNER_KEY))).connect_http(s.url.parse().unwrap());
    XmzSwap::new(s.contract, &provider)
        .newSwap(
            s.claimer,
            s.terms.claim_commitment,
            s.terms.refund_commitment,
            U256::from(D1),
            U256::from(D2),
            Address::ZERO,
            value,
            U256::from_be_bytes(rand::random::<[u8; 32]>()),
        )
        .value(value)
        .send()
        .await
        .unwrap()
        .get_receipt()
        .await
        .unwrap()
}

#[tokio::test]
async fn swap_succeeds_with_both_sides_using_the_engine_checks() {
    let Some(url) = rpc() else { return };
    let _chain = CHAIN.lock().await;
    let s = setup(url).await;
    let owner_p = ProviderBuilder::new().wallet(EthereumWallet::from(signer(OWNER_KEY))).connect_http(s.url.parse().unwrap());
    let claimer_p = ProviderBuilder::new().wallet(EthereumWallet::from(signer(CLAIMER_KEY))).connect_http(s.url.parse().unwrap());

    // Owner locks; claimer checks the lock against the terms before locking XMZ
    let receipt = lock(&s, s.terms.value).await;
    let swap = check_lock(&s.terms, &observe(&s.url, s.contract, &receipt).await).expect("the lock to match the terms");
    assert_eq!(swap.owner, s.owner);

    // (XMZ lock happens here.) Owner confirms it with setReady
    XmzSwap::new(s.contract, &owner_p).setReady(swap.clone()).send().await.unwrap().get_receipt().await.unwrap();
    let stage = Stage::try_from(XmzSwap::new(s.contract, &claimer_p).swaps(swap_id(&swap)).call().await.unwrap()).unwrap();
    let now = claimer_p.get_block_by_number(Default::default()).await.unwrap().unwrap().header.timestamp;
    assert!(claim_window(&swap, stage, now, 1800));

    // Claimer claims and receives the value
    let before = claimer_p.get_balance(s.claimer).await.unwrap();
    let claim = XmzSwap::new(s.contract, &claimer_p).claim(swap.clone(), s.claim_secret).send().await.unwrap().get_receipt().await.unwrap();
    let gas = U256::from(claim.gas_used) * U256::from(claim.effective_gas_price);
    assert_eq!(claimer_p.get_balance(s.claimer).await.unwrap(), before + s.terms.value - gas);

    // Owner finds the claimer's secret in the claim's events: it can now sweep the XMZ
    assert_eq!(revealed_secret(claim.inner.logs(), s.contract, &swap), Some(Revealed::Claimed(s.claim_secret)));
}

#[tokio::test]
async fn owner_refunds_and_claimer_learns_the_refund_secret() {
    let Some(url) = rpc() else { return };
    let _chain = CHAIN.lock().await;
    let s = setup(url).await;
    let owner_p = ProviderBuilder::new().wallet(EthereumWallet::from(signer(OWNER_KEY))).connect_http(s.url.parse().unwrap());

    let receipt = lock(&s, s.terms.value).await;
    let swap = check_lock(&s.terms, &observe(&s.url, s.contract, &receipt).await).unwrap();

    // Claimer never locks XMZ; owner never calls setReady. Between the timeouts nobody but
    // the claimer may act, so a refund then must fail; from timeout2 it succeeds.
    advance(&s.url, D1 + 60).await;
    let early = XmzSwap::new(s.contract, &owner_p).refund(swap.clone(), s.refund_secret).send().await;
    assert!(early.is_err(), "refund between the timeouts must be refused");
    advance(&s.url, D2).await;
    let refund = XmzSwap::new(s.contract, &owner_p).refund(swap.clone(), s.refund_secret).send().await.unwrap().get_receipt().await.unwrap();
    assert!(refund.status());

    // The claimer reads the owner's secret: it can now sweep its XMZ back
    assert_eq!(revealed_secret(refund.inner.logs(), s.contract, &swap), Some(Revealed::Refunded(s.refund_secret)));
}

#[tokio::test]
async fn claimer_refuses_a_lock_that_differs_from_the_terms() {
    let Some(url) = rpc() else { return };
    let _chain = CHAIN.lock().await;
    let s = setup(url).await;
    // The owner locks less than agreed
    let receipt = lock(&s, s.terms.value - U256::from(1)).await;
    let result = check_lock(&s.terms, &observe(&s.url, s.contract, &receipt).await);
    assert_eq!(result, Err(Error::TermsMismatch("value")));
}
