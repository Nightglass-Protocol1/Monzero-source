use super::*;
use crate::listing::{Side, LISTING_SCHEMA};
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
    }
    .sign(&maker.key)
}

/// The whole take, as the letters travel through relay mail
fn take(maker: &Trader, book: &mut Maker, taker: &Trader, now: u64) -> (Result<SavedTrade, ExchangeError>, Option<SavedTrade>) {
    let l = book.listing.listing.clone();
    let (mut t, request) = Taker::start(&l, &taker.seed, &taker.key, now).unwrap();
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
        let mut book = Maker::new(listing(&maker, side), None);
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
    let mut book = Maker::new(listing(&maker, Side::SellXmz), None);
    let (a, _) = take(&maker, &mut book, &first, NOW);
    a.unwrap();
    let (b, saved) = take(&maker, &mut book, &second, NOW);
    assert_eq!(b.unwrap_err(), ExchangeError::Refused("this offer was already taken".into()));
    assert!(saved.is_none());
    // The first taker asking again (a lost answer) gets the same agreement
    let (again, saved) = take(&maker, &mut book, &first, NOW + 5);
    assert_eq!(again.unwrap().transcript, saved.unwrap().transcript);
    // After a restart the maker knows who took it
    let mut restarted = Maker::new(book.listing.clone(), book.taken_by);
    assert!(take(&maker, &mut restarted, &second, NOW).0.is_err());
}

#[test]
fn bad_takes_are_refused_without_our_frame() {
    let (maker, taker, other) = (trader(1), trader(2), trader(3));
    let mut book = Maker::new(listing(&maker, Side::BuyXmz), None);
    let l = book.listing.listing.clone();
    let (_, good) = Taker::start(&l, &taker.seed, &taker.key, NOW).unwrap();
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
