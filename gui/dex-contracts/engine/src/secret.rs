//! One trader's secret share, valid on both chains.
//!
//! The same number k is
//!  - the trader's share of the shared XMZ spend key: an ed25519 scalar, little-endian; and
//!  - the secret the contract checks: a `bytes32` holding k big-endian, committed as the
//!    address of k·G on secp256k1.
//! k must be valid on both curves. The ed25519 group order l (about 2^252) is below the
//! secp256k1 order, so 1 <= k < l is enough; new shares are below 2^252, the range the
//! cross-curve proof covers. The engine's cross-curve DLEQ proof shows the
//! other trader that the ed25519 and secp256k1 public keys hide the same k.

use crate::{commitment, Error};
use alloy::primitives::{Address, B256};
use curve25519_dalek::constants::ED25519_BASEPOINT_TABLE;
use curve25519_dalek::Scalar;

#[derive(Clone, PartialEq, Eq)]
pub struct SecretShare(Scalar);

impl std::fmt::Debug for SecretShare {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("SecretShare(..)")
    }
}

impl SecretShare {
    /// A new share from 64 uniformly random bytes: reduced mod l, then cut to 252 bits so the
    /// cross-curve proof (which covers 252 bits) can be made for it.
    pub fn from_uniform_bytes(bytes: &[u8; 64]) -> Result<Self, Error> {
        let mut le = Scalar::from_bytes_mod_order_wide(bytes).to_bytes();
        le[31] &= 0x0f;
        Self::checked(Scalar::from_bytes_mod_order(le))
    }

    /// Below 2^252: our own shares always are; a share from elsewhere must be to be proven.
    pub fn in_proof_range(&self) -> bool {
        self.0.as_bytes()[31] & 0xf0 == 0
    }

    /// From the XMZ side: a canonical little-endian ed25519 scalar.
    pub fn from_ed25519_le(bytes: [u8; 32]) -> Result<Self, Error> {
        let scalar = Option::<Scalar>::from(Scalar::from_canonical_bytes(bytes)).ok_or(Error::InvalidSecret)?;
        Self::checked(scalar)
    }

    /// From the EVM side: the `bytes32` revealed in a `Claimed` / `Refunded` event.
    pub fn from_evm(secret: B256) -> Result<Self, Error> {
        let mut le = secret.0;
        le.reverse();
        Self::from_ed25519_le(le)
    }

    fn checked(scalar: Scalar) -> Result<Self, Error> {
        if scalar == Scalar::ZERO {
            return Err(Error::InvalidSecret);
        }
        Ok(SecretShare(scalar))
    }

    /// The `bytes32` the contract checks (k big-endian).
    pub fn to_evm(&self) -> B256 {
        let mut be = self.0.to_bytes();
        be.reverse();
        B256::from(be)
    }

    /// The XMZ key share (k little-endian).
    pub fn to_ed25519_le(&self) -> [u8; 32] {
        self.0.to_bytes()
    }

    /// The contract commitment: address of k·G on secp256k1.
    pub fn commitment(&self) -> Address {
        commitment(&self.to_evm()).expect("1 <= k < l < secp256k1 order")
    }

    /// The public XMZ spend share k·B (compressed ed25519 point).
    pub fn ed25519_public(&self) -> [u8; 32] {
        (&self.0 * ED25519_BASEPOINT_TABLE).compress().to_bytes()
    }
}

/// The private spend key of the shared XMZ address: (mine + theirs) mod l, little-endian.
pub fn combined_spend_key(mine: &SecretShare, theirs: &SecretShare) -> [u8; 32] {
    (mine.0 + theirs.0).to_bytes()
}

#[cfg(test)]
mod tests {
    use super::*;
    use curve25519_dalek::edwards::CompressedEdwardsY;

    // l = 2^252 + 27742317777372353535851937790883648493, little-endian
    const L_LE: [u8; 32] = [
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0x10,
    ];

    fn share(byte: u8) -> SecretShare {
        SecretShare::from_uniform_bytes(&[byte; 64]).unwrap()
    }

    #[test]
    fn one_is_01_at_opposite_ends() {
        let mut le = [0u8; 32];
        le[0] = 1;
        let k = SecretShare::from_ed25519_le(le).unwrap();
        assert_eq!(k.to_evm(), B256::with_last_byte(1));
        // the address of 1·G, a well-known Ethereum address
        assert_eq!(k.commitment(), "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf".parse::<Address>().unwrap());
    }

    #[test]
    fn evm_and_xmz_encodings_round_trip() {
        for byte in [1u8, 7, 0x42, 0xff] {
            let k = share(byte);
            assert_eq!(SecretShare::from_evm(k.to_evm()).unwrap(), k);
            assert_eq!(SecretShare::from_ed25519_le(k.to_ed25519_le()).unwrap(), k);
            assert_eq!(k.commitment(), commitment(&k.to_evm()).unwrap());
        }
    }

    #[test]
    fn rejects_zero_and_values_not_below_l() {
        assert_eq!(SecretShare::from_ed25519_le([0; 32]), Err(Error::InvalidSecret));
        assert_eq!(SecretShare::from_ed25519_le(L_LE), Err(Error::InvalidSecret));
        // Valid secp256k1 secrets that are too large for ed25519 must be refused on the EVM side
        let mut be = L_LE;
        be.reverse();
        assert_eq!(SecretShare::from_evm(B256::from(be)), Err(Error::InvalidSecret));
        assert_eq!(SecretShare::from_evm(B256::repeat_byte(0xff)), Err(Error::InvalidSecret));
    }

    #[test]
    fn combined_key_opens_the_shared_address() {
        // The shared public spend key is the sum of both public shares; the combined private
        // key must map to exactly that point.
        let (a, b) = (share(3), share(9));
        let sum = Scalar::from_canonical_bytes(combined_spend_key(&a, &b)).unwrap();
        let from_key = (&sum * ED25519_BASEPOINT_TABLE).compress();
        let pa = CompressedEdwardsY(a.ed25519_public()).decompress().unwrap();
        let pb = CompressedEdwardsY(b.ed25519_public()).decompress().unwrap();
        assert_eq!(from_key, (pa + pb).compress());
        assert_eq!(combined_spend_key(&a, &b), combined_spend_key(&b, &a));
    }

    #[test]
    fn new_shares_are_in_the_proof_range() {
        for byte in [1u8, 0x42, 0x7f, 0xff] {
            assert!(share(byte).in_proof_range());
        }
        let mut top = [0u8; 32];
        top[31] = 0x10; // 2^252 < l
        assert!(!SecretShare::from_ed25519_le(top).unwrap().in_proof_range());
    }

    #[test]
    fn debug_output_hides_the_secret() {
        assert_eq!(format!("{:?}", share(5)), "SecretShare(..)");
    }
}
