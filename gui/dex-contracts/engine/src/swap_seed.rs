//! Per-trade keys derived from one seed held in the seed vault.
//!
//! The app keeps one swap seed in the encrypted seed vault, derived from the wallet seed with
//! its own label. Everything secret a trade needs is derived from it and the offer, so
//! nothing per trade has to be stored, and restoring the wallet seed restores every trade's
//! keys:
//!  - our secret share and view share;
//!  - the randomness of our cross-curve proof, so our frame is the same bytes every time it
//!    is made (the EIP-191 signature is deterministic too, RFC 6979). After a restart we
//!    resend the frame the other trader already has, never a second, different one.
//!
//! Keys depend on the whole offer digest and our role, not only the trade id: a secret is
//! revealed on chain when it is used, so it must never serve two different offers.

use crate::negotiation::{LocalKeys, NegotiationError, Offer, Role, SignedFrame};
use alloy::signers::local::PrivateKeySigner;
use rand_chacha::ChaCha20Rng;
use rand_core::SeedableRng;
use zeroize::Zeroizing;


/// The seed behind every XMZ <-> EVM trade of this wallet. No Debug, no Clone, no
/// serialization; zeroized on drop.
pub struct SwapSeed(Zeroizing<[u8; 32]>);

impl SwapSeed {
    /// The swap seed of a wallet, from the seed in its vault.
    pub fn from_wallet_seed(wallet_seed: &[u8]) -> Self {
        SwapSeed(monzero_evm_keys::swap_seed(wallet_seed))
    }

    pub fn from_bytes(bytes: [u8; 32]) -> Self {
        SwapSeed(Zeroizing::new(bytes))
    }

    /// Our secrets for this offer and role.
    pub fn keys(&self, offer: &Offer, role: Role) -> LocalKeys {
        LocalKeys::generate(&mut self.stream(b"keys", offer, role))
    }

    /// Our signed frame for this offer and role: the same bytes on every call.
    pub fn frame(&self, offer: &Offer, role: Role, account: &PrivateKeySigner) -> Result<SignedFrame, NegotiationError> {
        self.keys(offer, role).frame(offer, role, account, &mut self.stream(b"proof", offer, role))
    }

    /// The password of this trade's XMZ swap wallet files: remade from the seed after a
    /// restart or restore, and different for every trade.
    pub fn wallet_password(&self, offer: &Offer, role: Role) -> Zeroizing<String> {
        let mut rng = self.stream(b"wallet password", offer, role);
        let mut bytes = Zeroizing::new([0u8; 32]);
        rand_core::RngCore::fill_bytes(&mut rng, &mut *bytes);
        Zeroizing::new(hex::encode(*bytes))
    }

    fn stream(&self, label: &[u8], offer: &Offer, role: Role) -> ChaCha20Rng {
        let role: &[u8] = match role {
            Role::Owner => b"owner",
            Role::Claimer => b"claimer",
        };
        ChaCha20Rng::from_seed(*prf(&*self.0, &[label, offer.digest().as_slice(), role]))
    }
}

/// HMAC-SHA256 over the swap domain (shared with the vault helper)
fn prf(key: &[u8], parts: &[&[u8]]) -> Zeroizing<[u8; 32]> {
    monzero_evm_keys::prf(key, parts)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::negotiation::{KeyExchange, NegotiationError as E};
    use crate::xmz::XmzNetwork;
    use alloy::primitives::{keccak256, Address, B256, U256};

    fn offer(owner: Address, claimer: Address) -> Offer {
        Offer {
            trade_id: B256::repeat_byte(0x42),
            chain_id: 97,
            contract: Address::repeat_byte(0xc0),
            owner,
            claimer,
            asset: Address::ZERO,
            value: U256::from(10u64.pow(17)),
            duration1: 3 * 3600,
            duration2: 6 * 3600,
            xmz_amount: 5_000_000_000_000,
            xmz_network: XmzNetwork::Stagenet,
            expires_at: 2_000,
        }
    }

    fn account(byte: u8) -> PrivateKeySigner {
        PrivateKeySigner::from_bytes(&B256::repeat_byte(byte)).unwrap()
    }

    fn commitment(seed: &SwapSeed, offer: &Offer, role: Role) -> Address {
        seed.keys(offer, role).secret.commitment()
    }

    #[test]
    fn a_restart_remakes_the_same_frame_and_the_other_trader_accepts_it() {
        let (alice, bob) = (account(1), account(2));
        let offer = offer(alice.address(), bob.address());
        let seed = SwapSeed::from_wallet_seed(b"owner wallet seed");
        let theirs = SwapSeed::from_wallet_seed(b"claimer wallet seed");

        let fo = seed.frame(&offer, Role::Owner, &alice).unwrap();
        let mut at_claimer = KeyExchange::new(offer.clone(), Role::Claimer, theirs.keys(&offer, Role::Claimer)).unwrap();
        at_claimer.accept(&fo, 1_000).unwrap();
        at_claimer.accept(&theirs.frame(&offer, Role::Claimer, &bob).unwrap(), 1_000).unwrap();

        // "Restart": a new handle on the same seed remakes byte-identical keys and frame
        let restarted = SwapSeed::from_wallet_seed(b"owner wallet seed");
        let again = restarted.frame(&offer, Role::Owner, &alice).unwrap();
        assert_eq!(again, fo);
        assert_eq!(at_claimer.accept(&again, 1_000), Ok(Role::Owner), "not seen as a second frame");

        // and our own exchange, rebuilt from the seed alone, accepts it as ours
        let mut at_owner = KeyExchange::new(offer.clone(), Role::Owner, restarted.keys(&offer, Role::Owner)).unwrap();
        at_owner.accept(&again, 1_000).unwrap();
        at_owner.accept(&theirs.frame(&offer, Role::Claimer, &bob).unwrap(), 1_000).unwrap();
        assert_eq!(at_owner.finish().unwrap().shared, at_claimer.finish().unwrap().shared);
    }

    #[test]
    fn keys_differ_by_offer_role_and_seed() {
        let base = offer(account(1).address(), account(2).address());
        let seed = SwapSeed::from_wallet_seed(b"wallet");
        let ours = commitment(&seed, &base, Role::Owner);
        // Same trade id, any other term changed: a fresh secret
        let later = Offer { expires_at: base.expires_at + 1, ..base.clone() };
        let more = Offer { value: base.value + U256::from(1), ..base.clone() };
        assert_ne!(commitment(&seed, &later, Role::Owner), ours);
        assert_ne!(commitment(&seed, &more, Role::Owner), ours);
        assert_ne!(commitment(&seed, &base, Role::Claimer), ours);
        assert_ne!(commitment(&SwapSeed::from_wallet_seed(b"other wallet"), &base, Role::Owner), ours);
        assert_ne!(seed.keys(&base, Role::Owner).view_share(), seed.keys(&later, Role::Owner).view_share());
        // The wallet seed is not used directly
        assert_ne!(commitment(&SwapSeed::from_wallet_seed(&[9; 32]), &base, Role::Owner), commitment(&SwapSeed::from_bytes([9; 32]), &base, Role::Owner));
        assert!(seed.keys(&base, Role::Owner).secret.in_proof_range());
    }

    #[test]
    fn keys_from_one_seed_for_both_roles_are_still_refused_together() {
        // Both roles from one seed get different keys, so a self-trade is not degenerate
        let (alice, bob) = (account(1), account(2));
        let offer = offer(alice.address(), bob.address());
        let seed = SwapSeed::from_wallet_seed(b"wallet");
        let mut ex = KeyExchange::new(offer.clone(), Role::Owner, seed.keys(&offer, Role::Owner)).unwrap();
        ex.accept(&seed.frame(&offer, Role::Owner, &alice).unwrap(), 1_000).unwrap();
        ex.accept(&seed.frame(&offer, Role::Claimer, &bob).unwrap(), 1_000).unwrap();
        ex.finish().unwrap();
        // A frame from another seed for our role is not ours
        let other = SwapSeed::from_wallet_seed(b"other").frame(&offer, Role::Owner, &alice).unwrap();
        let mut ex = KeyExchange::new(offer.clone(), Role::Owner, seed.keys(&offer, Role::Owner)).unwrap();
        assert_eq!(ex.accept(&other, 1_000), Err(E::NotOurKeys));
    }

    #[test]
    fn wallet_passwords_are_per_trade_and_reproducible() {
        let base = offer(account(1).address(), account(2).address());
        let seed = SwapSeed::from_wallet_seed(b"wallet");
        let pw = seed.wallet_password(&base, Role::Owner);
        assert_eq!(pw.len(), 64);
        assert_eq!(*SwapSeed::from_wallet_seed(b"wallet").wallet_password(&base, Role::Owner), *pw);
        assert_ne!(*seed.wallet_password(&base, Role::Claimer), *pw);
        assert_ne!(*seed.wallet_password(&Offer { expires_at: 2_001, ..base.clone() }, Role::Owner), *pw);
        // Not related to the view share, which the other trader learns
        assert_ne!(*pw, hex::encode(seed.keys(&base, Role::Owner).view_share()));
    }

    /// Pinned outputs: a change here strands every trade in flight after an upgrade (their
    /// keys could no longer be remade from the seed). Change the domain to change the scheme.
    #[test]
    fn derivation_is_pinned() {
        let (alice, bob) = (account(1), account(2));
        let offer = offer(alice.address(), bob.address());
        let seed = SwapSeed::from_bytes([7; 32]);
        let keys = seed.keys(&offer, Role::Owner);
        let frame = seed.frame(&offer, Role::Owner, &alice).unwrap();
        let got = format!(
            "{} {} {} {}",
            keys.secret.commitment(),
            hex::encode(keys.view_share()),
            keccak256(&frame.frame),
            keccak256(&frame.signature)
        );
        assert_eq!(
            got,
            "0x42CDA7434e44a0aB485Bc63458893cC34b6f78F0 a5d47583d9c35a3baf09cb82a354c39b15e01d8e49457154ed36c363a786aa0c \
             0x42bcbac5935775528bb22b3fc8a42729875c179459647d96f2b11d17cf4f08ea \
             0xc3b0c1330814d29755cd0e5496275bc543b31e84ac71929a68e6e5971e719a8f"
        );
    }
}
