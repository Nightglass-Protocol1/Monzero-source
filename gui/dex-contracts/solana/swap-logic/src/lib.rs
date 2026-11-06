//! Core logic for the XMZ<->Solana atomic swap, factored out so it is host-testable
//! without the Solana BPF toolchain. The on-chain program is a thin wrapper that adds
//! account handling + token transfers around these checks, and does the ed25519 secret
//! verification with Solana's curve25519 syscall (dalek's basepoint tables overflow the
//! BPF 4KB stack). So the stage/timelock checks here take an already-computed `secret_ok`
//! bool; `verify_secret`/`commit` below are the host/client reference for producing it.
//! Mirrors the proven EVM/BTC design (lock -> set_ready -> claim / refund, two timelocks).
#![forbid(unsafe_code)]

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Stage { Invalid = 0, Pending = 1, Ready = 2, Completed = 3 }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SwapError { WrongStage, InvalidSecret, TooEarly, TooLate, NotTimeToRefund, FeeTooHigh, BadTimeline, ZeroValue }

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

/// claim: allowed while READY, or (if the owner never set ready) once timeout1 passes, and
/// never at/after timeout2. `secret_ok` = the claim secret verified against its commitment.
pub fn check_claim(stage: Stage, now: u64, timeout1: u64, timeout2: u64, secret_ok: bool) -> Result<(), SwapError> {
    if stage != Stage::Pending && stage != Stage::Ready { return Err(SwapError::WrongStage); }
    if now < timeout1 && stage != Stage::Ready { return Err(SwapError::TooEarly); }
    if now >= timeout2 { return Err(SwapError::TooLate); }
    if !secret_ok { return Err(SwapError::InvalidSecret); }
    Ok(())
}

/// refund: owner reveals the refund secret. Early while PENDING (before timeout1), or from
/// timeout2 on. `secret_ok` = the refund secret verified against its commitment.
pub fn check_refund(stage: Stage, now: u64, timeout1: u64, timeout2: u64, secret_ok: bool) -> Result<(), SwapError> {
    if stage != Stage::Pending && stage != Stage::Ready { return Err(SwapError::WrongStage); }
    let early = now < timeout1 && stage == Stage::Pending;
    if !early && now < timeout2 { return Err(SwapError::NotTimeToRefund); }
    if !secret_ok { return Err(SwapError::InvalidSecret); }
    Ok(())
}

/// Relayed claim: the fee paid to the relayer must not exceed the swap value.
pub fn check_relay_fee(fee: u64, value: u64) -> Result<(), SwapError> {
    if fee > value { return Err(SwapError::FeeTooHigh); }
    Ok(())
}

// --- Host/client reference ed25519 commitment (NOT compiled for the Solana target) ------
// A secret `s` is an ed25519 scalar; its commitment is the compressed point `s*B`, the
// SAME group as the XMZ side. On-chain the program computes the equivalent with the
// curve25519 syscall; both must agree (checked by the LiteSVM integration tests).
#[cfg(not(target_os = "solana"))]
mod reference {
    use curve25519_dalek::edwards::EdwardsPoint;
    use curve25519_dalek::scalar::Scalar;

    pub fn verify_secret(secret: &[u8; 32], commitment: &[u8; 32]) -> bool {
        let s = match Scalar::from_canonical_bytes(*secret).into_option() {
            Some(s) if s != Scalar::ZERO => s,
            _ => return false,
        };
        EdwardsPoint::mul_base(&s).compress().as_bytes() == commitment
    }
    pub fn commit(secret: &[u8; 32]) -> Option<[u8; 32]> {
        let s = Scalar::from_canonical_bytes(*secret).into_option()?;
        if s == Scalar::ZERO { return None; }
        Some(EdwardsPoint::mul_base(&s).compress().to_bytes())
    }
    /// Combine two ed25519 key shares into the full Monero spend scalar `s = s_a + s_b`.
    /// The counterpart of the point sum `S_a + S_b`: once Bob learns the revealed `s_a` he
    /// combines it with his `s_b` here to sweep the XMZ 2-of-2.
    pub fn combine_shares(a: &[u8; 32], b: &[u8; 32]) -> Option<[u8; 32]> {
        let sa = Scalar::from_canonical_bytes(*a).into_option()?;
        let sb = Scalar::from_canonical_bytes(*b).into_option()?;
        Some((sa + sb).to_bytes())
    }
    /// The compressed ed25519 basepoint — the point the on-chain syscall multiplies by.
    pub const BASEPOINT_COMPRESSED: [u8; 32] = {
        let mut b = [0x66u8; 32]; b[0] = 0x58; b
    };
}
#[cfg(not(target_os = "solana"))]
pub use reference::{combine_shares, commit, verify_secret, BASEPOINT_COMPRESSED};

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
        assert!(!verify_secret(&[0xff; 32], &[0u8; 32]));
    }
    #[test] fn basepoint_matches_dalek() {
        // commit(1) must equal the basepoint we feed the on-chain syscall.
        assert_eq!(commit(&sec(1)).unwrap(), BASEPOINT_COMPRESSED);
    }
    #[test] fn combine_shares_is_point_additive() {
        // (s_a + s_b)*B == S_a + S_b : combining the private shares matches summing the
        // public shares, which is why revealing s_a lets Bob reconstruct the spend key.
        use curve25519_dalek::edwards::CompressedEdwardsY;
        let (a, b) = (sec(5), sec(11));
        let s = combine_shares(&a, &b).unwrap();
        let pa = CompressedEdwardsY(commit(&a).unwrap()).decompress().unwrap();
        let pb = CompressedEdwardsY(commit(&b).unwrap()).decompress().unwrap();
        assert_eq!(commit(&s).unwrap(), (pa + pb).compress().to_bytes());
        assert!(combine_shares(&[0xff; 32], &b).is_none(), "rejects a non-canonical share");
    }
    #[test] fn set_ready_rules() {
        assert_eq!(check_set_ready(Stage::Pending, 100, 200), Ok(()));
        assert_eq!(check_set_ready(Stage::Ready, 100, 200), Err(SwapError::WrongStage));
        assert_eq!(check_set_ready(Stage::Pending, 200, 200), Err(SwapError::TooLate));
    }
    #[test] fn claim_rules() {
        assert_eq!(check_claim(Stage::Ready, 150, 200, 400, true), Ok(()));
        assert_eq!(check_claim(Stage::Pending, 150, 200, 400, true), Err(SwapError::TooEarly));
        assert_eq!(check_claim(Stage::Pending, 250, 200, 400, true), Ok(()));
        assert_eq!(check_claim(Stage::Ready, 400, 200, 400, true), Err(SwapError::TooLate));
        assert_eq!(check_claim(Stage::Ready, 150, 200, 400, false), Err(SwapError::InvalidSecret));
        assert_eq!(check_claim(Stage::Completed, 150, 200, 400, true), Err(SwapError::WrongStage));
    }
    #[test] fn refund_rules() {
        assert_eq!(check_refund(Stage::Pending, 100, 200, 400, true), Ok(()));
        assert_eq!(check_refund(Stage::Ready, 300, 200, 400, true), Err(SwapError::NotTimeToRefund));
        assert_eq!(check_refund(Stage::Ready, 400, 200, 400, true), Ok(()));
        assert_eq!(check_refund(Stage::Pending, 100, 200, 400, false), Err(SwapError::InvalidSecret));
    }
    #[test] fn fee_and_new() {
        assert_eq!(check_relay_fee(5, 10), Ok(()));
        assert_eq!(check_relay_fee(11, 10), Err(SwapError::FeeTooHigh));
        assert_eq!(check_new(0, 1, 1), Err(SwapError::ZeroValue));
        assert_eq!(check_new(10, 0, 1), Err(SwapError::BadTimeline));
        assert_eq!(check_new(10, 1, 1), Ok(()));
    }
}
