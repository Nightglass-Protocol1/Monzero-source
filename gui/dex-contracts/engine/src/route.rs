//! XMZ → XMR as one route: two swaps back to back through the same coin on one chain.
//! Leg 1 sells XMZ for the coin (a maker's buy-XMZ offer), leg 2 spends that coin on XMR (a
//! maker's sell-XMR offer). Offers are taken whole, so a route is a pair of offers whose coin
//! amounts fit: leg 2 must cost no more than leg 1 pays, less what the taker needs for the
//! network fees of leg 2 (it locks the coin there, and may have to refund).
//!
//! Finding routes is pure; nothing here starts a trade.

use crate::listing::{Side, SignedListing};
use crate::Chain;
use crate::xmz::PrivateCoin;
use alloy::primitives::U256;
use serde::Serialize;

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Route {
    /// Sell XMZ for the coin
    pub leg1: SignedListing,
    /// Buy XMR with the coin
    pub leg2: SignedListing,
    /// XMZ atoms paid (leg 1)
    pub xmz: String,
    /// XMR atoms received (leg 2)
    pub xmr: String,
    /// Coin left in the trade account afterwards, before network fees (wei)
    pub spare: U256,
}

/// The routes these offers allow on one chain, best rate (XMR per XMZ) first. `reserve` is
/// the coin kept back for leg 2's network fees. For each XMR offer only the cheapest XMZ
/// offer that covers it is kept.
pub fn buy_xmr_routes(listings: &[SignedListing], chain: Chain, reserve: U256) -> Vec<Route> {
    let on_chain = |s: &&SignedListing| Chain::from_id(s.listing.chain_id) == Some(chain);
    let sells_xmz: Vec<&SignedListing> =
        listings.iter().filter(on_chain).filter(|s| s.listing.side == Side::BuyXmz && s.listing.xmz_network.coin() == PrivateCoin::Xmz).collect();
    let mut routes: Vec<Route> = listings
        .iter()
        .filter(on_chain)
        .filter(|s| s.listing.side == Side::SellXmz && s.listing.xmz_network.coin() == PrivateCoin::Xmr)
        .filter_map(|leg2| {
            let (need, l2) = (leg2.listing.value.checked_add(reserve)?, &leg2.listing);
            let leg1 = sells_xmz
                .iter()
                .filter(|s| s.listing.value >= need && s.listing.contract == l2.contract && s.listing.xmz_network.is_mainnet() == l2.xmz_network.is_mainnet())
                .min_by_key(|s| s.listing.xmz)?;
            Some(Route { leg1: (*leg1).clone(), leg2: leg2.clone(), xmz: leg1.listing.xmz.to_string(), xmr: l2.xmz.to_string(), spare: leg1.listing.value - l2.value })
        })
        .collect();
    // xmr_a / xmz_a > xmr_b / xmz_b, without division
    let rate = |r: &Route| (r.leg2.listing.xmz as u128, r.leg1.listing.xmz as u128);
    routes.sort_by(|a, b| {
        let ((xa, za), (xb, zb)) = (rate(a), rate(b));
        (xb * za).cmp(&(xa * zb)).then(a.leg2.listing.id.cmp(&b.leg2.listing.id))
    });
    routes
}

/// How long after selling XMZ the coin may be spent on XMR
pub const SALE_WINDOW_SECS: u64 = 24 * 3600;

/// XMR is sold only to an account that has just sold XMZ (the owner's rule): an offer that
/// sells XMR asks its taker for that sale. Local test chains have no relay record to ask.
///
/// 2026-11-20 the owner opened direct XMR pairs (XMR/ETH, XMR/BNB, XMR/SOL, XMR/USDT): XMR is
/// bought with any of those coins, so no offer asks for an XMZ sale any more. The check stays
/// (an app may still send its sale; old makers asked for it) but nothing requires it.
pub fn needs_xmz_sale(_listing: &crate::listing::Listing) -> bool {
    false
}

/// Does this recorded swap pay for taking `listing`: an XMZ sale on the same chain, recent, and
/// for at least what the XMR costs?
pub fn sale_covers(print: &crate::relay_client::Print, listing: &crate::listing::Listing, now: u64) -> bool {
    Chain::from_id(listing.chain_id).is_some_and(|c| print.pair == c.pair())
        && print.chain_id == listing.chain_id
        && print.value >= listing.value
        && print.at + SALE_WINDOW_SECS >= now
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::listing::{Listing, LISTING_SCHEMA};
    use crate::xmz::XmzNetwork;
    use alloy::primitives::{Address, B256};
    use alloy::signers::local::PrivateKeySigner;

    fn offer(id: u8, side: Side, network: XmzNetwork, value: u64, atoms: u64) -> SignedListing {
        let key = PrivateKeySigner::random();
        Listing {
            schema: LISTING_SCHEMA.into(),
            id: B256::repeat_byte(id),
            pair: Chain::BscMainnet.pair_for(network),
            chain_id: 56,
            contract: Address::repeat_byte(9),
            side,
            value: U256::from(value),
            xmz: atoms,
            maker: key.address(),
            mailbox: B256::repeat_byte(0x5a),
            xmz_network: network,
            created_at: 1000,
            expires_at: 5000,
            min_xmz: None,
        }
        .sign(&key)
    }

    #[test]
    fn a_route_pairs_an_xmz_sale_with_an_xmr_purchase_it_can_pay_for() {
        use XmzNetwork::{Mainnet, XmrMainnet};
        let offers = vec![
            offer(1, Side::BuyXmz, Mainnet, 1_000, 500),   // sell 500 XMZ atoms for 1000
            offer(2, Side::BuyXmz, Mainnet, 1_000, 400),   // the same coin for less XMZ: better
            offer(3, Side::BuyXmz, Mainnet, 600, 100),     // too small for offer 5 once fees are kept back
            offer(4, Side::SellXmz, Mainnet, 900, 300),    // XMZ for sale: not part of a route
            offer(5, Side::SellXmz, XmrMainnet, 900, 30),  // 30 XMR atoms for 900
            offer(6, Side::SellXmz, XmrMainnet, 500, 20),  // 20 XMR atoms for 500: paid by offer 3
            offer(7, Side::BuyXmz, XmrMainnet, 2_000, 10), // someone buying XMR: not a route
            offer(8, Side::SellXmz, XmrMainnet, 5_000, 99), // nothing pays this much
        ];
        let routes = buy_xmr_routes(&offers, Chain::BscMainnet, U256::from(100));
        let ids: Vec<(u8, u8)> = routes.iter().map(|r| (r.leg1.listing.id[0], r.leg2.listing.id[0])).collect();
        // 20 XMR for 100 XMZ beats 30 XMR for 400 XMZ
        assert_eq!(ids, vec![(3, 6), (2, 5)]);
        assert_eq!((routes[1].xmz.as_str(), routes[1].xmr.as_str(), routes[1].spare), ("400", "30", U256::from(100)));
        // With more kept back for fees, offer 3 no longer covers offer 6; the next cheapest does
        let routes = buy_xmr_routes(&offers, Chain::BscMainnet, U256::from(101));
        assert_eq!(routes.iter().map(|r| (r.leg1.listing.id[0], r.leg2.listing.id[0])).collect::<Vec<_>>(), vec![(2, 6)]);
        assert!(buy_xmr_routes(&offers, Chain::EthMainnet, U256::ZERO).is_empty(), "another chain");
    }

    #[test]
    fn xmr_is_sold_only_against_a_recent_xmz_sale_that_covers_it() {
        use crate::relay_client::Print;
        use XmzNetwork::{Mainnet, XmrMainnet};
        let xmr = offer(5, Side::SellXmz, XmrMainnet, 900, 30).listing;
        assert!(!needs_xmz_sale(&xmr), "direct XMR pairs (2026-11-20): no XMZ sale is asked for");
        assert!(!needs_xmz_sale(&offer(1, Side::SellXmz, Mainnet, 900, 30).listing), "XMZ offers ask for nothing");
        assert!(!needs_xmz_sale(&offer(7, Side::BuyXmz, XmrMainnet, 900, 30).listing), "selling XMR to a maker ends in XMZ by the route, not by proof");
        let sale = |pair: &str, chain_id, value: u64, at| Print { pair: pair.into(), chain_id, tx: String::new(), at, value: U256::from(value) };
        let now = 1_000_000;
        assert!(sale_covers(&sale("BNB/XMZ", 56, 900, now - 3600), &xmr, now));
        assert!(!sale_covers(&sale("BNB/XMZ", 56, 899, now - 3600), &xmr, now), "too small");
        assert!(!sale_covers(&sale("BNB/XMZ", 56, 900, now - SALE_WINDOW_SECS - 1), &xmr, now), "too old");
        assert!(!sale_covers(&sale("BNB/XMR", 56, 900, now - 3600), &xmr, now), "selling XMR does not count");
        assert!(!sale_covers(&sale("ETH/XMZ", 1, 900, now - 3600), &xmr, now), "another chain");
    }
}
