use super::*;
use crate::direct::DirectTiming;
use crate::{commitment, swap_id, Chain, Swap, XmzSwap};
use alloy::primitives::{Address, U256};
use alloy::rpc::types::Log;

const CONTRACT_A: Address = Address::repeat_byte(0xa0);
const CONTRACT_B: Address = Address::repeat_byte(0xb0);
const LEADER: Address = Address::repeat_byte(0x01);
const FOLLOWER: Address = Address::repeat_byte(0x02);
// Leg A: locked at A0, 1 h then 8 h. Leg B: locked at B0 (30 min later), 1 h then 4 h.
const A0: u64 = 1_000_000;
const A1: u64 = A0 + 3600;
const A2: u64 = A1 + 8 * 3600;
const B0: u64 = A0 + 1800;
const B1: u64 = B0 + 3600;
const B2: u64 = B1 + 4 * 3600;

fn secret(byte: u8) -> B256 {
    B256::repeat_byte(byte)
}

fn claim_secret() -> B256 {
    secret(0x11)
}

fn trade() -> DirectTrade {
    let leg = |contract, owner, claimer, refund: u8, d2| AgreedTerms {
        chain: Chain::Local,
        contract,
        owner,
        claimer,
        claim_commitment: commitment(&claim_secret()).unwrap(),
        refund_commitment: commitment(&secret(refund)).unwrap(),
        asset: Address::ZERO,
        value: U256::from(10u64.pow(18)),
        duration1: 3600,
        duration2: d2,
    };
    DirectTrade {
        leg_a: leg(CONTRACT_A, LEADER, FOLLOWER, 0x22, 8 * 3600),
        leg_b: leg(CONTRACT_B, FOLLOWER, LEADER, 0x33, 4 * 3600),
        timing: DirectTiming::DEFAULT,
        lock_deadline: A0 + 2 * 3600,
    }
}

fn lock(terms: &AgreedTerms, at: u64, change: impl FnOnce(&mut XmzSwap::New)) -> ObservedLock {
    let mut e = XmzSwap::New {
        swapId: B256::ZERO,
        owner: terms.owner,
        claimer: terms.claimer,
        claimCommitment: terms.claim_commitment,
        refundCommitment: terms.refund_commitment,
        timeout1: U256::from(at + terms.duration1),
        timeout2: U256::from(at + terms.duration1 + terms.duration2),
        asset: terms.asset,
        value: terms.value,
        nonce: U256::from(7),
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
            inner: alloy::primitives::Log { address: terms.contract, data: e },
            block_hash: None,
            block_number: None,
            block_timestamp: None,
            transaction_hash: None,
            transaction_index: None,
            log_index: None,
            removed: false,
        },
        block_timestamp: at,
        stage: Stage::Pending as u8,
    }
}

fn lock_a() -> ObservedLock {
    lock(&trade().leg_a, A0, |_| {})
}

fn lock_b() -> ObservedLock {
    lock(&trade().leg_b, B0, |_| {})
}

fn swap(l: &ObservedLock) -> SwapData {
    raw(l)
}

fn view(now: u64, lock: Option<ObservedLock>, stage: Option<Stage>, revealed: Option<Revealed>) -> LegView {
    LegView { now, lock, stage, revealed }
}

fn snap(a: LegView, b: LegView) -> DirectSnapshot {
    DirectSnapshot { a, b }
}

fn nothing(now: u64) -> LegView {
    view(now, None, None, None)
}

// ---------- the honest trade, both sides ----------

#[test]
fn honest_trade_runs_to_swapped_on_both_sides() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));

    // Leader locks A
    let (l, act) = leader_step(&LeaderState::Start, &t, &snap(nothing(A0 - 60), nothing(A0 - 60)));
    assert_eq!((l.clone(), act), (LeaderState::LockSent, vec![DirectAction::NewSwap(LegId::A)]));
    // Follower waits while A is only pending (still refundable early)
    let pending = snap(view(A0 + 600, Some(lock_a()), Some(Stage::Pending), None), nothing(A0 + 600));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &pending), (FollowerState::WaitingForA, vec![]));
    // Leader sees its lock and makes it Ready
    let (l, act) = leader_step(&l, &t, &pending);
    assert_eq!((l.clone(), act), (LeaderState::ReadySent { a: a.clone() }, vec![DirectAction::SetReady(LegId::A, a.clone())]));

    // A is Ready: the follower locks B
    let ready_a = view(A0 + 1200, Some(lock_a()), Some(Stage::Ready), None);
    let (f, act) = follower_step(&FollowerState::WaitingForA, &t, &snap(ready_a.clone(), nothing(A0 + 1200)));
    assert_eq!((f.clone(), act), (FollowerState::LockSent { a: a.clone() }, vec![DirectAction::NewSwap(LegId::B)]));
    let (l, act) = leader_step(&l, &t, &snap(ready_a.clone(), nothing(A0 + 1200)));
    assert_eq!((l.clone(), act), (LeaderState::Waiting { a: a.clone() }, vec![]));

    // B confirmed but pending: follower sets it Ready; leader may not claim yet
    let pending_b = snap(ready_a.clone(), view(B0 + 600, Some(lock_b()), Some(Stage::Pending), None));
    let (f, act) = follower_step(&f, &t, &pending_b);
    assert_eq!((f.clone(), act), (FollowerState::ReadySent { a: a.clone(), b: b.clone() }, vec![DirectAction::SetReady(LegId::B, b.clone())]));
    let (l, act) = leader_step(&l, &t, &pending_b);
    assert_eq!((l.clone(), act), (LeaderState::ClaimSent { a: a.clone(), b: b.clone() }, vec![]));

    // B Ready: leader claims
    let ready_b = snap(ready_a.clone(), view(B0 + 1200, Some(lock_b()), Some(Stage::Ready), None));
    let (f, act) = follower_step(&f, &t, &ready_b);
    assert_eq!((f.clone(), act), (FollowerState::Ready { a: a.clone(), b: b.clone() }, vec![]));
    let (l, act) = leader_step(&l, &t, &ready_b);
    assert_eq!((l.clone(), act), (LeaderState::ClaimSent { a: a.clone(), b: b.clone() }, vec![DirectAction::ClaimB(b.clone())]));

    // The claim publishes the secret: the follower claims A with it
    let claimed_b = snap(ready_a, view(B0 + 1500, Some(lock_b()), Some(Stage::Completed), Some(Revealed::Claimed(claim_secret()))));
    let (l, act) = leader_step(&l, &t, &claimed_b);
    assert_eq!((l, act), (LeaderState::Done(DirectOutcome::Swapped), vec![]));
    let (f, act) = follower_step(&f, &t, &claimed_b);
    assert_eq!((f.clone(), act), (FollowerState::Claiming { a: a.clone(), secret: claim_secret() }, vec![DirectAction::ClaimA(a.clone(), claim_secret())]));

    let claimed_a = snap(view(B0 + 1800, Some(lock_a()), Some(Stage::Completed), Some(Revealed::Claimed(claim_secret()))), claimed_b.b);
    assert_eq!(follower_step(&f, &t, &claimed_a), (FollowerState::Done(DirectOutcome::Swapped), vec![]));
}

// ---------- leader ----------

#[test]
fn leader_without_a_lock_of_b_waits_for_timeout2_of_a_then_refunds() {
    let t = trade();
    let a = swap(&lock_a());
    let waiting = LeaderState::Waiting { a: a.clone() };
    for now in [A1, A1 + 3600, A2 - 1] {
        let s = snap(view(now, Some(lock_a()), Some(Stage::Ready), None), nothing(now));
        assert_eq!(leader_step(&waiting, &t, &s), (waiting.clone(), vec![]), "no refund before timeout2 at {now}");
    }
    let s = snap(view(A2, Some(lock_a()), Some(Stage::Ready), None), nothing(A2));
    assert_eq!(leader_step(&waiting, &t, &s), (LeaderState::RefundSent { a: a.clone() }, vec![DirectAction::Refund(LegId::A, a.clone())]));
    let refunded = snap(view(A2 + 60, Some(lock_a()), Some(Stage::Completed), Some(Revealed::Refunded(secret(0x22)))), nothing(A2));
    assert_eq!(leader_step(&LeaderState::RefundSent { a }, &t, &refunded), (LeaderState::Done(DirectOutcome::Refunded), vec![]));
}

#[test]
fn leader_never_claims_against_a_lock_of_b_that_differs_from_the_terms() {
    let t = trade();
    let a = swap(&lock_a());
    let ready_a = view(B0 + 1200, Some(lock_a()), Some(Stage::Ready), None);
    let bad_locks = [
        lock(&t.leg_b, B0, |e| e.value = U256::from(1)),
        lock(&t.leg_b, B0, |e| e.claimer = FOLLOWER),
        lock(&t.leg_b, B0, |e| e.claimCommitment = commitment(&secret(0x44)).unwrap()),
        lock(&t.leg_b, B0, |e| e.timeout2 += U256::from(1)),
    ];
    for bad in bad_locks {
        let s = snap(ready_a.clone(), view(B0 + 1200, Some(bad), Some(Stage::Ready), None));
        assert_eq!(leader_step(&LeaderState::Waiting { a: a.clone() }, &t, &s), (LeaderState::Waiting { a: a.clone() }, vec![]));
    }
}

#[test]
fn leader_does_not_claim_close_to_timeout2_of_b_and_waits_for_its_own_refund() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));
    let late = B2 - t.timing.claim_margin;
    let s = snap(view(late, Some(lock_a()), Some(Stage::Ready), None), view(late, Some(lock_b()), Some(Stage::Ready), None));
    let (state, act) = leader_step(&LeaderState::ClaimSent { a: a.clone(), b }, &t, &s);
    assert_eq!((state.clone(), act), (LeaderState::GaveUp { a: a.clone() }, vec![]));
    // A claim sent earlier may still land: then the trade happened
    let landed = snap(view(late, Some(lock_a()), Some(Stage::Ready), None), view(late, Some(lock_b()), Some(Stage::Completed), Some(Revealed::Claimed(claim_secret()))));
    assert_eq!(leader_step(&state, &t, &landed).0, LeaderState::Done(DirectOutcome::Swapped));
    // Otherwise A comes back from its second time limit only
    let s = snap(view(A2 - 1, Some(lock_a()), Some(Stage::Ready), None), nothing(A2));
    assert_eq!(leader_step(&state, &t, &s), (state.clone(), vec![]));
    let s = snap(view(A2, Some(lock_a()), Some(Stage::Ready), None), nothing(A2));
    assert_eq!(leader_step(&state, &t, &s).1, vec![DirectAction::Refund(LegId::A, a)]);
}

#[test]
fn leader_refunds_its_own_mistaken_lock_early() {
    let t = trade();
    let wrong = lock(&t.leg_a, A0, |e| e.value = U256::from(5));
    let s = snap(view(A0 + 600, Some(wrong.clone()), Some(Stage::Pending), None), nothing(A0 + 600));
    let (state, act) = leader_step(&LeaderState::LockSent, &t, &s);
    assert_eq!(act, vec![DirectAction::Refund(LegId::A, swap(&wrong))]);
    assert!(matches!(state, LeaderState::RefundSent { .. }));
}

#[test]
fn leader_with_setready_missed_still_moves_on_after_timeout1() {
    let t = trade();
    let a = swap(&lock_a());
    let s = snap(view(A1, Some(lock_a()), Some(Stage::Pending), None), nothing(A1));
    assert_eq!(leader_step(&LeaderState::ReadySent { a: a.clone() }, &t, &s), (LeaderState::Waiting { a }, vec![]));
}

// ---------- follower ----------

#[test]
fn follower_never_locks_against_a_lock_of_a_that_differs_from_the_terms() {
    let t = trade();
    for bad in [
        lock(&t.leg_a, A0, |e| e.value = U256::from(1)),
        lock(&t.leg_a, A0, |e| e.claimer = LEADER),
        lock(&t.leg_a, A0, |e| e.claimCommitment = commitment(&secret(0x44)).unwrap()),
        lock(&t.leg_a, A0, |e| e.timeout1 += U256::from(1)),
    ] {
        let s = snap(view(A0 + 1200, Some(bad), Some(Stage::Ready), None), nothing(A0 + 1200));
        assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s), (FollowerState::Done(DirectOutcome::Aborted), vec![]));
    }
}

#[test]
fn follower_waits_while_a_could_still_be_refunded_early_and_gives_up_at_the_deadline() {
    let t = trade();
    // Pending past timeout1, but not yet by the hour's margin: an early refund could still be on its way
    let s = snap(view(A1 + 60, Some(lock_a()), Some(Stage::Pending), None), nothing(A1));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s).0, FollowerState::WaitingForA);
    let s = snap(view(t.lock_deadline, Some(lock_a()), Some(Stage::Pending), None), nothing(t.lock_deadline));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s).0, FollowerState::Done(DirectOutcome::Aborted));
    let s = snap(nothing(t.lock_deadline), nothing(t.lock_deadline));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s).0, FollowerState::Done(DirectOutcome::Aborted));
}

#[test]
fn follower_does_not_lock_after_seeing_a_refunded() {
    let t = trade();
    let s = snap(view(A0 + 1200, Some(lock_a()), Some(Stage::Pending), Some(Revealed::Refunded(secret(0x22)))), nothing(A0 + 1200));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s), (FollowerState::Done(DirectOutcome::Aborted), vec![]));
    let s = snap(view(A0 + 1200, Some(lock_a()), Some(Stage::Completed), None), nothing(A0 + 1200));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s), (FollowerState::Done(DirectOutcome::Aborted), vec![]));
}

#[test]
fn follower_does_not_lock_when_b_would_close_too_close_to_a() {
    let mut t = trade();
    t.lock_deadline = A2;
    // Leg B spans 5 h, the reserve 2 h and the clock allowance 10 min: past A2 - 7h10m it is too late
    let latest = A2 - (5 * 3600 + 7200 + 600);
    let s = snap(view(latest, Some(lock_a()), Some(Stage::Ready), None), nothing(latest));
    assert!(matches!(follower_step(&FollowerState::WaitingForA, &t, &s).0, FollowerState::LockSent { .. }));
    let s = snap(view(latest + 1, Some(lock_a()), Some(Stage::Ready), None), nothing(latest + 1));
    assert_eq!(follower_step(&FollowerState::WaitingForA, &t, &s), (FollowerState::Done(DirectOutcome::Aborted), vec![]));
}

#[test]
fn follower_takes_b_back_early_if_a_was_refunded_after_all() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));
    let s = snap(
        view(B0 + 600, Some(lock_a()), Some(Stage::Completed), Some(Revealed::Refunded(secret(0x22)))),
        view(B0 + 600, Some(lock_b()), Some(Stage::Pending), None),
    );
    let (state, act) = follower_step(&FollowerState::Locked { a: a.clone(), b: b.clone() }, &t, &s);
    assert_eq!((state, act), (FollowerState::RefundSent { a, b: b.clone() }, vec![DirectAction::Refund(LegId::B, b)]));
}

#[test]
fn follower_refunds_its_own_mistaken_lock() {
    let t = trade();
    let a = swap(&lock_a());
    let wrong = lock(&t.leg_b, B0, |e| e.claimer = FOLLOWER);
    let s = snap(view(B0 + 600, Some(lock_a()), Some(Stage::Ready), None), view(B0 + 600, Some(wrong.clone()), Some(Stage::Pending), None));
    let (state, act) = follower_step(&FollowerState::LockSent { a: a.clone() }, &t, &s);
    assert_eq!((state, act), (FollowerState::RefundSent { a, b: swap(&wrong) }, vec![DirectAction::Refund(LegId::B, swap(&wrong))]));
}

#[test]
fn follower_without_a_claim_refunds_b_from_its_timeout2_only() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));
    let ready = FollowerState::Ready { a: a.clone(), b: b.clone() };
    let s = snap(view(B2 - 1, Some(lock_a()), Some(Stage::Ready), None), view(B2 - 1, Some(lock_b()), Some(Stage::Ready), None));
    assert_eq!(follower_step(&ready, &t, &s), (ready.clone(), vec![]));
    let s = snap(view(B2, Some(lock_a()), Some(Stage::Ready), None), view(B2, Some(lock_b()), Some(Stage::Ready), None));
    let (state, act) = follower_step(&ready, &t, &s);
    assert_eq!((state.clone(), act), (FollowerState::RefundSent { a: a.clone(), b: b.clone() }, vec![DirectAction::Refund(LegId::B, b.clone())]));
    let done = snap(s.a.clone(), view(B2 + 60, Some(lock_b()), Some(Stage::Completed), Some(Revealed::Refunded(secret(0x33)))));
    assert_eq!(follower_step(&state, &t, &done), (FollowerState::Done(DirectOutcome::Refunded), vec![]));
}

#[test]
fn a_claim_of_b_that_lands_while_the_follower_refunds_still_lets_it_claim_a() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));
    let s = snap(
        view(B2 + 30, Some(lock_a()), Some(Stage::Ready), None),
        view(B2 + 30, Some(lock_b()), Some(Stage::Completed), Some(Revealed::Claimed(claim_secret()))),
    );
    let (state, act) = follower_step(&FollowerState::RefundSent { a: a.clone(), b }, &t, &s);
    assert_eq!((state, act), (FollowerState::Claiming { a: a.clone(), secret: claim_secret() }, vec![DirectAction::ClaimA(a, claim_secret())]));
}

#[test]
fn follower_claims_a_until_its_timeout2_and_reports_a_miss_only_on_a_refund_of_a() {
    let t = trade();
    let a = swap(&lock_a());
    let claiming = FollowerState::Claiming { a: a.clone(), secret: claim_secret() };
    let b = view(A2, None, Some(Stage::Completed), Some(Revealed::Claimed(claim_secret())));
    // Just before timeout2: still claims (no margin: the secret is public anyway)
    let s = snap(view(A2 - 1, Some(lock_a()), Some(Stage::Ready), None), b.clone());
    assert_eq!(follower_step(&claiming, &t, &s).1, vec![DirectAction::ClaimA(a.clone(), claim_secret())]);
    // From timeout2 the contract refuses claims: nothing is sent, and nothing is concluded yet
    let s = snap(view(A2, Some(lock_a()), Some(Stage::Ready), None), b.clone());
    assert_eq!(follower_step(&claiming, &t, &s), (claiming.clone(), vec![]));
    let s = snap(view(A2 + 60, Some(lock_a()), Some(Stage::Completed), Some(Revealed::Refunded(secret(0x22)))), b);
    assert_eq!(follower_step(&claiming, &t, &s), (FollowerState::Done(DirectOutcome::Missed), vec![]));
}

/// Every reachable situation: the leader never gets both coins, whatever the follower sees.
/// The leader claims B only when the follower then has time left to claim A.
#[test]
fn a_leader_claim_always_leaves_the_follower_a_window_on_a() {
    let t = trade();
    let (a, b) = (swap(&lock_a()), swap(&lock_b()));
    for now in (B0..A2).step_by(300) {
        let s = snap(view(now, Some(lock_a()), Some(Stage::Ready), None), view(now, Some(lock_b()), Some(Stage::Ready), None));
        let (_, act) = leader_step(&LeaderState::Waiting { a: a.clone() }, &t, &s);
        if act.contains(&DirectAction::ClaimB(b.clone())) {
            let left = timeouts(&a).1 - now;
            assert!(left >= t.timing.follower_reserve + t.timing.claim_margin, "claim at {now} leaves {left}s");
        }
    }
}

#[test]
fn leader_does_not_lock_when_the_follower_would_give_up_before_a_is_safe() {
    let t = trade();
    let late = t.lock_deadline - crate::direct_listing::LEADER_LOCK_BUDGET + 1;
    assert_eq!(leader_step(&LeaderState::Start, &t, &snap(nothing(late), nothing(late))), (LeaderState::Done(DirectOutcome::Aborted), vec![]));
    let in_time = late - 1;
    assert_eq!(leader_step(&LeaderState::Start, &t, &snap(nothing(in_time), nothing(in_time))).1, vec![DirectAction::NewSwap(LegId::A)]);
}
