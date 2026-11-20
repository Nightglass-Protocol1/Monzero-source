//! States of a direct (coin <-> coin) trade for both sides, without any I/O. The protocol is
//! described in [`crate::direct`].
//!
//! As in [`crate::machine`], the driver polls both legs into a [`DirectSnapshot`] and calls
//! `step`, which returns the next state and the actions to make sure of. Every action is safe to
//! return on many ticks: the driver signs a transaction once and rebroadcasts it.

use crate::direct::DirectTiming;
use crate::machine::SwapData;
use crate::{check_lock, claim_window, refund_window, AgreedTerms, ObservedLock, Revealed, Stage};
use alloy::primitives::B256;
use serde::{Deserialize, Serialize};

/// One agreed direct trade, as both sides hold it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DirectTrade {
    /// Leader locks, follower claims
    pub leg_a: AgreedTerms,
    /// Follower locks, leader claims
    pub leg_b: AgreedTerms,
    pub timing: DirectTiming,
    /// The follower gives up if A is not safely locked by then (chain A's clock)
    pub lock_deadline: u64,
}

/// What the driver observed on one leg.
#[derive(Clone, Debug, Default)]
pub struct LegView {
    /// Timestamp of the newest block: the clock this leg's contract runs on
    pub now: u64,
    /// The owner's lock, once it has enough confirmations
    pub lock: Option<ObservedLock>,
    /// The swap's stage, read at a confirmed depth
    pub stage: Option<Stage>,
    /// A secret published by a claim or a refund, at any depth
    pub revealed: Option<Revealed>,
}

#[derive(Clone, Debug, Default)]
pub struct DirectSnapshot {
    pub a: LegView,
    pub b: LegView,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum LegId {
    A,
    B,
}

/// What to make sure of next.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum DirectAction {
    /// Lock our leg (A for the leader, B for the follower)
    NewSwap(LegId),
    SetReady(LegId, SwapData),
    /// Leader: claim B with our secret (publishes it)
    ClaimB(SwapData),
    /// Follower: claim A with the secret the leader published on B
    ClaimA(SwapData, B256),
    /// Take our own leg back (publishes only our refund secret)
    Refund(LegId, SwapData),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum DirectOutcome {
    /// Each side has the other's coin
    Swapped,
    /// Our coin is back
    Refunded,
    /// Stopped before this side locked anything
    Aborted,
    /// Follower only: the leader took B and then A back before our claim of A landed
    /// (we were offline from the leader's claim until A's second time limit)
    Missed,
}

/// The lock checked against the terms, whatever its stage (the stage is read separately).
fn checked(terms: &AgreedTerms, lock: &ObservedLock) -> Result<SwapData, crate::Error> {
    let mut pending = lock.clone();
    pending.stage = Stage::Pending as u8;
    check_lock(terms, &pending).map(|s| (&s).into())
}

fn raw(lock: &ObservedLock) -> SwapData {
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

fn timeouts(swap: &SwapData) -> (u64, u64) {
    (swap.timeout1.saturating_to::<u64>(), swap.timeout2.saturating_to::<u64>())
}

// ---------- leader: locks A, claims B ----------

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum LeaderState {
    Start,
    LockSent,
    /// Our lock of A is confirmed: make it `Ready` so it can never be taken back early
    Locked { a: SwapData },
    ReadySent { a: SwapData },
    /// A can no longer be refunded early: waiting for the follower's lock of B
    Waiting { a: SwapData },
    ClaimSent { a: SwapData, b: SwapData },
    /// Too late to claim B safely: take A back from its second time limit
    GaveUp { a: SwapData },
    RefundSent { a: SwapData },
    Done(DirectOutcome),
}

pub fn leader_step(state: &LeaderState, trade: &DirectTrade, snap: &DirectSnapshot) -> (LeaderState, Vec<DirectAction>) {
    use LeaderState::*;
    if matches!(state, Done(_)) {
        return (state.clone(), vec![]);
    }
    // Our claim of B landed: the trade happened (the follower claims A with the secret it shows)
    if matches!(snap.b.revealed, Some(Revealed::Claimed(_))) && !matches!(state, Start | LockSent) {
        return (Done(DirectOutcome::Swapped), vec![]);
    }
    if matches!(snap.a.revealed, Some(Revealed::Refunded(_))) && !matches!(state, Start) {
        return (Done(DirectOutcome::Refunded), vec![]);
    }
    let a_now = snap.a.now;
    match state {
        // Not enough time left for the follower to see A safely locked before it gives up: a lock
        // now would only sit there until A's second time limit
        Start if a_now + crate::direct_listing::LEADER_LOCK_BUDGET > trade.lock_deadline => (Done(DirectOutcome::Aborted), vec![]),
        Start => (LockSent, vec![DirectAction::NewSwap(LegId::A)]),
        LockSent => match &snap.a.lock {
            None => (LockSent, vec![DirectAction::NewSwap(LegId::A)]),
            Some(lock) => match checked(&trade.leg_a, lock) {
                Ok(a) => leader_step(&Locked { a }, trade, snap),
                // Our own lock differs from the terms: the follower will not lock. Take it back.
                Err(_) => refund_a(raw(lock), snap),
            },
        },
        Locked { a } | ReadySent { a } => {
            let (t1, _) = timeouts(a);
            match snap.a.stage {
                Some(Stage::Ready) => leader_step(&Waiting { a: a.clone() }, trade, snap),
                Some(Stage::Pending) if a_now >= t1 => leader_step(&Waiting { a: a.clone() }, trade, snap),
                Some(Stage::Pending) => (ReadySent { a: a.clone() }, vec![DirectAction::SetReady(LegId::A, a.clone())]),
                _ => (state.clone(), vec![]),
            }
        }
        Waiting { a } => {
            if a_now >= timeouts(a).1 {
                return refund_a(a.clone(), snap);
            }
            match snap.b.lock.as_ref().map(|lock| checked(&trade.leg_b, lock)) {
                // A lock of B that differs from the terms is never claimed against
                Some(Ok(b)) => claim_or_wait(a.clone(), b, trade, snap),
                _ => (Waiting { a: a.clone() }, vec![]),
            }
        }
        ClaimSent { a, b } => claim_or_wait(a.clone(), b.clone(), trade, snap),
        GaveUp { a } => {
            if a_now >= timeouts(a).1 {
                refund_a(a.clone(), snap)
            } else {
                (GaveUp { a: a.clone() }, vec![])
            }
        }
        RefundSent { a } => refund_a(a.clone(), snap),
        Done(_) => unreachable!("handled above"),
    }
}

fn claim_or_wait(a: SwapData, b: SwapData, trade: &DirectTrade, snap: &DirectSnapshot) -> (LeaderState, Vec<DirectAction>) {
    let margin = trade.timing.claim_margin;
    // A claim that fails still publishes the secret: only on a stage actually read
    let can_claim = snap.b.stage.is_some_and(|stage| claim_window(&(&b).into(), stage, snap.b.now, margin));
    if can_claim {
        (LeaderState::ClaimSent { a, b: b.clone() }, vec![DirectAction::ClaimB(b)])
    } else if snap.b.now + margin >= timeouts(&b).1 || snap.b.stage == Some(Stage::Completed) {
        // Too late, or the follower took B back: our coin comes back from A's second time limit
        (LeaderState::GaveUp { a }, vec![])
    } else {
        // B not yet `Ready` (the follower sets it once its lock is confirmed)
        (LeaderState::ClaimSent { a, b }, vec![])
    }
}

/// Sends the refund only on a stage that was read and allows it (a refund that fails still
/// publishes our refund secret; harmless here, but the rule is the same as everywhere)
fn refund_a(a: SwapData, snap: &DirectSnapshot) -> (LeaderState, Vec<DirectAction>) {
    let allowed = snap.a.stage.is_some_and(|stage| refund_window(&(&a).into(), stage, snap.a.now));
    let actions = if allowed { vec![DirectAction::Refund(LegId::A, a.clone())] } else { vec![] };
    (LeaderState::RefundSent { a }, actions)
}

// ---------- follower: locks B, claims A ----------

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum FollowerState {
    /// Waiting for the leader's lock of A to be confirmed and no longer refundable early
    WaitingForA,
    LockSent { a: SwapData },
    /// Our lock of B is confirmed: make it `Ready` so the leader can claim
    Locked { a: SwapData, b: SwapData },
    ReadySent { a: SwapData, b: SwapData },
    /// Waiting for the leader's claim of B
    Ready { a: SwapData, b: SwapData },
    /// The leader claimed B: claim A with the secret it published
    Claiming { a: SwapData, secret: B256 },
    RefundSent { a: SwapData, b: SwapData },
    Done(DirectOutcome),
}

/// A is safe to lock against: `Ready` at a confirmed depth (its owner cannot take it back before
/// timeout2), or still `Pending` well past timeout1 (no early refund can be on its way), and no
/// refund of it seen at any depth.
fn a_is_safe(a: &SwapData, trade: &DirectTrade, view: &LegView) -> bool {
    let late = timeouts(a).0 + trade.timing.clock_skew + 3600;
    let stage_ok = match view.stage {
        Some(Stage::Ready) => true,
        Some(Stage::Pending) => view.now >= late,
        _ => false,
    };
    stage_ok && !matches!(view.revealed, Some(Revealed::Refunded(_)))
}

pub fn follower_step(state: &FollowerState, trade: &DirectTrade, snap: &DirectSnapshot) -> (FollowerState, Vec<DirectAction>) {
    use FollowerState::*;
    if matches!(state, Done(_)) {
        return (state.clone(), vec![]);
    }
    let t = &trade.timing;
    // Once B may be locked: the leader's claim of B means claim A; our refund of B means done
    if let Some(a) = swap_a(state) {
        match snap.b.revealed {
            Some(Revealed::Claimed(secret)) => return claim_a(a, secret, snap),
            Some(Revealed::Refunded(_)) if !matches!(state, Claiming { .. }) => return (Done(DirectOutcome::Refunded), vec![]),
            _ => {}
        }
    }
    match state {
        WaitingForA => {
            let give_up = snap.a.now >= trade.lock_deadline;
            let Some(lock) = &snap.a.lock else {
                return if give_up { (Done(DirectOutcome::Aborted), vec![]) } else { (WaitingForA, vec![]) };
            };
            let Ok(a) = checked(&trade.leg_a, lock) else {
                // Never lock against a lock that differs from the terms
                return (Done(DirectOutcome::Aborted), vec![]);
            };
            if matches!(snap.a.stage, Some(Stage::Completed)) || matches!(snap.a.revealed, Some(Revealed::Refunded(_))) {
                return (Done(DirectOutcome::Aborted), vec![]);
            }
            if !a_is_safe(&a, trade, &snap.a) {
                return if give_up { (Done(DirectOutcome::Aborted), vec![]) } else { (WaitingForA, vec![]) };
            }
            // B, locked now, must close with time to spare before A does
            let b = &trade.leg_b;
            let b_closes = snap.a.now + b.duration1 + b.duration2 + t.follower_reserve + t.clock_skew;
            if give_up || b_closes > timeouts(&a).1 {
                return (Done(DirectOutcome::Aborted), vec![]);
            }
            (LockSent { a }, vec![DirectAction::NewSwap(LegId::B)])
        }
        LockSent { a } => match &snap.b.lock {
            None => (LockSent { a: a.clone() }, vec![DirectAction::NewSwap(LegId::B)]),
            Some(lock) => match checked(&trade.leg_b, lock) {
                Ok(b) => follower_step(&Locked { a: a.clone(), b }, trade, snap),
                Err(_) => refund_b(a.clone(), raw(lock), snap),
            },
        },
        Locked { a, b } | ReadySent { a, b } => {
            // Should A have been taken back after all, B is never made claimable: take it back
            if matches!(snap.a.revealed, Some(Revealed::Refunded(_))) {
                return refund_b(a.clone(), b.clone(), snap);
            }
            let (t1, _) = timeouts(b);
            match snap.b.stage {
                Some(Stage::Ready) => follower_step(&Ready { a: a.clone(), b: b.clone() }, trade, snap),
                Some(Stage::Pending) if snap.b.now >= t1 => follower_step(&Ready { a: a.clone(), b: b.clone() }, trade, snap),
                Some(Stage::Pending) => (ReadySent { a: a.clone(), b: b.clone() }, vec![DirectAction::SetReady(LegId::B, b.clone())]),
                _ => (state.clone(), vec![]),
            }
        }
        Ready { a, b } => {
            if snap.b.now >= timeouts(b).1 {
                refund_b(a.clone(), b.clone(), snap)
            } else {
                (state.clone(), vec![])
            }
        }
        Claiming { a, secret } => claim_a(a.clone(), *secret, snap),
        RefundSent { a, b } => refund_b(a.clone(), b.clone(), snap),
        Done(_) => unreachable!("handled above"),
    }
}

fn swap_a(state: &FollowerState) -> Option<SwapData> {
    use FollowerState::*;
    match state {
        LockSent { a } | Locked { a, .. } | ReadySent { a, .. } | Ready { a, .. } | Claiming { a, .. } | RefundSent { a, .. } => Some(a.clone()),
        WaitingForA | Done(_) => None,
    }
}

/// The secret is public once the leader's claim is out, so our claim of A needs no margin: it is
/// sent while the contract allows it at all.
fn claim_a(a: SwapData, secret: B256, snap: &DirectSnapshot) -> (FollowerState, Vec<DirectAction>) {
    match snap.a.revealed {
        Some(Revealed::Claimed(_)) => return (FollowerState::Done(DirectOutcome::Swapped), vec![]),
        Some(Revealed::Refunded(_)) => return (FollowerState::Done(DirectOutcome::Missed), vec![]),
        None => {}
    }
    let open = snap.a.stage.is_some_and(|stage| claim_window(&(&a).into(), stage, snap.a.now, 0));
    let actions = if open { vec![DirectAction::ClaimA(a.clone(), secret)] } else { vec![] };
    (FollowerState::Claiming { a, secret }, actions)
}

fn refund_b(a: SwapData, b: SwapData, snap: &DirectSnapshot) -> (FollowerState, Vec<DirectAction>) {
    let allowed = snap.b.stage.is_some_and(|stage| refund_window(&(&b).into(), stage, snap.b.now));
    let actions = if allowed { vec![DirectAction::Refund(LegId::B, b.clone())] } else { vec![] };
    (FollowerState::RefundSent { a, b }, actions)
}

#[cfg(test)]
#[path = "direct_machine_tests.rs"]
mod tests;
