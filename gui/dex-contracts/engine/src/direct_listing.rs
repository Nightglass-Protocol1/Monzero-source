//! Public offers for direct (coin <-> coin) trades: "I give this much of one coin for this much
//! of another", as posted to the open relays.
//!
//! As with the XMZ offers (`listing`), a direct listing carries no secret and grants nothing; it
//! is signed (EIP-191) by the maker's trading account over its canonical JSON. A trade taken from
//! it is the [`DirectOffer`] that [`DirectListing::offer`] makes: the **taker leads** (locks what
//! the maker wants, leg A) and the **maker follows** (locks what it gives, leg B, once A is safe).
//! The trade id is the listing id, so both legs' on-chain nonces come from it.
//!
//! Whole offers only: one listing, one trade.

use crate::direct::{DirectOffer, Leg};
use crate::listing::{ListingError, CLOCK_SKEW, MAX_LIFETIME_V2};
use crate::Chain;
use alloy::primitives::{keccak256, Address, Bytes, Signature, B256, U256};
use alloy::signers::local::PrivateKeySigner;
use alloy::signers::SignerSync;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const DIRECT_LISTING_SCHEMA: &str = "monzero-dex-direct-listing/1";
/// Leg A (the taker's lock): first time limit, then the second after it
pub const LEG_A_DURATIONS: (u64, u64) = (3600, 8 * 3600);
/// Leg B (the maker's lock): closes well before A (see `direct`)
pub const LEG_B_DURATIONS: (u64, u64) = (3600, 4 * 3600);
/// The maker gives up on a taker whose lock of A is not safely in place this long after the keys
/// were agreed; the taker does not lock at all when less than [`LEADER_LOCK_BUDGET`] of it is left
pub const LOCK_WINDOW: u64 = 2 * 3600;
pub const LEADER_LOCK_BUDGET: u64 = 3600;

impl Chain {
    /// The chain's own coin ("ETH", "BNB", "SOL")
    pub fn ticker(self) -> &'static str {
        self.pair().split('/').next().expect("pair has a base")
    }

    /// Real coins (as opposed to a test network or a local chain)
    pub fn is_mainnet(self) -> bool {
        matches!(self, Chain::BscMainnet | Chain::EthMainnet | Chain::SolMainnet)
    }
}

/// The name of a direct pair: both tickers, alphabetical ("BNB/ETH", "BNB/SOL", "ETH/SOL"), so
/// both directions of a pair share one book
pub fn direct_pair(a: Chain, b: Chain) -> String {
    let (x, y) = (a.ticker(), b.ticker());
    if x <= y { format!("{x}/{y}") } else { format!("{y}/{x}") }
}

/// One side of a direct offer: a coin on a chain, in base units (wei, lamports)
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Coin {
    pub chain_id: u64,
    /// The pinned swap contract (on Solana: the stand-in address of the pinned program)
    pub contract: Address,
    /// The chain's own coin only, for now: always zero
    pub asset: Address,
    pub value: U256,
}

impl Coin {
    pub fn chain(&self) -> Result<Chain, ListingError> {
        Chain::from_id(self.chain_id).ok_or(ListingError::Chain(self.chain_id))
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct DirectListing {
    pub schema: String,
    pub id: B256,
    pub pair: String,
    pub maker: Address,
    /// When either coin is on Solana: the maker's Solana trade account (its 32-byte key)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub maker_sol: Option<B256>,
    /// The maker's mailbox (X25519 public key)
    pub mailbox: B256,
    /// What the maker locks (leg B of a trade)
    pub gives: Coin,
    /// What the maker wants: the taker locks it (leg A)
    pub wants: Coin,
    pub created_at: u64,
    pub expires_at: u64,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SignedDirectListing {
    pub listing: DirectListing,
    pub signature: Bytes,
}

/// What an app or relay accepts for direct offers
#[derive(Clone, Debug, Default)]
pub struct DirectRules {
    /// The pinned contract (program stand-in) per chain
    pub contracts: BTreeMap<u64, Address>,
    /// A cap per chain on what one side may be (base units)
    pub max_value: BTreeMap<u64, U256>,
    /// Offers with real coins are accepted. Off until direct pairs are opened after their
    /// real-funds tests; test networks are accepted either way.
    pub mainnet: bool,
    /// Tests only: more swap contracts on the local test chain (31337), so one local node can
    /// stand for both chains of a pair
    pub local_contracts: Vec<Address>,
}

impl DirectRules {
    fn pinned(&self, chain_id: u64, contract: Address) -> bool {
        self.contracts.get(&chain_id) == Some(&contract) || (chain_id == Chain::Local.id() && self.local_contracts.contains(&contract))
    }
}

fn message(bytes: &[u8]) -> String {
    format!("{DIRECT_LISTING_SCHEMA}\n{}", keccak256(bytes))
}

impl DirectListing {
    fn canonical(&self) -> Vec<u8> {
        serde_json::to_vec(self).expect("listing serializes")
    }

    pub fn sign(self, key: &PrivateKeySigner) -> SignedDirectListing {
        let signature = key.sign_message_sync(message(&self.canonical()).as_bytes()).expect("local signing");
        SignedDirectListing { listing: self, signature: signature.as_bytes().to_vec().into() }
    }

    pub fn uses_solana(&self) -> bool {
        [&self.gives, &self.wants].iter().any(|c| c.chain().is_ok_and(Chain::is_solana))
    }

    /// The offer's own rules (no signature)
    pub fn check(&self, rules: &DirectRules, now: u64) -> Result<(), ListingError> {
        if self.schema != DIRECT_LISTING_SCHEMA {
            return Err(ListingError::Schema);
        }
        if self.id == B256::ZERO {
            return Err(ListingError::Id);
        }
        if self.mailbox == B256::ZERO {
            return Err(ListingError::Mailbox);
        }
        let (give, want) = (self.gives.chain()?, self.wants.chain()?);
        for (coin, chain) in [(&self.gives, give), (&self.wants, want)] {
            if !rules.pinned(coin.chain_id, coin.contract) {
                return Err(ListingError::Contract);
            }
            if coin.asset != Address::ZERO {
                return Err(ListingError::Pair(self.pair.clone()));
            }
            if coin.value.is_zero() {
                return Err(ListingError::Zero);
            }
            if rules.max_value.get(&coin.chain_id).is_some_and(|max| coin.value > *max) {
                return Err(ListingError::TooLarge);
            }
            if chain.is_mainnet() && !rules.mainnet {
                return Err(ListingError::Chain(coin.chain_id));
            }
        }
        // Two different chains, both real or both test networks (two local test chains are one chain
        // with two contracts: tests only)
        let local = |c: Chain| matches!(c, Chain::Local | Chain::SolLocal);
        if give == want && !(local(give) && self.gives.contract != self.wants.contract) {
            return Err(ListingError::Pair(self.pair.clone()));
        }
        if give.is_mainnet() != want.is_mainnet() || local(give) != local(want) {
            return Err(ListingError::Pair(self.pair.clone()));
        }
        if self.pair != direct_pair(give, want) {
            return Err(ListingError::Pair(self.pair.clone()));
        }
        if self.uses_solana() != self.maker_sol.is_some() || self.maker_sol == Some(B256::ZERO) {
            return Err(ListingError::Pair(self.pair.clone()));
        }
        if self.created_at > now + CLOCK_SKEW || self.expires_at <= now || self.expires_at <= self.created_at || self.expires_at - self.created_at > MAX_LIFETIME_V2 {
            return Err(ListingError::Time);
        }
        Ok(())
    }

    /// The trade a taker makes of it: the taker leads with what the maker wants
    pub fn offer(&self, taker: Address) -> DirectOffer {
        let leg = |c: &Coin, (d1, d2): (u64, u64)| Leg { chain_id: c.chain_id, contract: c.contract, asset: c.asset, value: c.value, duration1: d1, duration2: d2 };
        DirectOffer {
            trade_id: self.id,
            leader: taker,
            follower: self.maker,
            leg_a: leg(&self.wants, LEG_A_DURATIONS),
            leg_b: leg(&self.gives, LEG_B_DURATIONS),
            expires_at: self.expires_at,
        }
    }
}

impl SignedDirectListing {
    pub fn verify(&self, rules: &DirectRules, now: u64) -> Result<(), ListingError> {
        self.listing.check(rules, now)?;
        let signature = Signature::try_from(self.signature.as_ref()).map_err(|_| ListingError::BadSignature)?;
        match signature.recover_address_from_msg(message(&self.listing.canonical()).as_bytes()) {
            Ok(a) if a == self.listing.maker => Ok(()),
            _ => Err(ListingError::BadSignature),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn key() -> PrivateKeySigner {
        PrivateKeySigner::from_bytes(&B256::repeat_byte(7)).unwrap()
    }

    fn rules() -> DirectRules {
        let contracts = [(Chain::EthSepolia.id(), Address::repeat_byte(1)), (Chain::BscTestnet.id(), Address::repeat_byte(2)), (Chain::SolDevnet.id(), Address::repeat_byte(3)), (Chain::EthMainnet.id(), Address::repeat_byte(4)), (Chain::BscMainnet.id(), Address::repeat_byte(5))];
        DirectRules { contracts: contracts.into_iter().collect(), ..Default::default() }
    }

    fn listing() -> DirectListing {
        DirectListing {
            schema: DIRECT_LISTING_SCHEMA.into(),
            id: B256::repeat_byte(9),
            pair: "BNB/ETH".into(),
            maker: key().address(),
            maker_sol: None,
            mailbox: B256::repeat_byte(8),
            gives: Coin { chain_id: Chain::BscTestnet.id(), contract: Address::repeat_byte(2), asset: Address::ZERO, value: U256::from(30) },
            wants: Coin { chain_id: Chain::EthSepolia.id(), contract: Address::repeat_byte(1), asset: Address::ZERO, value: U256::from(1) },
            created_at: 1_000,
            expires_at: 5_000,
        }
    }

    #[test]
    fn a_signed_offer_verifies_and_makes_the_trade_the_taker_leads() {
        let signed = listing().sign(&key());
        assert_eq!(signed.verify(&rules(), 1_100), Ok(()));
        let taker = Address::repeat_byte(0x77);
        let offer = signed.listing.offer(taker);
        assert_eq!((offer.leader, offer.follower), (taker, key().address()));
        assert_eq!(offer.leg_a.chain_id, Chain::EthSepolia.id(), "the taker locks what the maker wants");
        assert_eq!(offer.leg_b.value, U256::from(30));
        assert_eq!(offer.check(), Ok(()));
        assert_eq!(offer.trade_id, signed.listing.id);
        // Tampering breaks the signature
        let mut bad = signed.clone();
        bad.listing.gives.value = U256::from(31);
        assert_eq!(bad.verify(&rules(), 1_100), Err(ListingError::BadSignature));
    }

    #[test]
    fn offers_outside_the_rules_are_refused() {
        let check = |change: fn(&mut DirectListing)| {
            let mut l = listing();
            change(&mut l);
            l.check(&rules(), 1_100)
        };
        assert_eq!(check(|_| {}), Ok(()));
        assert_eq!(check(|l| l.pair = "ETH/BNB".into()), Err(ListingError::Pair("ETH/BNB".into())));
        assert_eq!(check(|l| l.gives.contract = Address::repeat_byte(9)), Err(ListingError::Contract));
        assert_eq!(check(|l| l.wants.value = U256::ZERO), Err(ListingError::Zero));
        assert_eq!(check(|l| l.gives.asset = Address::repeat_byte(1)), Err(ListingError::Pair("BNB/ETH".into())));
        assert_eq!(check(|l| l.wants = l.gives.clone()), Err(ListingError::Pair("BNB/ETH".into())), "one chain on both sides");
        assert_eq!(check(|l| l.expires_at = 1_000), Err(ListingError::Time));
        assert_eq!(check(|l| l.expires_at = 1_000 + MAX_LIFETIME_V2 + 1), Err(ListingError::Time));
        // A test coin for a real one
        assert!(check(|l| {
            l.wants = Coin { chain_id: Chain::EthMainnet.id(), contract: Address::repeat_byte(4), asset: Address::ZERO, value: U256::from(1) };
        })
        .is_err());
        // Solana needs the maker's Solana account
        assert!(check(|l| {
            l.wants = Coin { chain_id: Chain::SolDevnet.id(), contract: Address::repeat_byte(3), asset: Address::ZERO, value: U256::from(1) };
            l.pair = "BNB/SOL".into();
        })
        .is_err());
        assert_eq!(
            check(|l| {
                l.wants = Coin { chain_id: Chain::SolDevnet.id(), contract: Address::repeat_byte(3), asset: Address::ZERO, value: U256::from(1) };
                l.pair = "BNB/SOL".into();
                l.maker_sol = Some(B256::repeat_byte(4));
            }),
            Ok(())
        );
    }

    #[test]
    fn real_coins_only_once_opened_and_within_the_cap() {
        let mut l = listing();
        l.gives = Coin { chain_id: Chain::BscMainnet.id(), contract: Address::repeat_byte(5), asset: Address::ZERO, value: U256::from(30) };
        l.wants = Coin { chain_id: Chain::EthMainnet.id(), contract: Address::repeat_byte(4), asset: Address::ZERO, value: U256::from(1) };
        assert_eq!(l.check(&rules(), 1_100), Err(ListingError::Chain(Chain::BscMainnet.id())));
        let mut open = rules();
        open.mainnet = true;
        assert_eq!(l.check(&open, 1_100), Ok(()));
        open.max_value.insert(Chain::BscMainnet.id(), U256::from(29));
        assert_eq!(l.check(&open, 1_100), Err(ListingError::TooLarge));
    }
}
