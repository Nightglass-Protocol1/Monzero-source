//! Direct swaps between two contract chains (ETH, BNB, SOL): coin for coin, no XMZ leg.
//!
//! Both legs use the swap contracts already deployed for the XMZ pairs (`XmzSwap.sol` on the EVM
//! chains, the swap program on Solana): a lock that the claimer opens with a secret, and that its
//! owner takes back early (while not `Ready`, before `timeout1`) or late (from `timeout2`).
//!
//! The **leader** (the taker) locks leg A and claims leg B; the **follower** (the maker) locks
//! leg B and claims leg A. One secret `a`, known only to the leader, opens **both** claims:
//!
//!  1. the leader locks A and sets it `Ready` at once, so it can never take A back before
//!     `timeout2(A)`;
//!  2. once A is locked as agreed and can no longer be refunded early, the follower locks B, and
//!     sets it `Ready` once its lock is confirmed;
//!  3. the leader claims B, which publishes `a`;
//!  4. the follower reads `a` from that claim and claims A with it.
//!
//! If the follower never locks, the leader takes A back from `timeout2(A)`. If the leader never
//! claims, the follower takes B back from `timeout2(B)`, and the leader A from `timeout2(A)`.
//! Leg B closes well before leg A (the follower checks this before it locks), so a claim of B
//! that lands at the last moment still leaves the follower hours to claim A.
//!
//! Each owner also has its own refund secret (revealing it on a refund gives nothing away).
//! When the two legs are on different curves (an EVM chain and Solana), the leader proves with
//! the engine's cross-curve DLEQ proof that its claim commitments on both chains hide the same
//! `a`; without it the leader could claim B with a secret that does not open A.

use crate::negotiation::{NegotiationError, SignedFrame, PROOF_SYSTEM};
use crate::secret::SecretShare;
use crate::sol::SolBinding;
use crate::swap_seed::SwapSeed;
use crate::{AgreedTerms, Chain};
use alloy::primitives::{keccak256, Address, FixedBytes, Signature, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use alloy::signers::SignerSync;
use curve25519_dalek_ng::edwards::{CompressedEdwardsY, EdwardsPoint};
use curve25519_dalek_ng::scalar::Scalar as NgScalar;
use curve25519_dalek_ng::traits::Identity;
use k256::elliptic_curve::sec1::ToEncodedPoint;
use rand_core::{CryptoRng, RngCore};
use serde::{Deserialize, Serialize};
use sigma_fun::ext::dl_secp256k1_ed25519_eq::CrossCurveDLEQProof;

pub const DIRECT_SCHEMA: &str = "monzero-direct-swap-keys-v1";
const MAX_FRAME: usize = 200_000;
/// Shortest and longest time limits a leg may have (seconds)
pub const MIN_DURATION: u64 = 600;
pub const MAX_DURATION: u64 = 3 * 24 * 3600;

use NegotiationError as E;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Side {
    /// Locks leg A first, claims leg B (and so publishes the secret)
    Leader,
    /// Locks leg B once A is safely locked, claims leg A with the published secret
    Follower,
}

impl Side {
    fn label(self) -> &'static [u8] {
        match self {
            Side::Leader => b"leader",
            Side::Follower => b"follower",
        }
    }
}

/// One leg: what its owner locks, where, and its time limits. Contract and token addresses must
/// be the app's pinned ones for the chain; the app checks that before the exchange starts.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Leg {
    pub chain_id: u64,
    pub contract: Address,
    /// `Address::ZERO` for the chain's own coin
    pub asset: Address,
    /// In the chain's base units (wei, lamports)
    pub value: U256,
    /// Seconds from the lock to timeout1, and from timeout1 to timeout2
    pub duration1: u64,
    pub duration2: u64,
}

impl Leg {
    pub fn chain(&self) -> Result<Chain, NegotiationError> {
        Chain::from_id(self.chain_id).ok_or(E::UnknownChain(self.chain_id))
    }

    fn span(&self) -> u64 {
        self.duration1.saturating_add(self.duration2)
    }
}

/// The trade both traders accepted, before any keys.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct DirectOffer {
    /// Fresh for every trade
    pub trade_id: B256,
    /// The leader's account (EVM address; on Solana, the account derived from the same key)
    pub leader: Address,
    pub follower: Address,
    /// Locked by the leader, claimed by the follower
    pub leg_a: Leg,
    /// Locked by the follower, claimed by the leader
    pub leg_b: Leg,
    /// Unix seconds; no frame is accepted after this
    pub expires_at: u64,
}

impl DirectOffer {
    pub fn digest(&self) -> B256 {
        keccak256(serde_json::to_vec(self).expect("offer serializes"))
    }

    pub fn account(&self, side: Side) -> Address {
        match side {
            Side::Leader => self.leader,
            Side::Follower => self.follower,
        }
    }

    /// Either leg runs on Solana: each trader's frame then names its Solana account
    pub fn uses_solana(&self) -> bool {
        [&self.leg_a, &self.leg_b].iter().any(|l| l.chain().is_ok_and(Chain::is_solana))
    }

    /// The offer's own rules, checked by both traders before any keys are made.
    pub fn check(&self) -> Result<(), NegotiationError> {
        let (a, b) = (self.leg_a.chain()?, self.leg_b.chain()?);
        // Two coins of one chain may share its contract (BNB for USDT on BNB Smart Chain: each leg is
        // its own swap, owned by the other trader); one coin against itself may not
        if (a, self.leg_a.contract, self.leg_a.asset) == (b, self.leg_b.contract, self.leg_b.asset) {
            return Err(E::InvalidShare("both legs the same coin on the same contract"));
        }
        if self.leader == self.follower || self.leader == Address::ZERO || self.follower == Address::ZERO {
            return Err(E::InvalidShare("traders' accounts"));
        }
        for leg in [&self.leg_a, &self.leg_b] {
            if leg.value.is_zero() {
                return Err(E::InvalidShare("a leg with no value"));
            }
            for d in [leg.duration1, leg.duration2] {
                if !(MIN_DURATION..=MAX_DURATION).contains(&d) {
                    return Err(E::InvalidShare("a leg's time limit is out of range"));
                }
            }
        }
        // B must close before A: the follower needs time after B's last moment to claim A
        if self.leg_b.span() >= self.leg_a.span() {
            return Err(E::InvalidShare("leg B must close before leg A"));
        }
        Ok(())
    }
}

/// A public key on both curves: `k·G` (compressed secp256k1) and `k·B` (compressed ed25519).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct KeyPoints {
    pub secp: FixedBytes<33>,
    pub ed: B256,
}

/// The leader's claim key. When a leg is on Solana (ed25519) it carries the proof that both
/// points hide the same secret; between EVM chains both legs use the secp256k1 point alone, so
/// the proof (about 170 KB) is left out.
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct ClaimKey {
    pub secp: FixedBytes<33>,
    pub ed: B256,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub proof: Option<CrossCurveDLEQProof>,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct DirectFrame {
    schema: String,
    offer: B256,
    side: Side,
    /// Leader only: the key that opens the claims of both legs
    #[serde(default, skip_serializing_if = "Option::is_none")]
    claim: Option<ClaimKey>,
    /// The key that opens this trader's own refund (on the leg it locks). Only its owner uses
    /// it, so it needs no proof: a trader that sent mismatched points only harms its own refund.
    refund: KeyPoints,
    /// When either leg is on Solana: this trader's account there (its ed25519 public key)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    sol_account: Option<B256>,
}

fn signing_message(trade_id: B256, side: Side, frame: &[u8]) -> String {
    format!("Monzero direct swap keys\ntrade: {trade_id}\nside: {side:?}\nframe: {}", keccak256(frame))
}

/// This trader's secrets for one direct trade: no Debug, no serialization.
pub struct DirectKeys {
    /// The leader's claim secret `a`; None for the follower
    pub claim: Option<SecretShare>,
    /// Our refund secret on the leg we lock
    pub refund: SecretShare,
}

fn new_share<R: RngCore + CryptoRng>(rng: &mut R) -> SecretShare {
    let mut wide = [0u8; 64];
    rng.fill_bytes(&mut wide);
    SecretShare::from_uniform_bytes(&wide).expect("zero with negligible probability")
}

fn points(secret: &SecretShare) -> KeyPoints {
    let key = k256::SecretKey::from_slice(secret.to_evm().as_slice()).expect("1 <= k < l");
    let secp: [u8; 33] = key.public_key().to_encoded_point(true).as_bytes().try_into().expect("compressed point");
    KeyPoints { secp: FixedBytes(secp), ed: B256::from(secret.ed25519_public()) }
}

impl DirectKeys {
    pub fn generate<R: RngCore + CryptoRng>(side: Side, rng: &mut R) -> Self {
        let claim = (side == Side::Leader).then(|| new_share(rng));
        DirectKeys { claim, refund: new_share(rng) }
    }

    /// Our signed frame for this offer: make it once and keep it (the proof is randomised; the
    /// seed makes it the same bytes every time, see [`SwapSeed::direct_frame`]).
    pub fn frame<R: RngCore + CryptoRng>(&self, offer: &DirectOffer, side: Side, account: &PrivateKeySigner, rng: &mut R) -> Result<SignedFrame, NegotiationError> {
        let claim = self.claim.as_ref().map(|secret| {
            if offer.uses_solana() {
                let k = NgScalar::from_canonical_bytes(secret.to_ed25519_le()).expect("canonical share");
                let (proof, (secp, ed)) = PROOF_SYSTEM.prove(&k, rng);
                ClaimKey { secp: FixedBytes(secp.to_bytes()), ed: B256::from(ed.compress().to_bytes()), proof: Some(proof) }
            } else {
                let p = points(secret);
                ClaimKey { secp: p.secp, ed: p.ed, proof: None }
            }
        });
        let sol_account = offer.uses_solana().then(|| B256::from(crate::sol::account_key(&account.to_bytes().0).verifying_key().to_bytes()));
        let frame = DirectFrame { schema: DIRECT_SCHEMA.into(), offer: offer.digest(), side, claim, refund: points(&self.refund), sol_account };
        let bytes = serde_json::to_vec(&frame).map_err(|e| E::Malformed(e.to_string()))?;
        let signature = account.sign_message_sync(signing_message(offer.trade_id, side, &bytes).as_bytes()).map_err(|e| E::Malformed(e.to_string()))?;
        Ok(SignedFrame { frame: bytes.into(), signature: signature.as_bytes().to_vec().into() })
    }
}

impl SwapSeed {
    /// Our secrets for this direct offer and side
    pub fn direct_keys(&self, offer: &DirectOffer, side: Side) -> DirectKeys {
        DirectKeys::generate(side, &mut self.direct_stream(b"keys", offer.digest(), side.label()))
    }

    /// Our signed frame for this direct offer and side: the same bytes on every call
    pub fn direct_frame(&self, offer: &DirectOffer, side: Side, account: &PrivateKeySigner) -> Result<SignedFrame, NegotiationError> {
        self.direct_keys(offer, side).frame(offer, side, account, &mut self.direct_stream(b"proof", offer.digest(), side.label()))
    }
}

/// A key checked as public points: the commitment the EVM contract stores and the ed25519 point
/// the Solana program stores.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Checked {
    commitment: Address,
    ed: [u8; 32],
}

fn ed_point(bytes: &B256) -> Result<EdwardsPoint, NegotiationError> {
    let point = CompressedEdwardsY(bytes.0).decompress().ok_or(E::InvalidShare("ed25519 point"))?;
    if point.compress().to_bytes() != bytes.0 || !point.is_torsion_free() || point == EdwardsPoint::identity() {
        return Err(E::InvalidShare("noncanonical or degenerate ed25519 point"));
    }
    Ok(point)
}

fn check_points(p: &KeyPoints) -> Result<Checked, NegotiationError> {
    let key = k256::PublicKey::from_sec1_bytes(p.secp.as_slice()).map_err(|_| E::InvalidShare("secp256k1 point"))?;
    ed_point(&p.ed)?;
    let raw = key.to_encoded_point(false);
    Ok(Checked { commitment: Address::from_raw_public_key(&raw.as_bytes()[1..]), ed: p.ed.0 })
}

/// A Solana leg needs the cross-curve proof (its claim uses the ed25519 point); EVM-only trades
/// must not carry one (one encoding per frame, and nothing unchecked rides along).
fn check_claim(c: &ClaimKey, solana: bool) -> Result<Checked, NegotiationError> {
    match (&c.proof, solana) {
        (Some(proof), true) => {
            let secp = secp256kfun::Point::<secp256kfun::marker::Normal>::from_bytes(c.secp.0).ok_or(E::InvalidShare("secp256k1 point"))?;
            let ed = ed_point(&c.ed)?;
            if !PROOF_SYSTEM.verify(proof, (secp, ed)) {
                return Err(E::InvalidShare("cross-curve proof does not verify"));
            }
        }
        (None, false) => {}
        (None, true) => return Err(E::InvalidShare("a Solana leg needs the cross-curve proof")),
        (Some(_), false) => return Err(E::InvalidShare("no cross-curve proof between EVM chains")),
    }
    check_points(&KeyPoints { secp: c.secp, ed: c.ed })
}

#[derive(Clone)]
struct Accepted {
    claim: Option<Checked>,
    refund: Checked,
    signed: SignedFrame,
    sol_account: Option<B256>,
}

/// Collects both traders' frames (ours included) and checks them.
pub struct DirectExchange {
    offer: DirectOffer,
    digest: B256,
    side: Side,
    ours: (Option<Checked>, Checked),
    leader: Option<Accepted>,
    follower: Option<Accepted>,
}

/// The checked outcome: both legs' contract terms.
#[derive(Clone, Debug)]
pub struct DirectAgreed {
    /// Leader owns, follower claims
    pub leg_a: AgreedTerms,
    /// Follower owns, leader claims
    pub leg_b: AgreedTerms,
    /// For a leg on Solana: both accounts and the points the program stores
    pub sol_a: Option<SolBinding>,
    pub sol_b: Option<SolBinding>,
    /// keccak256 over both signed frames
    pub transcript: B256,
}

impl DirectExchange {
    pub fn new(offer: DirectOffer, side: Side, keys: &DirectKeys) -> Result<Self, NegotiationError> {
        offer.check()?;
        if keys.claim.is_some() != (side == Side::Leader) || !keys.refund.in_proof_range() || keys.claim.as_ref().is_some_and(|c| !c.in_proof_range()) {
            return Err(E::NotOurKeys);
        }
        let ours = (keys.claim.as_ref().map(|c| check_points(&points(c)).expect("our own key")), check_points(&points(&keys.refund)).expect("our own key"));
        let digest = offer.digest();
        Ok(DirectExchange { offer, digest, side, ours, leader: None, follower: None })
    }

    /// Accept a frame (ours or theirs). Accepting the same frame again is a no-op.
    pub fn accept(&mut self, signed: &SignedFrame, now: u64) -> Result<Side, NegotiationError> {
        self.take(signed, Some(now))
    }

    /// The agreement again from saved frames, after a restart (no expiry check)
    pub fn restore(offer: DirectOffer, side: Side, keys: &DirectKeys, frames: &[SignedFrame]) -> Result<DirectAgreed, NegotiationError> {
        let mut exchange = DirectExchange::new(offer, side, keys)?;
        for frame in frames {
            exchange.take(frame, None)?;
        }
        exchange.finish()
    }

    fn take(&mut self, signed: &SignedFrame, now: Option<u64>) -> Result<Side, NegotiationError> {
        if now.is_some_and(|now| now >= self.offer.expires_at) {
            return Err(E::Expired);
        }
        if signed.frame.is_empty() || signed.frame.len() > MAX_FRAME {
            return Err(E::Malformed("frame size".into()));
        }
        let frame: DirectFrame = serde_json::from_slice(&signed.frame).map_err(|e| E::Malformed(e.to_string()))?;
        if serde_json::to_vec(&frame).map_err(|e| E::Malformed(e.to_string()))? != signed.frame.as_ref() {
            return Err(E::Malformed("noncanonical encoding".into()));
        }
        if frame.schema != DIRECT_SCHEMA || frame.offer != self.digest {
            return Err(E::WrongContext);
        }
        let side = frame.side;
        let slot = match side {
            Side::Leader => &self.leader,
            Side::Follower => &self.follower,
        };
        if let Some(held) = slot {
            return if held.signed.frame == signed.frame { Ok(side) } else { Err(E::Equivocation(role_of(side))) };
        }
        let signature = Signature::try_from(signed.signature.as_ref()).map_err(|_| E::BadSignature(role_of(side)))?;
        let message = signing_message(self.offer.trade_id, side, &signed.frame);
        if signature.recover_address_from_msg(message.as_bytes()).ok() != Some(self.offer.account(side)) {
            return Err(E::BadSignature(role_of(side)));
        }
        let claim = match (side, &frame.claim) {
            (Side::Leader, Some(c)) => Some(check_claim(c, self.offer.uses_solana())?),
            (Side::Follower, None) => None,
            _ => return Err(E::InvalidShare("only the leader sends a claim key")),
        };
        let refund = check_points(&frame.refund)?;
        if self.offer.uses_solana() != frame.sol_account.is_some() || frame.sol_account == Some(B256::ZERO) {
            return Err(E::InvalidShare("Solana account"));
        }
        if side == self.side && (claim, refund) != self.ours {
            return Err(E::NotOurKeys);
        }
        let accepted = Accepted { claim, refund, signed: signed.clone(), sol_account: frame.sol_account };
        match side {
            Side::Leader => self.leader = Some(accepted),
            Side::Follower => self.follower = Some(accepted),
        }
        Ok(side)
    }

    pub fn finish(&self) -> Result<DirectAgreed, NegotiationError> {
        let (Some(leader), Some(follower)) = (&self.leader, &self.follower) else {
            return Err(E::Incomplete);
        };
        let claim = leader.claim.ok_or(E::Incomplete)?;
        let keys = [claim, leader.refund, follower.refund];
        if keys.iter().enumerate().any(|(i, k)| keys[i + 1..].iter().any(|o| o.commitment == k.commitment || o.ed == k.ed)) {
            return Err(E::InvalidShare("a key was used twice"));
        }
        let o = &self.offer;
        let terms = |leg: &Leg, owner: Address, claimer: Address, refund: Checked| -> Result<AgreedTerms, NegotiationError> {
            Ok(AgreedTerms {
                chain: leg.chain()?,
                contract: leg.contract,
                owner,
                claimer,
                claim_commitment: claim.commitment,
                refund_commitment: refund.commitment,
                asset: leg.asset,
                value: leg.value,
                duration1: leg.duration1,
                duration2: leg.duration2,
            })
        };
        let binding = |leg: &Leg, owner: &Accepted, claimer: &Accepted| -> Result<Option<SolBinding>, NegotiationError> {
            if !leg.chain()?.is_solana() {
                return Ok(None);
            }
            let (Some(o), Some(c)) = (owner.sol_account, claimer.sol_account) else {
                return Err(E::InvalidShare("Solana account"));
            };
            if o == c {
                return Err(E::InvalidShare("both traders sent the same Solana account"));
            }
            Ok(Some(SolBinding { owner: o.0, claimer: c.0, claim_point: claim.ed, refund_point: owner.refund.ed }))
        };
        let mut record = Vec::new();
        for f in [&leader.signed, &follower.signed] {
            for part in [&f.frame, &f.signature] {
                record.extend_from_slice(&(part.len() as u64).to_be_bytes());
                record.extend_from_slice(part);
            }
        }
        Ok(DirectAgreed {
            leg_a: terms(&o.leg_a, o.leader, o.follower, leader.refund)?,
            leg_b: terms(&o.leg_b, o.follower, o.leader, follower.refund)?,
            sol_a: binding(&o.leg_a, leader, follower)?,
            sol_b: binding(&o.leg_b, follower, leader)?,
            transcript: keccak256(record),
        })
    }
}

/// Errors name roles; a direct trade's leader is the owner of its first lock
fn role_of(side: Side) -> crate::negotiation::Role {
    match side {
        Side::Leader => crate::negotiation::Role::Owner,
        Side::Follower => crate::negotiation::Role::Claimer,
    }
}

/// Safety margins of a direct trade, in seconds.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct DirectTiming {
    /// The leader sends no claim of B this close to timeout2(B): a claim that lands late fails
    /// but still publishes the secret, after which the follower could both claim A and refund B
    pub claim_margin: u64,
    /// The follower locks B only if B's timeout2 then falls at least this long before A's:
    /// its time to see the leader's claim and get its own claim of A mined
    pub follower_reserve: u64,
    /// Allowance for the two chains' clocks disagreeing
    pub clock_skew: u64,
}

impl DirectTiming {
    pub const DEFAULT: DirectTiming = DirectTiming { claim_margin: 1800, follower_reserve: 7200, clock_skew: 600 };
}

/// What a trader writes to its journal once keys are agreed, before anything is locked: with
/// the swap seed it rebuilds the whole trade after a restart.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct SavedDirect {
    pub offer: DirectOffer,
    pub side: Side,
    pub their_frame: SignedFrame,
    pub timing: DirectTiming,
    /// The follower gives up if A is not safely locked by then (chain A's clock)
    pub lock_deadline: u64,
    /// First block (slot) searched for each leg's logs; taken before the first key request
    pub from_block_a: u64,
    pub from_block_b: u64,
    pub transcript: B256,
}

impl SavedDirect {
    pub fn agreed(&self, seed: &SwapSeed, account: &PrivateKeySigner) -> Result<DirectAgreed, NegotiationError> {
        let ours = seed.direct_frame(&self.offer, self.side, account)?;
        let agreed = DirectExchange::restore(self.offer.clone(), self.side, &seed.direct_keys(&self.offer, self.side), &[ours, self.their_frame.clone()])?;
        if agreed.transcript != self.transcript {
            return Err(E::InvalidShare("this seed and account do not rebuild the saved trade"));
        }
        Ok(agreed)
    }

    /// `newSwap` nonces: leg A from the first 8 bytes of the trade id, leg B from the next 8
    pub fn nonce_a(&self) -> u64 {
        u64::from_be_bytes(self.offer.trade_id[..8].try_into().expect("8 bytes"))
    }

    pub fn nonce_b(&self) -> u64 {
        u64::from_be_bytes(self.offer.trade_id[8..16].try_into().expect("8 bytes"))
    }
}

/// Our side of the exchange for one direct offer: one request and one answer, carried by relay
/// mail or any request/response transport (the frames are self-checking).
pub struct DirectSession {
    side: Side,
    ours: SignedFrame,
    theirs: Option<SignedFrame>,
    offer: DirectOffer,
    exchange: DirectExchange,
}

impl DirectSession {
    pub fn start(seed: &SwapSeed, offer: DirectOffer, side: Side, account: &PrivateKeySigner, now: u64) -> Result<Self, NegotiationError> {
        if account.address() != offer.account(side) {
            return Err(E::NotOurKeys);
        }
        let ours = seed.direct_frame(&offer, side, account)?;
        let mut exchange = DirectExchange::new(offer.clone(), side, &seed.direct_keys(&offer, side))?;
        exchange.accept(&ours, now)?;
        Ok(DirectSession { side, ours, theirs: None, offer, exchange })
    }

    pub fn frame(&self) -> &SignedFrame {
        &self.ours
    }

    /// Take the other trader's frame (a request or an answer); our own is refused here
    pub fn accept_theirs(&mut self, frame: &SignedFrame, now: u64) -> Result<DirectAgreed, NegotiationError> {
        if self.exchange.accept(frame, now)? == self.side {
            return Err(E::InvalidShare("a frame for our own side"));
        }
        self.theirs = Some(frame.clone());
        self.exchange.finish()
    }

    pub fn saved(&self, timing: DirectTiming, lock_deadline: u64, from_block_a: u64, from_block_b: u64) -> Option<SavedDirect> {
        let agreed = self.exchange.finish().ok()?;
        Some(SavedDirect {
            offer: self.offer.clone(),
            side: self.side,
            their_frame: self.theirs.clone()?,
            timing,
            lock_deadline,
            from_block_a,
            from_block_b,
            transcript: agreed.transcript,
        })
    }
}

#[cfg(test)]
#[path = "direct_tests.rs"]
mod tests;
