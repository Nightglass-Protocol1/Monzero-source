//! Direct (coin <-> coin) trades end to end against the real contract on a local node: two
//! deployments of `XmzSwap` stand for the two chains, and both traders run `DirectDriver` over
//! `AlloyChain`, exactly as the app would on BNB and Ethereum.
//!
//!   (cd dex-contracts && npx hardhat compile && npx hardhat node)
//!   MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --test direct_local
//!
//! Skipped (passes trivially) when MONZERO_EVM_RPC is not set.

use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{keccak256, Address, Bytes, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::direct::{DirectOffer, DirectSession, DirectTiming, Leg, Side};
use monzero_dex_evm::direct_driver::{DirectDriver, DirectRole, MemoryDirectJournal};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::Chain;
use std::str::FromStr;

// Hardhat's well-known development accounts 0 and 1 (test keys, never funded anywhere real)
const LEADER_KEY: &str = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const FOLLOWER_KEY: &str = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d";

static CHAIN: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

fn signer(key: &str) -> PrivateKeySigner {
    key.parse().unwrap()
}

async fn deploy(url: &str) -> Address {
    let artifact: serde_json::Value = serde_json::from_str(
        &std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json")).expect("the contract to be compiled (npx hardhat compile)"),
    )
    .unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(signer(LEADER_KEY))).connect_http(url.parse().unwrap());
    let receipt = provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap();
    receipt.contract_address.unwrap()
}

async fn advance(url: &str, seconds: u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request("evm_increaseTime".into(), [seconds]).await.unwrap();
    let _: serde_json::Value = provider.raw_request("evm_mine".into(), Vec::<serde_json::Value>::new()).await.unwrap();
}

async fn now(url: &str) -> (u64, u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let head = provider.get_block_number().await.unwrap();
    let block = provider.get_block_by_number(head.into()).await.unwrap().unwrap();
    (head, block.header.timestamp)
}

async fn balance(url: &str, who: Address) -> U256 {
    ProviderBuilder::new().connect_http(url.parse().unwrap()).get_balance(who).await.unwrap()
}

type Driver = DirectDriver<AlloyChain, AlloyChain, MemoryDirectJournal>;

struct Setup {
    url: String,
    leader: Driver,
    follower: Driver,
}

async fn setup(url: String, tag: &[u8]) -> Setup {
    let (ca, cb) = (deploy(&url).await, deploy(&url).await);
    let (la, fa) = (signer(LEADER_KEY), signer(FOLLOWER_KEY));
    let (head, t) = now(&url).await;
    let offer = DirectOffer {
        trade_id: keccak256([tag, &t.to_be_bytes()].concat()),
        leader: la.address(),
        follower: fa.address(),
        // "ETH" on A, "BNB" on B
        leg_a: Leg { chain_id: Chain::Local.id(), contract: ca, asset: Address::ZERO, value: U256::from(10u64.pow(18)), duration1: 3600, duration2: 8 * 3600 },
        leg_b: Leg { chain_id: Chain::Local.id(), contract: cb, asset: Address::ZERO, value: U256::from(3 * 10u64.pow(18)), duration1: 3600, duration2: 4 * 3600 },
        expires_at: t + 600,
    };
    let (ls, fs) = (SwapSeed::from_wallet_seed(tag), SwapSeed::from_wallet_seed(&[tag, b"/follower"].concat()));
    let mut lsess = DirectSession::start(&ls, offer.clone(), Side::Leader, &la, t).unwrap();
    let mut fsess = DirectSession::start(&fs, offer.clone(), Side::Follower, &fa, t).unwrap();
    fsess.accept_theirs(&lsess.frame().clone(), t).unwrap();
    lsess.accept_theirs(&fsess.frame().clone(), t).unwrap();
    let deadline = t + 2 * 3600;
    let lsaved = lsess.saved(DirectTiming::DEFAULT, deadline, head, head).unwrap();
    let fsaved = fsess.saved(DirectTiming::DEFAULT, deadline, head, head).unwrap();
    let transcript = lsaved.transcript;
    let chain = |contract, key: &PrivateKeySigner| AlloyChain::new(&url, Chain::Local, contract, head, key.clone()).unwrap();
    let mut leader = DirectDriver::start(lsaved, &ls, &la, chain(ca, &la), chain(cb, &la), MemoryDirectJournal(None)).unwrap();
    let mut follower = DirectDriver::start(fsaved, &fs, &fa, chain(ca, &fa), chain(cb, &fa), MemoryDirectJournal(None)).unwrap();
    leader.approve(transcript).unwrap();
    follower.approve(transcript).unwrap();
    Setup { url, leader, follower }
}

async fn run(s: &mut Setup, leader_online: impl Fn(u32) -> bool, rounds: u32, step: u64) -> (DirectRole, DirectRole) {
    let (mut l, mut f) = (s.leader.journal().0.clone().unwrap().state, s.follower.journal().0.clone().unwrap().state);
    for round in 0..rounds {
        if leader_online(round) && !l.is_done() {
            l = s.leader.tick().await.unwrap();
        }
        if !f.is_done() {
            f = s.follower.tick().await.unwrap();
        }
        if l.is_done() && f.is_done() {
            break;
        }
        advance(&s.url, step).await;
    }
    (l, f)
}

#[tokio::test]
async fn direct_swap_on_the_real_contract() {
    let Ok(url) = std::env::var("MONZERO_EVM_RPC") else { return };
    let _chain = CHAIN.lock().await;
    let mut s = setup(url.clone(), b"direct swap").await;
    let (la, fa) = (signer(LEADER_KEY).address(), signer(FOLLOWER_KEY).address());
    let before = (balance(&url, la).await, balance(&url, fa).await);
    let (l, f) = run(&mut s, |_| true, 60, 60).await;
    assert_eq!(l.stage(), "Done(Swapped)");
    assert_eq!(f.stage(), "Done(Swapped)");
    // On one local chain both legs pay in the same coin: the leader gave 1 and got 3, less fees
    let after = (balance(&url, la).await, balance(&url, fa).await);
    let gained = after.0 - before.0;
    assert!(gained > U256::from(19u64 * 10u64.pow(17)) && gained < U256::from(2 * 10u64.pow(18)), "leader +2 less fees: {gained}");
    assert!(before.1 - after.1 > U256::from(2 * 10u64.pow(18)), "follower -2 plus fees");
}

#[tokio::test]
async fn a_leader_that_vanishes_leaves_both_refunded_on_the_real_contract() {
    let Ok(url) = std::env::var("MONZERO_EVM_RPC") else { return };
    let _chain = CHAIN.lock().await;
    let mut s = setup(url.clone(), b"leader vanishes").await;
    // Leader: lock and Ready only (two rounds), then gone for 12 h of chain time; then back
    let (l, f) = run(&mut s, |round| round < 2 || round >= 50, 80, 900).await;
    assert_eq!(f.stage(), "Done(Refunded)");
    assert_eq!(l.stage(), "Done(Refunded)");
}
