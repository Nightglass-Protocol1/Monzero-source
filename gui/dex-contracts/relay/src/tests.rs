use super::*;
use alloy::signers::local::PrivateKeySigner;
use monzero_dex_evm::listing::{Cancel, Listing, Side, CANCEL_SCHEMA, LISTING_SCHEMA};
use monzero_dex_evm::xmz::XmzNetwork;

const NOW: u64 = 1_800_000_000;
const ETH: u64 = 1_000_000_000_000_000_000;

#[derive(Default)]
struct Mock {
    balances: Mutex<HashMap<Address, U256>>,
    claims: Mutex<HashMap<TxId, ClaimLookup>>,
    sol: bool,
    sol_balances: Mutex<HashMap<B256, U256>>,
}

impl ChainView for Mock {
    async fn balance(&self, a: Address) -> Result<U256, String> {
        Ok(self.balances.lock().unwrap().get(&a).copied().unwrap_or_default())
    }
    async fn claim(&self, tx: &TxId) -> Result<ClaimLookup, String> {
        Ok(self.claims.lock().unwrap().get(tx).cloned().unwrap_or(ClaimLookup::Pending))
    }
    fn solana(&self) -> bool {
        self.sol
    }
    async fn sol_balance(&self, a: B256) -> Result<U256, String> {
        Ok(self.sol_balances.lock().unwrap().get(&a).copied().unwrap_or_default())
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
        mailbox: B256::repeat_byte(0x5a),
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
    let tx: TxId = B256::repeat_byte(0xaa).into();

    // Not final yet: kept pending, then recorded on a later check
    assert_eq!(r.report(1, tx.clone(), NOW).await, Ok(PrintStatus::Pending));
    assert!(r.prints(None, 0).is_empty());
    let at = NOW + 5000;
    r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at });
    r.recheck(NOW + 60).await;
    let prints = r.prints(Some("ETH/XMZ"), 0);
    assert_eq!(prints.len(), 1);
    assert_eq!((prints[0].at, prints[0].xmz, prints[0].listing), (at, 11_193 * 100_000_000_000, B256::repeat_byte(5)));
    assert!((prints[0].price() - 0.005 / 11_193.0).abs() < 1e-15);
    assert!(r.listings(None, NOW).is_empty(), "filled offers close");
    assert_eq!(r.report(1, tx.clone(), NOW).await, Ok(PrintStatus::Recorded), "reporting twice is harmless");
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
        Swap { nonce: good.nonce + U256::from(1), ..good.clone() },
    ];
    for (i, swap) in cases.into_iter().enumerate() {
        let tx: TxId = B256::repeat_byte(i as u8 + 1).into();
        r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::Final { swap, at: NOW });
        assert_eq!(r.report(1, tx.clone(), NOW).await, Err(RelayError::UnknownListing), "case {i}");
    }
    let tx: TxId = B256::repeat_byte(0x77).into();
    r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::NotAClaim("reverted".into()));
    assert_eq!(r.report(1, tx.clone(), NOW).await, Err(RelayError::NotAClaim("reverted".into())));
    assert_eq!(r.report(56, tx.clone(), NOW).await, Err(RelayError::Chain(56)));
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
    let tx: TxId = B256::repeat_byte(0x88).into();
    r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at: NOW + 5000 });
    assert_eq!(r.report(1, tx.clone(), NOW + 5000).await, Ok(PrintStatus::Recorded));
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
        let tx: TxId = B256::repeat_byte(0x40 + i as u8).into();
        at = (NOW - NOW % 3600) + offset;
        r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::Final { swap: swap_for(&signed.listing, taker.address()), at });
        r.report(1, tx.clone(), NOW).await.unwrap();
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
    let tx: TxId = B256::repeat_byte(0x31).into();
    r.chains[&1].claims.lock().unwrap().insert(tx.clone(), ClaimLookup::Final { swap: swap_for(&filled.listing, taker.address()), at: NOW });
    r.report(1, tx.clone(), NOW).await.unwrap();
    r.report(1, B256::repeat_byte(0x32).into(), NOW).await.unwrap();

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

#[test]
fn mail_is_kept_per_mailbox_for_two_hours() {
    let r = relay();
    let (a, b) = (B256::repeat_byte(1), B256::repeat_byte(2));
    let letter = |x: u8| Bytes::from(vec![x; 100]);
    assert_eq!(r.post_mail(a, letter(1), NOW), Ok(()));
    assert_eq!(r.post_mail(a, letter(1), NOW), Ok(()), "the same letter again");
    assert_eq!(r.post_mail(a, letter(2), NOW + 60), Ok(()));
    assert_eq!(r.mail(a, 0).len(), 2);
    assert_eq!(r.mail(a, NOW + 30).len(), 1);
    assert!(r.mail(b, 0).is_empty());
    assert_eq!(r.post_mail(b, Bytes::from(vec![0; 10]), NOW), Err(RelayError::BadMail));
    assert_eq!(r.post_mail(b, Bytes::from(vec![0; MAX_SEALED + 1]), NOW), Err(RelayError::BadMail));
    assert_eq!(r.post_mail(B256::ZERO, letter(3), NOW), Err(RelayError::BadMail));
    for i in 0..MAIL_PER_BOX as u32 {
        r.post_mail(b, Bytes::from((i + 1000).to_be_bytes().repeat(25)), NOW).unwrap();
    }
    assert_eq!(r.post_mail(b, letter(99), NOW), Err(RelayError::Full));
    r.prune(NOW + MAIL_SECS + 1);
    assert_eq!(r.mail(a, 0).len(), 1, "the later letter is still kept");
    assert_eq!(r.post_mail(b, letter(99), NOW + MAIL_SECS + 1), Ok(()), "room again after expiry");
    r.prune(NOW + 60 + MAIL_SECS);
    assert!(r.mail(a, 0).is_empty());
}


// ---- Solana

const SOL_CHAIN: u64 = 501000102;

fn sol_relay() -> Relay<Mock> {
    let mut rules = Rules::default();
    rules.contracts.insert(SOL_CHAIN, contract());
    Relay::new(rules, BTreeMap::from([(SOL_CHAIN, Mock { sol: true, ..Mock::default() })]))
}

fn sol_offer(maker: &PrivateKeySigner, id: u8, side: Side, lamports: u64) -> SignedListing {
    let mut l = offer(maker, id, side, lamports, 20).listing;
    l.pair = "SOL/XMZ".into();
    l.chain_id = SOL_CHAIN;
    l.xmz_network = XmzNetwork::Testnet;
    l.sign(maker)
}

fn note(maker: &PrivateKeySigner, l: &SignedListing, account: u8) -> SignedSolAccount {
    use monzero_dex_evm::listing::{SolAccount, SOL_ACCOUNT_SCHEMA};
    SolAccount { schema: SOL_ACCOUNT_SCHEMA.into(), id: l.listing.id, account: B256::repeat_byte(account) }.sign(maker)
}

fn sol_tx(byte: u8) -> TxId {
    TxId::try_from(bs58::encode([byte; 64]).into_string()).unwrap()
}

#[tokio::test]
async fn a_solana_offer_needs_its_makers_signed_account_with_the_coin_in_it() {
    let (r, maker, other) = (sol_relay(), PrivateKeySigner::random(), PrivateKeySigner::random());
    let buy = sol_offer(&maker, 1, Side::BuyXmz, 50_000_000);
    assert_eq!(r.post(buy.clone(), NOW).await, Err(RelayError::SolAccount), "no account");
    assert_eq!(r.post_with(buy.clone(), Some(note(&other, &buy, 7)), NOW).await, Err(RelayError::Listing(ListingError::BadSignature)), "signed by someone else");
    let elsewhere = sol_offer(&maker, 2, Side::BuyXmz, 50_000_000);
    assert_eq!(r.post_with(buy.clone(), Some(note(&maker, &elsewhere, 7)), NOW).await, Err(RelayError::Listing(ListingError::Id)), "a note for another offer");
    assert_eq!(r.post_with(buy.clone(), Some(note(&maker, &buy, 7)), NOW).await, Err(RelayError::Unfunded), "an empty account");
    r.chains[&SOL_CHAIN].sol_balances.lock().unwrap().insert(B256::repeat_byte(7), U256::from(49_999_999u64));
    assert_eq!(r.post_with(buy.clone(), Some(note(&maker, &buy, 7)), NOW).await, Err(RelayError::Unfunded), "less than the offer");
    r.chains[&SOL_CHAIN].sol_balances.lock().unwrap().insert(B256::repeat_byte(7), U256::from(50_000_000u64));
    assert_eq!(r.post_with(buy.clone(), Some(note(&maker, &buy, 7)), NOW).await, Ok(true));
    assert_eq!(r.sol_accounts().len(), 1);
    // A seller needs only something for fees
    let sell = sol_offer(&maker, 3, Side::SellXmz, 50_000_000);
    assert_eq!(r.post_with(sell.clone(), Some(note(&maker, &sell, 8)), NOW).await, Err(RelayError::Unfunded));
    r.chains[&SOL_CHAIN].sol_balances.lock().unwrap().insert(B256::repeat_byte(8), U256::from(1u64));
    assert_eq!(r.post_with(sell.clone(), Some(note(&maker, &sell, 8)), NOW).await, Ok(true));
    // Saved and loaded again, the accounts are still with their offers
    let again = sol_relay();
    again.restore(serde_json::from_slice(&serde_json::to_vec(&r.snapshot()).unwrap()).unwrap());
    assert_eq!(again.sol_accounts().len(), 2);
    again.prune(NOW + 3600 + ARCHIVE_SECS + 1);
    assert_eq!(again.sol_accounts().len(), 0, "forgotten with their offers");
}

#[tokio::test]
async fn a_solana_claim_is_a_trade_only_with_the_makers_account_on_the_makers_side() {
    let (r, maker) = (sol_relay(), PrivateKeySigner::random());
    let (mine, theirs) = (B256::repeat_byte(7), B256::repeat_byte(9));
    r.chains[&SOL_CHAIN].sol_balances.lock().unwrap().insert(mine, U256::from(1_000_000_000u64));
    let sell = sol_offer(&maker, 1, Side::SellXmz, 50_000_000);
    r.post_with(sell.clone(), Some(note(&maker, &sell, 7)), NOW).await.unwrap();
    let nonce = u64::try_from(sell.listing.swap_nonce()).unwrap();
    let claim = |owner, claimer, lamports, nonce| ClaimLookup::FinalSol { owner, claimer, lamports, nonce, at: NOW + 60 };
    // Someone else's swap under the offer's nonce and amount: not this maker's trade, and the offer stays open
    for (i, wrong) in [claim(theirs, theirs, 50_000_000, nonce), claim(mine, theirs, 50_000_000, nonce), claim(theirs, mine, 49_000_000, nonce), claim(theirs, mine, 50_000_000, nonce + 1)].into_iter().enumerate() {
        let tx = sol_tx(i as u8 + 1);
        r.chains[&SOL_CHAIN].claims.lock().unwrap().insert(tx.clone(), wrong);
        assert_eq!(r.report(SOL_CHAIN, tx, NOW).await, Err(RelayError::UnknownListing), "case {i}");
    }
    assert_eq!(r.listings(None, NOW).len(), 1);
    assert_eq!(r.report(SOL_CHAIN, B256::repeat_byte(1).into(), NOW).await, Ok(PrintStatus::Pending), "the mock knows no such claim");
    let tx = sol_tx(0x55);
    r.chains[&SOL_CHAIN].claims.lock().unwrap().insert(tx.clone(), claim(theirs, mine, 50_000_000, nonce));
    assert_eq!(r.report(SOL_CHAIN, tx.clone(), NOW).await, Ok(PrintStatus::Recorded));
    let prints = r.prints(None, 0);
    assert_eq!((prints.len(), &prints[0].tx, prints[0].value), (1, &tx, U256::from(50_000_000u64)));
    // 0.05 SOL for 20 XMZ, in SOL per XMZ (lamports have 9 places)
    assert!((prints[0].price() - 0.0025).abs() < 1e-12, "{}", prints[0].price());
    assert!(r.listings(None, NOW).is_empty(), "filled");
    // The same swap made and claimed again is not a second trade
    let again = sol_tx(0x56);
    r.chains[&SOL_CHAIN].claims.lock().unwrap().insert(again.clone(), claim(theirs, mine, 50_000_000, nonce));
    assert_eq!(r.report(SOL_CHAIN, again, NOW).await, Ok(PrintStatus::Recorded));
    assert_eq!(r.prints(None, 0).len(), 1);
    // The id reads the same as text
    assert_eq!(serde_json::to_value(&tx).unwrap(), serde_json::json!(bs58::encode([0x55u8; 64]).into_string()));
    assert!(TxId::try_from("0x12".to_string()).is_err());
}
