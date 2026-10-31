use super::*;
use crate::{commitment, swap_id, Chain, XmzSwap};
use alloy::rpc::types::Log;

const CONTRACT: Address = Address::repeat_byte(0xc0);
const LOCK_TIME: u64 = 1_000_000;
const D1: u64 = 3 * 3600;
const D2: u64 = 6 * 3600;
const T1: u64 = LOCK_TIME + D1;
const T2: u64 = T1 + D2;
const XMZ: u64 = 5_000_000_000_000;

fn secret(byte: u8) -> B256 {
    B256::repeat_byte(byte)
}

fn trade() -> Trade {
    Trade {
        terms: AgreedTerms {
            chain: Chain::Local,
            contract: CONTRACT,
            owner: Address::repeat_byte(0x01),
            claimer: Address::repeat_byte(0x02),
            claim_commitment: commitment(&secret(0x11)).unwrap(),
            refund_commitment: commitment(&secret(0x22)).unwrap(),
            asset: Address::ZERO,
            value: U256::from(10u64.pow(18)),
            duration1: D1,
            duration2: D2,
        },
        xmz_amount: XMZ,
        timing: Timing::BSC,
        lock_deadline: LOCK_TIME + 600,
    }
}

/// The owner's lock as the contract emits it for `trade()`, optionally altered
fn lock_with(change: impl FnOnce(&mut XmzSwap::New)) -> ObservedLock {
    let t = trade().terms;
    let mut e = XmzSwap::New {
        swapId: B256::ZERO,
        owner: t.owner,
        claimer: t.claimer,
        claimCommitment: t.claim_commitment,
        refundCommitment: t.refund_commitment,
        timeout1: U256::from(T1),
        timeout2: U256::from(T2),
        asset: t.asset,
        value: t.value,
        nonce: U256::from(1),
    };
    change(&mut e);
    e.swapId = swap_id(&Swap {
        owner: e.owner,
        claimer: e.claimer,
        claimCommitment: e.claimCommitment,
        refundCommitment: e.refundCommitment,
        timeout1: e.timeout1,
        timeout2: e.timeout2,
        asset: e.asset,
        value: e.value,
        nonce: e.nonce,
    });
    ObservedLock {
        chain_id: Chain::Local.id(),
        log: Log {
            inner: alloy::primitives::Log { address: CONTRACT, data: e },
            block_hash: None,
            block_number: None,
            block_timestamp: None,
            transaction_hash: None,
            transaction_index: None,
            log_index: None,
            removed: false,
        },
        block_timestamp: LOCK_TIME,
        stage: Stage::Pending as u8,
    }
}

fn swap() -> SwapData {
    (&check_lock(&trade().terms, &lock_with(|_| {})).unwrap()).into()
}

/// A snapshot at `now` with the owner's lock confirmed and the stage read
fn at(now: u64, stage: Stage) -> Snapshot {
    Snapshot { now, evm_lock: Some(lock_with(|_| {})), stage: Some(stage), ..Default::default() }
}

fn xmz(confirmations: u64) -> Option<XmzLock> {
    Some(XmzLock { amount: XMZ, confirmations })
}

// ---------- owner ----------

#[test]
fn owner_happy_path() {
    let tr = trade();
    let (s, a) = owner_step(&OwnerState::Start, &tr, &Snapshot { now: LOCK_TIME - 5, ..Default::default() });
    assert_eq!((s.clone(), a), (OwnerState::LockSent, vec![Action::SendNewSwap]));

    // Not seen yet: keep making sure the lock is broadcast
    let (s, a) = owner_step(&s, &tr, &Snapshot { now: LOCK_TIME, ..Default::default() });
    assert_eq!(a, vec![Action::SendNewSwap]);

    let (s, a) = owner_step(&s, &tr, &at(LOCK_TIME + 60, Stage::Pending));
    assert_eq!((s.clone(), a), (OwnerState::Locked { swap: swap() }, vec![]));

    // XMZ lock seen but not yet confirmed enough
    let (s, a) = owner_step(&s, &tr, &Snapshot { xmz_lock: xmz(3), ..at(LOCK_TIME + 900, Stage::Pending) });
    assert_eq!(a, vec![]);
    let (s, a) = owner_step(&s, &tr, &Snapshot { xmz_lock: xmz(10), ..at(LOCK_TIME + 1800, Stage::Pending) });
    assert_eq!((s.clone(), a), (OwnerState::ReadySent { swap: swap() }, vec![Action::SendSetReady(swap())]));

    let (s, a) = owner_step(&s, &tr, &Snapshot { xmz_lock: xmz(12), ..at(LOCK_TIME + 1900, Stage::Ready) });
    assert_eq!((s.clone(), a), (OwnerState::Ready { swap: swap() }, vec![]));

    // The claimer claims: sweep the XMZ with its revealed share, until the sweep confirms
    let claimed = Snapshot { revealed: Some(Revealed::Claimed(secret(0x11))), ..at(LOCK_TIME + 2000, Stage::Completed) };
    let (s, a) = owner_step(&s, &tr, &claimed);
    assert_eq!(a, vec![Action::SweepXmz { their_secret: secret(0x11) }]);
    let (s, a) = owner_step(&s, &tr, &Snapshot { xmz_swept: true, ..claimed });
    assert_eq!((s, a), (OwnerState::Done(Outcome::Swapped), vec![]));
}

#[test]
fn owner_refunds_early_without_an_xmz_lock_and_finishes_on_the_refund_event() {
    let tr = trade();
    let locked = OwnerState::Locked { swap: swap() };
    let (s, a) = owner_step(&locked, &tr, &at(T1 - 1801, Stage::Pending));
    assert_eq!((s, a), (locked.clone(), vec![]), "still time for the XMZ lock");
    let (s, a) = owner_step(&locked, &tr, &at(T1 - 1800, Stage::Pending));
    assert_eq!((s.clone(), a), (OwnerState::RefundSent { swap: swap() }, vec![Action::SendRefund(swap())]));
    let (s, a) = owner_step(&s, &tr, &Snapshot { revealed: Some(Revealed::Refunded(secret(0x22))), ..at(T1 - 1700, Stage::Completed) });
    assert_eq!((s, a), (OwnerState::Done(Outcome::Refunded), vec![]));
}

#[test]
fn owner_refunds_early_on_a_wrong_xmz_amount() {
    let wrong = Snapshot { xmz_lock: Some(XmzLock { amount: XMZ - 1, confirmations: 10 }), ..at(LOCK_TIME + 1800, Stage::Pending) };
    let (s, a) = owner_step(&OwnerState::Locked { swap: swap() }, &trade(), &wrong);
    assert_eq!((s, a), (OwnerState::RefundSent { swap: swap() }, vec![Action::SendRefund(swap())]));
}

#[test]
fn owner_that_missed_the_early_refund_waits_for_timeout2() {
    let locked = OwnerState::Locked { swap: swap() };
    let (s, a) = owner_step(&locked, &trade(), &at(T1 + 10, Stage::Pending));
    assert_eq!((s, a), (locked.clone(), vec![]), "between the timeouts only the claimer may act");
    let (s, a) = owner_step(&locked, &trade(), &at(T2, Stage::Pending));
    assert_eq!((s, a), (OwnerState::RefundSent { swap: swap() }, vec![Action::SendRefund(swap())]));
}

#[test]
fn owner_never_sends_ready_too_close_to_timeout1() {
    let late = Snapshot { xmz_lock: xmz(10), ..at(T1 - 1800, Stage::Pending) };
    let (_, a) = owner_step(&OwnerState::Locked { swap: swap() }, &trade(), &late);
    assert_eq!(a, vec![Action::SendRefund(swap())], "refund instead of a setReady that may land late");
}

#[test]
fn owner_sweeps_when_the_claimer_claims_without_ready() {
    let claimed = Snapshot { revealed: Some(Revealed::Claimed(secret(0x11))), ..at(T1 + 5, Stage::Completed) };
    let (s, a) = owner_step(&OwnerState::Locked { swap: swap() }, &trade(), &claimed);
    assert_eq!((s, a), (OwnerState::Sweeping { their_secret: secret(0x11) }, vec![Action::SweepXmz { their_secret: secret(0x11) }]));
}

#[test]
fn owner_whose_refund_lost_the_race_sweeps_instead() {
    let claimed = Snapshot { revealed: Some(Revealed::Claimed(secret(0x11))), ..at(T2, Stage::Completed) };
    let (s, a) = owner_step(&OwnerState::RefundSent { swap: swap() }, &trade(), &claimed);
    assert_eq!(s, OwnerState::Sweeping { their_secret: secret(0x11) });
    assert_eq!(a, vec![Action::SweepXmz { their_secret: secret(0x11) }]);
}

#[test]
fn owner_never_refunds_on_an_unread_stage() {
    let unknown = Snapshot { stage: None, ..at(T2 + 10, Stage::Pending) };
    let (s, a) = owner_step(&OwnerState::Ready { swap: swap() }, &trade(), &unknown);
    assert_eq!((s, a), (OwnerState::RefundSent { swap: swap() }, vec![]));
}

#[test]
fn owner_never_refunds_early_once_ready() {
    // setReady landed although we thought it had not: an early refund would fail and leak the secret
    let (_, a) = owner_step(&OwnerState::RefundSent { swap: swap() }, &trade(), &at(T1 - 100, Stage::Ready));
    assert_eq!(a, vec![]);
}

#[test]
fn owner_falls_back_to_locked_when_set_ready_misses_timeout1() {
    let (s, a) = owner_step(&OwnerState::ReadySent { swap: swap() }, &trade(), &at(T1 - 2000, Stage::Pending));
    assert_eq!((s, a), (OwnerState::ReadySent { swap: swap() }, vec![Action::SendSetReady(swap())]));
    let (s, a) = owner_step(&OwnerState::ReadySent { swap: swap() }, &trade(), &at(T1, Stage::Pending));
    assert_eq!((s, a), (OwnerState::Locked { swap: swap() }, vec![]));
}

#[test]
fn owner_refunds_early_when_its_own_lock_differs_from_the_terms() {
    let snap = Snapshot { evm_lock: Some(lock_with(|e| e.value -= U256::from(1))), ..at(LOCK_TIME + 60, Stage::Pending) };
    let (s, a) = owner_step(&OwnerState::LockSent, &trade(), &snap);
    assert!(matches!(s, OwnerState::RefundSent { .. }));
    assert_eq!(a.len(), 1);
    assert!(matches!(a[0], Action::SendRefund(_)));
}

// ---------- claimer ----------

#[test]
fn claimer_happy_path() {
    let tr = trade();
    let (s, a) = claimer_step(&ClaimerState::WaitingForEvmLock, &tr, &Snapshot { now: LOCK_TIME, ..Default::default() });
    assert_eq!((s.clone(), a), (ClaimerState::WaitingForEvmLock, vec![]));

    let (s, a) = claimer_step(&s, &tr, &at(LOCK_TIME + 60, Stage::Pending));
    assert_eq!((s.clone(), a), (ClaimerState::XmzLockSent { swap: swap() }, vec![Action::LockXmz { amount: XMZ }]));

    let (s, a) = claimer_step(&s, &tr, &Snapshot { xmz_lock: xmz(4), ..at(LOCK_TIME + 600, Stage::Pending) });
    assert_eq!(a, vec![Action::LockXmz { amount: XMZ }], "keep making sure the XMZ lock is broadcast");

    // Confirmed, but the owner has not called setReady and timeout1 has not passed: wait
    let (s, a) = claimer_step(&s, &tr, &Snapshot { xmz_lock: xmz(10), ..at(LOCK_TIME + 1500, Stage::Pending) });
    assert_eq!((s.clone(), a), (ClaimerState::XmzLocked { swap: swap() }, vec![]));

    let (s, a) = claimer_step(&s, &tr, &at(LOCK_TIME + 1600, Stage::Ready));
    assert_eq!((s.clone(), a), (ClaimerState::ClaimSent { swap: swap() }, vec![Action::SendClaim(swap())]));

    let (s, a) = claimer_step(&s, &tr, &Snapshot { revealed: Some(Revealed::Claimed(secret(0x11))), ..at(LOCK_TIME + 1700, Stage::Completed) });
    assert_eq!((s, a), (ClaimerState::Done(Outcome::Swapped), vec![]));
}

#[test]
fn claimer_never_locks_xmz_against_a_lock_that_differs_from_the_terms() {
    let snap = Snapshot { evm_lock: Some(lock_with(|e| e.value -= U256::from(1))), ..at(LOCK_TIME + 60, Stage::Pending) };
    let (s, a) = claimer_step(&ClaimerState::WaitingForEvmLock, &trade(), &snap);
    assert_eq!((s, a), (ClaimerState::Done(Outcome::Aborted), vec![]));
}

#[test]
fn claimer_does_not_lock_xmz_too_close_to_timeout1() {
    let (s, a) = claimer_step(&ClaimerState::WaitingForEvmLock, &trade(), &at(T1 - 3600, Stage::Pending));
    assert_eq!((s, a), (ClaimerState::Done(Outcome::Aborted), vec![]));
    let (s, _) = claimer_step(&ClaimerState::WaitingForEvmLock, &trade(), &at(T1 - 3601, Stage::Pending));
    assert!(matches!(s, ClaimerState::XmzLockSent { .. }));
}

#[test]
fn claimer_gives_up_when_no_lock_arrives_by_the_deadline() {
    let tr = trade();
    let (s, a) = claimer_step(&ClaimerState::WaitingForEvmLock, &tr, &Snapshot { now: tr.lock_deadline, ..Default::default() });
    assert_eq!((s, a), (ClaimerState::Done(Outcome::Aborted), vec![]));
}

#[test]
fn claimer_claims_after_timeout1_when_the_owner_never_sets_ready() {
    let locked = ClaimerState::XmzLocked { swap: swap() };
    let (_, a) = claimer_step(&locked, &trade(), &at(T1 - 1, Stage::Pending));
    assert_eq!(a, vec![]);
    let (s, a) = claimer_step(&locked, &trade(), &at(T1, Stage::Pending));
    assert_eq!((s, a), (ClaimerState::ClaimSent { swap: swap() }, vec![Action::SendClaim(swap())]));
}

#[test]
fn claimer_stops_claiming_near_timeout2_and_sweeps_back_after_the_refund() {
    let tr = trade();
    let (s, a) = claimer_step(&ClaimerState::ClaimSent { swap: swap() }, &tr, &at(T2 - 1801, Stage::Ready));
    assert_eq!(a, vec![Action::SendClaim(swap())]);
    let (s, a) = claimer_step(&s, &tr, &at(T2 - 1800, Stage::Ready));
    assert_eq!((s.clone(), a), (ClaimerState::WaitingForRefund { swap: swap() }, vec![]));

    let refunded = Snapshot { revealed: Some(Revealed::Refunded(secret(0x22))), ..at(T2 + 60, Stage::Completed) };
    let (s, a) = claimer_step(&s, &tr, &refunded);
    assert_eq!((s.clone(), a), (ClaimerState::SweepingBack { their_secret: secret(0x22) }, vec![Action::SweepXmz { their_secret: secret(0x22) }]));
    let (s, a) = claimer_step(&s, &tr, &Snapshot { xmz_swept: true, ..refunded });
    assert_eq!((s, a), (ClaimerState::Done(Outcome::Refunded), vec![]));
}

#[test]
fn claimer_sweeps_back_after_an_early_refund() {
    let refunded = Snapshot { revealed: Some(Revealed::Refunded(secret(0x22))), ..at(T1 - 1700, Stage::Completed) };
    for state in [ClaimerState::XmzLockSent { swap: swap() }, ClaimerState::XmzLocked { swap: swap() }] {
        let (s, a) = claimer_step(&state, &trade(), &refunded);
        assert_eq!(s, ClaimerState::SweepingBack { their_secret: secret(0x22) });
        assert_eq!(a, vec![Action::SweepXmz { their_secret: secret(0x22) }]);
    }
}

#[test]
fn claimer_whose_claim_landed_in_time_finishes_from_waiting() {
    let claimed = Snapshot { revealed: Some(Revealed::Claimed(secret(0x11))), ..at(T2 - 100, Stage::Completed) };
    let (s, _) = claimer_step(&ClaimerState::WaitingForRefund { swap: swap() }, &trade(), &claimed);
    assert_eq!(s, ClaimerState::Done(Outcome::Swapped));
}

#[test]
fn claimer_never_claims_on_an_unread_stage() {
    let unknown = Snapshot { stage: None, ..at(T1 + 60, Stage::Pending) };
    let (s, a) = claimer_step(&ClaimerState::XmzLocked { swap: swap() }, &trade(), &unknown);
    assert_eq!((s, a), (ClaimerState::XmzLocked { swap: swap() }, vec![]));
}

// ---------- persistence ----------

#[test]
fn states_survive_a_restart() {
    let owner = [OwnerState::Locked { swap: swap() }, OwnerState::Sweeping { their_secret: secret(0x11) }, OwnerState::Done(Outcome::Swapped)];
    for s in owner {
        let json = serde_json::to_string(&s).unwrap();
        assert_eq!(serde_json::from_str::<OwnerState>(&json).unwrap(), s);
    }
    let claimer = [ClaimerState::WaitingForRefund { swap: swap() }, ClaimerState::SweepingBack { their_secret: secret(0x22) }];
    for s in claimer {
        let json = serde_json::to_string(&s).unwrap();
        assert_eq!(serde_json::from_str::<ClaimerState>(&json).unwrap(), s);
    }
}
