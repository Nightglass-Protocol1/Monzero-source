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
        SolChain::with_asset(url, Chain::SolLocal, program, sol::account_key(&self.key.to_bytes().0), agreed.sol.expect("Solana accounts in the frames"), nonce, self.saved.offer.value, self.saved.offer.asset)
            .unwrap()
    }

    fn resume(&self, url: &str, program: sol::Pubkey) -> Driver<SolChain, StandInXmz, FileJournal> {
        Driver::resume(&self.seed, &self.key, self.chain(url, program), self.xmz.clone(), FileJournal(self.journal.clone())).unwrap()
    }
}

/// Both traders agree on an offer over the usual key exchange and save the trade.
async fn agree(url: &str, program: sol::Pubkey, tag: u8, timing: Timing) -> (Party, Party) {
    agree_funded(url, program, tag, timing, LAMPORTS + 4_500_000, 1_500_000).await
}

/// As `agree`, with the lamports each side's account is given
async fn agree_funded(url: &str, program: sol::Pubkey, tag: u8, timing: Timing, owner_lamports: u64, claimer_lamports: u64) -> (Party, Party) {
    agree_asset(url, program, tag, timing, owner_lamports, claimer_lamports, Address::ZERO, LAMPORTS).await
}

/// As `agree_funded`, for a trade of `value` in `asset` (zero: SOL; else a token's stand-in)
#[allow(clippy::too_many_arguments)]
async fn agree_asset(url: &str, program: sol::Pubkey, tag: u8, timing: Timing, owner_lamports: u64, claimer_lamports: u64, asset: Address, value: u64) -> (Party, Party) {
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
        asset,
        value: U256::from(value),
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
    fund(url, &owner.account(), owner_lamports).await; // `agree`: exactly the reserve the driver asks for
    if claimer_lamports > 0 {
        fund(url, &claimer.account(), claimer_lamports).await;
    }
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

    // The relays' view: the claim, once final, proves the swap (who, how much, which trade's nonce);
    // the transaction that made the swap is not a claim
    let signature = |raw: &[u8]| -> [u8; 64] { bs58::decode(sol::transaction_id(raw).unwrap()).into_vec().unwrap().try_into().unwrap() };
    let claims: Vec<[u8; 64]> = FileJournal(claimer.journal.clone()).load().unwrap().retained["claim"].iter().map(|c| signature(&c.raw)).collect();
    let mut seen = None;
    'wait: for _ in 0..120 {
        for c in &claims {
            if let sol::ClaimSeen::Final(swap) = sol::claim_lookup(&url, &program, c).await.unwrap() {
                seen = Some(swap);
                break 'wait;
            }
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    let swap = seen.expect("the claim became final and was recognised");
    let trade = record.trade.as_ref().expect("saved trade");
    assert_eq!((swap.owner, swap.claimer, swap.lamports), (owner.account(), claimer.account(), LAMPORTS));
    assert_eq!(alloy::primitives::U256::from(swap.nonce), alloy::primitives::U256::from_be_slice(&trade.offer.trade_id[..8]));
    assert!(swap.at > 1_700_000_000);
    let made = signature(&record.retained["new_swap"].last().unwrap().raw);
    assert!(matches!(sol::claim_lookup(&url, &program, &made).await.unwrap(), sol::ClaimSeen::NotAClaim(_) | sol::ClaimSeen::Pending));
    assert_eq!(sol::claim_lookup(&url, &program, &[7u8; 64]).await.unwrap(), sol::ClaimSeen::Pending, "unknown to the node");

    // Afterwards the claimer sends what its account holds to its own wallet
    // A fresh address every run (an address is any 32 bytes)
    let home: [u8; 32] = rand::random();
    let (id, amount) = sol::pay_out(&url, &sol::account_key(&claimer.key.to_bytes().0), &home, 0).await.unwrap().expect("something to send");
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

#[tokio::test]
async fn a_claimer_holding_no_sol_collects_with_the_owners_fee_advance() {
    let Some((url, program)) = setup() else { return };
    let _chain = CHAIN.lock().await;
    let timing = Timing { xmz_confirmations: 10, xmz_lock_budget: 120, owner_refund_margin: 120, claim_margin: 120 };
    // The owner holds its lock, its reserve and the claimer's claim fee; the claimer holds nothing
    let (owner, claimer) = agree_funded(&url, program, 3, timing, LAMPORTS + 4_500_000 + 1_500_000, 0).await;
    assert_eq!(balance(&url, &claimer.account()).await, 0);
    let (mut owner_state, mut claimer_state) = (None, None);
    for round in 0..90 {
        owner_state = Some(owner.resume(&url, program).with_gas_advance(GasAdvance::Give).tick().await.unwrap());
        claimer_state = Some(claimer.resume(&url, program).with_gas_advance(GasAdvance::Expect).tick().await.unwrap());
        println!("round {round}: owner {} | claimer {}", owner_state.as_ref().unwrap().stage(), claimer_state.as_ref().unwrap().stage());
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    let record = FileJournal(owner.journal.clone()).load().unwrap();
    assert_eq!(record.retained["gas_advance"].len(), 1, "advanced once");
    // The claimer has the SOL it sold for, plus what is left of the advance after its claim fee
    assert_eq!(balance(&url, &claimer.account()).await, LAMPORTS + 1_500_000 - sol::FEE_LAMPORTS);
}

#[tokio::test]
async fn a_claimer_holding_no_sol_is_asked_for_it_when_no_advance_comes() {
    let Some((url, program)) = setup() else { return };
    let _chain = CHAIN.lock().await;
    let timing = Timing { xmz_confirmations: 10, xmz_lock_budget: 120, owner_refund_margin: 120, claim_margin: 120 };
    // An owner whose app does not advance (as before this change)
    let (owner, claimer) = agree_funded(&url, program, 4, timing, LAMPORTS + 4_500_000, 0).await;
    let mut asked = None;
    for _ in 0..90 {
        owner.resume(&url, program).with_gas_advance(GasAdvance::Off).tick().await.unwrap();
        match claimer.resume(&url, program).with_gas_advance(GasAdvance::Expect).tick().await {
            Err(DriverError::AwaitingFunding { needed, have }) => {
                asked = Some((needed, have));
                break;
            }
            r => {
                r.unwrap();
            }
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    let (needed, have) = asked.expect("the claim asks for SOL instead of failing");
    assert_eq!((needed, have), (U256::from(1_500_000u64), U256::ZERO));
    // Paying the fee in lets the claimer collect
    fund(&url, &claimer.account(), 1_500_000).await;
    let mut claimer_state = None;
    for _ in 0..60 {
        owner.resume(&url, program).with_gas_advance(GasAdvance::Off).tick().await.unwrap();
        claimer_state = Some(claimer.resume(&url, program).with_gas_advance(GasAdvance::Expect).tick().await.unwrap());
        if claimer_state.as_ref().unwrap().is_done() {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
}

/// USDT on Solana with a test mint: a buyer pays 25 USDT, a seller holding no SOL collects them
/// into a token account opened in its claim (the buyer's fee advance pays for it).
///   MONZERO_SOL_USDT_MINT=<mint> MONZERO_SOL_USDT_BANK=<hex seed of an account holding the mint's tokens and SOL>
#[tokio::test]
async fn a_usdt_swap_locks_the_tokens_and_the_seller_collects_them() {
    let Some((url, program)) = setup() else { return };
    let (Ok(mint), Ok(bank)) = (std::env::var("MONZERO_SOL_USDT_MINT"), std::env::var("MONZERO_SOL_USDT_BANK")) else { return };
    let _chain = CHAIN.lock().await;
    let mint = sol::from_base58(&mint).unwrap();
    sol::set_local_usdt_mint(mint);
    let bank = ed25519_dalek::SigningKey::from_bytes(&hex::decode(bank).unwrap().try_into().unwrap());
    let units = 25_000_000u64; // 25 USDT
    let timing = Timing { xmz_confirmations: 10, xmz_lock_budget: 120, owner_refund_margin: 120, claim_margin: 120 };
    let (owner, claimer) = agree_asset(&url, program, 5, timing, 1_000_000, 0, sol::asset_address(&mint), units).await;
    // The buyer's USDT and its SOL for fees, the vault, the swap account and the seller's advance
    let bank_sol = balance(&url, &bank.verifying_key().to_bytes()).await;
    sol::pay_out_with(&url, &bank, &owner.account(), bank_sol - 30_000_000, Some(mint)).await.unwrap().unwrap();
    for _ in 0..60 {
        if sol::token_balance(&url, &owner.account(), &mint).await.unwrap() >= units {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    let owner_tokens = sol::token_balance(&url, &owner.account(), &mint).await.unwrap();
    assert!(owner_tokens >= units, "the buyer holds its USDT");
    assert_eq!(balance(&url, &claimer.account()).await, 0, "the seller holds no SOL");
    let (mut owner_state, mut claimer_state) = (None, None);
    for round in 0..90 {
        owner_state = Some(owner.resume(&url, program).with_gas_advance(GasAdvance::Give).tick().await.unwrap());
        claimer_state = Some(claimer.resume(&url, program).with_gas_advance(GasAdvance::Expect).tick().await.unwrap());
        println!("round {round}: owner {} | claimer {}", owner_state.as_ref().unwrap().stage(), claimer_state.as_ref().unwrap().stage());
        if owner_state.as_ref().unwrap().is_done() && claimer_state.as_ref().unwrap().is_done() {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_secs(2)).await;
    }
    assert_eq!(owner_state, Some(RoleState::Owner(OwnerState::Done(Outcome::Swapped))));
    assert_eq!(claimer_state, Some(RoleState::Claimer(ClaimerState::Done(Outcome::Swapped))));
    assert_eq!(sol::token_balance(&url, &claimer.account(), &mint).await.unwrap(), units, "the seller collected exactly 25 USDT");
    assert_eq!(sol::token_balance(&url, &owner.account(), &mint).await.unwrap(), owner_tokens - units);
    assert_eq!(owner.xmz.ledger.lock().unwrap().swept_by, Some("owner"), "the buyer took the XMZ with the secret the USDT claim revealed");
    // Afterwards the seller sends everything home: its USDT (into a token account opened for it) and its SOL
    let home = ed25519_dalek::SigningKey::from_bytes(&rand::random()).verifying_key().to_bytes();
    sol::pay_out_with(&url, &sol::account_key(&claimer.key.to_bytes().0), &home, 0, Some(mint)).await.unwrap().expect("something to send");
    for _ in 0..60 {
        if sol::token_balance(&url, &home, &mint).await.unwrap() == units {
            break;
        }
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
    }
    assert_eq!(sol::token_balance(&url, &home, &mint).await.unwrap(), units, "the USDT reached the seller's own wallet");
}
