//! Taking a public offer: both traders build the same trade from the signed listing and the
//! taker's account, then run the usual key exchange through sealed relay mail (`mail`).
//!
//! Everything about the trade follows from the listing: its id is the trade id (so the swap
//! nonce ties a claim to the listing), and timeouts are fixed per chain. A listing is filled
//! once: the maker answers only the first taker whose frame passes every check, and answers
//! that taker again after a restart (frames are the same bytes from the swap seed).

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

/// The trade a listing becomes when `taker` takes it
pub fn offer_for(listing: &Listing, taker: Address) -> Offer {
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
        asset: Address::ZERO,
        value: listing.value,
        duration1: DURATION,
        duration2: DURATION,
        xmz_amount: listing.xmz,
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
}

impl Maker {
    pub fn new(listing: SignedListing, taken_by: Option<Address>) -> Self {
        Maker { listing, taken_by }
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
        if self.taken_by.is_some_and(|t| t != take.taker) {
            return refuse("this offer was already taken");
        }
        if at.now >= l.expires_at {
            return refuse("this offer has expired");
        }
        let Ok(chain) = chain_of(l) else { return refuse("unknown chain") };
        let mut session = match Session::start(seed, offer_for(l, take.taker), l.maker_role(), key, at.now) {
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
    pub fn start(listing: &Listing, seed: &SwapSeed, key: &PrivateKeySigner, now: u64) -> Result<(Taker, Take), ExchangeError> {
        let role = match listing.maker_role() {
            Role::Owner => Role::Claimer,
            Role::Claimer => Role::Owner,
        };
        let session = Session::start(seed, offer_for(listing, key.address()), role, key, now)?;
        let take = Take { listing: listing.id, taker: key.address(), reply_to: seed.mailbox(listing.id, false).address(), request: session.request() };
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
