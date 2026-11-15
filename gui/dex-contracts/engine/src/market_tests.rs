use super::*;
use crate::listing::{Side, LISTING_SCHEMA, PARTIAL_SCHEMA};
use crate::mail::Letter;
use crate::xmz::XmzNetwork;
use alloy::primitives::{B256, U256};
use rand_core::OsRng;

const NOW: u64 = 1_800_000_000;

fn at(now: u64) -> Heights {
    Heights { now, evm_from_block: 100, xmz_restore_height: 5000 }
}

struct Trader {
    seed: SwapSeed,
    key: PrivateKeySigner,
}

fn trader(byte: u8) -> Trader {
    Trader { seed: SwapSeed::from_bytes([byte; 32]), key: PrivateKeySigner::random() }
}

fn listing(maker: &Trader, side: Side) -> SignedListing {
    let id = B256::repeat_byte(0x61);
    Listing {
        schema: LISTING_SCHEMA.into(),
        id,
        pair: "ETH/XMZ".into(),
        chain_id: 31337,
        contract: Address::repeat_byte(0xc0),
        side,
        value: U256::from(5_000_000_000_000_000u64),
        xmz: 11_193 * 100_000_000_000,
        maker: maker.key.address(),
        mailbox: maker.seed.mailbox(id, true).address(),
        xmz_network: XmzNetwork::Testnet,
        created_at: NOW,
        expires_at: NOW + 3600,
        min_xmz: None,
    }
    .sign(&maker.key)
}

/// A partial offer: 1,000 XMZ, at least 100 a take
fn partial(maker: &Trader, side: Side) -> SignedListing {
    let mut l = listing(maker, side).listing;
    l.schema = crate::listing::PARTIAL_SCHEMA.into();
    l.xmz = 1_000 * 100_000_000_000;
    l.min_xmz = Some(100 * 100_000_000_000);
    l.sign(&maker.key)
}

/// The whole take, as the letters travel through relay mail
fn take(maker: &Trader, book: &mut Maker, taker: &Trader, now: u64) -> (Result<SavedTrade, ExchangeError>, Option<SavedTrade>) {
    take_some(maker, book, taker, None, now)
}

fn take_some(maker: &Trader, book: &mut Maker, taker: &Trader, xmz: Option<u64>, now: u64) -> (Result<SavedTrade, ExchangeError>, Option<SavedTrade>) {
    let l = book.listing.listing.clone();
    let (mut t, request) = Taker::start(&l, xmz, &taker.seed, &taker.key, now).unwrap();
    let sealed = Letter::Take(request).seal(l.mailbox, &mut OsRng).unwrap();
    let Some(Letter::Take(received)) = Letter::open(&maker.seed.mailbox(l.id, true), &sealed) else { panic!("the maker opens the take") };
    let (answer, maker_saved) = book.answer(&received, &maker.seed, &maker.key, at(now));
    let sealed = Letter::Answer(answer).seal(received.reply_to, &mut OsRng).unwrap();
    let Some(Letter::Answer(answer)) = Letter::open(&taker.seed.mailbox(l.id, false), &sealed) else { panic!("the taker opens the answer") };
    (t.on_answer(&answer, at(now)), maker_saved)
}

#[test]
fn a_taken_listing_is_the_same_trade_on_both_sides() {
    for side in [Side::SellXmz, Side::BuyXmz] {
        let (maker, taker) = (trader(1), trader(2));
        let mut book = Maker::new(listing(&maker, side), None, None);
        let (taker_saved, maker_saved) = take(&maker, &mut book, &taker, NOW + 10);
        let (t, m) = (taker_saved.unwrap(), maker_saved.unwrap());
        assert_eq!(t.offer, m.offer);
        assert_eq!(t.transcript, m.transcript);
        assert_ne!(t.role, m.role);
        assert_eq!(m.role, book.listing.listing.maker_role());
        assert_eq!(t.offer.trade_id, book.listing.listing.id);
        assert_eq!(t.swap_nonce(), book.listing.listing.swap_nonce(), "the relay ties the claim to the listing by this");
        assert_eq!((t.offer.duration1, t.offer.duration2, t.lock_deadline), (DURATION, DURATION, NOW + 10 + lock_wait(Chain::Local)));
        // Each side rebuilds the agreement from its own seed after a restart
        t.agreed(&taker.seed, &taker.key).unwrap();
        m.agreed(&maker.seed, &maker.key).unwrap();
        assert_eq!(book.taken_by, Some(taker.key.address()));
    }
}

#[test]
fn a_listing_is_filled_once() {
    let (maker, first, second) = (trader(1), trader(2), trader(3));
    let mut book = Maker::new(listing(&maker, Side::SellXmz), None, None);
    let (a, _) = take(&maker, &mut book, &first, NOW);
    a.unwrap();
    let (b, saved) = take(&maker, &mut book, &second, NOW);
    assert_eq!(b.unwrap_err(), ExchangeError::Refused("this offer was already taken".into()));
    assert!(saved.is_none());
    // The first taker asking again (a lost answer) gets the same agreement
    let (again, saved) = take(&maker, &mut book, &first, NOW + 5);
    assert_eq!(again.unwrap().transcript, saved.unwrap().transcript);
    // After a restart the maker knows who took it
    let mut restarted = Maker::new(book.listing.clone(), book.taken_by, book.taken_xmz);
    assert!(take(&maker, &mut restarted, &second, NOW).0.is_err());
}

#[test]
fn bad_takes_are_refused_without_our_frame() {
    let (maker, taker, other) = (trader(1), trader(2), trader(3));
    let mut book = Maker::new(listing(&maker, Side::BuyXmz), None, None);
    let l = book.listing.listing.clone();
    let (_, good) = Taker::start(&l, None, &taker.seed, &taker.key, NOW).unwrap();
    let refused = |a: &Answer| matches!(a.response, KeyResponse::Refused(_));
    // Claims another account than the one that signed its frame
    let lie = Take { taker: other.key.address(), ..good.clone() };
    let (answer, saved) = book.answer(&lie, &maker.seed, &maker.key, at(NOW));
    assert!(refused(&answer) && saved.is_none());
    // The maker's own account, an expired offer, another listing
    let own = Take { taker: maker.key.address(), ..good.clone() };
    assert!(refused(&book.answer(&own, &maker.seed, &maker.key, at(NOW)).0));
    assert!(refused(&book.answer(&good, &maker.seed, &maker.key, at(l.expires_at)).0));
    let elsewhere = Take { listing: B256::repeat_byte(9), ..good.clone() };
    assert!(refused(&book.answer(&elsewhere, &maker.seed, &maker.key, at(NOW)).0));
    assert_eq!(book.taken_by, None, "nothing was taken");
    // The honest take still works afterwards
    assert!(book.answer(&good, &maker.seed, &maker.key, at(NOW)).1.is_some());
}

const XMZ: u64 = 100_000_000_000;

#[test]
fn part_of_a_partial_offer_is_the_same_trade_on_both_sides_at_the_offer_price() {
    for side in [Side::SellXmz, Side::BuyXmz] {
        let (maker, taker) = (trader(1), trader(2));
        let mut book = Maker::new(partial(&maker, side), None, None);
        let l = book.listing.listing.clone();
        let (t, m) = take_some(&maker, &mut book, &taker, Some(250 * XMZ), NOW + 10);
        let (t, m) = (t.unwrap(), m.unwrap());
        assert_eq!(t.offer, m.offer);
        assert_eq!(t.offer.xmz_amount, 250 * XMZ);
        assert_eq!(t.offer.value, l.value / U256::from(4), "a quarter of the XMZ for a quarter of the coin");
        assert_eq!(t.offer.trade_id, l.id, "the claim still ties to the listing");
        assert_eq!((book.taken_by, book.taken_xmz), (Some(taker.key.address()), Some(250 * XMZ)));
        m.agreed(&maker.seed, &maker.key).unwrap();
    }
}

#[test]
fn a_partial_offer_takes_only_amounts_its_rules_allow() {
    let (maker, taker) = (trader(1), trader(2));
    let l = partial(&maker, Side::SellXmz).listing;
    // Below the minimum, above the offer, and a take that would leave less than the minimum
    for bad in [99 * XMZ, 1_001 * XMZ, 950 * XMZ, 0] {
        assert!(Taker::start(&l, Some(bad), &taker.seed, &taker.key, NOW).is_err(), "{bad}");
    }
    for good in [100 * XMZ, 900 * XMZ, 1_000 * XMZ] {
        assert!(Taker::start(&l, Some(good), &taker.seed, &taker.key, NOW).is_ok(), "{good}");
    }
    // A whole offer takes no amount but all of it
    let whole = listing(&maker, Side::SellXmz).listing;
    assert!(Taker::start(&whole, Some(whole.xmz / 2), &taker.seed, &taker.key, NOW).is_err());
    assert!(Taker::start(&whole, Some(whole.xmz), &taker.seed, &taker.key, NOW).is_err(), "no amount on a whole offer");
    // The maker checks too: a take letter naming an amount the offer does not allow is refused
    let mut book = Maker::new(partial(&maker, Side::SellXmz), None, None);
    let (_, mut letter) = Taker::start(&l, Some(200 * XMZ), &taker.seed, &taker.key, NOW).unwrap();
    letter.xmz = Some(50 * XMZ);
    let (answer, saved) = book.answer(&letter, &maker.seed, &maker.key, at(NOW));
    assert!(matches!(answer.response, KeyResponse::Refused(_)) && saved.is_none());
    // A different amount than the frame was made for: the frames do not match the maker's offer
    letter.xmz = Some(300 * XMZ);
    assert!(book.answer(&letter, &maker.seed, &maker.key, at(NOW)).1.is_none());
    assert_eq!(book.taken_by, None);
    let mut whole_book = Maker::new(listing(&maker, Side::SellXmz), None, None);
    let (_, mut letter) = Taker::start(&whole, None, &taker.seed, &taker.key, NOW).unwrap();
    letter.xmz = Some(whole.xmz / 2);
    assert!(whole_book.answer(&letter, &maker.seed, &maker.key, at(NOW)).1.is_none());
    // An app from before month-long offers names all of a whole version-2 offer: still the same trade
    let mut long = listing(&maker, Side::SellXmz);
    long.listing.schema = PARTIAL_SCHEMA.into();
    long.listing.min_xmz = Some(long.listing.xmz);
    let long = long.listing.sign(&maker.key);
    let mut long_book = Maker::new(long.clone(), None, None);
    let (_, mut named) = Taker::start(&long.listing, None, &taker.seed, &taker.key, NOW).unwrap();
    assert_eq!(named.xmz, None, "a whole offer is taken without an amount");
    named.xmz = Some(long.listing.xmz);
    assert!(long_book.answer(&named, &maker.seed, &maker.key, at(NOW)).1.is_some());
}

#[test]
fn a_partial_offer_is_still_taken_once() {
    let (maker, first, second) = (trader(1), trader(2), trader(3));
    let mut book = Maker::new(partial(&maker, Side::SellXmz), None, None);
    take_some(&maker, &mut book, &first, Some(300 * XMZ), NOW).0.unwrap();
    assert!(take_some(&maker, &mut book, &second, Some(300 * XMZ), NOW).0.is_err(), "the rest is a new offer");
    // The first taker asking again for another amount is refused; the same amount is answered again
    assert!(take_some(&maker, &mut book, &first, Some(400 * XMZ), NOW).1.is_none());
    assert!(take_some(&maker, &mut book, &first, Some(300 * XMZ), NOW + 5).1.is_some());
}

#[test]
fn the_rest_of_a_partial_offer_keeps_its_price_and_never_adds_up_to_more() {
    for side in [Side::SellXmz, Side::BuyXmz] {
        let maker = trader(1);
        let mut l = partial(&maker, side).listing;
        // An awkward price: rounding must still be exact across fills
        l.value = U256::from(1_000_000_007u64);
        let mut paid = U256::ZERO;
        let mut taken = 0;
        let mut current = l.clone();
        for (i, part) in [333 * XMZ, 100 * XMZ, 167 * XMZ].into_iter().enumerate() {
            paid += current.fill_value(part);
            taken += part;
            current = current.remainder(part, B256::repeat_byte(i as u8 + 1), B256::repeat_byte(0x77), NOW + 60).unwrap();
            assert_eq!(current.min_xmz, l.min_xmz);
            assert_eq!(current.expires_at, l.expires_at);
            assert_eq!(current.schema, l.schema);
        }
        assert_eq!(current.xmz, l.xmz - taken);
        assert_eq!(paid + current.value, l.value, "fills and what is left add up to the offer exactly");
        assert_eq!(current.fill_value(current.xmz), current.value);
        assert!(current.remainder(current.xmz, B256::repeat_byte(9), B256::repeat_byte(9), NOW).is_none(), "nothing left");
        // Rounding is in the maker's favour
        let third = l.fill_value(333 * XMZ);
        let exact = U256::from(1_000_000_007u64) * U256::from(333u64) / U256::from(1000u64);
        match side {
            Side::SellXmz => assert_eq!(third, exact + U256::from(1)),
            Side::BuyXmz => assert_eq!(third, exact),
        }
    }
}
