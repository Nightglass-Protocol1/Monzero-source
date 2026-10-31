use super::*;
use crate::secret::combined_spend_key;

struct Side {
    account: PrivateKeySigner,
    keys: LocalKeys,
}

fn side() -> Side {
    Side { account: PrivateKeySigner::random(), keys: LocalKeys::generate(&mut rand::thread_rng()) }
}

fn copy(keys: &LocalKeys) -> LocalKeys {
    LocalKeys::from_secrets(keys.secret.clone(), keys.view_share()).unwrap()
}

fn offer(owner: &Side, claimer: &Side) -> Offer {
    Offer {
        trade_id: B256::repeat_byte(0x42),
        chain_id: 97,
        contract: Address::repeat_byte(0xc0),
        owner: owner.account.address(),
        claimer: claimer.account.address(),
        asset: Address::ZERO,
        value: U256::from(10u64.pow(17)),
        duration1: 3 * 3600,
        duration2: 6 * 3600,
        xmz_amount: 5_000_000_000_000,
        xmz_network: XmzNetwork::Stagenet,
        expires_at: 2_000,
    }
}

fn frame(s: &Side, offer: &Offer, role: Role) -> SignedFrame {
    s.keys.frame(offer, role, &s.account, &mut rand::thread_rng()).unwrap()
}

/// A frame with any content, signed by `account` (a trader can sign whatever it likes)
fn forged(account: &PrivateKeySigner, offer: &Offer, role: Role, share: KeyShare) -> SignedFrame {
    let bytes = serde_json::to_vec(&Frame { schema: SCHEMA.into(), offer: offer.digest(), role, share }).unwrap();
    let signature = account.sign_message_sync(signing_message(offer.trade_id, role, &bytes).as_bytes()).unwrap();
    SignedFrame { frame: bytes.into(), signature: signature.as_bytes().to_vec().into() }
}

fn share_of(f: &SignedFrame) -> KeyShare {
    serde_json::from_slice::<Frame>(&f.frame).unwrap().share
}

#[test]
fn both_traders_agree_on_terms_and_the_shared_address() {
    let (owner, claimer) = (side(), side());
    let offer = offer(&owner, &claimer);
    let (fo, fc) = (frame(&owner, &offer, Role::Owner), frame(&claimer, &offer, Role::Claimer));

    let mut at_owner = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    assert_eq!(at_owner.accept(&fo, 1_000), Ok(Role::Owner));
    assert_eq!(at_owner.accept(&fc, 1_000), Ok(Role::Claimer));
    assert_eq!(at_owner.accept(&fc, 1_000), Ok(Role::Claimer), "the same frame again is fine");
    // the other order at the claimer
    let mut at_claimer = KeyExchange::new(offer.clone(), Role::Claimer, copy(&claimer.keys)).unwrap();
    at_claimer.accept(&fo, 1_000).unwrap();
    at_claimer.accept(&fc, 1_000).unwrap();

    let (a, b) = (at_owner.finish().unwrap(), at_claimer.finish().unwrap());
    assert_eq!(a.terms, b.terms);
    assert_eq!(a.shared, b.shared);
    assert_eq!(a.transcript, b.transcript);
    assert_eq!(a.terms.refund_commitment, owner.keys.secret.commitment());
    assert_eq!(a.terms.claim_commitment, claimer.keys.secret.commitment());
    assert_eq!((a.terms.chain, a.terms.owner, a.terms.claimer), (Chain::BscTestnet, offer.owner, offer.claimer));
    assert!(a.shared.opens(&combined_spend_key(&owner.keys.secret, &claimer.keys.secret)));
    assert!(a.shared.address().starts_with('N'), "stagenet address");
}

#[test]
fn a_second_different_frame_for_a_role_is_refused() {
    let (owner, claimer) = (side(), side());
    let offer = offer(&owner, &claimer);
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    ex.accept(&frame(&claimer, &offer, Role::Claimer), 1_000).unwrap();
    // Same keys, fresh proof: still a different frame
    assert_eq!(ex.accept(&frame(&claimer, &offer, Role::Claimer), 1_000), Err(E::Equivocation(Role::Claimer)));
}

#[test]
fn frames_must_be_signed_by_the_roles_account() {
    let (owner, claimer, stranger) = (side(), side(), side());
    let offer = offer(&owner, &claimer);
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    let by_stranger = Side { account: stranger.account.clone(), keys: copy(&claimer.keys) };
    assert_eq!(ex.accept(&frame(&by_stranger, &offer, Role::Claimer), 1_000), Err(E::BadSignature(Role::Claimer)));
    // The claimer cannot send a frame for the owner's role
    assert_eq!(ex.accept(&frame(&claimer, &offer, Role::Owner), 1_000), Err(E::BadSignature(Role::Owner)));
    // A signature over different bytes
    let mut f = frame(&claimer, &offer, Role::Claimer);
    f.signature = frame(&claimer, &offer, Role::Claimer).signature;
    assert_eq!(ex.accept(&f, 1_000), Err(E::BadSignature(Role::Claimer)));
}

#[test]
fn an_xmz_share_without_a_matching_proof_is_refused() {
    let (owner, claimer, other) = (side(), side(), side());
    let offer = offer(&owner, &claimer);
    let honest = share_of(&frame(&claimer, &offer, Role::Claimer));
    let unrelated = share_of(&frame(&other, &offer, Role::Claimer));
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();

    // Rogue key: an XMZ share chosen from the owner's (spend = T - owner's share) with the
    // claimer's own EVM point and proof
    let target = EdwardsPoint::identity() + &NgScalar::from(7u64) * &curve25519_dalek_ng::constants::ED25519_BASEPOINT_TABLE;
    let owner_spend = CompressedEdwardsY(owner.keys.secret.ed25519_public()).decompress().unwrap();
    let rogue = KeyShare { xmz_spend_public: B256::from((target - owner_spend).compress().to_bytes()), ..honest.clone() };
    assert_eq!(
        ex.accept(&forged(&claimer.account, &offer, Role::Claimer, rogue), 1_000),
        Err(E::InvalidShare("cross-curve proof does not verify"))
    );
    // EVM point of one secret, XMZ point and proof of another
    let mixed = KeyShare { secp_public: unrelated.secp_public, ..honest.clone() };
    assert_eq!(
        ex.accept(&forged(&claimer.account, &offer, Role::Claimer, mixed), 1_000),
        Err(E::InvalidShare("cross-curve proof does not verify"))
    );
    // Someone else's complete proof for other points
    let swapped = KeyShare { proof: unrelated.proof.clone(), ..honest.clone() };
    assert_eq!(
        ex.accept(&forged(&claimer.account, &offer, Role::Claimer, swapped), 1_000),
        Err(E::InvalidShare("cross-curve proof does not verify"))
    );
    let zero_view = KeyShare { view_share: B256::ZERO, ..honest.clone() };
    assert_eq!(ex.accept(&forged(&claimer.account, &offer, Role::Claimer, zero_view), 1_000), Err(E::InvalidShare("zero view share")));
    let mut identity = [0u8; 32];
    identity[0] = 1;
    let small = KeyShare { xmz_spend_public: B256::from(identity), ..honest.clone() };
    assert!(matches!(ex.accept(&forged(&claimer.account, &offer, Role::Claimer, small), 1_000), Err(E::InvalidShare(_))));
    // None of the refused frames took the slot: the honest one is still accepted
    ex.accept(&forged(&claimer.account, &offer, Role::Claimer, honest), 1_000).unwrap();
}

#[test]
fn a_copied_share_is_refused_at_finish() {
    let (owner, claimer) = (side(), side());
    let offer = offer(&owner, &claimer);
    let fo = frame(&owner, &offer, Role::Owner);
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    ex.accept(&fo, 1_000).unwrap();
    // The claimer resends the owner's share (with its valid proof) under its own signature
    ex.accept(&forged(&claimer.account, &offer, Role::Claimer, share_of(&fo)), 1_000).unwrap();
    assert!(matches!(ex.finish(), Err(E::InvalidShare("both traders sent the same key"))));
}

#[test]
fn frames_are_bound_to_the_offer_time_and_encoding() {
    let (owner, claimer) = (side(), side());
    let offer = offer(&owner, &claimer);
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    let other = Offer { value: offer.value + U256::from(1), ..offer.clone() };
    assert_eq!(ex.accept(&frame(&claimer, &other, Role::Claimer), 1_000), Err(E::WrongContext));
    let fc = frame(&claimer, &offer, Role::Claimer);
    assert_eq!(ex.accept(&fc, offer.expires_at), Err(E::Expired));
    // Pretty-printed JSON of the same frame, validly signed: one encoding only
    let pretty = serde_json::to_vec_pretty(&serde_json::from_slice::<serde_json::Value>(&fc.frame).unwrap()).unwrap();
    let sig = claimer.account.sign_message_sync(signing_message(offer.trade_id, Role::Claimer, &pretty).as_bytes()).unwrap();
    let f = SignedFrame { frame: pretty.into(), signature: sig.as_bytes().to_vec().into() };
    assert!(matches!(ex.accept(&f, 1_000), Err(E::Malformed(_))));
    assert!(matches!(ex.finish(), Err(E::Incomplete)));
}

#[test]
fn our_own_frame_must_carry_our_keys() {
    let (owner, claimer, other) = (side(), side(), side());
    let offer = offer(&owner, &claimer);
    let mut ex = KeyExchange::new(offer.clone(), Role::Owner, copy(&owner.keys)).unwrap();
    // Signed by our account but with other keys (e.g. a stale frame from a lost vault)
    let wrong = Side { account: owner.account.clone(), keys: copy(&other.keys) };
    assert_eq!(ex.accept(&frame(&wrong, &offer, Role::Owner), 1_000), Err(E::NotOurKeys));
    // Our secret but another view share
    let other_view = Side { account: owner.account.clone(), keys: LocalKeys::from_secrets(owner.keys.secret.clone(), other.keys.view_share()).unwrap() };
    assert_eq!(ex.accept(&frame(&other_view, &offer, Role::Owner), 1_000), Err(E::NotOurKeys));
}

#[test]
fn unknown_chains_and_out_of_range_secrets_are_refused() {
    let (owner, claimer) = (side(), side());
    let bad = Offer { chain_id: 1, ..offer(&owner, &claimer) };
    assert!(matches!(KeyExchange::new(bad, Role::Owner, copy(&owner.keys)), Err(E::UnknownChain(1))));
    let mut top = [0u8; 32];
    top[31] = 0x10;
    let big = SecretShare::from_ed25519_le(top).unwrap();
    assert!(LocalKeys::from_secrets(big, owner.keys.view_share()).is_err());
}
