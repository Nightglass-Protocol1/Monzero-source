//! Public offers ("listings") for XMZ <-> ETH / BNB, as posted to the open relays.
//!
//! A listing carries no secret and grants nothing: it says which trade the maker would take and
//! where to reach it. It is signed (EIP-191) by the maker's EVM trading account. The signature
//! covers the canonical JSON of the listing (the fields in declaration order, as serde writes
//! them), so the same listing has one signed form however a relay re-encodes it.
//!
//! A trade taken from a listing has the listing's id as its trade id, so the swap's on-chain
//! nonce (the id's first 8 bytes) ties a claim on chain to the offer's XMZ amount. See
//! `PUBLIC-OPENING.md`.

use crate::negotiation::Role;
use crate::xmz::XmzNetwork;
use crate::Chain;
use alloy::primitives::{keccak256, Address, Bytes, Signature, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use alloy::signers::SignerSync;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const LISTING_SCHEMA: &str = "monzero-dex-listing/1";
pub const CANCEL_SCHEMA: &str = "monzero-dex-cancel/1";
/// The longest a listing may stay open
pub const MAX_LIFETIME: u64 = 24 * 3600;
/// How far in the future a maker's clock may be
pub const CLOCK_SKEW: u64 = 300;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Side {
    /// The maker sells XMZ and receives the coin: the maker is the swap's claimer
    SellXmz,
    /// The maker pays the coin and receives XMZ: the maker is the swap's owner
    BuyXmz,
}

impl Side {
    pub fn maker_role(self) -> Role {
        match self {
            Side::SellXmz => Role::Claimer,
            Side::BuyXmz => Role::Owner,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Listing {
    pub schema: String,
    pub id: B256,
    pub pair: String,
    pub chain_id: u64,
    pub contract: Address,
    pub side: Side,
    /// Coin amount in wei
    pub value: U256,
    /// XMZ amount in atoms (1e11 per XMZ), as a string: it can exceed what JSON numbers carry exactly
    #[serde(with = "u64_string")]
    pub xmz: u64,
    pub maker: Address,
    /// The maker's mailbox (X25519 public key): takes are sealed to it and left on the relays
    pub mailbox: B256,
    pub xmz_network: XmzNetwork,
    pub created_at: u64,
    pub expires_at: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SignedListing {
    pub listing: Listing,
    pub signature: Bytes,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Cancel {
    pub schema: String,
    pub id: B256,
    pub maker: Address,
    pub at: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SignedCancel {
    pub cancel: Cancel,
    pub signature: Bytes,
}

/// What a relay (or app) accepts: the pinned contract per chain and an optional coin cap.
#[derive(Clone, Debug, Default)]
pub struct Rules {
    pub contracts: BTreeMap<u64, Address>,
    pub max_value: BTreeMap<u64, U256>,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum ListingError {
    #[error("unknown schema")]
    Schema,
    #[error("the signature is not the maker's")]
    BadSignature,
    #[error("chain {0} is not served here")]
    Chain(u64),
    #[error("pair {0} does not match the chain")]
    Pair(String),
    #[error("the contract is not the pinned deployment for this chain")]
    Contract,
    #[error("the XMZ network does not match the chain")]
    XmzNetwork,
    #[error("amounts must be above zero")]
    Zero,
    #[error("above this relay's limit for the chain")]
    TooLarge,
    #[error("expired, from the future, or open longer than 24 hours")]
    Time,
    #[error("the mailbox key must not be zero")]
    Mailbox,
    #[error("the id must not be zero")]
    Id,
}

impl Chain {
    /// The pair a listing on this chain trades
    pub fn pair(self) -> &'static str {
        match self {
            Chain::BscMainnet | Chain::BscTestnet => "BNB/XMZ",
            Chain::EthMainnet | Chain::EthSepolia | Chain::Local => "ETH/XMZ",
            Chain::SolMainnet | Chain::SolDevnet | Chain::SolLocal => "SOL/XMZ",
        }
    }

    fn xmz_network_allowed(self, n: XmzNetwork) -> bool {
        match self {
            Chain::BscMainnet | Chain::EthMainnet | Chain::SolMainnet => n == XmzNetwork::Mainnet,
            Chain::BscTestnet | Chain::EthSepolia | Chain::SolDevnet => n != XmzNetwork::Mainnet,
            Chain::Local | Chain::SolLocal => true,
        }
    }
}

fn message(schema: &str, bytes: &[u8]) -> String {
    format!("{schema}\n{}", keccak256(bytes))
}

impl Listing {
    fn canonical(&self) -> Vec<u8> {
        serde_json::to_vec(self).expect("listing serializes")
    }

    pub fn digest(&self) -> B256 {
        keccak256(self.canonical())
    }

    pub fn sign(self, key: &PrivateKeySigner) -> SignedListing {
        let signature = key.sign_message_sync(message(LISTING_SCHEMA, &self.canonical()).as_bytes()).expect("local signing");
        SignedListing { listing: self, signature: signature.as_bytes().to_vec().into() }
    }

    pub fn maker_role(&self) -> Role {
        self.side.maker_role()
    }

    /// The swap nonce a trade from this listing uses: a trade's id is its listing's id, and
    /// the owner's `newSwap` nonce is the first 8 bytes of the trade id
    pub fn swap_nonce(&self) -> U256 {
        U256::from_be_slice(&self.id[..8])
    }

    pub fn check(&self, rules: &Rules, now: u64) -> Result<Chain, ListingError> {
        if self.schema != LISTING_SCHEMA {
            return Err(ListingError::Schema);
        }
        if self.id == B256::ZERO {
            return Err(ListingError::Id);
        }
        let chain = Chain::from_id(self.chain_id).ok_or(ListingError::Chain(self.chain_id))?;
        let pinned = rules.contracts.get(&self.chain_id).ok_or(ListingError::Chain(self.chain_id))?;
        if self.pair != chain.pair() {
            return Err(ListingError::Pair(self.pair.clone()));
        }
        if &self.contract != pinned {
            return Err(ListingError::Contract);
        }
        if !chain.xmz_network_allowed(self.xmz_network) {
            return Err(ListingError::XmzNetwork);
        }
        if self.value.is_zero() || self.xmz == 0 {
            return Err(ListingError::Zero);
        }
        if rules.max_value.get(&self.chain_id).is_some_and(|max| self.value > *max) {
            return Err(ListingError::TooLarge);
        }
        if self.created_at > now + CLOCK_SKEW || self.expires_at <= now || self.expires_at <= self.created_at || self.expires_at - self.created_at > MAX_LIFETIME {
            return Err(ListingError::Time);
        }
        if self.mailbox == B256::ZERO {
            return Err(ListingError::Mailbox);
        }
        Ok(chain)
    }
}

impl SignedListing {
    /// The listing, if its maker signed it and it passes `rules` at `now`
    pub fn verify(&self, rules: &Rules, now: u64) -> Result<Chain, ListingError> {
        let chain = self.listing.check(rules, now)?;
        let signature = Signature::try_from(self.signature.as_ref()).map_err(|_| ListingError::BadSignature)?;
        let signer = signature
            .recover_address_from_msg(message(LISTING_SCHEMA, &self.listing.canonical()).as_bytes())
            .map_err(|_| ListingError::BadSignature)?;
        if signer != self.listing.maker {
            return Err(ListingError::BadSignature);
        }
        Ok(chain)
    }
}

impl Cancel {
    pub fn sign(self, key: &PrivateKeySigner) -> SignedCancel {
        let bytes = serde_json::to_vec(&self).expect("cancel serializes");
        let signature = key.sign_message_sync(message(CANCEL_SCHEMA, &bytes).as_bytes()).expect("local signing");
        SignedCancel { cancel: self, signature: signature.as_bytes().to_vec().into() }
    }
}

impl SignedCancel {
    pub fn verify(&self) -> Result<(), ListingError> {
        if self.cancel.schema != CANCEL_SCHEMA {
            return Err(ListingError::Schema);
        }
        let bytes = serde_json::to_vec(&self.cancel).expect("cancel serializes");
        let signature = Signature::try_from(self.signature.as_ref()).map_err(|_| ListingError::BadSignature)?;
        match signature.recover_address_from_msg(message(CANCEL_SCHEMA, &bytes).as_bytes()) {
            Ok(a) if a == self.cancel.maker => Ok(()),
            _ => Err(ListingError::BadSignature),
        }
    }
}

mod u64_string {
    use serde::{de::Error, Deserialize, Deserializer, Serializer};

    pub fn serialize<S: Serializer>(v: &u64, s: S) -> Result<S::Ok, S::Error> {
        s.serialize_str(&v.to_string())
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(d: D) -> Result<u64, D::Error> {
        let s = String::deserialize(d)?;
        if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) || (s.len() > 1 && s.starts_with('0')) {
            return Err(D::Error::custom("expected a decimal string"));
        }
        s.parse().map_err(D::Error::custom)
    }
}

#[cfg(test)]
#[path = "listing_tests.rs"]
mod tests;
