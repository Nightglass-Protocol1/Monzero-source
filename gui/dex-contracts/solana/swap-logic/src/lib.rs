//! Core logic for the XMZ<->Solana atomic swap, factored out so it is host-testable
//! without the Solana BPF toolchain. The on-chain program is a thin wrapper that adds
//! account handling + token transfers around these checks. Mirrors the proven EVM/BTC
//! design (lock -> set_ready -> claim / refund, secret-reveal, two timelocks).
#![forbid(unsafe_code)]
use curve25519_dalek::edwards::EdwardsPoint;
use curve25519_dalek::scalar::Scalar;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage { Invalid = 0, Pending = 1, Ready = 2, Completed = 3 }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SwapError { WrongStage, InvalidSecret, TooEarly, TooLate, NotTimeToRefund, FeeTooHigh, BadTimeline, ZeroValue }

/// A secret `s` is an ed25519 scalar; its commitment is the compressed point `s*B`.
/// This is the SAME group as the XMZ side (ed25519), so no cross-curve proof is needed.
pub fn verify_secret(secret: &[u8; 32], commitment: &[u8; 32]) -> bool {
    // Reject non-canonical / zero scalars.
    let s = match Scalar::from_canonical_bytes(*secret).into_option() {
        Some(s) if s != Scalar::ZERO => s,
        _ => return false,
    };
    let point = EdwardsPoint::mul_base(&s).compress();
    point.as_bytes() == commitment
}

/// Derive the public commitment for a secret (used by the client to build a swap).
pub fn commit(secret: &[u8; 32]) -> Option<[u8; 32]> {
    let s = Scalar::from_canonical_bytes(*secret).into_option()?;
    if s == Scalar::ZERO { return None; }
    Some(EdwardsPoint::mul_base(&s).compress().to_bytes())
}

pub fn check_new(value: u64, dur1: u64, dur2: u64) -> Result<(), SwapError> {
    if value == 0 { return Err(SwapError::ZeroValue); }
    if dur1 == 0 || dur2 == 0 { return Err(SwapError::BadTimeline); }
    Ok(())
}

/// set_ready: owner-only (checked on-chain), only while PENDING and before timeout1.
pub fn check_set_ready(stage: Stage, now: u64, timeout1: u64) -> Result<(), SwapError> {
    if stage != Stage::Pending { return Err(SwapError::WrongStage); }
    if now >= timeout1 { return Err(SwapError::TooLate); }
    Ok(())
}

/// claim: claimer reveals the claim secret. Allowed while READY, or (if the owner never
/// set ready) once timeout1 passes, and never at/after timeout2.
pub fn check_claim(stage: Stage, now: u64, timeout1: u64, timeout2: u64,
    secret: &[u8; 32], claim_commitment: &[u8; 32]) -> Result<(), SwapError> {
    if stage != Stage::Pending && stage != Stage::Ready { return Err(SwapError::WrongStage); }
    if now < timeout1 && stage != Stage::Ready { return Err(SwapError::TooEarly); }
    if now >= timeout2 { return Err(SwapError::TooLate); }
    if !verify_secret(secret, claim_commitment) { return Err(SwapError::InvalidSecret); }
    Ok(())
}

/// refund: owner reveals the refund secret. Early while PENDING (before timeout1), or
/// from timeout2 on (ready or not). Never between the timeouts once READY.
pub fn check_refund(stage: Stage, now: u64, timeout1: u64, timeout2: u64,
    secret: &[u8; 32], refund_commitment: &[u8; 32]) -> Result<(), SwapError> {
    if stage != Stage::Pending && stage != Stage::Ready { return Err(SwapError::WrongStage); }
    let early = now < timeout1 && stage == Stage::Pending;
    if !early && now < timeout2 { return Err(SwapError::NotTimeToRefund); }
    if !verify_secret(secret, refund_commitment) { return Err(SwapError::InvalidSecret); }
    Ok(())
}

/// Relayed claim: the fee paid to the relayer must not exceed the swap value.
pub fn check_relay_fee(fee: u64, value: u64) -> Result<(), SwapError> {
    if fee > value { return Err(SwapError::FeeTooHigh); }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn sec(b: u8) -> [u8; 32] { let mut s = [0u8; 32]; s[0] = b; s }

    #[test] fn secret_commit_roundtrip() {
        let s = sec(42); let c = commit(&s).unwrap();
        assert!(verify_secret(&s, &c));
        assert!(!verify_secret(&sec(43), &c));
    }
    #[test] fn rejects_zero_and_noncanonical() {
        assert!(commit(&[0u8; 32]).is_none());
        assert!(!verify_secret(&[0u8; 32], &[0u8; 32]));
        assert!(!verify_secret(&[0xff; 32], &[0u8; 32])); // > group order
    }
    #[test] fn set_ready_rules() {
        assert_eq!(check_set_ready(Stage::Pending, 100, 200), Ok(()));
        assert_eq!(check_set_ready(Stage::Ready, 100, 200), Err(SwapError::WrongStage));
        assert_eq!(check_set_ready(Stage::Pending, 200, 200), Err(SwapError::TooLate));
    }
    #[test] fn claim_rules() {
        let s = sec(7); let c = commit(&s).unwrap();
        assert_eq!(check_claim(Stage::Ready, 150, 200, 400, &s, &c), Ok(()));
        assert_eq!(check_claim(Stage::Pending, 150, 200, 400, &s, &c), Err(SwapError::TooEarly));
        assert_eq!(check_claim(Stage::Pending, 250, 200, 400, &s, &c), Ok(()));
        assert_eq!(check_claim(Stage::Ready, 400, 200, 400, &s, &c), Err(SwapError::TooLate));
        assert_eq!(check_claim(Stage::Ready, 150, 200, 400, &sec(8), &c), Err(SwapError::InvalidSecret));
        assert_eq!(check_claim(Stage::Completed, 150, 200, 400, &s, &c), Err(SwapError::WrongStage));
    }
    #[test] fn refund_rules() {
        let s = sec(9); let c = commit(&s).unwrap();
        assert_eq!(check_refund(Stage::Pending, 100, 200, 400, &s, &c), Ok(()));      // early
        assert_eq!(check_refund(Stage::Ready, 300, 200, 400, &s, &c), Err(SwapError::NotTimeToRefund));
        assert_eq!(check_refund(Stage::Ready, 400, 200, 400, &s, &c), Ok(()));        // after t2
        assert_eq!(check_refund(Stage::Pending, 100, 200, 400, &sec(10), &c), Err(SwapError::InvalidSecret));
    }
    #[test] fn fee_and_new() {
        assert_eq!(check_relay_fee(5, 10), Ok(()));
        assert_eq!(check_relay_fee(11, 10), Err(SwapError::FeeTooHigh));
        assert_eq!(check_new(0, 1, 1), Err(SwapError::ZeroValue));
        assert_eq!(check_new(10, 0, 1), Err(SwapError::BadTimeline));
        assert_eq!(check_new(10, 1, 1), Ok(()));
    }
}
