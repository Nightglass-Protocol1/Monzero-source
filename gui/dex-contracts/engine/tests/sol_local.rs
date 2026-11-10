//! Two drivers trading SOL for (stand-in) XMZ on a local Solana validator running the swap
//! program: the same driver, machine and key exchange as the EVM pairs, over `sol::SolChain`.
//!
//!   solana-test-validator --reset --bpf-program <PROGRAM_ID> monzero_sol_swap.so
//!   MONZERO_SOL_RPC=http://127.0.0.1:8899 MONZERO_SOL_PROGRAM=<PROGRAM_ID> cargo test --test sol_local -- --nocapture
//!
//! Skipped (passes trivially) when MONZERO_SOL_RPC is not set.

use alloy::primitives::{Address, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::driver::*;
use monzero_dex_evm::exchange::{SavedTrade, Session};
use monzero_dex_evm::machine::{ClaimerState, Outcome, OwnerState, Timing, XmzLock};
use monzero_dex_evm::negotiation::{Offer, Role};
use monzero_dex_evm::secret::combined_spend_key;
use monzero_dex_evm::sol::{self, SolChain};
use monzero_dex_evm::swap_seed::SwapSeed;
use monzero_dex_evm::xmz::XmzNetwork;
use monzero_dex_evm::Chain;
use serde_json::{json, Value};
use std::sync::{Arc, Mutex};

const LAMPORTS: u64 = 50_000_000; // 0.05 SOL
const XMZ: u64 = 5_000_000_000_000;
const D: u64 = 600;

static CHAIN: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());

#[derive(Default)]
struct Ledger {
    lock: Option<XmzLock>,
    lock_sends: u32,
    swept_by: Option<&'static str>,
}

/// One trader's view of the shared XMZ address: a sweep succeeds only with the exact combined
/// spend key, so a secret read back wrongly from Solana fails the test.
#[derive(Clone)]
struct StandInXmz {
    ledger: Arc<Mutex<Ledger>>,
    who: &'static str,
    shared_key: [u8; 32],
}

impl XmzSide for StandInXmz {
    async fn lock_status(&self) -> Result<Option<XmzLock>, DriverError> {
        Ok(self.ledger.lock().unwrap().lock)
    }
    async fn ensure_lock(&self, amount: u64) -> Result<(), DriverError> {
        let mut l = self.ledger.lock().unwrap();
        if l.lock.is_none() {
            l.lock = Some(XmzLock { amount, confirmations: 10 });
            l.lock_sends += 1;
        }
        Ok(())
    }
    async fn ensure_sweep(&self, spend_key: [u8; 32]) -> Result<(), DriverError> {
        assert_eq!(spend_key, self.shared_key, "{} swept with a wrong key", self.who);
        let mut l = self.ledger.lock().unwrap();
        if l.swept_by.is_none() {
            l.swept_by = Some(self.who);
        }
        Ok(())
    }
    async fn swept(&self) -> Result<bool, DriverError> {
        Ok(self.ledger.lock().unwrap().swept_by == Some(self.who))
    }
}

async fn rpc(url: &str, method: &str, params: Value) -> Value {
    let reply: Value = reqwest::Client::new()
        .post(url)
        .json(&json!({"jsonrpc": "2.0", "id": 1, "method": method, "params": params}))
        .send()
        .await
        .unwrap()
        .json()
        .await
        .unwrap();
    assert!(reply.get("error").is_none(), "{method}: {reply}");
    reply["result"].clone()
}

async fn balance(url: &str, account: &sol::Pubkey) -> u64 {
    rpc(url, "getBalance", json!([sol::to_base58(account), {"commitment": "confirmed"}])).await["value"].as_u64().unwrap()
}

async fn fund(url: &str, account: &sol::Pubkey, lamports: u64) {
    rpc(url, "requestAirdrop", json!([sol::to_base58(account), lamports])).await;
    for _ in 0..60 {
        if balance(url, account).await >= lamports {
            return;
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    panic!("the airdrop did not arrive");
}

struct Party {
    seed: SwapSeed,
    key: PrivateKeySigner,
    saved: SavedTrade,
    journal: std::path::PathBuf,
    xmz: StandInXmz,
}

impl Party {
    fn account(&self) -> sol::Pubkey {
        sol::account_key(&self.key.to_bytes().0).verifying_key().to_bytes()
    }

    fn chain(&self, url: &str, program: sol::Pubkey) -> SolChain {
        let agreed = self.saved.agreed(&self.seed, &self.key).unwrap();
        let nonce = u64::try_from(self.saved.swap_nonce()).unwrap();
        SolChain::new(url, Chain::SolLocal, program, sol::account_key(&self.key.to_bytes().0), agreed.sol.expect("Solana accounts in the frames"), nonce, self.saved.offer.value)
            .unwrap()
    }

    fn resume(&self, url: &str, program: sol::Pubkey) -> Driver<SolChain, StandInXmz, FileJournal> {
        Driver::resume(&self.seed, &self.key, self.chain(url, program), self.xmz.clone(), FileJournal(self.journal.clone())).unwrap()
    }
}

/// Both traders agree on an offer over the usual key exchange and save the trade.
async fn agree(url: &str, program: sol::Pubkey, tag: u8, timing: Timing) -> (Party, Party) {
    let (owner_key, claimer_key) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    let (owner_seed, claimer_seed) = (SwapSeed::from_bytes([tag; 32]), SwapSeed::from_bytes([tag ^ 0xFF; 32]));
    let mut id = [tag; 32];
    id[..8].copy_from_slice(&rand::random::<u64>().to_be_bytes());
    let offer = Offer {
        trade_id: B256::from(id),
        chain_id: Chain::SolLocal.id(),
        contract: sol::contract_address(&program),
        owner: owner_key.address(),
        claimer: claimer_key.address(),
        asset: Address::ZERO,
        value: U256::from(LAMPORTS),
        duration1: D,
        duration2: D,
        xmz_amount: XMZ,
        xmz_network: XmzNetwork::Testnet,
        expires_at: u64::MAX,
    };
    let mut maker = Session::start(&owner_seed, offer.clone(), Role::Owner, &owner_key, 0).unwrap();
    let mut taker = Session::start(&claimer_seed, offer.clone(), Role::Claimer, &claimer_key, 0).unwrap();
    taker.on_response(&maker.respond(&taker.request(), 0), 0).unwrap();
    let owner_saved = maker.saved(timing, u64::MAX, 0, 0).unwrap();
    let claimer_saved = taker.saved(timing, u64::MAX, 0, 0).unwrap();
    let shared_key = combined_spend_key(&owner_saved.secret(&owner_seed), &claimer_saved.secret(&claimer_seed));
    let ledger: Arc<Mutex<Ledger>> = Default::default();
    let path = |who: &str| std::env::temp_dir().join(format!("monzero-dex-sol-{who}-{tag}-{}.json", std::process::id()));
    let party = |who: &'static str, seed, key, saved| Party {
        seed,
        key,
        saved,
        journal: path(who),
        xmz: StandInXmz { ledger: ledger.clone(), who, shared_key },
    };
    let (owner, claimer) = (party("owner", owner_seed, owner_key, owner_saved), party("claimer", claimer_seed, claimer_key, claimer_saved));
    for p in [&owner, &claimer] {
        let _ = std::fs::remove_file(&p.journal);
        let mut d = Driver::start(p.saved.clone(), &p.seed, &p.key, p.chain(url, program), p.xmz.clone(), FileJournal(p.journal.clone())).unwrap();
        d.approve(p.saved.transcript).unwrap();
    }
    fund(url, &owner.account(), LAMPORTS + 4_500_000).await; // exactly the reserve the driver asks for
    fund(url, &claimer.account(), 1_500_000).await;
    (owner, claimer)
}

fn setup() -> Option<(String, sol::Pubkey)> {
    let url = std::env::var("MONZERO_SOL_RPC").ok()?;
    let program = sol::from_base58(&std::env::var("MONZERO_SOL_PROGRAM").expect("MONZERO_SOL_PROGRAM")).unwrap();
    Some((url, program))
}

#[tokio::test]
async fn full_swap_with_both_traders_resumed_from_their_journals_every_tick() {
    let Some((url, program)) = setup() else { return };
    let _chain = CHAIN.lock().await;
    let timing = Timing { xmz_confirmations: 10, xmz_lock_budget: 120, owner_refund_margin: 120, claim_margin: 120 };
    let (owner, claimer) = agree(&url, program, 1, timing).await;
    let (owner_before, claimer_before) = (balance(&url, &owner.account()).await, balance(&url, &claimer.account()).await);

    let (mut owner_state, mut claimer_state) = (None, None);
    for round in 0..90 {
        owner_state = Some(owner.resume(&url, program).tick().await.unwrap());
        claimer_state = Some(claimer.resume(&url, program).tick().await.unwrap());
        println!("round {round}: owner {} | claimer {}", owner_state.as_ref().unwrap().stage(), claimer_state.as_ref().unwrap().stage());
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    let ledger = owner.xmz.ledger.lock().unwrap();
    assert_eq!(ledger.swept_by, Some("owner"), "the owner took the XMZ with the key combined from the secret the claim revealed on Solana");
    assert_eq!(ledger.lock_sends, 1);
    drop(ledger);
    // The claimer received the SOL (less one fee); the owner paid it, its fees, and got the rent back
    assert_eq!(balance(&url, &claimer.account()).await, claimer_before + LAMPORTS - sol::FEE_LAMPORTS);
    let owner_after = balance(&url, &owner.account()).await;
    assert!(owner_before - owner_after >= LAMPORTS && owner_before - owner_after <= LAMPORTS + 10 * sol::FEE_LAMPORTS, "owner paid {}", owner_before - owner_after);
    let record = FileJournal(owner.journal.clone()).load().unwrap();
    assert!(record.retained.contains_key("new_swap") && record.retained.contains_key("set_ready"));
    assert!(FileJournal(claimer.journal.clone()).load().unwrap().retained.contains_key("claim"));

    // Afterwards the claimer sends what its account holds to its own wallet
    // A fresh address every run (an address is any 32 bytes)
    let home: [u8; 32] = rand::random();
    let (id, amount) = sol::pay_out(&url, &sol::account_key(&claimer.key.to_bytes().0), &home).await.unwrap().expect("something to send");
    assert!(!id.is_empty());
    for _ in 0..40 {
        if balance(&url, &home).await >= amount {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    assert!(balance(&url, &home).await >= amount);
    assert_eq!(balance(&url, &claimer.account()).await, 0);
}

#[tokio::test]
async fn the_owner_takes_its_sol_back_when_no_xmz_is_locked() {
    let Some((url, program)) = setup() else { return };
    let _chain = CHAIN.lock().await;
    // The owner gives up 30 seconds after its lock (refund margin = all of duration1 but 30 s)
    let timing = Timing { xmz_confirmations: 10, xmz_lock_budget: 120, owner_refund_margin: D - 30, claim_margin: 120 };
    let (owner, claimer) = agree(&url, program, 2, timing).await;
    let before = balance(&url, &owner.account()).await;
    let mut state = None;
    for round in 0..90 {
        state = Some(owner.resume(&url, program).tick().await.unwrap());
        println!("round {round}: owner {}", state.as_ref().unwrap().stage());
        if state.as_ref().unwrap().is_done() {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    assert_eq!(state, Some(RoleState::Owner(OwnerState::Done(Outcome::Refunded))));
    let after = balance(&url, &owner.account()).await;
    assert!(before - after <= 10 * sol::FEE_LAMPORTS, "only fees were spent: {}", before - after);
    // The claimer never locked anything and sees nothing to claim
    assert_eq!(claimer.xmz.ledger.lock().unwrap().lock_sends, 0);
}
