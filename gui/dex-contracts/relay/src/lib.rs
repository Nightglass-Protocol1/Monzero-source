//! An open relay for Monzero DEX offers and completed trades.
//!
//! It holds no funds and no keys. Offers are kept only if their maker signed them and they match
//! the pinned contract for their chain; a completed trade (a "print") is kept only once the relay
//! has read the claim on chain itself and matched the swap's nonce to an offer. See
//! `../PUBLIC-OPENING.md`.

pub mod btc;
pub mod chain;
pub mod http;

use alloy::primitives::{Address, Bytes, B256, U256};
use monzero_dex_evm::mail::MAX_SEALED;
use monzero_dex_evm::listing::{ListingError, Rules, SignedCancel, SignedListing};
use monzero_dex_evm::negotiation::Role;
use monzero_dex_evm::Swap;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::Mutex;

/// Active offers one maker may have
pub const MAX_PER_MAKER: usize = 5;
/// Active offers in total
pub const MAX_LISTINGS: usize = 2000;
/// How long a listing stays matchable after it closes (a trade takes hours after the take)
pub const ARCHIVE_SECS: u64 = 7 * 24 * 3600;
/// How long a reported claim may stay unconfirmed before it is dropped
pub const PENDING_SECS: u64 = 4 * 3600;
pub const MAX_PENDING: usize = 500;
pub const INTERVALS: [u64; 3] = [3600, 4 * 3600, 24 * 3600];
/// Mail is kept this long; senders resend until answered
pub const MAIL_SECS: u64 = 2 * 3600;
/// A BTC trade's negotiation runs over relay mail for hours (keepalives every minute)
pub const MAIL_PER_BOX: usize = 2048;
/// All mail held at once, in bytes
pub const MAIL_TOTAL: usize = 256 * 1024 * 1024;

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum RelayError {
    #[error("{0}")]
    Listing(#[from] ListingError),
    #[error("this offer was cancelled or already filled")]
    Closed,
    #[error("too many open offers for this account (at most {MAX_PER_MAKER})")]
    MakerLimit,
    #[error("the relay is full; try again later")]
    Full,
    #[error("the maker's account does not hold enough to back this offer")]
    Unfunded,
    #[error("no such offer")]
    NotFound,
    #[error("chain {0} is not served here")]
    Chain(u64),
    #[error("not a claim on the pinned contract: {0}")]
    NotAClaim(String),
    #[error("the claimed swap does not come from an offer this relay knows")]
    UnknownListing,
    #[error("a letter must be a sealed message of at most 512 KB to a mailbox")]
    BadMail,
    #[error("the chain could not be read: {0}")]
    ChainRead(String),
}

/// What the chain says about a reported claim transaction
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClaimLookup {
    /// Unknown to the node yet, or not yet final
    Pending,
    Final { swap: Swap, at: u64 },
    NotAClaim(String),
}

/// Read-only chain access, one per chain id
pub trait ChainView: Send + Sync {
    fn balance(&self, account: Address) -> impl std::future::Future<Output = Result<U256, String>> + Send;
    fn claim(&self, tx: B256) -> impl std::future::Future<Output = Result<ClaimLookup, String>> + Send;
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Print {
    pub pair: String,
    pub chain_id: u64,
    pub tx: B256,
    /// Block time of the claim
    pub at: u64,
    /// Coin amount (wei), from the chain
    pub value: U256,
    /// XMZ atoms, from the maker-signed offer
    #[serde(with = "u64_string")]
    pub xmz: u64,
    pub listing: B256,
}

impl Print {
    /// Coin per XMZ
    pub fn price(&self) -> f64 {
        let coin = self.value.to_string().parse::<f64>().unwrap_or(0.0) / 1e18;
        coin / (self.xmz as f64 / 1e11)
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Candle {
    /// Bucket start, unix seconds
    pub t: u64,
    pub o: f64,
    pub h: f64,
    pub l: f64,
    pub c: f64,
    /// XMZ volume
    pub v: f64,
    /// Coin volume
    pub vc: f64,
    /// Number of trades
    pub n: u32,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    pub active: Vec<SignedListing>,
    pub archive: Vec<SignedListing>,
    /// id -> time it closed (cancelled or filled)
    pub closed: BTreeMap<B256, u64>,
    pub prints: Vec<Print>,
    /// (chain id, tx) -> first reported
    pub pending: Vec<(u64, B256, u64)>,
    #[serde(default)]
    pub btc_active: Vec<btc::SignedBtcListing>,
    #[serde(default)]
    pub btc_closed: Vec<(B256, u64)>,
}

#[derive(Default)]
struct State {
    active: HashMap<B256, SignedListing>,
    archive: HashMap<B256, SignedListing>,
    closed: HashMap<B256, u64>,
    prints: Vec<Print>,
    print_txs: HashSet<B256>,
    pending: HashMap<(u64, B256), u64>,
    /// mailbox -> (received at, sealed letter)
    mail: HashMap<B256, Vec<(u64, Bytes)>>,
    mail_bytes: usize,
}

pub struct Relay<C> {
    rules: Rules,
    chains: BTreeMap<u64, C>,
    state: Mutex<State>,
    /// BTC/XMZ offers, for the networks this relay serves ("btc-mainnet", ...)
    btc_networks: std::collections::BTreeSet<String>,
    btc: Mutex<btc::BtcBook>,
}

#[derive(Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum PrintStatus {
    Recorded,
    Pending,
}

impl<C: ChainView> Relay<C> {
    pub fn new(rules: Rules, chains: BTreeMap<u64, C>) -> Self {
        Relay { rules, chains, state: Mutex::default(), btc_networks: Default::default(), btc: Mutex::default() }
    }

    pub fn with_btc_networks(mut self, networks: impl IntoIterator<Item = String>) -> Self {
        self.btc_networks = networks.into_iter().collect();
        self
    }

    pub fn btc_networks(&self) -> &std::collections::BTreeSet<String> {
        &self.btc_networks
    }

    fn btc_book(&self) -> std::sync::MutexGuard<'_, btc::BtcBook> {
        self.btc.lock().unwrap_or_else(|e| e.into_inner())
    }

    pub fn post_btc(&self, signed: btc::SignedBtcListing, now: u64) -> Result<bool, btc::BtcError> {
        self.btc_book().post(signed, &self.btc_networks, now)
    }

    pub fn cancel_btc(&self, signed: &btc::SignedBtcCancel, now: u64) -> Result<(), btc::BtcError> {
        self.btc_book().cancel(signed, now)
    }

    pub fn btc_listings(&self, now: u64) -> Vec<btc::SignedBtcListing> {
        self.btc_book().listings(now)
    }

    pub fn btc_listing(&self, id: B256) -> Option<btc::SignedBtcListing> {
        self.btc_book().get(id)
    }

    pub fn rules(&self) -> &Rules {
        &self.rules
    }

    fn state(&self) -> std::sync::MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn chain(&self, id: u64) -> Result<&C, RelayError> {
        self.chains.get(&id).ok_or(RelayError::Chain(id))
    }

    /// Accepts a signed offer. Idempotent for the same offer.
    pub async fn post(&self, signed: SignedListing, now: u64) -> Result<bool, RelayError> {
        signed.verify(&self.rules, now)?;
        let l = &signed.listing;
        {
            let s = self.state();
            if s.closed.contains_key(&l.id) {
                return Err(RelayError::Closed);
            }
            if let Some(known) = s.active.get(&l.id) {
                return if known == &signed { Ok(false) } else { Err(RelayError::Closed) };
            }
            if s.active.values().filter(|o| o.listing.maker == l.maker && o.listing.expires_at > now).count() >= MAX_PER_MAKER {
                return Err(RelayError::MakerLimit);
            }
            if s.active.len() >= MAX_LISTINGS {
                return Err(RelayError::Full);
            }
        }
        // Spam guard: the maker's account backs the offer (the coin for a buyer, gas for a seller)
        let balance = self.chain(l.chain_id)?.balance(l.maker).await.map_err(RelayError::ChainRead)?;
        let backed = match l.maker_role() {
            Role::Owner => balance >= l.value,
            Role::Claimer => !balance.is_zero(),
        };
        if !backed {
            return Err(RelayError::Unfunded);
        }
        let mut s = self.state();
        if s.closed.contains_key(&l.id) || s.active.contains_key(&l.id) {
            return Ok(false);
        }
        s.active.insert(l.id, signed);
        Ok(true)
    }

    pub fn cancel(&self, signed: &SignedCancel, now: u64) -> Result<(), RelayError> {
        signed.verify()?;
        let mut s = self.state();
        let listing = s.active.get(&signed.cancel.id).ok_or(RelayError::NotFound)?;
        if listing.listing.maker != signed.cancel.maker {
            return Err(RelayError::NotFound);
        }
        let listing = s.active.remove(&signed.cancel.id).expect("present");
        s.closed.insert(listing.listing.id, now);
        s.archive.insert(listing.listing.id, listing);
        Ok(())
    }

    /// Open offers, lowest price (coin per XMZ) first
    pub fn listings(&self, pair: Option<&str>, now: u64) -> Vec<SignedListing> {
        let s = self.state();
        let mut out: Vec<_> = s.active.values().filter(|o| o.listing.expires_at > now && pair.is_none_or(|p| o.listing.pair == p)).cloned().collect();
        // a.value / a.xmz against b.value / b.xmz, without rounding
        let key = |o: &SignedListing, other: &SignedListing| o.listing.value.saturating_mul(U256::from(other.listing.xmz));
        out.sort_by(|a, b| key(a, b).cmp(&key(b, a)).then(a.listing.id.cmp(&b.listing.id)));
        out
    }

    /// An open or recently closed offer
    pub fn listing(&self, id: B256) -> Option<SignedListing> {
        let s = self.state();
        s.active.get(&id).or_else(|| s.archive.get(&id)).cloned()
    }

    /// Adds a closed offer learned from a peer relay, so its trades can be matched
    pub fn remember(&self, signed: SignedListing) -> Result<(), RelayError> {
        // Its time window has passed; check everything else as of when it was made
        signed.verify(&self.rules, signed.listing.created_at)?;
        let mut s = self.state();
        if !s.active.contains_key(&signed.listing.id) {
            s.archive.entry(signed.listing.id).or_insert(signed);
        }
        Ok(())
    }

    pub fn prints(&self, pair: Option<&str>, since: u64) -> Vec<Print> {
        let s = self.state();
        let mut out: Vec<_> = s.prints.iter().filter(|p| p.at >= since && pair.is_none_or(|x| p.pair == x)).cloned().collect();
        out.sort_by(|a, b| b.at.cmp(&a.at).then(a.tx.cmp(&b.tx)));
        out
    }

    pub fn known_print(&self, tx: B256) -> bool {
        self.state().print_txs.contains(&tx)
    }

    /// A reported claim: recorded once final on chain, kept pending until then
    pub async fn report(&self, chain_id: u64, tx: B256, now: u64) -> Result<PrintStatus, RelayError> {
        if self.known_print(tx) {
            return Ok(PrintStatus::Recorded);
        }
        let lookup = self.chain(chain_id)?.claim(tx).await.map_err(RelayError::ChainRead)?;
        match lookup {
            ClaimLookup::NotAClaim(why) => {
                self.state().pending.remove(&(chain_id, tx));
                Err(RelayError::NotAClaim(why))
            }
            ClaimLookup::Pending => {
                let mut s = self.state();
                if !s.pending.contains_key(&(chain_id, tx)) && s.pending.len() >= MAX_PENDING {
                    return Err(RelayError::Full);
                }
                s.pending.entry((chain_id, tx)).or_insert(now);
                Ok(PrintStatus::Pending)
            }
            ClaimLookup::Final { swap, at } => {
                let mut s = self.state();
                s.pending.remove(&(chain_id, tx));
                // A trade's id is its listing's id; the swap nonce is the id's first 8 bytes
                let listing = s
                    .active
                    .values()
                    .chain(s.archive.values())
                    .find(|o| o.listing.swap_nonce() == swap.nonce)
                    .cloned()
                    .ok_or(RelayError::UnknownListing)?;
                let id = listing.listing.id;
                let l = &listing.listing;
                let maker_ok = match l.maker_role() {
                    Role::Owner => swap.owner == l.maker,
                    Role::Claimer => swap.claimer == l.maker,
                };
                if l.chain_id != chain_id || swap.asset != Address::ZERO || swap.value != l.value || !maker_ok {
                    return Err(RelayError::UnknownListing);
                }
                if s.print_txs.insert(tx) {
                    s.prints.push(Print { pair: l.pair.clone(), chain_id, tx, at, value: swap.value, xmz: l.xmz, listing: id });
                }
                // One fill per offer
                if let Some(filled) = s.active.remove(&id) {
                    s.archive.insert(id, filled);
                }
                s.closed.entry(id).or_insert(now);
                Ok(PrintStatus::Recorded)
            }
        }
    }

    /// Checks every pending claim again
    pub async fn recheck(&self, now: u64) {
        let pending: Vec<_> = self.state().pending.keys().copied().collect();
        for (chain, tx) in pending {
            let _ = self.report(chain, tx, now).await;
        }
    }

    /// Leaves a sealed letter for a mailbox. Idempotent for the same letter.
    pub fn post_mail(&self, to: B256, sealed: Bytes, now: u64) -> Result<(), RelayError> {
        if sealed.len() > MAX_SEALED || sealed.len() < 48 || to == B256::ZERO {
            return Err(RelayError::BadMail);
        }
        let mut s = self.state();
        if s.mail.get(&to).is_some_and(|b| b.iter().any(|(_, m)| m == &sealed)) {
            return Ok(());
        }
        if s.mail.get(&to).is_some_and(|b| b.len() >= MAIL_PER_BOX) || s.mail_bytes + sealed.len() > MAIL_TOTAL {
            return Err(RelayError::Full);
        }
        s.mail_bytes += sealed.len();
        s.mail.entry(to).or_default().push((now, sealed));
        Ok(())
    }

    /// Letters for a mailbox received at or after `since`, oldest first
    pub fn mail(&self, to: B256, since: u64) -> Vec<(u64, Bytes)> {
        self.state().mail.get(&to).map(|b| b.iter().filter(|(at, _)| *at >= since).cloned().collect()).unwrap_or_default()
    }

    /// Closes expired offers and forgets old ones
    pub fn prune(&self, now: u64) {
        let mut s = self.state();
        let mut freed = 0;
        for b in s.mail.values_mut() {
            b.retain(|(at, m)| {
                let keep = *at + MAIL_SECS > now;
                if !keep {
                    freed += m.len();
                }
                keep
            });
        }
        s.mail.retain(|_, b| !b.is_empty());
        s.mail_bytes -= freed;
        drop(s);
        self.btc_book().prune(now);
        let mut s = self.state();
        let expired: Vec<_> = s.active.iter().filter(|(_, o)| o.listing.expires_at <= now).map(|(id, _)| *id).collect();
        for id in expired {
            let o = s.active.remove(&id).expect("present");
            s.archive.insert(id, o);
        }
        s.archive.retain(|_, o| o.listing.expires_at + ARCHIVE_SECS > now);
        s.closed.retain(|_, at| *at + ARCHIVE_SECS > now);
        s.pending.retain(|_, at| *at + PENDING_SECS > now);
    }

    pub fn candles(&self, pair: &str, interval: u64, since: u64) -> Vec<Candle> {
        let s = self.state();
        let mut prints: Vec<_> = s.prints.iter().filter(|p| p.pair == pair && p.at >= since).collect();
        prints.sort_by(|a, b| a.at.cmp(&b.at).then(a.tx.cmp(&b.tx)));
        let mut out: Vec<Candle> = Vec::new();
        for p in prints {
            let (t, price) = (p.at - p.at % interval, p.price());
            let (v, vc) = (p.xmz as f64 / 1e11, price * p.xmz as f64 / 1e11);
            match out.last_mut() {
                Some(c) if c.t == t => {
                    c.h = c.h.max(price);
                    c.l = c.l.min(price);
                    c.c = price;
                    c.v += v;
                    c.vc += vc;
                    c.n += 1;
                }
                _ => out.push(Candle { t, o: price, h: price, l: price, c: price, v, vc, n: 1 }),
            }
        }
        out
    }

    pub fn snapshot(&self) -> Snapshot {
        // One lock of the BTC book at a time (a guard lives to the end of its expression)
        let (btc_active, btc_closed) = self.btc_book().snapshot();
        let s = self.state();
        Snapshot {
            active: s.active.values().cloned().collect(),
            archive: s.archive.values().cloned().collect(),
            closed: s.closed.iter().map(|(k, v)| (*k, *v)).collect(),
            prints: s.prints.clone(),
            pending: s.pending.iter().map(|((c, t), at)| (*c, *t, *at)).collect(),
            btc_active,
            btc_closed,
        }
    }

    /// Loads a saved snapshot, checking every signed offer again
    pub fn restore(&self, snap: Snapshot) {
        let mut s = self.state();
        for o in snap.active {
            if o.verify(&self.rules, o.listing.created_at).is_ok() {
                s.active.insert(o.listing.id, o);
            }
        }
        for o in snap.archive {
            if o.verify(&self.rules, o.listing.created_at).is_ok() {
                s.archive.insert(o.listing.id, o);
            }
        }
        s.closed.extend(snap.closed);
        for p in snap.prints {
            if s.print_txs.insert(p.tx) {
                s.prints.push(p);
            }
        }
        s.pending.extend(snap.pending.into_iter().map(|(c, t, at)| ((c, t), at)));
        drop(s);
        self.btc_book().restore(snap.btc_active, snap.btc_closed, &self.btc_networks);
    }
}

mod u64_string {
    use serde::{de::Error, Deserialize, Deserializer, Serializer};

    pub fn serialize<S: Serializer>(v: &u64, s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&v.to_string())
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<u64, D::Error> {
        String::deserialize(d)?.parse().map_err(D::Error::custom)
    }
}

#[cfg(test)]
mod tests;
