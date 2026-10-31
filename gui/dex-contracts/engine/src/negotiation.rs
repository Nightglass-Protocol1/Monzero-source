//! Key exchange for one XMZ <-> BNB trade, after both traders accepted the same [`Offer`].
//!
//! Each trader sends one signed frame with its key share:
//!  - k·G on secp256k1, whose address is the trader's commitment in the contract;
//!  - k·B on ed25519, the trader's share of the shared XMZ spend key;
//!  - a cross-curve DLEQ proof that both points hide the same k (the proof system of the
//!    BTC/LTC/BCH swaps). Without it, a trader could commit on the EVM side to a secret that
//!    does not open its XMZ share, or pick its XMZ share from the other trader's to control
//!    the shared address alone;
//!  - its private view share (both traders watch the shared address).
//!
//! A frame is bound to the offer's digest and its role, and signed (EIP-191) by that role's
//! EVM account from the offer. A second, different frame for the same role is refused.
//! Frames carry private view shares: never log them or publish them with order-book data.

use crate::machine::{Timing, Trade};
use crate::secret::SecretShare;
use crate::xmz::{SharedXmz, XmzNetwork};
use crate::{AgreedTerms, Chain};
use alloy::primitives::{keccak256, Address, Bytes, FixedBytes, Signature, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use alloy::signers::SignerSync;
use curve25519_dalek_ng::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek_ng::scalar::Scalar as NgScalar;
use curve25519_dalek_ng::traits::Identity;
use rand_core::{CryptoRng, RngCore};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use sigma_fun::ext::dl_secp256k1_ed25519_eq::{CrossCurveDLEQ, CrossCurveDLEQProof};
use sigma_fun::HashTranscript;
use std::sync::LazyLock;

pub const SCHEMA: &str = "monzero-xmz-evm-key-exchange-v1";
const MAX_FRAME: usize = 200_000;

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum NegotiationError {
    #[error("the offer has expired")]
    Expired,
    #[error("malformed frame: {0}")]
    Malformed(String),
    #[error("frame for another offer or schema")]
    WrongContext,
    #[error("frame not signed by the {0:?}'s account")]
    BadSignature(Role),
    #[error("a different frame was already accepted for the {0:?}")]
    Equivocation(Role),
    #[error("invalid key share: {0}")]
    InvalidShare(&'static str),
    #[error("our own frame does not match our keys")]
    NotOurKeys,
    #[error("key exchange incomplete")]
    Incomplete,
    #[error("unknown chain id {0}")]
    UnknownChain(u64),
}

use NegotiationError as E;

/// Proof system shared with the engine's other pairs: Pedersen blinding generators are the
/// BIP-341 NUMS point on secp256k1 and Monero's H on ed25519.
static PROOF_SYSTEM: LazyLock<CrossCurveDLEQ<HashTranscript<Sha256, rand_chacha::ChaCha20Rng>>> = LazyLock::new(|| {
    let g = secp256kfun::G.normalize().to_bytes_uncompressed();
    let h_secp = secp256kfun::Point::<secp256kfun::marker::EvenY>::from_xonly_bytes(Sha256::digest(g).into())
        .expect("SHA-256 of the uncompressed generator is a valid x-coordinate")
        .normalize();
    let h_ed = CompressedEdwardsY(MONERO_H).decompress().expect("Monero H is a valid point");
    CrossCurveDLEQ::new(h_secp, h_ed)
});

/// Monero's Pedersen commitment generator H
const MONERO_H: [u8; 32] = [
    0x8b, 0x65, 0x59, 0x70, 0x15, 0x37, 0x99, 0xaf, 0x2a, 0xea, 0xdc, 0x9f, 0xf1, 0xad, 0xd0, 0xea, 0x6c, 0x72, 0x51, 0xd5,
    0x41, 0x54, 0xcf, 0xa9, 0x2c, 0x17, 0x3a, 0x0d, 0xd3, 0x9c, 0x1f, 0x94,
];

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    /// Locks the BNB / token, receives the XMZ
    Owner,
    /// Locks the XMZ, claims the BNB / token
    Claimer,
}

/// The trade both traders accepted, before any keys. Contract and token addresses must be
/// the app's pinned ones for the chain; the app checks that before starting the exchange.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Offer {
    /// Fresh for every trade
    pub trade_id: B256,
    pub chain_id: u64,
    pub contract: Address,
    pub owner: Address,
    pub claimer: Address,
    pub asset: Address,
    pub value: U256,
    pub duration1: u64,
    pub duration2: u64,
    pub xmz_amount: u64,
    pub xmz_network: XmzNetwork,
    /// Unix seconds; no frame is accepted after this
    pub expires_at: u64,
}

impl Offer {
    pub fn digest(&self) -> B256 {
        keccak256(serde_json::to_vec(self).expect("offer serializes"))
    }
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct KeyShare {
    /// k·G, compressed
    pub secp_public: FixedBytes<33>,
    /// k·B, compressed
    pub xmz_spend_public: B256,
    /// Private view share, little-endian scalar
    pub view_share: B256,
    pub proof: CrossCurveDLEQProof,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct Frame {
    schema: String,
    offer: B256,
    role: Role,
    share: KeyShare,
}

/// A frame as sent: its exact bytes and the EIP-191 signature of its role's account.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SignedFrame {
    pub frame: Bytes,
    pub signature: Bytes,
}

/// What the account signs: short and readable in a wallet, bound to the frame bytes.
fn signing_message(trade_id: B256, role: Role, frame: &[u8]) -> String {
    format!("Monzero XMZ swap keys\ntrade: {trade_id}\nrole: {role:?}\nframe: {}", keccak256(frame))
}

/// This trader's secrets for the trade. The caller stores them in the seed vault until the
/// swap is finished; no Debug, no serialization.
pub struct LocalKeys {
    pub secret: SecretShare,
    view: [u8; 32],
}

impl LocalKeys {
    pub fn generate<R: RngCore + CryptoRng>(rng: &mut R) -> Self {
        let mut wide = [0u8; 64];
        rng.fill_bytes(&mut wide);
        let secret = SecretShare::from_uniform_bytes(&wide).expect("zero with negligible probability");
        rng.fill_bytes(&mut wide);
        let view = NgScalar::from_bytes_mod_order_wide(&wide).to_bytes();
        LocalKeys { secret, view }
    }

    pub fn from_secrets(secret: SecretShare, view: [u8; 32]) -> Result<Self, NegotiationError> {
        if !secret.in_proof_range() {
            return Err(E::InvalidShare("secret is outside the proof range"));
        }
        match NgScalar::from_canonical_bytes(view) {
            Some(v) if v != NgScalar::zero() => Ok(LocalKeys { secret, view }),
            _ => Err(E::InvalidShare("view share")),
        }
    }

    pub fn view_share(&self) -> [u8; 32] {
        self.view
    }

    /// Our share with a fresh proof.
    pub fn share<R: RngCore + CryptoRng>(&self, rng: &mut R) -> KeyShare {
        let k = NgScalar::from_canonical_bytes(self.secret.to_ed25519_le()).expect("canonical share");
        let (proof, (secp, ed)) = PROOF_SYSTEM.prove(&k, rng);
        KeyShare {
            secp_public: FixedBytes(secp.to_bytes()),
            xmz_spend_public: B256::from(ed.compress().to_bytes()),
            view_share: B256::from(self.view),
            proof,
        }
    }

    /// Our signed frame for this offer. Make it once and keep it: the proof is randomised,
    /// and the other trader refuses a second, different frame for our role.
    pub fn frame<R: RngCore + CryptoRng>(
        &self,
        offer: &Offer,
        role: Role,
        account: &PrivateKeySigner,
        rng: &mut R,
    ) -> Result<SignedFrame, NegotiationError> {
        let frame = Frame { schema: SCHEMA.into(), offer: offer.digest(), role, share: self.share(rng) };
        let bytes = serde_json::to_vec(&frame).map_err(|e| E::Malformed(e.to_string()))?;
        let signature = account
            .sign_message_sync(signing_message(offer.trade_id, role, &bytes).as_bytes())
            .map_err(|e| E::Malformed(e.to_string()))?;
        Ok(SignedFrame { frame: bytes.into(), signature: signature.as_bytes().to_vec().into() })
    }
}

/// A share whose proof verified.
#[derive(Clone)]
struct Checked {
    commitment: Address,
    spend: EdwardsPoint,
    view: NgScalar,
    signed: SignedFrame,
}

fn check_share(share: &KeyShare) -> Result<(Address, EdwardsPoint, NgScalar), NegotiationError> {
    let secp = secp256kfun::Point::<secp256kfun::marker::Normal>::from_bytes(share.secp_public.0)
        .ok_or(E::InvalidShare("secp256k1 point"))?;
    let spend = CompressedEdwardsY(share.xmz_spend_public.0).decompress().ok_or(E::InvalidShare("ed25519 point"))?;
    if spend.compress().to_bytes() != share.xmz_spend_public.0 || !spend.is_torsion_free() || spend == EdwardsPoint::identity() {
        return Err(E::InvalidShare("noncanonical or degenerate ed25519 point"));
    }
    let view = NgScalar::from_canonical_bytes(share.view_share.0).ok_or(E::InvalidShare("view share"))?;
    if view == NgScalar::zero() {
        return Err(E::InvalidShare("zero view share"));
    }
    if !PROOF_SYSTEM.verify(&share.proof, (secp, spend)) {
        return Err(E::InvalidShare("cross-curve proof does not verify"));
    }
    let commitment = Address::from_raw_public_key(&secp.to_bytes_uncompressed()[1..]);
    Ok((commitment, spend, view))
}

/// Collects both traders' frames (ours included) and checks them.
pub struct KeyExchange {
    offer: Offer,
    digest: B256,
    role: Role,
    keys: LocalKeys,
    owner: Option<Checked>,
    claimer: Option<Checked>,
}

/// The checked outcome: the contract terms with both commitments, and the shared address.
#[derive(Clone, Debug)]
pub struct Agreed {
    pub terms: AgreedTerms,
    pub xmz_amount: u64,
    pub shared: SharedXmz,
    /// keccak256 over both signed frames: the record of this exchange
    pub transcript: B256,
}

impl Agreed {
    pub fn trade(&self, timing: Timing, lock_deadline: u64) -> Trade {
        Trade { terms: self.terms.clone(), xmz_amount: self.xmz_amount, timing, lock_deadline }
    }
}

impl KeyExchange {
    pub fn new(offer: Offer, role: Role, keys: LocalKeys) -> Result<Self, NegotiationError> {
        Chain::from_id(offer.chain_id).ok_or(E::UnknownChain(offer.chain_id))?;
        if !keys.secret.in_proof_range() {
            return Err(E::NotOurKeys);
        }
        let digest = offer.digest();
        Ok(KeyExchange { offer, digest, role, keys, owner: None, claimer: None })
    }

    /// Accept a frame (ours or theirs). Accepting the same frame again is a no-op.
    pub fn accept(&mut self, signed: &SignedFrame, now: u64) -> Result<Role, NegotiationError> {
        self.take(signed, Some(now))
    }

    /// The agreement again from saved frames (ours and theirs), after a restart: every check
    /// of `accept` except the offer's expiry, which only limits when a trade can be agreed.
    pub fn restore(offer: Offer, role: Role, keys: LocalKeys, frames: &[SignedFrame]) -> Result<Agreed, NegotiationError> {
        let mut exchange = KeyExchange::new(offer, role, keys)?;
        for frame in frames {
            exchange.take(frame, None)?;
        }
        exchange.finish()
    }

    fn take(&mut self, signed: &SignedFrame, now: Option<u64>) -> Result<Role, NegotiationError> {
        if now.is_some_and(|now| now >= self.offer.expires_at) {
            return Err(E::Expired);
        }
        if signed.frame.is_empty() || signed.frame.len() > MAX_FRAME {
            return Err(E::Malformed("frame size".into()));
        }
        let frame: Frame = serde_json::from_slice(&signed.frame).map_err(|e| E::Malformed(e.to_string()))?;
        // One encoding only, so the signed bytes are the frame
        if serde_json::to_vec(&frame).map_err(|e| E::Malformed(e.to_string()))? != signed.frame.as_ref() {
            return Err(E::Malformed("noncanonical encoding".into()));
        }
        if frame.schema != SCHEMA || frame.offer != self.digest {
            return Err(E::WrongContext);
        }
        let role = frame.role;
        let slot = match role {
            Role::Owner => &self.owner,
            Role::Claimer => &self.claimer,
        };
        if let Some(held) = slot {
            return if held.signed.frame == signed.frame { Ok(role) } else { Err(E::Equivocation(role)) };
        }
        let account = match role {
            Role::Owner => self.offer.owner,
            Role::Claimer => self.offer.claimer,
        };
        let signature = Signature::try_from(signed.signature.as_ref()).map_err(|_| E::BadSignature(role))?;
        let message = signing_message(self.offer.trade_id, role, &signed.frame);
        if signature.recover_address_from_msg(message.as_bytes()).ok() != Some(account) {
            return Err(E::BadSignature(role));
        }
        let (commitment, spend, view) = check_share(&frame.share)?;
        if role == self.role
            && (commitment != self.keys.secret.commitment()
                || spend.compress().to_bytes() != self.keys.secret.ed25519_public()
                || view.to_bytes() != self.keys.view)
        {
            return Err(E::NotOurKeys);
        }
        let checked = Checked { commitment, spend, view, signed: signed.clone() };
        match role {
            Role::Owner => self.owner = Some(checked),
            Role::Claimer => self.claimer = Some(checked),
        }
        Ok(role)
    }

    pub fn finish(&self) -> Result<Agreed, NegotiationError> {
        let (Some(owner), Some(claimer)) = (&self.owner, &self.claimer) else {
            return Err(E::Incomplete);
        };
        if owner.commitment == claimer.commitment || owner.spend == claimer.spend {
            return Err(E::InvalidShare("both traders sent the same key"));
        }
        if owner.spend + claimer.spend == EdwardsPoint::identity() || owner.view + claimer.view == NgScalar::zero() {
            return Err(E::InvalidShare("degenerate combined keys"));
        }
        let (mine, theirs) = match self.role {
            Role::Owner => (owner, claimer),
            Role::Claimer => (claimer, owner),
        };
        let shared = SharedXmz::from_shares(
            self.offer.xmz_network,
            &self.keys.secret,
            theirs.spend.compress().to_bytes(),
            mine.view.to_bytes(),
            theirs.view.to_bytes(),
        )
        .map_err(|_| E::InvalidShare("shared address"))?;
        let mut record = Vec::new();
        for f in [&owner.signed, &claimer.signed] {
            for part in [&f.frame, &f.signature] {
                record.extend_from_slice(&(part.len() as u64).to_be_bytes());
                record.extend_from_slice(part);
            }
        }
        let o = &self.offer;
        Ok(Agreed {
            terms: AgreedTerms {
                chain: Chain::from_id(o.chain_id).ok_or(E::UnknownChain(o.chain_id))?,
                contract: o.contract,
                owner: o.owner,
                claimer: o.claimer,
                claim_commitment: claimer.commitment,
                refund_commitment: owner.commitment,
                asset: o.asset,
                value: o.value,
                duration1: o.duration1,
                duration2: o.duration2,
            },
            xmz_amount: o.xmz_amount,
            shared,
            transcript: keccak256(record),
        })
    }
}

#[cfg(test)]
#[path = "negotiation_tests.rs"]
mod tests;
