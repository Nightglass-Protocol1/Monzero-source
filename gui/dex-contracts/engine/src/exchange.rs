//! The key exchange between the two traders of one offer, over a request/response transport
//! (libp2p in the app, see [`p2p`]).
//!
//! One round trip: the trader who starts sends its signed frame; the other answers with its
//! own frame only after the request's frame passed every check of [`KeyExchange::accept`]
//! (signed by the offer's account for that role, valid cross-curve proof, ...). A refusal
//! never carries our frame, so our private view share only goes to the account the offer
//! names.
//!
//! Frames come from the [`SwapSeed`], so they are the same bytes after a restart: either
//! trader can simply ask or answer again until the offer expires. Once agreed, the app keeps
//! the [`Agreed`] terms in the swap journal; the exchange is not needed after that.

use crate::machine::{Timing, Trade};
use crate::negotiation::{Agreed, KeyExchange, NegotiationError, Offer, Role, SignedFrame};
use crate::secret::SecretShare;
use crate::swap_seed::SwapSeed;
use alloy::primitives::{B256, U256};
use alloy::signers::local::PrivateKeySigner;
use serde::{Deserialize, Serialize};

pub const PROTOCOL: &str = "/monzero/xmz-evm/key-exchange/1.0.0";

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct KeyRequest {
    pub trade_id: B256,
    pub frame: SignedFrame,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum KeyResponse {
    Frame(SignedFrame),
    /// Why the request was refused (for the log; never contains a frame)
    Refused(String),
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum ExchangeError {
    #[error(transparent)]
    Negotiation(#[from] NegotiationError),
    #[error("the other trader refused: {0}")]
    Refused(String),
    #[error("the other trader answered with a frame for our own role")]
    Reflected,
    #[error("our account is not the {0:?} of this offer")]
    NotOurAccount(Role),
    #[error("this seed and account do not rebuild the saved trade")]
    NotThisTrade,
}

/// Our side of the exchange for one offer.
pub struct Session {
    trade_id: B256,
    role: Role,
    ours: SignedFrame,
    /// The other trader's frame once accepted (our seed cannot remake it)
    theirs: Option<SignedFrame>,
    offer: Offer,
    exchange: KeyExchange,
}

/// What a trader writes to its swap journal once keys are agreed, **before** anything is
/// locked: with the swap seed it rebuilds the whole trade after a restart, even after the
/// offer has expired. Our keys come from the seed and this offer, so a trader that lost it
/// could not refund. It holds the other trader's private view share: the journal stays
/// private to the wallet.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct SavedTrade {
    pub offer: Offer,
    pub role: Role,
    pub their_frame: SignedFrame,
    pub timing: Timing,
    /// The claimer gives up if the owner has not locked by then
    pub lock_deadline: u64,
    /// First EVM block searched for the trade's logs. The claimer must take it from before
    /// its *first* key request and keep it for retries: the owner may lock as soon as it has
    /// answered any request, and a lock before this block is never found
    pub evm_from_block: u64,
    /// XMZ height the shared-address wallets are restored from
    pub xmz_restore_height: u64,
    /// The agreement's transcript: a rebuild must reproduce it, so another seed (a wrong
    /// wallet restored) cannot quietly produce a different trade
    pub transcript: B256,
}

impl SavedTrade {
    /// The agreement again: our frame remade from the seed, theirs from the journal, both
    /// checked as in the exchange.
    pub fn agreed(&self, seed: &SwapSeed, account: &PrivateKeySigner) -> Result<Agreed, ExchangeError> {
        let ours = seed.frame(&self.offer, self.role, account)?;
        let frames = [ours, self.their_frame.clone()];
        let agreed = KeyExchange::restore(self.offer.clone(), self.role, seed.keys(&self.offer, self.role), &frames)?;
        if agreed.transcript != self.transcript {
            return Err(ExchangeError::NotThisTrade);
        }
        Ok(agreed)
    }

    pub fn trade(&self, agreed: &Agreed) -> Trade {
        agreed.trade(self.timing, self.lock_deadline)
    }

    pub fn secret(&self, seed: &SwapSeed) -> SecretShare {
        seed.keys(&self.offer, self.role).secret
    }

    /// The owner's `newSwap` nonce: the first 8 bytes of the trade id
    pub fn swap_nonce(&self) -> U256 {
        U256::from_be_slice(&self.offer.trade_id[..8])
    }
}

impl Session {
    /// Our frame for this offer, from the swap seed (the same bytes on every start).
    pub fn start(seed: &SwapSeed, offer: Offer, role: Role, account: &PrivateKeySigner, now: u64) -> Result<Self, ExchangeError> {
        let named = match role {
            Role::Owner => offer.owner,
            Role::Claimer => offer.claimer,
        };
        if account.address() != named {
            return Err(ExchangeError::NotOurAccount(role));
        }
        let ours = seed.frame(&offer, role, account)?;
        let trade_id = offer.trade_id;
        let mut exchange = KeyExchange::new(offer.clone(), role, seed.keys(&offer, role))?;
        exchange.accept(&ours, now)?;
        Ok(Session { trade_id, role, ours, theirs: None, offer, exchange })
    }

    pub fn trade_id(&self) -> B256 {
        self.trade_id
    }

    /// What we send when we start the exchange.
    pub fn request(&self) -> KeyRequest {
        KeyRequest { trade_id: self.trade_id, frame: self.ours.clone() }
    }

    /// Answer the other trader's request: our frame once theirs is accepted, else a refusal.
    pub fn respond(&mut self, request: &KeyRequest, now: u64) -> KeyResponse {
        if request.trade_id != self.trade_id {
            return KeyResponse::Refused("unknown trade".into());
        }
        match self.exchange.accept(&request.frame, now) {
            Ok(role) if role != self.role => {
                self.theirs = Some(request.frame.clone());
                KeyResponse::Frame(self.ours.clone())
            }
            Ok(_) => KeyResponse::Refused("that frame is for our own role".into()),
            Err(e) => KeyResponse::Refused(e.to_string()),
        }
    }

    /// Take the other trader's answer to our request.
    pub fn on_response(&mut self, response: &KeyResponse, now: u64) -> Result<Agreed, ExchangeError> {
        match response {
            KeyResponse::Refused(why) => Err(ExchangeError::Refused(why.chars().take(200).collect())),
            KeyResponse::Frame(frame) => {
                if self.exchange.accept(frame, now)? == self.role {
                    return Err(ExchangeError::Reflected);
                }
                self.theirs = Some(frame.clone());
                Ok(self.exchange.finish()?)
            }
        }
    }

    /// The agreed terms, once we hold both frames.
    pub fn agreed(&self) -> Result<Agreed, ExchangeError> {
        Ok(self.exchange.finish()?)
    }

    /// What to write to the journal before locking anything (None until agreed).
    pub fn saved(&self, timing: Timing, lock_deadline: u64, evm_from_block: u64, xmz_restore_height: u64) -> Option<SavedTrade> {
        let agreed = self.agreed().ok()?;
        Some(SavedTrade {
            transcript: agreed.transcript,
            offer: self.offer.clone(),
            role: self.role,
            their_frame: self.theirs.clone()?,
            timing,
            lock_deadline,
            evm_from_block,
            xmz_restore_height,
        })
    }
}

/// The exchange as a libp2p request-response protocol (JSON, as frames are JSON already).
pub mod p2p {
    use super::{KeyRequest, KeyResponse, PROTOCOL};
    use libp2p::request_response::{json, Config, ProtocolSupport};
    use libp2p::StreamProtocol;
    use std::time::Duration;

    pub type Behaviour = json::Behaviour<KeyRequest, KeyResponse>;

    /// Both traders may ask and answer.
    pub fn behaviour() -> Behaviour {
        json::Behaviour::new(
            [(StreamProtocol::new(PROTOCOL), ProtocolSupport::Full)],
            Config::default().with_request_timeout(Duration::from_secs(60)),
        )
    }
}

#[cfg(test)]
#[path = "exchange_tests.rs"]
mod tests;
