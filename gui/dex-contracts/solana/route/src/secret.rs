//! The secret binding — the crux of XMZ<->Solana atomicity.
//!
//! Alice's Solana claim reveals `s_a` on-chain (it is in the claim instruction data, and
//! the program also logs it). Because Solana and Monero are both ed25519, `s_a` *is* Alice's
//! Monero key share, so reading it from the claim lets Bob reconstruct `s = s_a + s_b` and
//! sweep the XMZ. Symmetrically, Bob's refund reveals `s_b` for Alice to reclaim XMZ.
//!
//! These helpers extract the revealed secret from a settling instruction and verify it
//! against the commitment the swap was set up with (`claim_commitment = S_a`,
//! `refund_commitment = S_b`). See `../DEX_SOLANA_ORCHESTRATION.md`.
use monzero_sol_swap_logic as logic;

/// Instruction opcodes that reveal a secret, with the 32-byte secret right after the opcode:
/// 2=Claim, 3=Refund, 5=ClaimSpl, 6=RefundSpl, 7=ClaimRelayed. (0/4 = New/NewSpl carry
/// commitments, not a secret.)
pub fn reveals_secret(opcode: u8) -> bool {
    matches!(opcode, 2 | 3 | 5 | 6 | 7)
}

/// Pull the revealed 32-byte secret out of a settling instruction's data, or `None` if this
/// instruction does not reveal one (or is malformed).
pub fn extract_revealed_secret(ix_data: &[u8]) -> Option<[u8; 32]> {
    let op = *ix_data.first()?;
    if !reveals_secret(op) {
        return None;
    }
    ix_data.get(1..33)?.try_into().ok()
}

/// True if the revealed secret matches the commitment — i.e. it is the key share that
/// unlocks the XMZ side. (`s*B == A`, verified with the host ed25519 reference.)
pub fn binds_to_commitment(secret: &[u8; 32], commitment: &[u8; 32]) -> bool {
    logic::verify_secret(secret, commitment)
}

#[cfg(test)]
mod tests {
    use super::*;
    use monzero_sol_swap_logic as logic;

    fn claim_ix(secret: &[u8; 32]) -> Vec<u8> {
        let mut d = vec![2u8];
        d.extend_from_slice(secret);
        d
    }

    #[test]
    fn extracts_from_each_revealing_opcode() {
        let s = [7u8; 32];
        for op in [2u8, 3, 5, 6, 7] {
            let mut d = vec![op];
            d.extend_from_slice(&s);
            if op == 7 {
                d.extend_from_slice(&1234u64.to_le_bytes()); // ClaimRelayed carries a fee after
            }
            assert_eq!(extract_revealed_secret(&d), Some(s), "opcode {op}");
        }
    }

    #[test]
    fn ignores_non_revealing_and_malformed() {
        assert_eq!(extract_revealed_secret(&[0u8; 40]), None, "New carries no secret");
        assert_eq!(extract_revealed_secret(&[4u8; 40]), None, "NewSpl carries no secret");
        assert_eq!(extract_revealed_secret(&[1u8]), None, "SetReady");
        assert_eq!(extract_revealed_secret(&[2u8, 0, 0]), None, "truncated secret");
        assert_eq!(extract_revealed_secret(&[]), None);
    }

    #[test]
    fn revealed_secret_binds_to_its_commitment() {
        // A valid claim: the revealed secret matches claim_commitment = S_a.
        let secret = {
            let mut b = [9u8; 32]; b[31] = 0; b
        };
        let commitment = logic::commit(&secret).expect("commit");
        let data = claim_ix(&secret);
        let revealed = extract_revealed_secret(&data).unwrap();
        assert!(binds_to_commitment(&revealed, &commitment), "s_a unlocks the XMZ share");
        // a different secret must not bind
        let other = { let mut b = [10u8; 32]; b[31] = 0; b };
        assert!(!binds_to_commitment(&other, &commitment));
    }
}
