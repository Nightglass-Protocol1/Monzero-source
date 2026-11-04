//! Two drivers trading against each other on a local node, with a stand-in XMZ side.
//!
//!   (cd dex-contracts && npx hardhat compile && npx hardhat node)
//!   MONZERO_EVM_RPC=http://127.0.0.1:8545 cargo test --test driver_local
//!
//! Skipped (passes trivially) when MONZERO_EVM_RPC is not set. The swap over real XMZ wallets
//! also needs MONZERO_XMZ_BIN (see tests/xmz_local.rs).

mod common;

use alloy::consensus::{Transaction, TxEnvelope};
use alloy::eips::Decodable2718;
use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, Bytes, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::chain::AlloyChain;
use monzero_dex_evm::driver::*;
use monzero_dex_evm::machine::{ClaimerState, OwnerState, Outcome, Timing, Trade, XmzLock};
use monzero_dex_evm::secret::{combined_spend_key, SecretShare};
use monzero_dex_evm::exchange::{SavedTrade, Session};
use monzero_dex_evm::negotiation::{KeyExchange, LocalKeys, Offer, Role};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::xmz::{SharedXmz, WalletRpcConfig, WalletRpcXmz, XmzNetwork};
use monzero_dex_evm::{AgreedTerms, Chain};
use serde_json::json;
use std::str::FromStr;
use std::sync::{Arc, Mutex};

// Hardhat's well-known development accounts (test keys only)
const DEPLOYER_KEY: &str = "0x5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a";
const OWNER_KEY: &str = "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80";
const CLAIMER_KEY: &str = "0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d";
const D1: u64 = 3 * 3600;
const D2: u64 = 6 * 3600;
const XMZ: u64 = 5_000_000_000_000;

static CHAIN: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

// ---------- stand-in XMZ side ----------

#[derive(Default)]
struct Ledger {
    lock: Option<XmzLock>,
    lock_sends: u32,
    swept_by: Option<&'static str>,
}

/// One trader's view of the shared XMZ address. A sweep succeeds only with the exact
/// combined spend key, so a wrong secret encoding or combination fails the test.
#[derive(Clone)]
struct StandInXmz {
    ledger: Arc<Mutex<Ledger>>,
    who: &'static str,
    shared_key: [u8; 32],
}

impl StandInXmz {
    fn mine(&self, blocks: u64) {
        if let Some(lock) = self.ledger.lock().unwrap().lock.as_mut() {
            lock.confirmations += blocks;
        }
    }
}

impl XmzSide for StandInXmz {
    async fn lock_status(&self) -> Result<Option<XmzLock>, DriverError> {
        Ok(self.ledger.lock().unwrap().lock)
    }

    async fn ensure_lock(&self, amount: u64) -> Result<(), DriverError> {
        let mut l = self.ledger.lock().unwrap();
        if l.lock.is_none() {
            l.lock = Some(XmzLock { amount, confirmations: 0 });
            l.lock_sends += 1;
        }
        Ok(())
    }

    async fn ensure_sweep(&self, spend_key: [u8; 32]) -> Result<(), DriverError> {
        assert_eq!(spend_key, self.shared_key, "{} swept with a wrong key", self.who);
        let mut l = self.ledger.lock().unwrap();
        assert!(l.lock.is_some(), "nothing to sweep");
        if l.swept_by.is_none() {
            l.swept_by = Some(self.who);
        }
        Ok(())
    }

    async fn swept(&self) -> Result<bool, DriverError> {
        Ok(self.ledger.lock().unwrap().swept_by == Some(self.who))
    }
}

// ---------- setup ----------

fn key(k: &str) -> PrivateKeySigner {
    k.parse().unwrap()
}

fn share() -> SecretShare {
    let mut bytes = [0u8; 64];
    for b in bytes.iter_mut() {
        *b = rand::random();
    }
    SecretShare::from_uniform_bytes(&bytes).unwrap()
}

async fn deploy(url: &str) -> Address {
    let artifact: serde_json::Value = serde_json::from_str(
        &std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../artifacts/contracts/XmzSwap.sol/XmzSwap.json")).unwrap(),
    )
    .unwrap();
    let code = Bytes::from_str(artifact["bytecode"].as_str().unwrap()).unwrap();
    let provider = ProviderBuilder::new().wallet(EthereumWallet::from(key(DEPLOYER_KEY))).connect_http(url.parse().unwrap());
    let receipt = provider.send_transaction(TransactionRequest::default().with_deploy_code(code)).await.unwrap().get_receipt().await.unwrap();
    receipt.contract_address.unwrap()
}

async fn advance(url: &str, seconds: u64) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request("evm_increaseTime".into(), [seconds]).await.unwrap();
    let _: serde_json::Value = provider.raw_request("evm_mine".into(), Vec::<serde_json::Value>::new()).await.unwrap();
}

async fn balance(url: &str, who: Address) -> U256 {
    ProviderBuilder::new().connect_http(url.parse().unwrap()).get_balance(who).await.unwrap()
}

struct World {
    url: String,
    trade: Trade,
    from_block: u64,
    owner_secret: SecretShare,
    claimer_secret: SecretShare,
    ledger: Arc<Mutex<Ledger>>,
    shared_key: [u8; 32],
}

async fn world(url: String) -> World {
    // A failed test may have left the node mining by hand
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request("evm_setAutomine".into(), [true]).await.unwrap();
    let contract = deploy(&url).await;
    let from_block = ProviderBuilder::new().connect_http(url.parse().unwrap()).get_block_number().await.unwrap();
    let owner_secret = share();
    let claimer_secret = share();
    let trade = Trade {
        terms: AgreedTerms {
            chain: Chain::Local,
            contract,
            owner: key(OWNER_KEY).address(),
            claimer: key(CLAIMER_KEY).address(),
            claim_commitment: claimer_secret.commitment(),
            refund_commitment: owner_secret.commitment(),
            asset: Address::ZERO,
            value: U256::from(10u64.pow(18)),
            duration1: D1,
            duration2: D2,
        },
        xmz_amount: XMZ,
        timing: Timing::BSC,
        lock_deadline: u64::MAX,
    };
    let shared_key = combined_spend_key(&owner_secret, &claimer_secret);
    World { url, trade, from_block, owner_secret, claimer_secret, ledger: Default::default(), shared_key }
}

impl World {
    fn xmz(&self, who: &'static str) -> StandInXmz {
        StandInXmz { ledger: self.ledger.clone(), who, shared_key: self.shared_key }
    }

    fn chain(&self, k: &str) -> AlloyChain {
        AlloyChain::new(&self.url, Chain::Local, self.trade.terms.contract, self.from_block, key(k)).unwrap()
    }

    fn owner<J: Journal>(&self, journal: J) -> Driver<AlloyChain, StandInXmz, J> {
        Driver::new(self.trade.clone(), self.owner_secret.clone(), U256::from(7), self.chain(OWNER_KEY), self.xmz("owner"), journal).unwrap()
    }

    fn claimer(&self) -> Driver<AlloyChain, StandInXmz, MemoryJournal> {
        let journal = MemoryJournal(Record::new(RoleState::Claimer(ClaimerState::WaitingForEvmLock)));
        Driver::new(self.trade.clone(), self.claimer_secret.clone(), U256::ZERO, self.chain(CLAIMER_KEY), self.xmz("claimer"), journal).unwrap()
    }
}

fn file_journal(name: &str) -> FileJournal {
    let path = std::env::temp_dir().join(format!("monzero-dex-evm-{name}-{}.json", std::process::id()));
    let mut j = FileJournal(path);
    j.save(&Record::new(RoleState::Owner(OwnerState::Start))).unwrap();
    j
}

// ---------- scenarios ----------

#[tokio::test]
async fn full_swap_with_the_owner_restarted_on_every_tick() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let journal_path = file_journal("swap").0;
    let mut claimer = w.claimer();
    let claimer_before = balance(&w.url, w.trade.terms.claimer).await;

    let (mut owner_state, mut claimer_state) = (None, None);
    for _round in 0..20 {
        // A fresh owner driver every tick, from the journal file only
        let mut owner = w.owner(FileJournal(journal_path.clone()));
        owner_state = Some(owner.tick().await.unwrap());
        claimer_state = Some(claimer.tick().await.unwrap());
        w.xmz("chain").mine(5);
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    let ledger = w.ledger.lock().unwrap();
    assert_eq!(ledger.swept_by, Some("owner"), "the owner took the XMZ with the combined key");
    assert_eq!(ledger.lock_sends, 1, "XMZ was locked once");
    drop(ledger);
    let gained = balance(&w.url, w.trade.terms.claimer).await - claimer_before;
    assert!(gained > U256::from(99) * U256::from(10u64.pow(16)), "claimer received the BNB minus its claim gas");
    // The lock was signed once and kept: only one newSwap in the journal
    let record = FileJournal(journal_path.clone()).load().unwrap();
    assert!(record.retained.contains_key("new_swap") && record.retained.contains_key("set_ready"));
    let _ = std::fs::remove_file(journal_path);
}

/// The app's path: both traders agree over the exchange, save the trade with `Driver::start`
/// and the owner is rebuilt from its journal and seed alone (`Driver::resume`) every tick.
#[tokio::test]
async fn swap_from_saved_trades_with_the_owner_resumed_every_tick() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let (owner_seed, claimer_seed) = (SwapSeed::from_wallet_seed(b"owner wallet"), SwapSeed::from_wallet_seed(b"claimer wallet"));
    let (owner_key, claimer_key) = (key(OWNER_KEY), key(CLAIMER_KEY));
    let offer = Offer {
        trade_id: B256::repeat_byte(0x77),
        chain_id: Chain::Local.id(),
        contract: w.trade.terms.contract,
        owner: owner_key.address(),
        claimer: claimer_key.address(),
        asset: Address::ZERO,
        value: w.trade.terms.value,
        duration1: D1,
        duration2: D2,
        xmz_amount: XMZ,
        xmz_network: XmzNetwork::Mainnet,
        expires_at: 1_000,
    };
    let mut maker = Session::start(&owner_seed, offer.clone(), Role::Owner, &owner_key, 0).unwrap();
    let mut taker = Session::start(&claimer_seed, offer.clone(), Role::Claimer, &claimer_key, 0).unwrap();
    assert!(maker.saved(Timing::BSC, u64::MAX, w.from_block, 0).is_none(), "nothing to save before agreement");
    taker.on_response(&maker.respond(&taker.request(), 0), 0).unwrap();
    let owner_saved = maker.saved(Timing::BSC, u64::MAX, w.from_block, 0).unwrap();
    let claimer_saved = taker.saved(Timing::BSC, u64::MAX, w.from_block, 0).unwrap();
    // The chain's clock is far past expires_at: the saved trades rebuild anyway
    let shared_key = combined_spend_key(&owner_saved.secret(&owner_seed), &claimer_saved.secret(&claimer_seed));
    let xmz = |who| StandInXmz { ledger: w.ledger.clone(), who, shared_key };
    let path = |who: &str| std::env::temp_dir().join(format!("monzero-dex-evm-saved-{who}-{}.json", std::process::id()));
    let (owner_path, claimer_path) = (path("owner"), path("claimer"));
    let _ = (std::fs::remove_file(&owner_path), std::fs::remove_file(&claimer_path));

    Driver::start(owner_saved.clone(), &owner_seed, &owner_key, w.chain(OWNER_KEY), xmz("owner"), FileJournal(owner_path.clone())).unwrap();
    assert!(FileJournal(owner_path.clone()).load().unwrap().trade.is_some(), "saved before anything was locked");
    let other = SavedTrade { lock_deadline: 1, ..owner_saved.clone() };
    assert!(Driver::start(other, &owner_seed, &owner_key, w.chain(OWNER_KEY), xmz("owner"), FileJournal(owner_path.clone())).is_err(), "journal holds another trade");
    let wrong_seed = SwapSeed::from_wallet_seed(b"someone else");
    assert!(Driver::resume(&wrong_seed, &owner_key, w.chain(OWNER_KEY), xmz("owner"), FileJournal(owner_path.clone())).is_err(), "another seed cannot take over");
    let mut claimer = Driver::start(claimer_saved.clone(), &claimer_seed, &claimer_key, w.chain(CLAIMER_KEY), xmz("claimer"), FileJournal(claimer_path.clone())).unwrap();

    // Nothing is locked before the user approves this exact trade
    let mut owner = Driver::resume(&owner_seed, &owner_key, w.chain(OWNER_KEY), xmz("owner"), FileJournal(owner_path.clone())).unwrap();
    assert!(matches!(owner.tick().await, Err(DriverError::AwaitingApproval)));
    assert_eq!(FileJournal(owner_path.clone()).load().unwrap().state, RoleState::Owner(OwnerState::Start), "state not advanced");
    assert!(owner.approve(claimer_saved.transcript ^ B256::repeat_byte(1)).is_err(), "approval for another trade");
    owner.approve(owner_saved.transcript).unwrap();
    owner.tick().await.unwrap(); // the BNB lock
    // The claimer sees the lock but does not lock XMZ unapproved; it keeps waiting
    assert!(matches!(claimer.tick().await, Err(DriverError::AwaitingApproval)));
    assert_eq!(w.ledger.lock().unwrap().lock_sends, 0);
    claimer.approve(claimer_saved.transcript).unwrap();

    let (mut owner_state, mut claimer_state) = (None, None);
    for _round in 0..20 {
        let mut owner = Driver::resume(&owner_seed, &owner_key, w.chain(OWNER_KEY), xmz("owner"), FileJournal(owner_path.clone())).unwrap();
        owner_state = Some(owner.tick().await.unwrap());
        claimer_state = Some(claimer.tick().await.unwrap());
        xmz("chain").mine(5);
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    assert_eq!(w.ledger.lock().unwrap().swept_by, Some("owner"));
    let _ = (std::fs::remove_file(owner_path), std::fs::remove_file(claimer_path));
}

async fn set_balance(url: &str, who: Address, wei: U256) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request("hardhat_setBalance".into(), (who, wei)).await.unwrap();
}

/// Funding from outside (e.g. MetaMask): nothing is locked until each account can pay for its
/// lock and for the transactions that protect it; then the swap goes ahead as usual.
#[tokio::test]
async fn nothing_is_locked_until_each_account_is_funded() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let rich = U256::from(10_000u64) * U256::from(10u64.pow(18));
    let (owner_addr, claimer_addr) = (w.trade.terms.owner, w.trade.terms.claimer);
    // The buyer holds half its trade value, the seller nothing at all
    set_balance(&w.url, owner_addr, w.trade.terms.value / U256::from(2)).await;
    set_balance(&w.url, claimer_addr, U256::ZERO).await;
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));
    let mut claimer = w.claimer();

    match owner.tick().await {
        Err(DriverError::AwaitingFunding { needed, have }) => {
            assert_eq!(have, w.trade.terms.value / U256::from(2));
            assert!(needed > w.trade.terms.value, "the lock plus gas for the later steps");
        }
        other => panic!("expected AwaitingFunding, got {other:?}"),
    }
    assert!(owner.journal().load().unwrap().retained.is_empty(), "nothing signed");
    assert_eq!(owner.journal().load().unwrap().state, RoleState::Owner(OwnerState::Start));

    // Funded (as a MetaMask payment would): the buyer locks
    set_balance(&w.url, owner_addr, rich).await;
    owner.tick().await.unwrap();
    assert!(matches!(owner.tick().await.unwrap(), RoleState::Owner(OwnerState::Locked { .. })));

    // The seller sees the lock but must not lock XMZ without gas to collect afterwards
    match claimer.tick().await {
        Err(DriverError::AwaitingFunding { have, .. }) => assert_eq!(have, U256::ZERO),
        other => panic!("expected AwaitingFunding, got {other:?}"),
    }
    assert_eq!(w.ledger.lock().unwrap().lock_sends, 0, "no XMZ locked");

    set_balance(&w.url, claimer_addr, rich).await;
    let (mut o, mut c) = (None, None);
    for _ in 0..20 {
        o = Some(owner.tick().await.unwrap());
        c = Some(claimer.tick().await.unwrap());
        w.xmz("chain").mine(5);
        if o.as_ref().unwrap().is_done() && c.as_ref().unwrap().is_done() {
            break;
        }
    }
    assert_eq!(o, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(c, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));

    // Afterwards the seller's account pays everything it holds to the user's own wallet
    let metamask: Address = "0x00000000000000000000000000000000000Ad0E5".parse().unwrap();
    let before = balance(&w.url, metamask).await;
    let held = balance(&w.url, claimer_addr).await;
    let (_, amount) = w.chain(CLAIMER_KEY).pay_out(metamask).await.unwrap().expect("a payout");
    assert_eq!(balance(&w.url, metamask).await - before, amount);
    assert!(amount < held && held - amount < U256::from(10u64.pow(16)), "only the transfer fee stays behind");
    assert!(balance(&w.url, claimer_addr).await < U256::from(10u64.pow(15)));
    assert_eq!(w.chain(CLAIMER_KEY).pay_out(metamask).await.unwrap(), None, "nothing left: repeating sends nothing");
    set_balance(&w.url, claimer_addr, rich).await;
}

#[tokio::test]
async fn owner_refunds_early_when_no_xmz_arrives() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));

    owner.tick().await.unwrap(); // lock
    let state = owner.tick().await.unwrap();
    assert!(matches!(state, RoleState::Owner(OwnerState::Locked { .. })));
    // The claimer never locks XMZ. 30 minutes before timeout1 the owner refunds early.
    advance(&w.url, D1 - 1800).await;
    let state = owner.tick().await.unwrap();
    assert!(matches!(state, RoleState::Owner(OwnerState::RefundSent { .. })));
    let state = owner.tick().await.unwrap();
    assert_eq!(state, RoleState::Owner(OwnerState::Done(Outcome::Refunded)));
}

#[tokio::test]
async fn a_lock_far_past_the_start_block_is_found_in_chunks() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    // 5,000 blocks between the trade's start block and the lock: three log queries
    rpc(&w.url, "hardhat_mine", json!(["0x1388"])).await;
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));
    owner.tick().await.unwrap();
    let state = w.claimer().tick().await.unwrap();
    assert!(matches!(state, RoleState::Claimer(ClaimerState::XmzLockSent { .. })), "{state:?}");
    w.chain(CLAIMER_KEY).check_logs().await.unwrap();
    // An endpoint with a low range cap: many small queries, then only the new blocks
    let small = w.chain(CLAIMER_KEY).with_log_chunk(300);
    let found = small.find_lock(&w.trade.terms).await.unwrap().expect("lock found in 300-block chunks");
    rpc(&w.url, "hardhat_mine", json!(["0x10"])).await;
    assert_eq!(small.find_lock(&w.trade.terms).await.unwrap().unwrap().log.transaction_hash, found.log.transaction_hash, "found again from the saved position");
}

#[tokio::test]
async fn claimer_offline_until_after_timeout2_gets_its_xmz_back() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));
    let mut claimer = w.claimer();

    owner.tick().await.unwrap(); // owner locks
    claimer.tick().await.unwrap(); // claimer checks the lock and locks XMZ
    w.xmz("chain").mine(10);
    owner.tick().await.unwrap(); // Locked -> setReady
    let state = owner.tick().await.unwrap();
    assert!(matches!(state, RoleState::Owner(OwnerState::Ready { .. })));

    // The claimer goes offline. After timeout2 the owner refunds.
    advance(&w.url, D1 + D2 + 60).await;
    owner.tick().await.unwrap();
    let state = owner.tick().await.unwrap();
    assert_eq!(state, RoleState::Owner(OwnerState::Done(Outcome::Refunded)));

    // The claimer comes back: too late to claim, but the refund revealed the owner's share
    let state = claimer.tick().await.unwrap();
    assert!(matches!(state, RoleState::Claimer(ClaimerState::SweepingBack { .. })), "{state:?}");
    let state = claimer.tick().await.unwrap();
    assert_eq!(state, RoleState::Claimer(ClaimerState::Done(Outcome::Refunded)));
    assert_eq!(w.ledger.lock().unwrap().swept_by, Some("claimer"));
}

#[tokio::test]
async fn a_call_the_contract_would_reject_is_never_signed() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url).await;
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));
    owner.tick().await.unwrap();
    let RoleState::Owner(OwnerState::Locked { swap }) = owner.tick().await.unwrap() else { panic!("expected Locked") };
    let chain = w.chain(CLAIMER_KEY);
    // A claim before setReady and before timeout1 would revert: nothing may be signed,
    // because a signed and published claim would reveal the secret even when it fails.
    let early = chain.sign(&EvmCall::Claim((&swap).into(), w.claimer_secret.to_evm())).await;
    assert!(early.is_err(), "an early claim must not be signed");
    // A refund with the claimer's secret instead of the owner's, likewise
    let wrong = w.chain(OWNER_KEY).sign(&EvmCall::Refund((&swap).into(), w.claimer_secret.to_evm())).await;
    assert!(wrong.is_err(), "a refund with the wrong secret must not be signed");
}

async fn rpc(url: &str, method: &'static str, params: serde_json::Value) {
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let _: serde_json::Value = provider.raw_request(method.into(), params).await.unwrap();
}

fn max_fee(raw: &Bytes) -> u128 {
    TxEnvelope::decode_2718(&mut raw.as_ref()).unwrap().max_fee_per_gas()
}

#[tokio::test]
async fn a_lock_stuck_behind_a_fee_spike_is_replaced_and_mined_once() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let _chain = CHAIN.lock().await;
    let w = world(url.clone()).await;
    let provider = ProviderBuilder::new().connect_http(url.parse().unwrap());
    let owner_address = w.trade.terms.owner;
    let nonce_before = provider.get_transaction_count(owner_address).await.unwrap();
    let mut owner = w.owner(MemoryJournal(Record::new(RoleState::Owner(OwnerState::Start))));

    // Blocks are mined by hand from here, so the lock can sit in the mempool
    rpc(&url, "evm_setAutomine", json!([false])).await;
    owner.tick().await.unwrap();
    let versions = |o: &Driver<AlloyChain, StandInXmz, MemoryJournal>| o.journal().0.retained["new_swap"].clone();
    let first = versions(&owner)[0].raw.clone();

    // Fees jump above what the lock offers: blocks go by without it
    let spike = max_fee(&first) * 4;
    for _ in 0..Chain::Local.bump_after_blocks() {
        rpc(&url, "hardhat_setNextBlockBaseFeePerGas", json!([format!("{spike:#x}")])).await;
        rpc(&url, "evm_mine", json!([])).await;
    }
    assert_eq!(provider.get_transaction_count(owner_address).await.unwrap(), nonce_before, "the lock is still unmined");

    // The next tick replaces it: same call and nonce, fees above the spike
    owner.tick().await.unwrap();
    let kept = versions(&owner);
    assert_eq!(kept.len(), 2, "replaced once");
    let replaced = TxEnvelope::decode_2718(&mut kept[1].raw.as_ref()).unwrap();
    let original = TxEnvelope::decode_2718(&mut first.as_ref()).unwrap();
    assert_eq!((replaced.nonce(), replaced.input(), replaced.value()), (original.nonce(), original.input(), original.value()));
    assert!(replaced.max_fee_per_gas() >= spike && replaced.max_priority_fee_per_gas() > original.max_priority_fee_per_gas());
    // Not again before another bump_after_blocks
    owner.tick().await.unwrap();
    assert_eq!(versions(&owner).len(), 2);

    rpc(&url, "evm_setAutomine", json!([true])).await;
    rpc(&url, "evm_mine", json!([])).await;
    let mut state = owner.tick().await.unwrap();
    for _ in 0..5 {
        if matches!(state, RoleState::Owner(OwnerState::Locked { .. })) {
            break;
        }
        rpc(&url, "evm_mine", json!([])).await;
        state = owner.tick().await.unwrap();
    }
    assert!(matches!(state, RoleState::Owner(OwnerState::Locked { .. })), "{state:?}");
    assert_eq!(provider.get_transaction_count(owner_address).await.unwrap(), nonce_before + 1, "one lock mined");
    // Once mined there is nothing to replace
    assert_eq!(w.chain(OWNER_KEY).replacement(&kept[1].raw).await.unwrap(), None);
    assert_eq!(w.chain(OWNER_KEY).replacement(&first).await.unwrap(), None);
}

// ---------- over real XMZ wallets ----------

type RealDriver = Driver<AlloyChain, WalletRpcXmz, MemoryJournal>;

struct RealXmz {
    owner: RealDriver,
    claimer: RealDriver,
    owner_address: String,
    owner_view_key: serde_json::Value,
    claimer_address: String,
}

/// Both traders over real wallets: each has its own funds wallet and a wallet RPC for the
/// shared address. The claimer's wallet is funded by mining.
async fn real_xmz_traders(w: &World, xmz_chain: &mut common::XmzChain) -> RealXmz {
    let (owner_main, owner_address) = xmz_chain.trader("owner").await;
    let (claimer_main, claimer_address) = xmz_chain.trader("claimer").await;
    let (owner_swap, claimer_swap) = (xmz_chain.wallet_rpc("owner-swap").await, xmz_chain.wallet_rpc("claimer-swap").await);
    xmz_chain.mine(&claimer_address, 80).await;
    let height = xmz_chain.height().await;
    let owner_view_key = owner_main.call("query_key", json!({"key_type": "view_key"})).await.unwrap()["key"].clone();

    // The key exchange: both traders send their signed frames with the cross-curve proofs
    let offer = Offer {
        trade_id: B256::repeat_byte(0x11),
        chain_id: Chain::Local.id(),
        contract: w.trade.terms.contract,
        owner: w.trade.terms.owner,
        claimer: w.trade.terms.claimer,
        asset: w.trade.terms.asset,
        value: w.trade.terms.value,
        duration1: w.trade.terms.duration1,
        duration2: w.trade.terms.duration2,
        xmz_amount: w.trade.xmz_amount,
        xmz_network: XmzNetwork::Mainnet,
        expires_at: u64::MAX,
    };
    let keys = |secret: &SecretShare| LocalKeys::from_secrets(secret.clone(), share().to_ed25519_le()).unwrap();
    let (owner_keys, claimer_keys) = (keys(&w.owner_secret), keys(&w.claimer_secret));
    let rng = &mut rand::thread_rng();
    let frames = [
        owner_keys.frame(&offer, Role::Owner, &key(OWNER_KEY), rng).unwrap(),
        claimer_keys.frame(&offer, Role::Claimer, &key(CLAIMER_KEY), rng).unwrap(),
    ];
    let agree = |role, keys| {
        let mut exchange = KeyExchange::new(offer.clone(), role, keys).unwrap();
        for f in &frames {
            exchange.accept(f, 0).unwrap();
        }
        exchange.finish().unwrap()
    };
    let (at_owner, at_claimer) = (agree(Role::Owner, owner_keys), agree(Role::Claimer, claimer_keys));
    // The negotiated terms are the ones the drivers run (commitments from the frames)
    assert_eq!(at_owner.trade(Timing::BSC, u64::MAX), w.trade);
    assert_eq!(at_claimer.terms, at_owner.terms);
    assert_eq!(at_owner.shared, at_claimer.shared);
    let (owner_shared, claimer_shared) = (at_owner.shared, at_claimer.shared);
    let cfg = |shared: SharedXmz, who: &str| WalletRpcConfig {
        shared,
        restore_height: height - 1,
        wallet_name: format!("{who}-shared"),
        wallet_password: "test-only".into(),
        sweep_confirmations: 10,
        record: xmz_chain.root.join(format!("{who}-xmz.json")),
    };
    let start = |state| MemoryJournal(Record::new(state));
    let owner = Driver::new(
        w.trade.clone(),
        w.owner_secret.clone(),
        U256::from(7),
        w.chain(OWNER_KEY),
        WalletRpcXmz::new(cfg(owner_shared, "owner"), owner_main, owner_swap),
        start(RoleState::Owner(OwnerState::Start)),
    )
    .unwrap();
    let claimer = Driver::new(
        w.trade.clone(),
        w.claimer_secret.clone(),
        U256::ZERO,
        w.chain(CLAIMER_KEY),
        WalletRpcXmz::new(cfg(claimer_shared, "claimer"), claimer_main, claimer_swap),
        start(RoleState::Claimer(ClaimerState::WaitingForEvmLock)),
    )
    .unwrap();
    RealXmz { owner, claimer, owner_address, owner_view_key, claimer_address }
}

#[tokio::test]
async fn full_swap_over_real_xmz_wallets() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let Some(mut xmz_chain) = common::XmzChain::start("e2e").await else {
        eprintln!("MONZERO_XMZ_BIN not set: skipped");
        return;
    };
    let _chain = CHAIN.lock().await;
    let mut w = world(url).await;
    w.trade.xmz_amount = 2_000_000_000_000;
    let RealXmz { mut owner, mut claimer, owner_address, owner_view_key, claimer_address } = real_xmz_traders(&w, &mut xmz_chain).await;
    let claimer_before = balance(&w.url, w.trade.terms.claimer).await;

    let (mut owner_state, mut claimer_state) = (None, None);
    for _round in 0..60 {
        owner_state = Some(owner.tick().await.unwrap());
        claimer_state = Some(claimer.tick().await.unwrap());
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
        xmz_chain.mine(&claimer_address, 3).await;
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    let gained = balance(&w.url, w.trade.terms.claimer).await - claimer_before;
    assert!(gained > U256::from(99) * U256::from(10u64.pow(16)), "claimer received the BNB minus its claim gas");

    // The owner's own wallet (empty before) received the swept XMZ, less the sweep fee
    let observer = xmz_chain.wallet_rpc("observer").await;
    observer
        .call(
            "generate_from_keys",
            json!({"filename": "owner-view", "password": "", "address": owner_address, "viewkey": owner_view_key, "restore_height": 0}),
        )
        .await
        .unwrap();
    observer.call("refresh", json!({})).await.unwrap();
    let received = observer.call("get_balance", json!({})).await.unwrap()["balance"].as_u64().unwrap();
    assert!(received > w.trade.xmz_amount * 99 / 100 && received < w.trade.xmz_amount, "owner received {received}");
}

#[tokio::test]
async fn refunded_swap_returns_the_xmz_over_real_wallets() {
    let Some(url) = std::env::var("MONZERO_EVM_RPC").ok() else { return };
    let Some(mut xmz_chain) = common::XmzChain::start("e2e-refund").await else {
        eprintln!("MONZERO_XMZ_BIN not set: skipped");
        return;
    };
    let _chain = CHAIN.lock().await;
    let mut w = world(url).await;
    w.trade.xmz_amount = 2_000_000_000_000;
    let RealXmz { mut owner, mut claimer, claimer_address, .. } = real_xmz_traders(&w, &mut xmz_chain).await;

    owner.tick().await.unwrap(); // owner locks BNB
    let state = claimer.tick().await.unwrap(); // claimer checks the lock and locks XMZ
    assert!(matches!(state, RoleState::Claimer(ClaimerState::XmzLockSent { .. })), "{state:?}");
    xmz_chain.mine(&claimer_address, 10).await;
    // Locked -> setReady -> Ready; a busy machine can need an extra wallet refresh
    let mut state = owner.tick().await.unwrap();
    for _ in 0..5 {
        if matches!(state, RoleState::Owner(OwnerState::Ready { .. })) {
            break;
        }
        state = owner.tick().await.unwrap();
    }
    assert!(matches!(state, RoleState::Owner(OwnerState::Ready { .. })), "{state:?}");

    // The claimer goes offline. After timeout2 the owner refunds, revealing its share.
    advance(&w.url, D1 + D2 + 60).await;
    owner.tick().await.unwrap();
    assert_eq!(owner.tick().await.unwrap(), RoleState::Owner(OwnerState::Done(Outcome::Refunded)));

    // The claimer comes back and sweeps the shared address with both shares
    let mut state = claimer.tick().await.unwrap();
    for _round in 0..20 {
        if state.is_done() {
            break;
        }
        xmz_chain.mine(&claimer_address, 3).await;
        state = claimer.tick().await.unwrap();
    }
    assert_eq!(state, RoleState::Claimer(ClaimerState::Done(Outcome::Refunded)));
}
