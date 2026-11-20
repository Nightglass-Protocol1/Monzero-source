//! Runs one direct (coin <-> coin) trade: polls both legs into a [`DirectSnapshot`], steps the
//! side's state and carries out the actions, with the recovery rules of [`crate::driver`]:
//!  - the next state is saved before any action runs;
//!  - every transaction is signed once and kept in the journal before it is broadcast, then
//!    rebroadcast (or replaced with higher fees after `Chain::bump_after_blocks`);
//!  - signing runs the call first (gas estimation, or the program's simulation), so a claim or
//!    refund that would fail on chain is never signed.
//!
//! Each leg is a chain view (`AlloyChain` on BNB / Ethereum, `SolChain` on Solana) for that leg's
//! swap only; the same account key serves both (on Solana, the key derived from it).

use crate::direct::{DirectKeys, SavedDirect, Side};
use crate::direct_machine::{follower_step, leader_step, DirectAction, DirectSnapshot, DirectTrade, FollowerState, LeaderState, LegId, LegView};
use crate::driver::{durable_write, funding_needed, DriverError, EvmCall, EvmChain, Signed};
use crate::machine::SwapData;
use crate::swap_seed::SwapSeed;
use crate::{check_lock, swap_id, AgreedTerms, Swap};
use alloy::primitives::{Address, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, thiserror::Error)]
pub enum DirectError {
    #[error(transparent)]
    Driver(#[from] DriverError),
    /// Nothing was locked: our account on `chain_id` holds `have` and needs `needed` (base units)
    /// before our lock — the coin we lock plus its fees, or the fees of our claim on the other leg
    /// `asset`: what is missing (zero: the chain's coin; else the trade's token, e.g. USDT)
    #[error("waiting for funding on chain {chain_id}: the account holds {have} and needs {needed}")]
    AwaitingFunding { chain_id: u64, asset: Address, needed: U256, have: U256 },
}

impl From<crate::negotiation::NegotiationError> for DirectError {
    fn from(e: crate::negotiation::NegotiationError) -> Self {
        DirectError::Driver(DriverError::Agreement(e.to_string()))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum DirectRole {
    Leader(LeaderState),
    Follower(FollowerState),
}

impl DirectRole {
    /// Short stage name for the app, e.g. "Waiting" or "Done(Swapped)"
    pub fn stage(&self) -> String {
        let debug = match self {
            DirectRole::Leader(LeaderState::Done(o)) | DirectRole::Follower(FollowerState::Done(o)) => return format!("Done({o:?})"),
            DirectRole::Leader(s) => format!("{s:?}"),
            DirectRole::Follower(s) => format!("{s:?}"),
        };
        debug.split([' ', '{', '(']).next().unwrap_or_default().to_string()
    }

    pub fn is_done(&self) -> bool {
        matches!(self, DirectRole::Leader(LeaderState::Done(_)) | DirectRole::Follower(FollowerState::Done(_)))
    }
}

/// What the journal keeps for one direct trade.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct DirectRecord {
    pub state: DirectRole,
    /// Signed transactions by leg and kind ("a:new_swap", "b:claim", ...): every version, newest last
    pub retained: BTreeMap<String, Vec<Signed>>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trade: Option<SavedDirect>,
    /// The saved trade's transcript, once the user approved locking our funds for it
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub approved: Option<B256>,
}

impl DirectRecord {
    pub fn new(state: DirectRole) -> Self {
        DirectRecord { state, retained: BTreeMap::new(), trade: None, approved: None }
    }
}

pub trait DirectJournal {
    fn load(&self) -> Result<DirectRecord, DriverError>;
    fn save(&mut self, record: &DirectRecord) -> Result<(), DriverError>;
}

#[derive(Clone, Debug)]
pub struct MemoryDirectJournal(pub Option<DirectRecord>);

impl DirectJournal for MemoryDirectJournal {
    fn load(&self) -> Result<DirectRecord, DriverError> {
        self.0.clone().ok_or_else(|| DriverError::Journal("empty".into()))
    }

    fn save(&mut self, record: &DirectRecord) -> Result<(), DriverError> {
        self.0 = Some(record.clone());
        Ok(())
    }
}

/// Journal in a JSON file, replaced atomically. No secret: those stay derivable from the seed.
pub struct DirectFileJournal(pub std::path::PathBuf);

impl DirectJournal for DirectFileJournal {
    fn load(&self) -> Result<DirectRecord, DriverError> {
        let text = std::fs::read_to_string(&self.0).map_err(|e| DriverError::Journal(e.to_string()))?;
        serde_json::from_str(&text).map_err(|e| DriverError::Journal(e.to_string()))
    }

    fn save(&mut self, record: &DirectRecord) -> Result<(), DriverError> {
        let text = serde_json::to_string_pretty(record).map_err(|e| DriverError::Journal(e.to_string()))?;
        durable_write(&self.0, text.as_bytes()).map_err(|e| DriverError::Journal(e.to_string()))
    }
}

/// Record the user's approval of the saved trade; works on the journal alone.
pub fn approve_direct<J: DirectJournal>(journal: &mut J, transcript: B256) -> Result<(), DriverError> {
    let mut record = journal.load()?;
    let saved = record.trade.as_ref().ok_or_else(|| DriverError::Journal("no saved trade to approve".into()))?;
    if saved.transcript != transcript {
        return Err(DriverError::Agreement("approval for another trade".into()));
    }
    record.approved = Some(transcript);
    journal.save(&record)
}

pub struct DirectDriver<A, B, J> {
    trade: DirectTrade,
    keys: DirectKeys,
    nonce_a: U256,
    nonce_b: U256,
    a: A,
    b: B,
    journal: J,
}

impl<A: EvmChain, B: EvmChain, J: DirectJournal> DirectDriver<A, B, J> {
    /// Low level (tests). The journal must hold the starting state; our keys must open our side's
    /// commitments.
    pub fn new(trade: DirectTrade, keys: DirectKeys, nonce_a: U256, nonce_b: U256, a: A, b: B, journal: J) -> Result<Self, DriverError> {
        let ok = match journal.load()?.state {
            DirectRole::Leader(_) => {
                keys.claim.as_ref().is_some_and(|c| c.commitment() == trade.leg_a.claim_commitment && c.commitment() == trade.leg_b.claim_commitment)
                    && keys.refund.commitment() == trade.leg_a.refund_commitment
            }
            DirectRole::Follower(_) => keys.refund.commitment() == trade.leg_b.refund_commitment,
        };
        if !ok {
            return Err(DriverError::SecretMismatch);
        }
        Ok(DirectDriver { trade, keys, nonce_a, nonce_b, a, b, journal })
    }

    /// A new trade: the saved trade goes into the journal before anything can act. Starting again
    /// with the same saved trade is a resume; a journal holding another trade is refused.
    pub fn start(saved: SavedDirect, seed: &SwapSeed, account: &PrivateKeySigner, a: A, b: B, mut journal: J) -> Result<Self, DirectError> {
        match journal.load() {
            Ok(record) if record.trade.as_ref() == Some(&saved) => {}
            Ok(_) => return Err(DriverError::Journal("the journal holds another trade".into()).into()),
            Err(_) => {
                let state = match saved.side {
                    Side::Leader => DirectRole::Leader(LeaderState::Start),
                    Side::Follower => DirectRole::Follower(FollowerState::WaitingForA),
                };
                journal.save(&DirectRecord { trade: Some(saved.clone()), ..DirectRecord::new(state) })?;
            }
        }
        Self::resume(seed, account, a, b, journal)
    }

    /// The trade in the journal, rebuilt with the swap seed after a restart.
    pub fn resume(seed: &SwapSeed, account: &PrivateKeySigner, a: A, b: B, journal: J) -> Result<Self, DirectError> {
        let saved = journal.load()?.trade.ok_or_else(|| DriverError::Journal("no saved trade in the journal".into()))?;
        let agreed = saved.agreed(seed, account)?;
        let trade = DirectTrade { leg_a: agreed.leg_a, leg_b: agreed.leg_b, timing: saved.timing, lock_deadline: saved.lock_deadline };
        let keys = seed.direct_keys(&saved.offer, saved.side);
        Ok(Self::new(trade, keys, U256::from(saved.nonce_a()), U256::from(saved.nonce_b()), a, b, journal)?)
    }

    pub fn approve(&mut self, transcript: B256) -> Result<(), DriverError> {
        approve_direct(&mut self.journal, transcript)
    }

    pub fn journal(&self) -> &J {
        &self.journal
    }

    pub fn trade(&self) -> &DirectTrade {
        &self.trade
    }

    /// One round: observe both legs, step, save, act. Returns the new state.
    pub async fn tick(&mut self) -> Result<DirectRole, DirectError> {
        let record = self.journal.load()?;
        let snap = DirectSnapshot {
            a: view(&self.a, &self.trade.leg_a, known(&record.state, LegId::A)).await?,
            b: view(&self.b, &self.trade.leg_b, known(&record.state, LegId::B)).await?,
        };
        let (next, actions) = match &record.state {
            DirectRole::Leader(s) => {
                let (n, a) = leader_step(s, &self.trade, &snap);
                (DirectRole::Leader(n), a)
            }
            DirectRole::Follower(s) => {
                let (n, a) = follower_step(s, &self.trade, &snap);
                (DirectRole::Follower(n), a)
            }
        };
        let leader = matches!(record.state, DirectRole::Leader(_));
        let locks = actions.iter().any(|a| matches!(a, DirectAction::NewSwap(_)));
        if locks && record.trade.as_ref().is_some_and(|t| record.approved != Some(t.transcript)) {
            return Err(DriverError::AwaitingApproval.into());
        }
        // Before our first lock: our account on our leg pays for the lock and what protects it,
        // and our account on the other leg can pay for our claim there
        let (ours, key) = if leader { (LegId::A, "a:new_swap") } else { (LegId::B, "b:new_swap") };
        if locks && !record.retained.contains_key(key) {
            let (lock_terms, claim_terms) = if leader { (&self.trade.leg_a, &self.trade.leg_b) } else { (&self.trade.leg_b, &self.trade.leg_a) };
            match ours {
                LegId::A => {
                    enough(&self.a, lock_terms, true).await?;
                    enough(&self.b, claim_terms, false).await?;
                }
                LegId::B => {
                    enough(&self.b, lock_terms, true).await?;
                    enough(&self.a, claim_terms, false).await?;
                }
            }
        }
        let mut record = record;
        record.state = next.clone();
        self.journal.save(&record)?;
        for action in actions {
            self.execute(action).await?;
        }
        Ok(next)
    }

    async fn execute(&mut self, action: DirectAction) -> Result<(), DirectError> {
        let claim_secret = || self.keys.claim.as_ref().map(|c| c.to_evm()).ok_or(DriverError::SecretMismatch);
        let refund_secret = self.keys.refund.to_evm();
        match action {
            DirectAction::NewSwap(LegId::A) => {
                let call = EvmCall::NewSwap { terms: self.trade.leg_a.clone(), nonce: self.nonce_a };
                lock(&self.a, &mut self.journal, "a", &self.trade.leg_a, call).await
            }
            DirectAction::NewSwap(LegId::B) => {
                let call = EvmCall::NewSwap { terms: self.trade.leg_b.clone(), nonce: self.nonce_b };
                lock(&self.b, &mut self.journal, "b", &self.trade.leg_b, call).await
            }
            DirectAction::SetReady(LegId::A, s) => send(&self.a, &mut self.journal, "a:set_ready", &self.trade.leg_a, EvmCall::SetReady((&s).into())).await,
            DirectAction::SetReady(LegId::B, s) => send(&self.b, &mut self.journal, "b:set_ready", &self.trade.leg_b, EvmCall::SetReady((&s).into())).await,
            DirectAction::ClaimB(s) => {
                let secret = claim_secret()?;
                send(&self.b, &mut self.journal, "b:claim", &self.trade.leg_b, EvmCall::Claim((&s).into(), secret)).await
            }
            DirectAction::ClaimA(s, secret) => send(&self.a, &mut self.journal, "a:claim", &self.trade.leg_a, EvmCall::Claim((&s).into(), secret)).await,
            DirectAction::Refund(LegId::A, s) => send(&self.a, &mut self.journal, "a:refund", &self.trade.leg_a, EvmCall::Refund((&s).into(), refund_secret)).await,
            DirectAction::Refund(LegId::B, s) => send(&self.b, &mut self.journal, "b:refund", &self.trade.leg_b, EvmCall::Refund((&s).into(), refund_secret)).await,
        }
    }
}

/// The swap a state already holds for a leg
fn known(state: &DirectRole, leg: LegId) -> Option<SwapData> {
    use FollowerState as F;
    use LeaderState as L;
    match (state, leg) {
        (DirectRole::Leader(L::Locked { a } | L::ReadySent { a } | L::Waiting { a } | L::ClaimSent { a, .. } | L::GaveUp { a } | L::RefundSent { a }), LegId::A) => Some(a.clone()),
        (DirectRole::Leader(L::ClaimSent { b, .. }), LegId::B) => Some(b.clone()),
        (DirectRole::Follower(F::LockSent { a } | F::Locked { a, .. } | F::ReadySent { a, .. } | F::Ready { a, .. } | F::Claiming { a, .. } | F::RefundSent { a, .. }), LegId::A) => {
            Some(a.clone())
        }
        (DirectRole::Follower(F::Locked { b, .. } | F::ReadySent { b, .. } | F::Ready { b, .. } | F::RefundSent { b, .. }), LegId::B) => Some(b.clone()),
        _ => None,
    }
}

/// One leg as the chain shows it now. A lock that does not match the terms is still read, so its
/// owner can take it back.
async fn view<C: EvmChain>(chain: &C, terms: &AgreedTerms, known: Option<SwapData>) -> Result<LegView, DriverError> {
    let now = chain.now().await?;
    let lock = chain.find_lock(terms).await?;
    let swap: Option<Swap> = known.map(|s| (&s).into()).or_else(|| {
        lock.as_ref().map(|l| {
            let mut pending = l.clone();
            pending.stage = crate::Stage::Pending as u8;
            check_lock(terms, &pending).unwrap_or_else(|_| {
                let e = &l.log.inner.data;
                Swap {
                    owner: e.owner,
                    claimer: e.claimer,
                    claimCommitment: e.claimCommitment,
                    refundCommitment: e.refundCommitment,
                    timeout1: e.timeout1,
                    timeout2: e.timeout2,
                    asset: e.asset,
                    value: e.value,
                    nonce: e.nonce,
                }
            })
        })
    });
    let (stage, revealed) = match &swap {
        Some(s) => (Some(chain.stage(swap_id(s)).await?), chain.revealed(s).await?),
        None => (None, None),
    };
    Ok(LegView { now, lock, stage, revealed })
}

/// Our account on `chain` can pay for its part: the lock and its protection (`owner`), or a claim
async fn enough<C: EvmChain>(chain: &C, terms: &AgreedTerms, owner: bool) -> Result<(), DirectError> {
    let funds = chain.funds().await?;
    let needed = funding_needed(terms, owner, funds.fee_per_gas) + chain.reserve(owner);
    if funds.balance < needed {
        return Err(DirectError::AwaitingFunding { chain_id: terms.chain.id(), asset: Address::ZERO, needed, have: funds.balance });
    }
    // A token we lock: the tokens themselves too (the coin above pays the fees)
    if owner && terms.asset != Address::ZERO {
        let held = chain.token_held(terms.asset).await?;
        if held < terms.value {
            return Err(DirectError::AwaitingFunding { chain_id: terms.chain.id(), asset: terms.asset, needed: terms.value, have: held });
        }
    }
    Ok(())
}

/// Our lock; a token lock first lets the contract take the tokens (kept and resent like any step)
async fn lock<C: EvmChain, J: DirectJournal>(chain: &C, journal: &mut J, leg: &str, terms: &AgreedTerms, call: EvmCall) -> Result<(), DirectError> {
    let kind = format!("{leg}:new_swap");
    if terms.asset != Address::ZERO && !journal.load()?.retained.contains_key(&kind) && chain.allowance(terms.asset).await? < terms.value {
        return send(chain, journal, &format!("{leg}:approve"), terms, EvmCall::Approve { token: terms.asset, amount: terms.value }).await;
    }
    send(chain, journal, &kind, terms, call).await
}

/// Sign once and keep it, replace it with higher fees when it is stuck, then (re)broadcast the
/// newest kept version.
async fn send<C: EvmChain, J: DirectJournal>(chain: &C, journal: &mut J, kind: &str, terms: &AgreedTerms, call: EvmCall) -> Result<(), DirectError> {
    let mut record = journal.load()?;
    let head = chain.head().await?;
    let newest = match record.retained.get(kind).and_then(|v| v.last()) {
        None => Some(chain.sign(&call).await?),
        Some(last) if head >= last.block + terms.chain.bump_after_blocks() => chain.replacement(&last.raw).await?,
        Some(_) => None,
    };
    if let Some(raw) = newest {
        record.retained.entry(kind.to_string()).or_default().push(Signed { raw, block: head });
        journal.save(&record)?;
    }
    let raw = &record.retained[kind].last().expect("kept above").raw;
    Ok(chain.broadcast(raw).await?)
}

#[cfg(test)]
#[path = "direct_driver_tests.rs"]
mod tests;
