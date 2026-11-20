//! Taking a public offer: both traders build the same trade from the signed listing and the
//! taker's account, then run the usual key exchange through sealed relay mail (`mail`).
//!
//! Everything about the trade follows from the listing: its id is the trade id (so the swap
//! nonce ties a claim to the listing), and timeouts are fixed per chain. A listing is filled
//! once: the maker answers only the first taker whose frame passes every check, and answers
//! that taker again after a restart (frames are the same bytes from the swap seed).
//!
//! A partial listing (`minXmz`) is still taken once, but for the amount the taker names; the
//! coin follows at the listing's price ([`Listing::fill_value`]). The maker then offers what is
//! left as a new listing (`helper_market::serve`), so every fill keeps its own trade id.

use crate::exchange::{ExchangeError, SavedTrade, Session};
use crate::listing::{Listing, SignedListing};
use crate::machine::Timing;
use crate::mail::{Answer, Take};
use crate::negotiation::{Offer, Role};
use crate::swap_seed::SwapSeed;
use crate::Chain;
use crate::exchange::KeyResponse;
use alloy::primitives::Address;
use alloy::signers::local::PrivateKeySigner;

/// Both timeouts of a trade from a listing, as in the mainnet tests (two hours each)
pub const DURATION: u64 = 2 * 3600;

pub fn timing(chain: Chain) -> Timing {
    match chain {
        Chain::EthMainnet | Chain::EthSepolia => Timing::ETH,
        _ => Timing::BSC,
    }
}

/// How long the claimer waits for the owner's lock after agreeing
pub fn lock_wait(chain: Chain) -> u64 {
    match chain {
        Chain::EthMainnet | Chain::EthSepolia => 3600,
        Chain::Local => 600,
        _ => 1800,
    }
}

/// The trade a listing becomes when `taker` takes `xmz` atoms of it (all of it for a whole offer)
pub fn offer_for(listing: &Listing, taker: Address, xmz: u64) -> Offer {
    let (owner, claimer) = match listing.maker_role() {
        Role::Claimer => (taker, listing.maker),
        Role::Owner => (listing.maker, taker),
    };
    Offer {
        trade_id: listing.id,
        chain_id: listing.chain_id,
        contract: listing.contract,
        owner,
        claimer,
        asset: listing.asset(),
        value: listing.fill_value(xmz),
        duration1: DURATION,
        duration2: DURATION,
        xmz_amount: xmz,
        xmz_network: listing.xmz_network,
        expires_at: listing.expires_at,
    }
}

fn chain_of(listing: &Listing) -> Result<Chain, ExchangeError> {
    Chain::from_id(listing.chain_id).ok_or(ExchangeError::Refused(format!("unknown chain {}", listing.chain_id)))
}

/// Where the chains stood when the keys were agreed (the saved trade needs them)
#[derive(Clone, Copy, Debug)]
pub struct Heights {
    pub now: u64,
    pub evm_from_block: u64,
    pub xmz_restore_height: u64,
}

/// The maker's side of one listing
pub struct Maker {
    pub listing: SignedListing,
    /// The taker this listing went to, once one was answered
    pub taken_by: Option<Address>,
    /// The XMZ (atoms) that taker took
    pub taken_xmz: Option<u64>,
}

impl Maker {
    pub fn new(listing: SignedListing, taken_by: Option<Address>, taken_xmz: Option<u64>) -> Self {
        Maker { listing, taken_by, taken_xmz }
    }

    /// Answers a take: our frame once the taker's passes, else a refusal. The saved trade comes
    /// with our frame: write it to the journal before sending the answer.
    pub fn answer(&mut self, take: &Take, seed: &SwapSeed, key: &PrivateKeySigner, at: Heights) -> (Answer, Option<SavedTrade>) {
        let l = &self.listing.listing;
        let refuse = |why: &str| (Answer { listing: l.id, response: KeyResponse::Refused(why.into()) }, None);
        if take.listing != l.id {
            return refuse("unknown listing");
        }
        if take.taker == l.maker || take.taker == Address::ZERO {
            return refuse("the taker must be another account");
        }
        // A whole offer is taken whole; a partial one for any amount its rules allow
        let xmz = take.xmz.unwrap_or(l.xmz);
        // A whole offer only for all of it (an app may name that amount explicitly)
        if take.xmz.is_some_and(|x| x != l.xmz) && !l.is_partial() || !l.fill_ok(xmz) {
            return refuse("this amount cannot be taken from this offer");
        }
        if self.taken_by.is_some_and(|t| t != take.taker) || self.taken_xmz.is_some_and(|x| x != xmz) {
            return refuse("this offer was already taken");
        }
        if at.now >= l.expires_at {
            return refuse("this offer has expired");
        }
        let Ok(chain) = chain_of(l) else { return refuse("unknown chain") };
        let mut session = match Session::start(seed, offer_for(l, take.taker, xmz), l.maker_role(), key, at.now) {
            Ok(s) => s,
            Err(e) => return refuse(&e.to_string()),
        };
        let response = session.respond(&take.request, at.now);
        let saved = match response {
            KeyResponse::Frame(_) => session.saved(timing(chain), at.now + lock_wait(chain), at.evm_from_block, at.xmz_restore_height),
            KeyResponse::Refused(_) => None,
        };
        if saved.is_some() {
            self.taken_by = Some(take.taker);
            self.taken_xmz = Some(xmz);
        }
        (Answer { listing: l.id, response }, saved)
    }
}

/// The taker's side: our request, then the trade once the maker's answer passes
pub struct Taker {
    session: Session,
    listing: Listing,
}

impl Taker {
    /// Our take of `xmz` atoms (None: all of it, the only choice on a whole offer)
    pub fn start(listing: &Listing, xmz: Option<u64>, seed: &SwapSeed, key: &PrivateKeySigner, now: u64) -> Result<(Taker, Take), ExchangeError> {
        let role = match listing.maker_role() {
            Role::Owner => Role::Claimer,
            Role::Claimer => Role::Owner,
        };
        let amount = xmz.unwrap_or(listing.xmz);
        if xmz.is_some() && !listing.is_partial() || !listing.fill_ok(amount) {
            return Err(ExchangeError::Refused("this amount cannot be taken from this offer".into()));
        }
        // All of a partial offer is sent as an amount too: the maker then knows we chose it
        let xmz = listing.is_partial().then_some(amount);
        let session = Session::start(seed, offer_for(listing, key.address(), amount), role, key, now)?;
        let take = Take { listing: listing.id, taker: key.address(), reply_to: seed.mailbox(listing.id, false).address(), request: session.request(), xmz, xmz_sale: None };
        Ok((Taker { session, listing: listing.clone() }, take))
    }

    pub fn on_answer(&mut self, answer: &Answer, at: Heights) -> Result<SavedTrade, ExchangeError> {
        if answer.listing != self.listing.id {
            return Err(ExchangeError::Refused("an answer for another listing".into()));
        }
        self.session.on_response(&answer.response, at.now)?;
        let chain = chain_of(&self.listing)?;
        self.session
            .saved(timing(chain), at.now + lock_wait(chain), at.evm_from_block, at.xmz_restore_height)
            .ok_or(ExchangeError::Refused("no agreement".into()))
    }
}

#[cfg(test)]
#[path = "market_tests.rs"]
mod tests;
