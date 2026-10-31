//! XMZ <-> BNB swap states for both traders, without any I/O.
//!
//! The driver (the engine's journal) polls both chains, builds a [`Snapshot`] and calls
//! `step`. `step` returns the next state and the actions to take. Every action means
//! "make sure this has happened": the journal signs a transaction once, keeps it, and
//! rebroadcasts the same transaction until the next snapshot shows its effect. So the same
//! action may be returned on many ticks, and that is safe.
//!
//! Roles, as in the contract: the **owner** holds BNB (or a token) and buys XMZ; the
//! **claimer** holds XMZ and sells it. Each holds one secret share of the shared XMZ key.
//! The XMZ actions (lock to the shared address, sweep with both shares) are carried out by
//! the existing XMZ wallet code.

use crate::{check_lock, claim_window, refund_window, AgreedTerms, ObservedLock, Revealed, Stage, Swap};
use alloy::primitives::{Address, B256, U256};
use serde::{Deserialize, Serialize};

/// Safety margins, in seconds unless noted.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Timing {
    /// XMZ confirmations before the owner trusts the XMZ lock (10, as in the BTC/XMZ swaps)
    pub xmz_confirmations: u64,
    /// The claimer locks XMZ only if this much time is left before timeout1: enough for the
    /// XMZ lock to confirm and the owner to call setReady.
    pub xmz_lock_budget: u64,
    /// Without a confirmed, correct XMZ lock this close to timeout1, the owner refunds early.
    pub owner_refund_margin: u64,
    /// No claim is sent this close to timeout2: a claim that lands after timeout2 fails but
    /// still reveals the claimer's secret.
    pub claim_margin: u64,
}

impl Timing {
    /// Defaults for BNB Chain with the design's 3 h / 6 h timeouts.
    pub const BSC: Timing = Timing {
        xmz_confirmations: 10,
        xmz_lock_budget: 3600,
        owner_refund_margin: 1800,
        claim_margin: 1800,
    };
}

/// One agreed trade, as both sides hold it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Trade {
    pub terms: AgreedTerms,
    /// XMZ the claimer locks, in atomic units
    pub xmz_amount: u64,
    pub timing: Timing,
    /// The claimer gives up waiting for the owner's lock after this time
    pub lock_deadline: u64,
}

/// The swap as stored in a state (the contract's struct, in serializable form).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SwapData {
    pub owner: Address,
    pub claimer: Address,
    pub claim_commitment: Address,
    pub refund_commitment: Address,
    pub timeout1: U256,
    pub timeout2: U256,
    pub asset: Address,
    pub value: U256,
    pub nonce: U256,
}

impl From<&Swap> for SwapData {
    fn from(s: &Swap) -> Self {
        SwapData {
            owner: s.owner,
            claimer: s.claimer,
            claim_commitment: s.claimCommitment,
            refund_commitment: s.refundCommitment,
            timeout1: s.timeout1,
            timeout2: s.timeout2,
            asset: s.asset,
            value: s.value,
            nonce: s.nonce,
        }
    }
}

impl From<&SwapData> for Swap {
    fn from(s: &SwapData) -> Self {
        Swap {
            owner: s.owner,
            claimer: s.claimer,
            claimCommitment: s.claim_commitment,
            refundCommitment: s.refund_commitment,
            timeout1: s.timeout1,
            timeout2: s.timeout2,
            asset: s.asset,
            value: s.value,
            nonce: s.nonce,
        }
    }
}

/// The XMZ at the shared address, as the watcher sees it with the shared view key.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct XmzLock {
    pub amount: u64,
    pub confirmations: u64,
}

/// What the driver observed on both chains for this swap.
#[derive(Clone, Debug, Default)]
pub struct Snapshot {
    /// Timestamp of the latest confirmed EVM block (the contract's clock)
    pub now: u64,
    /// The owner's `New` event, once it has enough confirmations
    pub evm_lock: Option<ObservedLock>,
    /// `swaps(id)` read after enough confirmations
    pub stage: Option<Stage>,
    /// A secret revealed by `Claimed` or `Refunded` (see [`crate::revealed_secret`])
    pub revealed: Option<Revealed>,
    /// XMZ received at the shared address
    pub xmz_lock: Option<XmzLock>,
    /// The shared XMZ output has been spent by our sweep and confirmed
    pub xmz_swept: bool,
}

/// What to make sure of next.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Action {
    /// Owner: lock the agreed value with `newSwap`
    SendNewSwap,
    /// Owner: confirm the XMZ lock
    SendSetReady(SwapData),
    /// Owner: take the funds back; reveals the owner's secret
    SendRefund(SwapData),
    /// Claimer: send `amount` XMZ to the shared address
    LockXmz { amount: u64 },
    /// Claimer: take the funds; reveals the claimer's secret
    SendClaim(SwapData),
    /// Either: sweep the shared XMZ with our share plus the other side's revealed share
    SweepXmz { their_secret: B256 },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum Outcome {
    /// The trade happened: the owner has the XMZ, the claimer the BNB
    Swapped,
    /// Both sides have their own funds back
    Refunded,
    /// Stopped before this side locked anything
    Aborted,
}

// ---------- owner ----------

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum OwnerState {
    Start,
    LockSent,
    Locked { swap: SwapData },
    ReadySent { swap: SwapData },
    Ready { swap: SwapData },
    RefundSent { swap: SwapData },
    /// The claimer claimed: take the XMZ with both shares
    Sweeping { their_secret: B256 },
    Done(Outcome),
}

pub fn owner_step(state: &OwnerState, trade: &Trade, snap: &Snapshot) -> (OwnerState, Vec<Action>) {
    use OwnerState::*;
    // A revealed claim always means: sweep the XMZ (whatever this side was doing)
    if let Some(Revealed::Claimed(secret)) = snap.revealed {
        if !matches!(state, Done(_)) {
            return sweeping(Sweeping { their_secret: secret }, secret, snap, Outcome::Swapped);
        }
    }
    let t = &trade.timing;
    match state {
        Start => (LockSent, vec![Action::SendNewSwap]),
        LockSent => match &snap.evm_lock {
            None => (LockSent, vec![Action::SendNewSwap]),
            Some(lock) => match check_lock(&trade.terms, lock) {
                Ok(swap) => owner_step(&Locked { swap: (&swap).into() }, trade, snap),
                // Our own lock differs from the terms: the claimer will not lock XMZ. Refund early.
                Err(_) => {
                    let swap = swap_of(lock);
                    refund(swap, snap)
                }
            },
        },
        Locked { swap } => {
            let (t1, t2) = timeouts(swap);
            if matches!(snap.revealed, Some(Revealed::Refunded(_))) {
                return (Done(Outcome::Refunded), vec![]);
            }
            match snap.xmz_lock {
                Some(l) if l.amount != trade.xmz_amount && snap.now < t1 => refund(swap.clone(), snap),
                Some(l) if l.confirmations >= t.xmz_confirmations && snap.now + t.owner_refund_margin < t1 => {
                    (ReadySent { swap: swap.clone() }, vec![Action::SendSetReady(swap.clone())])
                }
                _ if snap.now + t.owner_refund_margin >= t1 && snap.now < t1 => refund(swap.clone(), snap),
                // Missed the early refund (owner was offline): only from timeout2 on
                _ if snap.now >= t2 => refund(swap.clone(), snap),
                _ => (Locked { swap: swap.clone() }, vec![]),
            }
        }
        ReadySent { swap } => {
            let (t1, _) = timeouts(swap);
            match snap.stage {
                Some(Stage::Ready) => owner_step(&Ready { swap: swap.clone() }, trade, snap),
                // setReady did not land before timeout1; the claimer may claim anyway
                Some(Stage::Pending) if snap.now >= t1 => owner_step(&Locked { swap: swap.clone() }, trade, snap),
                _ => (ReadySent { swap: swap.clone() }, vec![Action::SendSetReady(swap.clone())]),
            }
        }
        Ready { swap } => {
            let (_, t2) = timeouts(swap);
            if snap.now >= t2 {
                refund(swap.clone(), snap)
            } else {
                (Ready { swap: swap.clone() }, vec![])
            }
        }
        RefundSent { swap } => match snap.revealed {
            Some(Revealed::Refunded(_)) => (Done(Outcome::Refunded), vec![]),
            _ => refund(swap.clone(), snap),
        },
        Sweeping { their_secret } => sweeping(Sweeping { their_secret: *their_secret }, *their_secret, snap, Outcome::Swapped),
        Done(outcome) => (Done(*outcome), vec![]),
    }
}

/// Sends the refund only when the contract's stage has been read and allows it; otherwise
/// waits in RefundSent. A refund that fails on chain still publishes the owner's secret in
/// its calldata, so it is never sent on a guessed stage.
fn refund(swap: SwapData, snap: &Snapshot) -> (OwnerState, Vec<Action>) {
    let allowed = snap.stage.is_some_and(|stage| refund_window(&(&swap).into(), stage, snap.now));
    let actions = if allowed { vec![Action::SendRefund(swap.clone())] } else { vec![] };
    (OwnerState::RefundSent { swap }, actions)
}

// ---------- claimer ----------

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum ClaimerState {
    WaitingForEvmLock,
    XmzLockSent { swap: SwapData },
    XmzLocked { swap: SwapData },
    ClaimSent { swap: SwapData },
    /// Too close to timeout2 to claim safely: wait for the owner's refund
    WaitingForRefund { swap: SwapData },
    /// The owner refunded: take the XMZ back with both shares
    SweepingBack { their_secret: B256 },
    Done(Outcome),
}

pub fn claimer_step(state: &ClaimerState, trade: &Trade, snap: &Snapshot) -> (ClaimerState, Vec<Action>) {
    use ClaimerState::*;
    let t = &trade.timing;
    // Once our XMZ may be locked, an owner refund means: sweep it back
    if let Some(Revealed::Refunded(secret)) = snap.revealed {
        if !matches!(state, WaitingForEvmLock | Done(_)) {
            return claimer_sweeping(secret, snap);
        }
    }
    match state {
        WaitingForEvmLock => match &snap.evm_lock {
            None if snap.now >= trade.lock_deadline => (Done(Outcome::Aborted), vec![]),
            None => (WaitingForEvmLock, vec![]),
            Some(lock) => match check_lock(&trade.terms, lock) {
                // Never lock XMZ against a lock that differs from the terms
                Err(_) => (Done(Outcome::Aborted), vec![]),
                Ok(swap) => {
                    let (t1, _) = timeouts(&(&swap).into());
                    if snap.now + t.xmz_lock_budget >= t1 {
                        (Done(Outcome::Aborted), vec![])
                    } else {
                        (XmzLockSent { swap: (&swap).into() }, vec![Action::LockXmz { amount: trade.xmz_amount }])
                    }
                }
            },
        },
        XmzLockSent { swap } => match snap.xmz_lock {
            Some(l) if l.confirmations >= t.xmz_confirmations => claimer_step(&XmzLocked { swap: swap.clone() }, trade, snap),
            _ => (XmzLockSent { swap: swap.clone() }, vec![Action::LockXmz { amount: trade.xmz_amount }]),
        },
        XmzLocked { swap } | ClaimSent { swap } => {
            if matches!(snap.revealed, Some(Revealed::Claimed(_))) {
                return (Done(Outcome::Swapped), vec![]);
            }
            // As for refunds: a claim that fails still publishes the secret, so the stage
            // must have been read, never guessed
            let can_claim = snap.stage.is_some_and(|stage| claim_window(&swap.into(), stage, snap.now, t.claim_margin));
            if can_claim {
                (ClaimSent { swap: swap.clone() }, vec![Action::SendClaim(swap.clone())])
            } else if snap.now + t.claim_margin >= timeouts(swap).1 {
                (WaitingForRefund { swap: swap.clone() }, vec![])
            } else {
                (state.clone(), vec![])
            }
        }
        WaitingForRefund { swap } => match snap.revealed {
            // A claim sent earlier may still have landed in time
            Some(Revealed::Claimed(_)) => (Done(Outcome::Swapped), vec![]),
            _ => (WaitingForRefund { swap: swap.clone() }, vec![]),
        },
        SweepingBack { their_secret } => claimer_sweeping(*their_secret, snap),
        Done(outcome) => (Done(*outcome), vec![]),
    }
}

fn claimer_sweeping(secret: B256, snap: &Snapshot) -> (ClaimerState, Vec<Action>) {
    if snap.xmz_swept {
        (ClaimerState::Done(Outcome::Refunded), vec![])
    } else {
        (ClaimerState::SweepingBack { their_secret: secret }, vec![Action::SweepXmz { their_secret: secret }])
    }
}

fn sweeping(state: OwnerState, secret: B256, snap: &Snapshot, outcome: Outcome) -> (OwnerState, Vec<Action>) {
    if snap.xmz_swept {
        (OwnerState::Done(outcome), vec![])
    } else {
        (state, vec![Action::SweepXmz { their_secret: secret }])
    }
}

fn timeouts(swap: &SwapData) -> (u64, u64) {
    (swap.timeout1.saturating_to::<u64>(), swap.timeout2.saturating_to::<u64>())
}

fn swap_of(lock: &ObservedLock) -> SwapData {
    let e = &lock.log.inner.data;
    SwapData {
        owner: e.owner,
        claimer: e.claimer,
        claim_commitment: e.claimCommitment,
        refund_commitment: e.refundCommitment,
        timeout1: e.timeout1,
        timeout2: e.timeout2,
        asset: e.asset,
        value: e.value,
        nonce: e.nonce,
    }
}

#[cfg(test)]
#[path = "machine_tests.rs"]
mod tests;
