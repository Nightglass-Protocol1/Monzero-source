//! Sealed messages between the maker and the taker of one offer, carried by the open relays.
//!
//! Most traders cannot accept incoming connections (home routers), so the key exchange goes
//! through relay mailboxes instead of a direct libp2p connection. Every message is sealed to
//! the recipient's mailbox key (X25519, a fresh sender key per message, ChaCha20-Poly1305):
//! a relay sees only who has mail, never the frames, which carry private view shares. A relay
//! can drop or delay mail, which costs time, never funds: the frames inside are signed by the
//! traders' EVM accounts and checked as in the direct exchange.
//!
//! Mailbox keys come from the swap seed and the listing id, so a restarted app reads the same
//! mailbox and remakes the same replies.

use crate::exchange::{KeyRequest, KeyResponse};
use alloy::primitives::{Address, Bytes, B256};
use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Key, Nonce};
use rand_core::{CryptoRng, RngCore};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use x25519_dalek::{PublicKey, StaticSecret};
use zeroize::Zeroizing;

pub const MAIL_SCHEMA: &str = "monzero-dex-mail/1";
/// Largest sealed message a relay keeps: a letter with a frame is about 340 KB, nearly all
/// of it the cross-curve proof
pub const MAX_SEALED: usize = 512 * 1024;

/// A mailbox: the X25519 key pair messages to us are sealed with
pub struct Mailbox(StaticSecret);

impl Mailbox {
    pub fn from_secret(secret: Zeroizing<[u8; 32]>) -> Self {
        Mailbox(StaticSecret::from(*secret))
    }

    pub fn address(&self) -> B256 {
        B256::from(PublicKey::from(&self.0).to_bytes())
    }

    /// Opens a message sealed to this mailbox
    pub fn open(&self, sealed: &[u8]) -> Option<Vec<u8>> {
        if sealed.len() < 32 + 16 || sealed.len() > MAX_SEALED {
            return None;
        }
        let sender = PublicKey::from(<[u8; 32]>::try_from(&sealed[..32]).ok()?);
        let shared = self.0.diffie_hellman(&sender);
        if !shared.was_contributory() {
            return None;
        }
        let key = message_key(shared.as_bytes(), sender.as_bytes(), &self.address().0);
        ChaCha20Poly1305::new(&Key::from(*key)).decrypt(&Nonce::default(), &sealed[32..]).ok()
    }
}

fn message_key(shared: &[u8; 32], sender: &[u8; 32], recipient: &[u8; 32]) -> Zeroizing<[u8; 32]> {
    let mut h = Sha256::new();
    h.update(MAIL_SCHEMA.as_bytes());
    h.update(shared);
    h.update(sender);
    h.update(recipient);
    Zeroizing::new(h.finalize().into())
}

/// Seals `plain` to the mailbox `to`; None for a key no message can be sealed to
pub fn seal<R: RngCore + CryptoRng>(to: B256, plain: &[u8], rng: &mut R) -> Option<Vec<u8>> {
    let recipient = PublicKey::from(to.0);
    let mut secret = Zeroizing::new([0u8; 32]);
    rng.fill_bytes(&mut *secret);
    let ephemeral = StaticSecret::from(*secret);
    let sender = PublicKey::from(&ephemeral);
    let shared = ephemeral.diffie_hellman(&recipient);
    if !shared.was_contributory() {
        return None;
    }
    // A fresh sender key per message makes every key unique, so the zero nonce is never reused
    let key = message_key(shared.as_bytes(), sender.as_bytes(), &to.0);
    let body = ChaCha20Poly1305::new(&Key::from(*key)).encrypt(&Nonce::default(), plain).ok()?;
    let sealed = [sender.as_bytes().as_slice(), &body].concat();
    (sealed.len() <= MAX_SEALED).then_some(sealed)
}

/// A taker asks to take an offer
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Take {
    pub listing: B256,
    /// The taker's EVM account: the offer's other party
    pub taker: Address,
    /// Where the maker's answer goes
    pub reply_to: B256,
    pub request: KeyRequest,
}

/// The maker's answer to a take
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Answer {
    pub listing: B256,
    pub response: KeyResponse,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase", tag = "kind")]
pub enum Letter {
    Take(Take),
    Answer(Answer),
}

impl Letter {
    pub fn seal<R: RngCore + CryptoRng>(&self, to: B256, rng: &mut R) -> Option<Bytes> {
        seal(to, &serde_json::to_vec(self).ok()?, rng).map(Bytes::from)
    }

    pub fn open(mailbox: &Mailbox, sealed: &[u8]) -> Option<Letter> {
        serde_json::from_slice(&Zeroizing::new(mailbox.open(sealed)?)).ok()
    }
}

/// Mail as relays store and serve it
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Envelope {
    pub to: B256,
    pub sealed: Bytes,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::negotiation::SignedFrame;
    use rand_core::OsRng;

    fn mailbox() -> Mailbox {
        let mut s = Zeroizing::new([0u8; 32]);
        OsRng.fill_bytes(&mut *s);
        Mailbox::from_secret(s)
    }

    fn letter() -> Letter {
        Letter::Answer(Answer {
            listing: B256::repeat_byte(3),
            response: KeyResponse::Frame(SignedFrame { frame: vec![7; 1500].into(), signature: vec![9; 65].into() }),
        })
    }

    #[test]
    fn only_the_recipient_opens_a_letter() {
        let (to, other) = (mailbox(), mailbox());
        let sealed = letter().seal(to.address(), &mut OsRng).unwrap();
        assert_eq!(Letter::open(&to, &sealed), Some(letter()));
        assert_eq!(Letter::open(&other, &sealed), None);
        // Each seal is different (fresh sender key), and none carries the plaintext
        let again = letter().seal(to.address(), &mut OsRng).unwrap();
        assert_ne!(sealed, again);
        assert!(!sealed.windows(8).any(|w| w == [7u8; 8]));
    }

    #[test]
    fn tampered_or_malformed_mail_is_refused() {
        let to = mailbox();
        let mut sealed = letter().seal(to.address(), &mut OsRng).unwrap().to_vec();
        let last = sealed.len() - 1;
        sealed[last] ^= 1;
        assert_eq!(Letter::open(&to, &sealed), None);
        sealed[last] ^= 1;
        sealed[0] ^= 1;
        assert_eq!(Letter::open(&to, &sealed), None, "another sender key");
        assert_eq!(to.open(&[0u8; 20]), None);
        assert_eq!(to.open(&vec![1u8; MAX_SEALED + 1]), None);
        // A low-order key (all zero) gives no shared secret: nothing is sealed to it or opened from it
        assert_eq!(seal(B256::ZERO, b"x", &mut OsRng), None);
        let zero_sender = [[0u8; 32].as_slice(), &[5u8; 40]].concat();
        assert_eq!(to.open(&zero_sender), None);
    }

    #[test]
    fn the_same_secret_is_the_same_mailbox() {
        let s = Zeroizing::new([42u8; 32]);
        assert_eq!(Mailbox::from_secret(s.clone()).address(), Mailbox::from_secret(s).address());
    }
}
