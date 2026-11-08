use super::*;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::listing::{Cancel, Listing, Side, CANCEL_SCHEMA, LISTING_SCHEMA};
use monzero_dex_evm::xmz::XmzNetwork;

const NOW: u64 = 1_800_000_000;
const ETH: u64 = 1_000_000_000_000_000_000;

#[derive(Default)]
struct Mock {
    balances: Mutex<HashMap<Address, U256>>,
    claims: Mutex<HashMap<B256, ClaimLookup>>,
}

impl ChainView for Mock {
    async fn balance(&self, a: Address) -> Result<U256, String> {
        Ok(self.balances.lock().unwrap().get(&a).copied().unwrap_or_default())
    }
    async fn claim(&self, tx: B256) -> Result<ClaimLookup, String> {
        Ok(self.claims.lock().unwrap().get(&tx).cloned().unwrap_or(ClaimLookup::Pending))
    }
}

fn contract() -> Address {
    Address::repeat_byte(0xc0)
}

fn relay() -> Relay<Mock> {
    let mut rules = Rules::default();
    rules.contracts.insert(1, contract());
    rules.max_value.insert(1, U256::from(ETH / 10));
    Relay::new(rules, BTreeMap::from([(1, Mock::default())]))
}

fn fund(r: &Relay<Mock>, a: Address, wei: u64) {
    r.chains[&1].balances.lock().unwrap().insert(a, U256::from(wei));
}

fn offer(maker: &PrivateKeySigner, id: u8, side: Side, value: u64, xmz_coins: u64) -> SignedListing {
    Listing {
        schema: LISTING_SCHEMA.into(),
        id: B256::repeat_byte(id),
        pair: "ETH/XMZ".into(),
        chain_id: 1,
        contract: contract(),
        side,
        value: U256::from(value),
        xmz: xmz_coins * 100_000_000_000,
        maker: maker.address(),
        peer: "/ip4/203.0.113.5/tcp/47101/p2p/12D3KooWJLkmg3mWLckBzVLAMVrqCM76mgYZaQpq16pw2y3U5qRU".into(),
        xmz_network: XmzNetwork::Mainnet,
        created_at: NOW,
        expires_at: NOW + 3600,
    }
    .sign(maker)
}

fn swap_for(l: &Listing, taker: Address) -> Swap {
    let (owner, claimer) = match l.side {
        Side::SellXmz => (taker, l.maker),
        Side::BuyXmz => (l.maker, taker),
    };
    Swap {
        owner,
        claimer,
        claimCommitment: Address::repeat_byte(1),
        refundCommitment: Address::repeat_byte(2),
        timeout1: U256::from(NOW + 7200),
        timeout2: U256::from(NOW + 14400),
        asset: Address::ZERO,
        value: l.value,
        nonce: l.swap_nonce(),
    }
}

#[tokio::test]
async fn offers_are_listed_cheapest_first_and_cancelled_only_by_their_maker() {
    let r = relay();
    let (a, b) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    fund(&r, a.address(), 1);
    fund(&r, b.address(), ETH);
    // a sells 10,000 XMZ for 0.01 ETH; b buys 5,000 XMZ for 0.004 ETH (cheaper per XMZ)
    assert_eq!(r.post(offer(&a, 1, Side::SellXmz, ETH / 100, 10_000), NOW).await, Ok(true));
    assert_eq!(r.post(offer(&b, 2, Side::BuyXmz, ETH / 250, 5_000), NOW).await, Ok(true));
    assert_eq!(r.post(offer(&a, 1, Side::SellXmz, ETH / 100, 10_000), NOW).await, Ok(false), "the same offer again");
    let ids: Vec<_> = r.listings(Some("ETH/XMZ"), NOW).iter().map(|o| o.listing.id).collect();
    assert_eq!(ids, [B256::repeat_byte(2), B256::repeat_byte(1)]);
    assert!(r.listings(Some("BNB/XMZ"), NOW).is_empty());
    assert!(r.listings(None, NOW + 3600).is_empty(), "expired offers are not shown");

    let by_b = Cancel { schema: CANCEL_SCHEMA.into(), id: B256::repeat_byte(1), maker: b.address(), at: NOW }.sign(&b);
    assert_eq!(r.cancel(&by_b, NOW), Err(RelayError::NotFound));
    let by_a = Cancel { schema: CANCEL_SCHEMA.into(), id: B256::repeat_byte(1), maker: a.address(), at: NOW }.sign(&a);
    assert_eq!(r.cancel(&by_a, NOW), Ok(()));
    assert_eq!(r.listings(None, NOW).len(), 1);
    assert_eq!(r.post(offer(&a, 1, Side::SellXmz, ETH / 100, 10_000), NOW).await, Err(RelayError::Closed), "a cancelled offer cannot come back");
}

#[tokio::test]
async fn offers_must_be_backed_signed_and_within_limits() {
    let r = relay();
    let a = PrivateKeySigner::random();
    assert_eq!(r.post(offer(&a, 1, Side::SellXmz, ETH / 100, 10_000), NOW).await, Err(RelayError::Unfunded), "no gas");
    fund(&r, a.address(), ETH / 200);
    assert_eq!(r.post(offer(&a, 2, Side::BuyXmz, ETH / 100, 10_000), NOW).await, Err(RelayError::Unfunded), "cannot pay");
    assert!(matches!(r.post(offer(&a, 3, Side::SellXmz, ETH, 10_000), NOW).await, Err(RelayError::Listing(_))), "above the cap");
    let mut forged = offer(&a, 4, Side::SellXmz, ETH / 100, 10_000);
    forged.listing.xmz += 1;
    assert!(matches!(r.post(forged, NOW).await, Err(RelayError::Listing(_))));
    for i in 0..MAX_PER_MAKER as u8 {
        assert_eq!(r.post(offer(&a, 10 + i, Side::SellXmz, ETH / 100, 10_000), NOW).await, Ok(true));
    }
    assert_eq!(r.post(offer(&a, 99, Side::SellXmz, ETH / 100, 10_000), NOW).await, Err(RelayError::MakerLimit));
}

#[tokio::test]
async fn a_claim_on_chain_becomes_a_print_and_fills_the_offer() {
    let r = relay();
    let (maker, taker) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    fund(&r, maker.address(), 1);
    let signed = offer(&maker, 5, Side::SellXmz, 5 * ETH / 1000, 11_193);
    r.post(signed.clone(), NOW).await.unwrap();
    let tx = B256::repeat_byte(0xaa);

    // Not final yet: kept pending, then recorded on a later check
    assert_eq!(r.report(1, tx, NOW).await, Ok(PrintStatus::Pending));
    assert!(r.prints(None, 0).is_empty());
    let at = NOW + 5000;
    r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at });
    r.recheck(NOW + 60).await;
    let prints = r.prints(Some("ETH/XMZ"), 0);
    assert_eq!(prints.len(), 1);
    assert_eq!((prints[0].at, prints[0].xmz, prints[0].listing), (at, 11_193 * 100_000_000_000, B256::repeat_byte(5)));
    assert!((prints[0].price() - 0.005 / 11_193.0).abs() < 1e-15);
    assert!(r.listings(None, NOW).is_empty(), "filled offers close");
    assert_eq!(r.report(1, tx, NOW).await, Ok(PrintStatus::Recorded), "reporting twice is harmless");
    assert_eq!(r.prints(None, 0).len(), 1);
    assert_eq!(r.post(signed, NOW).await, Err(RelayError::Closed));
}

#[tokio::test]
async fn claims_that_do_not_match_an_offer_are_refused() {
    let r = relay();
    let (maker, taker) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    fund(&r, maker.address(), ETH);
    let signed = offer(&maker, 6, Side::BuyXmz, ETH / 100, 10_000);
    r.post(signed.clone(), NOW).await.unwrap();
    let good = swap_for(&signed.listing, taker.address());
    let cases = [
        Swap { value: good.value + U256::from(1), ..good.clone() },
        Swap { owner: taker.address(), ..good.clone() },
        Swap { asset: Address::repeat_byte(9), ..good.clone() },
        Swap { nonce: U256::from(12345), ..good.clone() },
    ];
    for (i, swap) in cases.into_iter().enumerate() {
        let tx = B256::repeat_byte(i as u8 + 1);
        r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::Final { swap, at: NOW });
        assert_eq!(r.report(1, tx, NOW).await, Err(RelayError::UnknownListing), "case {i}");
    }
    let tx = B256::repeat_byte(0x77);
    r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::NotAClaim("reverted".into()));
    assert_eq!(r.report(1, tx, NOW).await, Err(RelayError::NotAClaim("reverted".into())));
    assert_eq!(r.report(56, tx, NOW).await, Err(RelayError::Chain(56)));
    assert!(r.prints(None, 0).is_empty());
    assert_eq!(r.listings(None, NOW).len(), 1, "the offer stays open");
}

#[tokio::test]
async fn trades_still_match_after_their_offer_expired() {
    let r = relay();
    let (maker, taker) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    fund(&r, maker.address(), 1);
    let signed = offer(&maker, 8, Side::SellXmz, ETH / 100, 10_000);
    r.post(signed.clone(), NOW).await.unwrap();
    r.prune(NOW + 4000);
    assert!(r.listings(None, NOW + 4000).is_empty());
    let tx = B256::repeat_byte(0x88);
    r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at: NOW + 5000 });
    assert_eq!(r.report(1, tx, NOW + 5000).await, Ok(PrintStatus::Recorded));
    r.prune(NOW + ARCHIVE_SECS + 4000);
    assert!(r.listing(signed.listing.id).is_none(), "forgotten after a week");
    assert_eq!(r.prints(None, 0).len(), 1, "trades are kept");
}

#[tokio::test]
async fn candles_aggregate_prints_by_interval() {
    let r = relay();
    let taker = PrivateKeySigner::random();
    let mut at = NOW - NOW % 3600;
    // Three trades in the first hour at 1, 3 and 2 (micro-ETH per XMZ), one in the next hour
    for (i, (micro, coins, offset)) in [(1u64, 1000u64, 10u64), (3, 1000, 20), (2, 2000, 30), (4, 1000, 3600)].into_iter().enumerate() {
        let maker = PrivateKeySigner::random();
        fund(&r, maker.address(), 1);
        let signed = offer(&maker, 20 + i as u8, Side::SellXmz, micro * coins * 1_000_000_000_000, coins);
        r.post(signed.clone(), NOW).await.unwrap();
        let tx = B256::repeat_byte(0x40 + i as u8);
        at = (NOW - NOW % 3600) + offset;
        r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at });
        r.report(1, tx, NOW).await.unwrap();
    }
    let c = r.candles("ETH/XMZ", 3600, 0);
    assert_eq!(c.len(), 2);
    let micro = |x: f64| (x * 1e6 * 1000.0).round() / 1000.0;
    assert_eq!((micro(c[0].o), micro(c[0].h), micro(c[0].l), micro(c[0].c), c[0].n), (1.0, 3.0, 1.0, 2.0, 3));
    assert_eq!(c[0].v, 4000.0);
    assert_eq!((micro(c[1].o), c[1].n, c[1].t), (4.0, 1, at - at % 3600));
    assert_eq!(r.candles("ETH/XMZ", 86400, 0).iter().map(|c| c.n).sum::<u32>(), 4);
    assert!(r.candles("BNB/XMZ", 3600, 0).is_empty());
}

#[tokio::test]
async fn state_survives_a_restart() {
    let r = relay();
    let (maker, taker) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    fund(&r, maker.address(), 1);
    let (open, filled) = (offer(&maker, 30, Side::SellXmz, ETH / 100, 10_000), offer(&maker, 31, Side::SellXmz, ETH / 100, 10_000));
    r.post(open.clone(), NOW).await.unwrap();
    r.post(filled.clone(), NOW).await.unwrap();
    let tx = B256::repeat_byte(0x31);
    r.chains[&1].claims.lock().unwrap().insert(tx, ClaimLookup::Final { swap: swap_for(&filled.listing, taker.address()), at: NOW });
    r.report(1, tx, NOW).await.unwrap();
    r.report(1, B256::repeat_byte(0x32), NOW).await.unwrap();

    let json = serde_json::to_vec(&r.snapshot()).unwrap();
    let back = relay();
    back.restore(serde_json::from_slice(&json).unwrap());
    assert_eq!(back.listings(None, NOW), vec![open]);
    assert_eq!(back.prints(None, 0), r.prints(None, 0));
    assert_eq!(back.post(filled, NOW).await, Err(RelayError::Closed));
    // A tampered saved offer is dropped on load
    let mut snap = r.snapshot();
    snap.active[0].listing.xmz += 1;
    let tampered = relay();
    tampered.restore(snap);
    assert!(tampered.listings(None, NOW).is_empty());
}
