//! Keys for XMZ <-> EVM swaps, derived from the wallet seed held in the seed vault.
//!
//! Shared by the swap engine (`monzero-dex-evm`) and the vault helper, so the vault can hand
//! the swap side only what it needs (the swap seed and one EVM account key) and the wallet
//! seed never leaves it. Changing any output strands trades in flight: the tests pin them.

use hmac::{Hmac, Mac};
use k256::elliptic_curve::sec1::ToEncodedPoint;
use k256::elliptic_curve::PrimeField;
use k256::{FieldBytes, Scalar, SecretKey};
use sha2::{Sha256, Sha512};
use zeroize::Zeroizing;

/// Domain of every swap-seed derivation
pub const DOMAIN: &[u8] = b"monzero-xmz-evm-swap-v1";
const HARDENED: u32 = 1 << 31;

#[derive(Debug, PartialEq, Eq)]
pub enum Error {
    /// A wallet seed is 16 to 64 bytes (64 for a BIP-39 seed)
    SeedLength,
    /// Account indexes are below 2^31
    Index,
    /// Probability about 2^-127 per step; BIP-32 says to use the next index
    InvalidChild,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Error::SeedLength => "wallet seed must be 16 to 64 bytes",
            Error::Index => "account index must be below 2^31",
            Error::InvalidChild => "this index derives an invalid key; use the next one",
        })
    }
}

impl std::error::Error for Error {}

/// HMAC-SHA256 keyed by `key` over the domain and length-prefixed parts.
pub fn prf(key: &[u8], parts: &[&[u8]]) -> Zeroizing<[u8; 32]> {
    let mut mac = Hmac::<Sha256>::new_from_slice(key).expect("HMAC takes any key length");
    mac.update(DOMAIN);
    for part in parts {
        mac.update(&(part.len() as u32).to_be_bytes());
        mac.update(part);
    }
    Zeroizing::new(mac.finalize().into_bytes().into())
}

/// The seed behind every swap of a wallet, from the wallet seed in its vault.
pub fn swap_seed(wallet_seed: &[u8]) -> Zeroizing<[u8; 32]> {
    prf(wallet_seed, &[b"swap seed"])
}

/// The private key of the EVM account at m/44'/60'/0'/0/`index` (BIP-32, the Ethereum path):
/// the wallet's recovery words restore the same account in any EVM wallet.
pub fn evm_private_key(wallet_seed: &[u8], index: u32) -> Result<Zeroizing<[u8; 32]>, Error> {
    if !(16..=64).contains(&wallet_seed.len()) {
        return Err(Error::SeedLength);
    }
    if index >= HARDENED {
        return Err(Error::Index);
    }
    let (mut key, mut chain) = split(hmac512(b"Bitcoin seed", &[wallet_seed]))?;
    for step in [44 | HARDENED, 60 | HARDENED, HARDENED, 0, index] {
        (key, chain) = child(&key, &chain, step)?;
    }
    Ok(Zeroizing::new(key.to_bytes().into()))
}

/// The EVM address (20 bytes) of a private key: the last 20 bytes of Keccak-256 over the
/// uncompressed public key.
pub fn evm_address(private_key: &[u8; 32]) -> Option<[u8; 20]> {
    use sha3::{Digest, Keccak256};
    let public = SecretKey::from_bytes(&FieldBytes::from(*private_key)).ok()?.public_key().to_encoded_point(false);
    let hash = Keccak256::digest(&public.as_bytes()[1..]);
    let mut out = [0u8; 20];
    out.copy_from_slice(&hash[12..]);
    Some(out)
}

fn child(key: &Scalar, chain: &[u8; 32], i: u32) -> Result<(Scalar, Zeroizing<[u8; 32]>), Error> {
    let i_bytes = i.to_be_bytes();
    let out = if i >= HARDENED {
        let k = Zeroizing::new(key.to_bytes());
        hmac512(chain, &[&[0], &k, &i_bytes])
    } else {
        let public = SecretKey::from_bytes(&key.to_bytes()).expect("nonzero").public_key().to_encoded_point(true);
        hmac512(chain, &[public.as_bytes(), &i_bytes])
    };
    let (tweak, chain) = split(out)?;
    let child = tweak + key;
    if bool::from(child.is_zero()) {
        return Err(Error::InvalidChild);
    }
    Ok((child, chain))
}

fn hmac512(key: &[u8], parts: &[&[u8]]) -> Zeroizing<[u8; 64]> {
    let mut mac = Hmac::<Sha512>::new_from_slice(key).expect("HMAC takes any key length");
    for part in parts {
        mac.update(part);
    }
    Zeroizing::new(mac.finalize().into_bytes().into())
}

/// Left half as a key (nonzero, below the group order), right half as chain code.
fn split(i: Zeroizing<[u8; 64]>) -> Result<(Scalar, Zeroizing<[u8; 32]>), Error> {
    let key = Option::<Scalar>::from(Scalar::from_repr(FieldBytes::from(<[u8; 32]>::try_from(&i[..32]).expect("32 bytes"))))
        .filter(|k| !bool::from(k.is_zero()))
        .ok_or(Error::InvalidChild)?;
    let mut chain = Zeroizing::new([0u8; 32]);
    chain.copy_from_slice(&i[32..]);
    Ok((key, chain))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// BIP-39 seed of "test test test test test test test test test test test junk"
    const SEED: &str = "9dfc3c64c2f8bede1533b6a79f8570e5943e0b8fd1cf77107adf7b72cef42185d564a3aee24cab43f80e3c4538087d70fc824eabbad596a23c97b6ee8322ccc0";

    #[test]
    fn evm_keys_match_other_wallets() {
        let seed = hex::decode(SEED).unwrap();
        // The private keys every EVM tool shows for this phrase's first two accounts
        assert_eq!(hex::encode(*evm_private_key(&seed, 0).unwrap()), "ac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80");
        assert_eq!(hex::encode(*evm_private_key(&seed, 1).unwrap()), "59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d");
        assert_eq!(hex::encode(evm_address(&evm_private_key(&seed, 0).unwrap()).unwrap()), "f39fd6e51aad88f6f4ce6ab8827279cfffb92266");
        assert_eq!(evm_address(&[0; 32]), None);
        assert_eq!(evm_private_key(&seed[..15], 0), Err(Error::SeedLength));
        assert_eq!(evm_private_key(&seed, HARDENED), Err(Error::Index));
    }

    #[test]
    fn swap_seed_is_pinned() {
        assert_eq!(hex::encode(*swap_seed(&[9; 32])), "624ec00514813457261791e59284ba6699586bb286481e47f6840cb138a6eaaf");
    }
}
