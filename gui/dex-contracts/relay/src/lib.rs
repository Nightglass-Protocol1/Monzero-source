//! An open relay for Monzero DEX offers and completed trades.
//!
//! It holds no funds and no keys. Offers are kept only if their maker signed them and they match
//! the pinned contract for their chain; a completed trade (a "print") is kept only once the relay
//! has read the claim on chain itself and matched the swap's nonce to an offer. See
//! `../PUBLIC-OPENING.md`.

pub mod utxo;
pub mod chain;
pub mod http;

use alloy::primitives::{Address, Bytes, B256, U256};
use monzero_dex_evm::mail::MAX_SEALED;
use monzero_dex_evm::listing::{ListingError, Rules, SignedCancel, SignedListing, SignedSolAccount};
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
    #[error("an offer on Solana needs the maker's signed Solana account next to it")]
    SolAccount,
    #[error("not a transaction id of this chain")]
    BadTx,
    #[error("the chain could not be read: {0}")]
    ChainRead(String),
}

/// A transaction's id as its chain shows it: `0x` and 64 hex digits (EVM), or a Solana signature
/// in base58. Kept as that text, so trades of every chain share one record.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(try_from = "String", into = "String")]
pub struct TxId(String);

impl TxId {
    pub fn evm(&self) -> Option<B256> {
        (self.0.len() == 66 && self.0.starts_with("0x")).then(|| self.0.parse().ok()).flatten()
    }

    pub fn solana(&self) -> Option<[u8; 64]> {
        if self.0.len() < 64 || self.0.len() > 90 {
            return None;
        }
        bs58::decode(&self.0).into_vec().ok()?.try_into().ok()
    }
}

impl TryFrom<String> for TxId {
    type Error = String;
    fn try_from(text: String) -> Result<Self, String> {
        let id = TxId(text);
        match (id.evm(), id.solana()) {
            // One form per hash, however it was typed
            (Some(hash), _) => Ok(TxId(hash.to_string())),
            (None, Some(_)) => Ok(id),
            _ => Err("not a transaction id".into()),
        }
    }
}

impl From<TxId> for String {
    fn from(id: TxId) -> String {
        id.0
    }
}

impl From<B256> for TxId {
    fn from(hash: B256) -> Self {
        TxId(hash.to_string())
    }
}

impl std::fmt::Display for TxId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// What the chain says about a reported claim transaction
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClaimLookup {
    /// Unknown to the node yet, or not yet final
    Pending,
    Final { swap: Swap, at: u64 },
    /// As `Final`, on Solana: the traders are Solana accounts and the amount is in lamports
    FinalSol { owner: B256, claimer: B256, lamports: u64, nonce: u64, at: u64 },
    NotAClaim(String),
}

/// Read-only chain access, one per chain id
pub trait ChainView: Send + Sync {
    fn balance(&self, account: Address) -> impl std::future::Future<Output = Result<U256, String>> + Send;
    fn claim(&self, tx: &TxId) -> impl std::future::Future<Output = Result<ClaimLookup, String>> + Send;
    /// A Solana cluster: offers there come with the maker's signed Solana account
    fn solana(&self) -> bool {
        false
    }
    /// Lamports a Solana account holds
    fn sol_balance(&self, _account: B256) -> impl std::future::Future<Output = Result<U256, String>> + Send {
        async { Err("not a Solana chain".to_string()) }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Print {
    pub pair: String,
    pub chain_id: u64,
    pub tx: TxId,
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
        // The coin's smallest unit: wei (18 places), or lamports on Solana (9)
        let unit = if monzero_dex_evm::Chain::from_id(self.chain_id).is_some_and(|c| c.is_solana()) { 1e9 } else { 1e18 };
        let coin = self.value.to_string().parse::<f64>().unwrap_or(0.0) / unit;
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
    pub pending: Vec<(u64, TxId, u64)>,
    /// Makers' signed Solana accounts, for the offers on Solana
    #[serde(default)]
    pub sol_accounts: Vec<SignedSolAccount>,
    #[serde(default)]
    pub utxo_active: Vec<utxo::SignedUtxoListing>,
    #[serde(default)]
    pub utxo_closed: Vec<(B256, u64)>,
}

#[derive(Default)]
struct State {
    active: HashMap<B256, SignedListing>,
    archive: HashMap<B256, SignedListing>,
    closed: HashMap<B256, u64>,
    prints: Vec<Print>,
    print_txs: HashSet<TxId>,
    pending: HashMap<(u64, TxId), u64>,
    /// listing id -> the maker's signed Solana account
    sol_accounts: HashMap<B256, SignedSolAccount>,
    /// mailbox -> (received at, sealed letter)
    mail: HashMap<B256, Vec<(u64, Bytes)>>,
    mail_bytes: usize,
}

pub struct Relay<C> {
    rules: Rules,
    chains: BTreeMap<u64, C>,
    state: Mutex<State>,
    /// BTC/XMZ and LTC/XMZ offers, for the networks this relay serves ("btc-mainnet", "ltc-mainnet", ...)
    utxo_networks: std::collections::BTreeSet<String>,
    utxo: Mutex<utxo::UtxoBook>,
}

#[derive(Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum PrintStatus {
    Recorded,
    Pending,
}

impl<C: ChainView> Relay<C> {
    pub fn new(rules: Rules, chains: BTreeMap<u64, C>) -> Self {
        Relay { rules, chains, state: Mutex::default(), utxo_networks: Default::default(), utxo: Mutex::default() }
    }

    pub fn with_utxo_networks(mut self, networks: impl IntoIterator<Item = String>) -> Self {
        self.utxo_networks = networks.into_iter().collect();
        self
    }

    pub fn utxo_networks(&self) -> &std::collections::BTreeSet<String> {
        &self.utxo_networks
    }

    fn utxo_book(&self) -> std::sync::MutexGuard<'_, utxo::UtxoBook> {
        self.utxo.lock().unwrap_or_else(|e| e.into_inner())
    }

    pub fn post_utxo(&self, signed: utxo::SignedUtxoListing, now: u64) -> Result<bool, utxo::UtxoError> {
        self.utxo_book().post(signed, &self.utxo_networks, now)
    }

    pub fn cancel_utxo(&self, signed: &utxo::SignedUtxoCancel, now: u64) -> Result<(), utxo::UtxoError> {
        self.utxo_book().cancel(signed, now)
    }

    pub fn utxo_listings(&self, now: u64) -> Vec<utxo::SignedUtxoListing> {
        self.utxo_book().listings(now)
    }

    pub fn utxo_listing(&self, id: B256) -> Option<utxo::SignedUtxoListing> {
        self.utxo_book().get(id)
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
        self.post_with(signed, None, now).await
    }

    /// As `post`; `account` is the maker's signed Solana account, which an offer on Solana needs
    pub async fn post_with(&self, signed: SignedListing, account: Option<SignedSolAccount>, now: u64) -> Result<bool, RelayError> {
        signed.verify(&self.rules, now)?;
        let l = &signed.listing;
        let chain = self.chain(l.chain_id)?;
        let account = match (chain.solana(), account) {
            (true, Some(a)) => {
                a.verify(l)?;
                Some(a)
            }
            (true, None) => return Err(RelayError::SolAccount),
            (false, _) => None,
        };
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
        let balance = match &account {
            Some(a) => chain.sol_balance(a.note.account).await,
            None => chain.balance(l.maker).await,
        }
        .map_err(RelayError::ChainRead)?;
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
        if let Some(a) = account {
            s.sol_accounts.insert(l.id, a);
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
    pub fn remember(&self, signed: SignedListing, account: Option<SignedSolAccount>) -> Result<(), RelayError> {
        // Its time window has passed; check everything else as of when it was made
        signed.verify(&self.rules, signed.listing.created_at)?;
        if let Some(a) = &account {
            a.verify(&signed.listing)?;
        }
        let mut s = self.state();
        if let Some(a) = account {
            s.sol_accounts.entry(signed.listing.id).or_insert(a);
        }
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

    pub fn known_print(&self, tx: &TxId) -> bool {
        self.state().print_txs.contains(tx)
    }

    /// The signed Solana accounts of the offers held (open or recently closed)
    pub fn sol_accounts(&self) -> Vec<SignedSolAccount> {
        self.state().sol_accounts.values().cloned().collect()
    }

    pub fn sol_account(&self, id: B256) -> Option<SignedSolAccount> {
        self.state().sol_accounts.get(&id).cloned()
    }

    /// A reported claim: recorded once final on chain, kept pending until then
    pub async fn report(&self, chain_id: u64, tx: TxId, now: u64) -> Result<PrintStatus, RelayError> {
        if self.known_print(&tx) {
            return Ok(PrintStatus::Recorded);
        }
        let lookup = self.chain(chain_id)?.claim(&tx).await.map_err(RelayError::ChainRead)?;
        // What the claim paid out, in the offer's terms: (nonce, amount, the maker's side matches)
        type Seen<'a> = (U256, U256, u64, Box<dyn Fn(&SignedListing, Option<&SignedSolAccount>) -> bool + Send + 'a>);
        let (nonce, value, at, maker_ok): Seen = match lookup {
            ClaimLookup::NotAClaim(why) => {
                self.state().pending.remove(&(chain_id, tx));
                return Err(RelayError::NotAClaim(why));
            }
            ClaimLookup::Pending => {
                let mut s = self.state();
                if !s.pending.contains_key(&(chain_id, tx.clone())) && s.pending.len() >= MAX_PENDING {
                    return Err(RelayError::Full);
                }
                s.pending.entry((chain_id, tx)).or_insert(now);
                return Ok(PrintStatus::Pending);
            }
            ClaimLookup::Final { swap, at } => {
                let native = swap.asset == Address::ZERO;
                let (owner, claimer) = (swap.owner, swap.claimer);
                (swap.nonce, swap.value, at, Box::new(move |o, _| {
                    native
                        && match o.listing.maker_role() {
                            Role::Owner => owner == o.listing.maker,
                            Role::Claimer => claimer == o.listing.maker,
                        }
                }))
            }
            // On Solana the maker's side is the account its signed note names
            ClaimLookup::FinalSol { owner, claimer, lamports, nonce, at } => (U256::from(nonce), U256::from(lamports), at, Box::new(move |o, account| {
                account.is_some_and(|a| match o.listing.maker_role() {
                    Role::Owner => owner == a.note.account,
                    Role::Claimer => claimer == a.note.account,
                })
            })),
        };
        let mut s = self.state();
        s.pending.remove(&(chain_id, tx.clone()));
        // A trade's id is its listing's id; the swap nonce is the id's first 8 bytes
        let listing = s.active.values().chain(s.archive.values()).find(|o| o.listing.swap_nonce() == nonce).cloned().ok_or(RelayError::UnknownListing)?;
        let id = listing.listing.id;
        let l = &listing.listing;
        if l.chain_id != chain_id || value != l.value || !maker_ok(&listing, s.sol_accounts.get(&id)) {
            return Err(RelayError::UnknownListing);
        }
        // One trade per offer: a second claim under the same offer is not another trade
        if s.print_txs.insert(tx.clone()) && !s.prints.iter().any(|p| p.listing == id) {
            s.prints.push(Print { pair: l.pair.clone(), chain_id, tx, at, value, xmz: l.xmz, listing: id });
        }
        if let Some(filled) = s.active.remove(&id) {
            s.archive.insert(id, filled);
        }
        s.closed.entry(id).or_insert(now);
        Ok(PrintStatus::Recorded)
    }

    /// Checks every pending claim again
    pub async fn recheck(&self, now: u64) {
        let pending: Vec<_> = self.state().pending.keys().cloned().collect();
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
        self.utxo_book().prune(now);
        let mut s = self.state();
        let expired: Vec<_> = s.active.iter().filter(|(_, o)| o.listing.expires_at <= now).map(|(id, _)| *id).collect();
        for id in expired {
            let o = s.active.remove(&id).expect("present");
            s.archive.insert(id, o);
        }
        s.archive.retain(|_, o| o.listing.expires_at + ARCHIVE_SECS > now);
        let State { active, archive, sol_accounts, .. } = &mut *s;
        sol_accounts.retain(|id, _| active.contains_key(id) || archive.contains_key(id));
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
        let (utxo_active, utxo_closed) = self.utxo_book().snapshot();
        let s = self.state();
        Snapshot {
            active: s.active.values().cloned().collect(),
            archive: s.archive.values().cloned().collect(),
            closed: s.closed.iter().map(|(k, v)| (*k, *v)).collect(),
            prints: s.prints.clone(),
            pending: s.pending.iter().map(|((c, t), at)| (*c, t.clone(), *at)).collect(),
            sol_accounts: s.sol_accounts.values().cloned().collect(),
            utxo_active,
            utxo_closed,
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
        for a in snap.sol_accounts {
            let known = s.active.get(&a.note.id).or_else(|| s.archive.get(&a.note.id));
            if known.is_some_and(|o| a.verify(&o.listing).is_ok()) {
                s.sol_accounts.insert(a.note.id, a);
            }
        }
        for p in snap.prints {
            if s.print_txs.insert(p.tx.clone()) {
                s.prints.push(p);
            }
        }
        s.pending.extend(snap.pending.into_iter().map(|(c, t, at)| ((c, t), at)));
        drop(s);
        self.utxo_book().restore(snap.utxo_active, snap.utxo_closed, &self.utxo_networks);
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
