//! Driving the coordinator to completion.
//!
//! The [`Coordinator`] is pure state; a `LegDriver` is the thing that actually performs the
//! on-chain work and observes the two legs. The real driver wires:
//!   * `Action::PublishReady`  -> `SolSwapClient.setReady(...)`
//!   * `Action::PublishRefund` -> `SolSwapClient.refund(...)` (reveals `s_b`)
//!   * `Action::RedeemXmr`     -> read `s_a` from Alice's Solana claim (see `crate::secret`)
//!                                then sweep the XMZ 2-of-2 via the eigenwallet engine
//! and feeds back observed `Event`s (lock confirmations, the XMZ lock, Alice's claim, the
//! timelocks). This module gives the trait + the drive loop + a mock, so the whole
//! orchestration is testable without the real chains. See `../DEX_SOLANA_ORCHESTRATION.md`.
use crate::{Action, Coordinator, Event, State, TransitionError};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Outcome {
    /// XMZ swept — the swap succeeded.
    Completed,
    /// The Solana asset was refunded (and Alice can reclaim XMZ).
    Refunded,
    /// Aborted before any funds were locked.
    Aborted,
}

#[derive(Debug, PartialEq, Eq)]
pub enum DriveError {
    /// The driver failed to perform an action or observe an event.
    Driver(String),
    /// An observed event was invalid for the current state (protocol violation / bad maker).
    Transition(TransitionError),
}

/// What the coordinator drives. A real implementation talks to `SolSwapClient` and the
/// eigenwallet Monero engine; a test implementation scripts events.
pub trait LegDriver {
    /// Perform the on-chain action the coordinator requires on entering its current state
    /// (`Action::None` is a no-op — just wait).
    fn act(&mut self, action: Action) -> Result<(), String>;
    /// Observe and return the next event from the two legs. A real driver blocks here
    /// (watching confirmations, the XMZ lock, Alice's claim, the timelocks).
    fn next_event(&mut self) -> Result<Event, String>;
}

/// Drive a swap from `Start` to a terminal state, performing each state's action and
/// applying each observed event in order.
pub fn drive<D: LegDriver>(driver: &mut D) -> Result<Outcome, DriveError> {
    let mut c = Coordinator::new();
    loop {
        driver.act(c.state.action()).map_err(DriveError::Driver)?;
        match c.state {
            State::Done => return Ok(Outcome::Completed),
            State::Refunded => return Ok(Outcome::Refunded),
            State::Aborted => return Ok(Outcome::Aborted),
            _ => {}
        }
        let ev = driver.next_event().map_err(DriveError::Driver)?;
        c.handle(ev).map_err(DriveError::Transition)?;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Event::*;

    /// Scripts a fixed event sequence and records every action the coordinator requested.
    struct MockDriver {
        events: std::vec::IntoIter<Event>,
        pub actions: Vec<Action>,
    }
    impl MockDriver {
        fn new(events: Vec<Event>) -> Self {
            Self { events: events.into_iter(), actions: Vec::new() }
        }
    }
    impl LegDriver for MockDriver {
        fn act(&mut self, a: Action) -> Result<(), String> {
            self.actions.push(a);
            Ok(())
        }
        fn next_event(&mut self) -> Result<Event, String> {
            self.events.next().ok_or_else(|| "mock ran out of events".to_string())
        }
    }

    #[test]
    fn drives_a_full_successful_swap() {
        let mut d = MockDriver::new(vec![
            SolLockPublished, SolLockConfirmed, XmrLockSeen, XmrLockConfirmed,
            ReadyPublished, AliceClaimedSol, XmrRedeemed,
        ]);
        assert_eq!(drive(&mut d).unwrap(), Outcome::Completed);
        // the coordinator told the driver to set_ready (once XMZ confirmed) and to sweep XMZ
        // (once Alice's claim revealed s_a), and never to refund.
        assert!(d.actions.contains(&Action::PublishReady));
        assert!(d.actions.contains(&Action::RedeemXmr));
        assert!(!d.actions.contains(&Action::PublishRefund));
        // ready is requested only after the XMZ lock is confirmed, never before.
        let ready_at = d.actions.iter().position(|a| *a == Action::PublishReady).unwrap();
        let redeem_at = d.actions.iter().position(|a| *a == Action::RedeemXmr).unwrap();
        assert!(ready_at < redeem_at, "set_ready precedes the XMZ sweep");
    }

    #[test]
    fn drives_a_timeout_refund() {
        let mut d = MockDriver::new(vec![
            SolLockPublished, SolLockConfirmed, XmrLockSeen, XmrLockConfirmed,
            ReadyPublished, Timeout1Expired, RefundPublished, RefundConfirmed,
        ]);
        assert_eq!(drive(&mut d).unwrap(), Outcome::Refunded);
        // committed refund is coordinator-driven (via CancelReady -> PublishRefund)
        assert!(d.actions.contains(&Action::PublishRefund));
        assert!(!d.actions.contains(&Action::RedeemXmr));
    }

    #[test]
    fn drives_an_early_cancel() {
        // Before Ready, Bob bails: the driver publishes the refund itself (no coordinator
        // PublishRefund action on the early path).
        let mut d = MockDriver::new(vec![
            SolLockPublished, SolLockConfirmed, RefundPublished, RefundConfirmed,
        ]);
        assert_eq!(drive(&mut d).unwrap(), Outcome::Refunded);
        assert!(!d.actions.contains(&Action::PublishReady));
    }

    #[test]
    fn rejects_a_protocol_violating_event() {
        // A maker that signals XmrRedeemed before Alice ever claimed is rejected.
        let mut d = MockDriver::new(vec![
            SolLockPublished, SolLockConfirmed, XmrLockSeen, XmrLockConfirmed,
            ReadyPublished, XmrRedeemed,
        ]);
        match drive(&mut d) {
            Err(DriveError::Transition(_)) => {}
            other => panic!("expected a transition error, got {other:?}"),
        }
    }
}
