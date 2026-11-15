use super::*;

const NOW: u64 = 1_800_000_000;

fn contract() -> Address {
    Address::repeat_byte(0xc0)
}

fn rules() -> Rules {
    let mut r = Rules::default();
    r.contracts.insert(1, contract());
    r.max_value.insert(1, U256::from(10u64.pow(16)));
    r
}

fn listing(maker: &PrivateKeySigner) -> Listing {
    Listing {
        schema: LISTING_SCHEMA.into(),
        id: B256::repeat_byte(7),
        pair: "ETH/XMZ".into(),
        chain_id: 1,
        contract: contract(),
        side: Side::SellXmz,
        value: U256::from(5_000_000_000_000_000u64),
        xmz: 1_119_300_000_000_000,
        maker: maker.address(),
        mailbox: B256::repeat_byte(0x5a),
        xmz_network: XmzNetwork::Mainnet,
        created_at: NOW,
        expires_at: NOW + 3600,
        min_xmz: None,
    }
}

#[test]
fn a_signed_listing_verifies_and_round_trips_as_json() {
    let key = PrivateKeySigner::random();
    let signed = listing(&key).sign(&key);
    assert_eq!(signed.verify(&rules(), NOW + 10), Ok(Chain::EthMainnet));
    let json = serde_json::to_string(&signed).unwrap();
    assert!(json.contains(r#""xmz":"1119300000000000""#), "{json}");
    assert!(json.contains(r#""side":"sell-xmz""#), "{json}");
    let back: SignedListing = serde_json::from_str(&json).unwrap();
    assert_eq!(back, signed);
    assert_eq!(back.verify(&rules(), NOW + 10), Ok(Chain::EthMainnet));
    // Whitespace or field order in transit does not change what was signed
    let pretty: SignedListing = serde_json::from_str(&serde_json::to_string_pretty(&signed).unwrap()).unwrap();
    assert_eq!(pretty.verify(&rules(), NOW + 10), Ok(Chain::EthMainnet));
    assert_eq!(signed.listing.maker_role(), Role::Claimer);
}

#[test]
fn any_change_breaks_the_signature() {
    let key = PrivateKeySigner::random();
    let signed = listing(&key).sign(&key);
    let mut cheaper = signed.clone();
    cheaper.listing.xmz += 1;
    assert_eq!(cheaper.verify(&rules(), NOW), Err(ListingError::BadSignature));
    // Someone else cannot post in the maker's name
    let other = PrivateKeySigner::random();
    let forged = Listing { maker: key.address(), ..listing(&other) }.sign(&other);
    assert_eq!(forged.verify(&rules(), NOW), Err(ListingError::BadSignature));
    let mut garbage = signed.clone();
    garbage.signature = vec![1, 2, 3].into();
    assert_eq!(garbage.verify(&rules(), NOW), Err(ListingError::BadSignature));
}

#[test]
fn listings_outside_the_rules_are_refused() {
    let key = PrivateKeySigner::random();
    let base = listing(&key);
    let check = |l: Listing| l.sign(&key).verify(&rules(), NOW);
    assert_eq!(check(Listing { chain_id: 56, pair: "BNB/XMZ".into(), ..base.clone() }), Err(ListingError::Chain(56)));
    assert_eq!(check(Listing { chain_id: 10, ..base.clone() }), Err(ListingError::Chain(10)));
    assert_eq!(check(Listing { pair: "BNB/XMZ".into(), ..base.clone() }), Err(ListingError::Pair("BNB/XMZ".into())));
    assert_eq!(check(Listing { contract: Address::repeat_byte(1), ..base.clone() }), Err(ListingError::Contract));
    assert_eq!(check(Listing { xmz_network: XmzNetwork::Testnet, ..base.clone() }), Err(ListingError::XmzNetwork));
    assert_eq!(check(Listing { value: U256::ZERO, ..base.clone() }), Err(ListingError::Zero));
    assert_eq!(check(Listing { xmz: 0, ..base.clone() }), Err(ListingError::Zero));
    assert_eq!(check(Listing { value: U256::from(10u64.pow(16)) + U256::from(1), ..base.clone() }), Err(ListingError::TooLarge));
    assert_eq!(check(Listing { expires_at: NOW, ..base.clone() }), Err(ListingError::Time));
    assert_eq!(check(Listing { created_at: NOW + CLOCK_SKEW + 1, expires_at: NOW + 7200, ..base.clone() }), Err(ListingError::Time));
    assert_eq!(check(Listing { expires_at: NOW + MAX_LIFETIME + 1, ..base.clone() }), Err(ListingError::Time));
    assert_eq!(check(Listing { mailbox: B256::ZERO, ..base.clone() }), Err(ListingError::Mailbox));
    assert_eq!(check(Listing { id: B256::ZERO, ..base.clone() }), Err(ListingError::Id));
    assert_eq!(check(Listing { schema: "monzero-dex-listing/3".into(), ..base.clone() }), Err(ListingError::Schema));
    assert_eq!(check(Listing { schema: PARTIAL_SCHEMA.into(), ..base.clone() }), Err(ListingError::Schema), "a /2 listing names its minimum");
}

#[test]
fn malformed_json_is_refused() {
    let key = PrivateKeySigner::random();
    let json = serde_json::to_string(&listing(&key).sign(&key)).unwrap();
    for bad in [
        json.replace(r#""xmz":"1119300000000000""#, r#""xmz":1119300000000000"#),
        json.replace(r#""xmz":"1119300000000000""#, r#""xmz":"01119300000000000""#),
        json.replace(r#""xmz":"1119300000000000""#, r#""xmz":"-1""#),
        json.replace(r#""side":"sell-xmz""#, r#""side":"sell""#),
        json.replace(r#""pair":"#, r#""extra":1,"pair":"#),
    ] {
        assert!(serde_json::from_str::<SignedListing>(&bad).is_err(), "{bad}");
    }
}

#[test]
fn the_listing_id_is_the_swap_nonce() {
    let key = PrivateKeySigner::random();
    let mut id = [0u8; 32];
    id[..8].copy_from_slice(&0x0102030405060708u64.to_be_bytes());
    id[31] = 0xff;
    let l = Listing { id: B256::from(id), ..listing(&key) };
    assert_eq!(l.swap_nonce(), U256::from(0x0102030405060708u64));
}

#[test]
fn only_the_maker_can_cancel() {
    let key = PrivateKeySigner::random();
    let cancel = Cancel { schema: CANCEL_SCHEMA.into(), id: B256::repeat_byte(7), maker: key.address(), at: NOW };
    assert_eq!(cancel.clone().sign(&key).verify(), Ok(()));
    let other = PrivateKeySigner::random();
    assert_eq!(cancel.sign(&other).verify(), Err(ListingError::BadSignature));
}

fn partial(maker: &PrivateKeySigner) -> Listing {
    Listing { schema: PARTIAL_SCHEMA.into(), min_xmz: Some(100 * MIN_FILL_FLOOR), ..listing(maker) }
}

#[test]
fn a_partial_listing_verifies_and_a_whole_one_keeps_its_bytes() {
    let key = PrivateKeySigner::random();
    let signed = partial(&key).sign(&key);
    assert_eq!(signed.verify(&rules(), NOW + 10), Ok(Chain::EthMainnet));
    let json = serde_json::to_string(&signed).unwrap();
    assert!(json.contains(r#""minXmz":"10000000000000""#), "{json}");
    assert_eq!(serde_json::from_str::<SignedListing>(&json).unwrap(), signed);
    // A whole offer has no minXmz field at all: its signed form is as before partial offers
    let whole = serde_json::to_string(&listing(&key).sign(&key)).unwrap();
    assert!(!whole.contains("minXmz"), "{whole}");
    // The schema is part of what is signed: a partial offer relabelled is not the maker's
    let mut relabelled = signed.clone();
    relabelled.listing.min_xmz = Some(MIN_FILL_FLOOR);
    assert_eq!(relabelled.verify(&rules(), NOW + 10), Err(ListingError::BadSignature));
}

#[test]
fn a_partial_listing_needs_its_schema_and_a_sane_minimum() {
    let key = PrivateKeySigner::random();
    let check = |l: Listing| l.check(&rules(), NOW + 10);
    let base = partial(&key);
    assert_eq!(check(Listing { schema: LISTING_SCHEMA.into(), ..base.clone() }), Err(ListingError::Schema));
    assert_eq!(check(Listing { min_xmz: None, ..base.clone() }), Err(ListingError::Schema));
    assert_eq!(check(Listing { min_xmz: Some(MIN_FILL_FLOOR - 1), ..base.clone() }), Err(ListingError::Minimum));
    assert_eq!(check(Listing { min_xmz: Some(base.xmz + 1), ..base.clone() }), Err(ListingError::Minimum));
    assert!(check(Listing { min_xmz: Some(base.xmz), ..base.clone() }).is_ok(), "a minimum of all of it is a whole offer in effect");
}

#[test]
fn a_version_2_listing_may_stay_open_a_month_a_first_version_one_a_day() {
    let key = PrivateKeySigner::random();
    let check = |l: Listing| l.check(&rules(), NOW + 10);
    let whole = listing(&key);
    let month = MAX_LIFETIME_V2;
    assert_eq!(check(Listing { expires_at: whole.created_at + MAX_LIFETIME + 1, ..whole.clone() }), Err(ListingError::Time));
    let long = Listing { schema: PARTIAL_SCHEMA.into(), min_xmz: Some(whole.xmz), expires_at: whole.created_at + month, ..whole.clone() };
    assert!(check(long.clone()).is_ok(), "a month, as a version-2 offer");
    assert_eq!(check(Listing { expires_at: whole.created_at + month + 1, ..long.clone() }), Err(ListingError::Time));
    // A minimum of all of it: whole, taken only whole
    assert!(!long.is_partial());
    assert!(long.fill_ok(long.xmz) && !long.fill_ok(long.xmz / 2));
    assert!(partial(&key).is_partial());
}
