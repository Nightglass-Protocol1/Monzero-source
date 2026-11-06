//! XMZ <-> Solana swap coordinator — the taker (Bob) side.
//!
//! Bob locks the Solana asset (SOL/USDC/USDT) and wants XMZ; Alice (the maker) locks XMZ
//! and claims the Solana asset. This is the control-flow skeleton that binds the two legs,
//! enforcing the ordering that keeps the swap safe. It is **pure logic** (no chain deps):
//! the real driver feeds it observed `Event`s and performs the chain `Action`s it is told
//! to take. See `../DEX_SOLANA_ORCHESTRATION.md` for the protocol and the secret binding
//! (`claim_commitment = S_a`, `refund_commitment = S_b`; both ed25519 Monero key shares).
//!
//! Safety invariants this machine encodes:
//!  * Bob never calls `set_ready` on the Solana swap until the XMZ lock is **confirmed**
//!    (before that he can still cancel freely).
//!  * Once ready, Bob is committed: Alice may claim, and Bob can only refund after the
//!    Solana `timeout2` has elapsed — never in between.
//!  * Bob only sweeps XMZ **after** observing Alice's Solana claim (which reveals `s_a`).
#![forbid(unsafe_code)]

pub mod driver;
pub mod secret;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    /// Keys + commitments agreed; nothing locked yet.
    Start,
    /// Bob published the Solana lock (New/NewSpl); awaiting confirmation.
    SolLockPublished,
    /// Solana lock confirmed on-chain (Pending on the program).
    SolLocked,
    /// Alice's XMZ lock seen but not yet fully confirmed.
    XmrLockSeen,
    /// XMZ lock confirmed (right amount + confirmations). Safe to let Alice claim.
    XmrLocked,
    /// Bob called `set_ready`; Alice may now claim the Solana asset.
    Ready,
    /// Alice claimed the Solana asset, revealing `s_a` (observed on-chain).
    SolClaimed,
    /// `timeout1` passed in `Ready` with no claim; Bob may now refund after `timeout2`.
    CancelReady,
    /// Bob published the Solana refund (reveals `s_b`).
    RefundPublished,
    // --- terminal ---
    /// Bob swept XMZ with `s_a + s_b`. Success.
    Done,
    /// Solana refund confirmed; the asset is back with Bob and Alice can reclaim XMZ.
    Refunded,
    /// Aborted before anything was locked.
    Aborted,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Event {
    SolLockPublished,
    SolLockConfirmed,
    XmrLockSeen,
    XmrLockConfirmed,
    ReadyPublished,
    /// Alice's Solana claim observed; `s_a` is now known to Bob.
    AliceClaimedSol,
    /// Bob's XMZ sweep confirmed.
    XmrRedeemed,
    /// Solana `timeout1` reached (only meaningful once `Ready`).
    Timeout1Expired,
    /// Bob published the Solana refund.
    RefundPublished,
    /// Solana refund confirmed.
    RefundConfirmed,
    /// Bob abandons the swap (only valid before he is committed).
    Abort,
}

/// The chain action the driver should take on entering a state (advisory; the driver maps
/// it to `SolSwapClient` / the Monero engine). `None` means "just wait for the next event".
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    None,
    /// Call `set_ready` on the Solana swap.
    PublishReady,
    /// Sweep the XMZ 2-of-2 using `s_a + s_b`.
    RedeemXmr,
    /// Publish the Solana refund (reveals `s_b`).
    PublishRefund,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransitionError {
    /// This event is not valid in this state.
    Invalid,
    /// The swap is already in a terminal state.
    Terminal,
}

impl State {
    pub fn is_terminal(self) -> bool {
        matches!(self, State::Done | State::Refunded | State::Aborted)
    }

    /// Whether Bob is "committed" — past the point where he can cancel freely. Once
    /// committed, a refund is only allowed after `timeout2` (via `CancelReady`).
    pub fn is_committed(self) -> bool {
        matches!(self, State::Ready | State::SolClaimed | State::CancelReady | State::RefundPublished)
    }

    /// Advance on an observed event. Returns the next state, or why the event is rejected.
    /// (State and Event deliberately share some names, so everything is fully qualified.)
    pub fn on(self, event: Event) -> Result<State, TransitionError> {
        use Event as E;
        use State as S;
        if self.is_terminal() {
            return Err(TransitionError::Terminal);
        }
        let next = match (self, event) {
            // --- happy path ---
            (S::Start, E::SolLockPublished) => S::SolLockPublished,
            (S::SolLockPublished, E::SolLockConfirmed) => S::SolLocked,
            (S::SolLocked, E::XmrLockSeen) => S::XmrLockSeen,
            (S::XmrLockSeen, E::XmrLockConfirmed) => S::XmrLocked,
            (S::XmrLocked, E::ReadyPublished) => S::Ready,
            (S::Ready, E::AliceClaimedSol) => S::SolClaimed,
            (S::SolClaimed, E::XmrRedeemed) => S::Done,

            // --- abort before any lock ---
            (S::Start, E::Abort) => S::Aborted,

            // --- early cancel (still Pending, not yet Ready): Bob may refund freely ---
            (S::SolLocked | S::XmrLockSeen | S::XmrLocked, E::RefundPublished) => S::RefundPublished,

            // --- timeout cancel (committed: only after timeout1 -> refund after timeout2) ---
            (S::Ready, E::Timeout1Expired) => S::CancelReady,
            (S::CancelReady, E::RefundPublished) => S::RefundPublished,

            // --- refund settles ---
            (S::RefundPublished, E::RefundConfirmed) => S::Refunded,

            _ => return Err(TransitionError::Invalid),
        };
        Ok(next)
    }

    /// The action the driver should take on entering this state.
    pub fn action(self) -> Action {
        match self {
            State::XmrLocked => Action::PublishReady,
            State::SolClaimed => Action::RedeemXmr,
            State::CancelReady => Action::PublishRefund,
            _ => Action::None,
        }
    }
}

/// A running coordinator: current state + a convenience driver loop over observed events.
#[derive(Clone, Copy, Debug)]
pub struct Coordinator {
    pub state: State,
}
impl Default for Coordinator {
    fn default() -> Self { Self { state: State::Start } }
}
impl Coordinator {
    pub fn new() -> Self { Self::default() }
    /// Apply an event; on success returns the action to take next.
    pub fn handle(&mut self, event: Event) -> Result<Action, TransitionError> {
        self.state = self.state.on(event)?;
        Ok(self.state.action())
    }
}

#[cfg(test)]
mod tests {
    use super::{Action, Coordinator, Event::*, State, TransitionError::*};

    #[test]
    fn happy_path_sol_claimed_then_xmr_redeemed() {
        let mut c = Coordinator::new();
        assert_eq!(c.handle(SolLockPublished).unwrap(), Action::None);
        assert_eq!(c.handle(SolLockConfirmed).unwrap(), Action::None);
        assert_eq!(c.handle(XmrLockSeen).unwrap(), Action::None);
        // confirming the XMZ lock tells Bob to publish set_ready
        assert_eq!(c.handle(XmrLockConfirmed).unwrap(), Action::PublishReady);
        assert_eq!(c.handle(ReadyPublished).unwrap(), Action::None);
        // Alice's claim reveals s_a -> Bob should redeem XMZ
        assert_eq!(c.handle(AliceClaimedSol).unwrap(), Action::RedeemXmr);
        assert_eq!(c.handle(XmrRedeemed).unwrap(), Action::None);
        assert_eq!(c.state, State::Done);
        assert!(c.state.is_terminal());
    }

    #[test]
    fn never_ready_before_xmr_confirmed() {
        // Bob must not set_ready until XMZ is confirmed; action is None until XmrLocked.
        let mut c = Coordinator::new();
        c.handle(SolLockPublished).unwrap();
        c.handle(SolLockConfirmed).unwrap();
        assert_eq!(c.state.action(), Action::None); // SolLocked: wait
        c.handle(XmrLockSeen).unwrap();
        assert_eq!(c.state.action(), Action::None); // seen but not confirmed: wait
        assert_eq!(c.handle(XmrLockConfirmed).unwrap(), Action::PublishReady);
    }

    #[test]
    fn early_cancel_while_pending() {
        // Before Ready, Bob can refund freely (SOL program's early-cancel path).
        let mut c = Coordinator::new();
        c.handle(SolLockPublished).unwrap();
        c.handle(SolLockConfirmed).unwrap();
        assert!(!c.state.is_committed());
        assert_eq!(c.handle(RefundPublished).unwrap(), Action::None);
        assert_eq!(c.state, State::RefundPublished);
        assert_eq!(c.handle(RefundConfirmed).unwrap(), Action::None);
        assert_eq!(c.state, State::Refunded);
    }

    #[test]
    fn committed_refund_only_after_timeout() {
        // Once Ready, a bare refund is rejected; must go through timeout1 -> CancelReady.
        let mut c = Coordinator::new();
        for e in [SolLockPublished, SolLockConfirmed, XmrLockSeen, XmrLockConfirmed, ReadyPublished] {
            c.handle(e).unwrap();
        }
        assert!(c.state.is_committed());
        assert_eq!(c.state.on(RefundPublished), Err(Invalid), "cannot refund a ready swap early");
        assert_eq!(c.handle(Timeout1Expired).unwrap(), Action::PublishRefund);
        assert_eq!(c.state, State::CancelReady);
        c.handle(RefundPublished).unwrap();
        c.handle(RefundConfirmed).unwrap();
        assert_eq!(c.state, State::Refunded);
    }

    #[test]
    fn cannot_skip_steps() {
        let c = State::Ready;
        assert_eq!(c.on(XmrRedeemed), Err(Invalid), "no redeem before Alice claims");
        assert_eq!(State::SolLocked.on(ReadyPublished), Err(Invalid), "no ready before XMZ confirmed");
        assert_eq!(State::Start.on(SolLockConfirmed), Err(Invalid), "no confirm before publish");
    }

    #[test]
    fn terminal_states_reject_events() {
        assert_eq!(State::Done.on(RefundPublished), Err(Terminal));
        assert_eq!(State::Refunded.on(AliceClaimedSol), Err(Terminal));
        assert_eq!(State::Aborted.on(SolLockPublished), Err(Terminal));
    }

    #[test]
    fn abort_only_before_locking() {
        assert_eq!(State::Start.on(Abort), Ok(State::Aborted));
        assert_eq!(State::SolLocked.on(Abort), Err(Invalid), "cannot abort once funds are locked");
    }
}
