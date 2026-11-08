//! BTC/XMZ offers on the relay.
//!
//! A BTC trade is set up from both traders' identity cards and two signatures over its terms (see
//! `gui/tools/dex-trade-kit`), then negotiated through sealed relay mail. A BTC offer announces
//! one: amounts, side, network, the maker's identity key (Ed25519, the same key that will sign the
//! terms) and its mailbox key. The maker signs the offer with that key over
//! "MONZERO-DEX-BTC-LISTING-V1\n" + the offer's canonical JSON (sorted keys, no spaces), as the
//! trade kit writes it. Nothing here is checked on a chain: an offer is a signed promise to talk,
//! never a commitment of funds, and the trade's own terms and signatures decide everything.

use alloy::primitives::B256;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::{BTreeSet, HashMap};

pub const BTC_LISTING_SCHEMA: &str = "monzero-dex-btc-listing/1";
pub const BTC_CANCEL_SCHEMA: &str = "monzero-dex-btc-cancel/1";
const LISTING_DOMAIN: &[u8] = b"MONZERO-DEX-BTC-LISTING-V1\n";
const CANCEL_DOMAIN: &[u8] = b"MONZERO-DEX-BTC-CANCEL-V1\n";
pub const MAX_PER_MAKER: usize = 5;
pub const MAX_LISTINGS: usize = 2000;
const MAX_LIFETIME: u64 = 24 * 3600;
const CLOCK_SKEW: u64 = 300;
/// The most a BTC offer may name (sats); the app's signed control document sets the real limits
const MAX_SATS: u64 = 21_000_000 * 100_000_000;

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SignedBtcListing {
    pub listing: Map<String, Value>,
    /// Ed25519 signature, hex
    pub signature: String,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SignedBtcCancel {
    pub cancel: Map<String, Value>,
    pub signature: String,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum BtcError {
    #[error("not a BTC offer: {0}")]
    Malformed(&'static str),
    #[error("the signature is not the maker's")]
    BadSignature,
    #[error("network {0} is not served here")]
    Network(String),
    #[error("expired, from the future, or open longer than 24 hours")]
    Time,
    #[error("this offer was cancelled")]
    Closed,
    #[error("too many open offers for this key (at most {MAX_PER_MAKER})")]
    MakerLimit,
    #[error("the relay is full; try again later")]
    Full,
    #[error("no such offer")]
    NotFound,
}

/// The fields of a checked offer the relay needs
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BtcOffer {
    pub id: B256,
    pub maker_key: String,
    pub expires_at: u64,
    pub created_at: u64,
}

fn canonical(map: &Map<String, Value>) -> Vec<u8> {
    // serde_json's map is ordered by key, as Python's sort_keys; compact, UTF-8 unescaped
    serde_json::to_vec(map).expect("map serializes")
}

fn hex32(v: Option<&Value>) -> Option<[u8; 32]> {
    let s = v?.as_str()?;
    let s = s.strip_prefix("0x").unwrap_or(s);
    if s.len() != 64 || s.bytes().any(|b| !b.is_ascii_hexdigit() || b.is_ascii_uppercase()) {
        return None;
    }
    hex::decode(s).ok()?.try_into().ok()
}

fn digits(v: Option<&Value>, max: u64) -> Option<u64> {
    let s = v?.as_str()?;
    if s.is_empty() || s.len() > 20 || !s.bytes().all(|b| b.is_ascii_digit()) || (s.len() > 1 && s.starts_with('0')) {
        return None;
    }
    s.parse().ok().filter(|n| *n > 0 && *n <= max)
}

fn verify(domain: &[u8], map: &Map<String, Value>, key: &[u8; 32], signature: &str) -> Result<(), BtcError> {
    let key = VerifyingKey::from_bytes(key).map_err(|_| BtcError::Malformed("makerKey is not an Ed25519 key"))?;
    let sig = hex::decode(signature).ok().and_then(|b| <[u8; 64]>::try_from(b).ok()).ok_or(BtcError::BadSignature)?;
    key.verify(&[domain, &canonical(map)].concat(), &Signature::from_bytes(&sig)).map_err(|_| BtcError::BadSignature)
}

impl SignedBtcListing {
    /// The offer, if its maker signed it and it is well formed and open at `now`
    pub fn check(&self, networks: &BTreeSet<String>, now: u64) -> Result<BtcOffer, BtcError> {
        let l = &self.listing;
        const FIELDS: [&str; 11] = ["schema", "id", "pair", "network", "side", "btc", "xmz", "makerKey", "mailbox", "createdAt", "expiresAt"];
        if l.len() != FIELDS.len() || FIELDS.iter().any(|f| !l.contains_key(*f)) {
            return Err(BtcError::Malformed("fields"));
        }
        if l["schema"] != BTC_LISTING_SCHEMA || l["pair"] != "BTC/XMZ" {
            return Err(BtcError::Malformed("schema or pair"));
        }
        if l["side"] != "sell-xmz" && l["side"] != "buy-xmz" {
            return Err(BtcError::Malformed("side"));
        }
        let network = l["network"].as_str().ok_or(BtcError::Malformed("network"))?;
        if !networks.contains(network) {
            return Err(BtcError::Network(network.into()));
        }
        let id = hex32(l.get("id")).filter(|b| *b != [0; 32]).ok_or(BtcError::Malformed("id"))?;
        let key = hex32(l.get("makerKey")).ok_or(BtcError::Malformed("makerKey"))?;
        hex32(l.get("mailbox")).filter(|b| *b != [0; 32]).ok_or(BtcError::Malformed("mailbox"))?;
        digits(l.get("btc"), MAX_SATS).ok_or(BtcError::Malformed("btc"))?;
        digits(l.get("xmz"), u64::MAX).ok_or(BtcError::Malformed("xmz"))?;
        let (created, expires) = (l["createdAt"].as_u64().ok_or(BtcError::Malformed("createdAt"))?, l["expiresAt"].as_u64().ok_or(BtcError::Malformed("expiresAt"))?);
        if created > now + CLOCK_SKEW || expires <= now || expires <= created || expires - created > MAX_LIFETIME {
            return Err(BtcError::Time);
        }
        verify(LISTING_DOMAIN, l, &key, &self.signature)?;
        Ok(BtcOffer { id: B256::from(id), maker_key: hex::encode(key), expires_at: expires, created_at: created })
    }
}

#[derive(Default)]
pub struct BtcBook {
    active: HashMap<B256, (SignedBtcListing, BtcOffer)>,
    closed: HashMap<B256, u64>,
}

impl BtcBook {
    pub fn post(&mut self, signed: SignedBtcListing, networks: &BTreeSet<String>, now: u64) -> Result<bool, BtcError> {
        let offer = signed.check(networks, now)?;
        if self.closed.contains_key(&offer.id) {
            return Err(BtcError::Closed);
        }
        if let Some((known, _)) = self.active.get(&offer.id) {
            return if *known == signed { Ok(false) } else { Err(BtcError::Closed) };
        }
        if self.active.values().filter(|(_, o)| o.maker_key == offer.maker_key && o.expires_at > now).count() >= MAX_PER_MAKER {
            return Err(BtcError::MakerLimit);
        }
        if self.active.len() >= MAX_LISTINGS {
            return Err(BtcError::Full);
        }
        self.active.insert(offer.id, (signed, offer));
        Ok(true)
    }

    pub fn cancel(&mut self, signed: &SignedBtcCancel, now: u64) -> Result<(), BtcError> {
        let c = &signed.cancel;
        if c.len() != 4 || c.get("schema").and_then(Value::as_str) != Some(BTC_CANCEL_SCHEMA) || !c.get("at").is_some_and(Value::is_u64) {
            return Err(BtcError::Malformed("cancel"));
        }
        let id = B256::from(hex32(c.get("id")).ok_or(BtcError::Malformed("id"))?);
        let key = hex32(c.get("makerKey")).ok_or(BtcError::Malformed("makerKey"))?;
        verify(CANCEL_DOMAIN, c, &key, &signed.signature)?;
        match self.active.get(&id) {
            Some((_, o)) if o.maker_key == hex::encode(key) => {
                self.active.remove(&id);
                self.closed.insert(id, now);
                Ok(())
            }
            _ => Err(BtcError::NotFound),
        }
    }

    pub fn listings(&self, now: u64) -> Vec<SignedBtcListing> {
        let mut out: Vec<_> = self.active.values().filter(|(_, o)| o.expires_at > now).collect();
        out.sort_by_key(|(_, o)| (o.created_at, o.id));
        out.into_iter().map(|(s, _)| s.clone()).collect()
    }

    pub fn get(&self, id: B256) -> Option<SignedBtcListing> {
        self.active.get(&id).map(|(s, _)| s.clone())
    }

    pub fn prune(&mut self, now: u64) {
        self.active.retain(|_, (_, o)| o.expires_at > now);
        self.closed.retain(|_, at| *at + 7 * 24 * 3600 > now);
    }

    pub fn snapshot(&self) -> (Vec<SignedBtcListing>, Vec<(B256, u64)>) {
        (self.active.values().map(|(s, _)| s.clone()).collect(), self.closed.iter().map(|(k, v)| (*k, *v)).collect())
    }

    pub fn restore(&mut self, active: Vec<SignedBtcListing>, closed: Vec<(B256, u64)>, networks: &BTreeSet<String>) {
        self.closed.extend(closed);
        for s in active {
            // Checked again as of when it was made: a tampered saved offer is dropped
            let created = s.listing.get("createdAt").and_then(Value::as_u64).unwrap_or(0);
            if let Ok(o) = s.check(networks, created) {
                if !self.closed.contains_key(&o.id) {
                    self.active.insert(o.id, (s, o));
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};
    use serde_json::json;

    const NOW: u64 = 1_800_000_000;

    fn nets() -> BTreeSet<String> {
        ["btc-regtest".to_string()].into()
    }

    fn sign(key: &SigningKey, domain: &[u8], map: &Map<String, Value>) -> String {
        hex::encode(key.sign(&[domain, &canonical(map)].concat()).to_bytes())
    }

    fn offer(key: &SigningKey, id: u8) -> SignedBtcListing {
        let listing = json!({"schema": BTC_LISTING_SCHEMA, "id": format!("0x{}", hex::encode([id; 32])), "pair": "BTC/XMZ",
            "network": "btc-regtest", "side": "sell-xmz", "btc": "200000", "xmz": "200000000000",
            "makerKey": hex::encode(key.verifying_key().to_bytes()), "mailbox": hex::encode([9u8; 32]),
            "createdAt": NOW, "expiresAt": NOW + 3600});
        let map = listing.as_object().unwrap().clone();
        SignedBtcListing { signature: sign(key, LISTING_DOMAIN, &map), listing: map }
    }

    #[test]
    fn signed_btc_offers_are_listed_and_cancelled_by_their_maker() {
        let (a, b) = (SigningKey::from_bytes(&[1; 32]), SigningKey::from_bytes(&[2; 32]));
        let mut book = BtcBook::default();
        assert_eq!(book.post(offer(&a, 1), &nets(), NOW), Ok(true));
        assert_eq!(book.post(offer(&a, 1), &nets(), NOW), Ok(false));
        assert_eq!(book.listings(NOW).len(), 1);
        // The canonical form survives a JSON round trip (as a peer relay or the kit would send it)
        let again: SignedBtcListing = serde_json::from_str(&serde_json::to_string_pretty(&offer(&a, 2)).unwrap()).unwrap();
        assert_eq!(book.post(again, &nets(), NOW), Ok(true));
        let cancel = |key: &SigningKey, id: u8| {
            let map = json!({"schema": BTC_CANCEL_SCHEMA, "id": format!("0x{}", hex::encode([id; 32])),
                "makerKey": hex::encode(a.verifying_key().to_bytes()), "at": NOW}).as_object().unwrap().clone();
            SignedBtcCancel { signature: sign(key, CANCEL_DOMAIN, &map), cancel: map }
        };
        assert_eq!(book.cancel(&cancel(&b, 1), NOW), Err(BtcError::BadSignature), "only the maker's key cancels");
        assert_eq!(book.cancel(&cancel(&a, 1), NOW), Ok(()));
        assert_eq!(book.post(offer(&a, 1), &nets(), NOW), Err(BtcError::Closed));
        assert_eq!(book.listings(NOW).len(), 1);
    }

    #[test]
    fn bad_btc_offers_are_refused() {
        let a = SigningKey::from_bytes(&[1; 32]);
        let check = |mutate: &dyn Fn(&mut Map<String, Value>), resign: bool| {
            let mut s = offer(&a, 3);
            mutate(&mut s.listing);
            if resign {
                s.signature = sign(&a, LISTING_DOMAIN, &s.listing);
            }
            s.check(&nets(), NOW)
        };
        assert_eq!(check(&|m| { m.insert("btc".into(), json!("200001")); }, false), Err(BtcError::BadSignature));
        assert_eq!(check(&|m| { m.insert("network".into(), json!("btc-mainnet")); }, true), Err(BtcError::Network("btc-mainnet".into())));
        assert_eq!(check(&|m| { m.insert("btc".into(), json!(200000)); }, true), Err(BtcError::Malformed("btc")));
        assert_eq!(check(&|m| { m.insert("xmz".into(), json!("0")); }, true), Err(BtcError::Malformed("xmz")));
        assert_eq!(check(&|m| { m.insert("side".into(), json!("sell")); }, true), Err(BtcError::Malformed("side")));
        assert_eq!(check(&|m| { m.insert("extra".into(), json!(1)); }, true), Err(BtcError::Malformed("fields")));
        assert_eq!(check(&|m| { m.insert("expiresAt".into(), json!(NOW + 90000)); }, true), Err(BtcError::Time));
        assert_eq!(check(&|m| { m.insert("mailbox".into(), json!(hex::encode([0u8; 32]))); }, true), Err(BtcError::Malformed("mailbox")));
        let mut forged = offer(&a, 3);
        forged.listing.insert("makerKey".into(), json!(hex::encode(SigningKey::from_bytes(&[5; 32]).verifying_key().to_bytes())));
        assert_eq!(forged.check(&nets(), NOW), Err(BtcError::BadSignature));
    }
}
