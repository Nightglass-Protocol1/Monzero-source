use super::*;
use crate::negotiation::Role;

fn account(byte: u8) -> PrivateKeySigner {
    PrivateKeySigner::from_bytes(&B256::repeat_byte(byte)).unwrap()
}

fn leg(chain: Chain, contract: u8, value: u64, d2: u64) -> Leg {
    Leg { chain_id: chain.id(), contract: Address::repeat_byte(contract), asset: Address::ZERO, value: U256::from(value), duration1: 3600, duration2: d2 }
}

fn offer(a: Chain, b: Chain) -> DirectOffer {
    DirectOffer {
        trade_id: keccak256(b"a direct trade"),
        leader: account(1).address(),
        follower: account(2).address(),
        leg_a: leg(a, 0xa0, 10u64.pow(16), 8 * 3600),
        leg_b: leg(b, 0xb0, 3 * 10u64.pow(9), 4 * 3600),
        expires_at: 2_000,
    }
}

fn seeds() -> (SwapSeed, SwapSeed) {
    (SwapSeed::from_wallet_seed(b"leader wallet"), SwapSeed::from_wallet_seed(b"follower wallet"))
}

fn agree(o: &DirectOffer) -> (DirectAgreed, DirectAgreed) {
    let (ls, fs) = seeds();
    let mut leader = DirectSession::start(&ls, o.clone(), Side::Leader, &account(1), 1_000).unwrap();
    let mut follower = DirectSession::start(&fs, o.clone(), Side::Follower, &account(2), 1_000).unwrap();
    let at_follower = follower.accept_theirs(&leader.frame().clone(), 1_000).unwrap();
    let at_leader = leader.accept_theirs(&follower.frame().clone(), 1_000).unwrap();
    (at_leader, at_follower)
}

#[test]
fn both_traders_agree_on_both_legs() {
    let o = offer(Chain::EthSepolia, Chain::BscTestnet);
    let (l, f) = agree(&o);
    assert_eq!(l.transcript, f.transcript);
    assert_eq!(l.leg_a, f.leg_a);
    assert_eq!(l.leg_b, f.leg_b);
    let (ls, fs) = seeds();
    let a = ls.direct_keys(&o, Side::Leader).claim.unwrap();
    // One secret opens both claims; each owner refunds with its own
    assert_eq!(l.leg_a.claim_commitment, a.commitment());
    assert_eq!(l.leg_b.claim_commitment, a.commitment());
    assert_eq!(l.leg_a.refund_commitment, ls.direct_keys(&o, Side::Leader).refund.commitment());
    assert_eq!(l.leg_b.refund_commitment, fs.direct_keys(&o, Side::Follower).refund.commitment());
    assert_eq!((l.leg_a.owner, l.leg_a.claimer), (o.leader, o.follower));
    assert_eq!((l.leg_b.owner, l.leg_b.claimer), (o.follower, o.leader));
    assert!(l.sol_a.is_none() && l.sol_b.is_none());
}

#[test]
fn a_solana_leg_binds_both_accounts_and_the_ed25519_points() {
    let o = offer(Chain::EthSepolia, Chain::SolDevnet);
    let (l, f) = agree(&o);
    let (ls, fs) = seeds();
    let sol = l.sol_b.unwrap();
    assert_eq!(f.sol_b, Some(sol));
    assert!(l.sol_a.is_none());
    assert_eq!(sol.claim_point, ls.direct_keys(&o, Side::Leader).claim.unwrap().ed25519_public());
    assert_eq!(sol.refund_point, fs.direct_keys(&o, Side::Follower).refund.ed25519_public());
    assert_eq!(sol.owner, crate::sol::account_key(&account(2).to_bytes().0).verifying_key().to_bytes());
    assert_eq!(sol.claimer, crate::sol::account_key(&account(1).to_bytes().0).verifying_key().to_bytes());
}

#[test]
fn a_restart_remakes_the_same_frame_and_rebuilds_the_saved_trade() {
    let o = offer(Chain::SolDevnet, Chain::BscTestnet);
    let (ls, fs) = seeds();
    let theirs = fs.direct_frame(&o, Side::Follower, &account(2)).unwrap();
    let mut session = DirectSession::start(&ls, o.clone(), Side::Leader, &account(1), 1_000).unwrap();
    session.accept_theirs(&theirs, 1_000).unwrap();
    let saved = session.saved(DirectTiming::DEFAULT, 5_000, 10, 20).unwrap();
    assert_eq!(ls.direct_frame(&o, Side::Leader, &account(1)).unwrap(), *session.frame());
    // After the offer expired, from the journal and the seed alone
    let again = saved.agreed(&SwapSeed::from_wallet_seed(b"leader wallet"), &account(1)).unwrap();
    assert_eq!(again.transcript, saved.transcript);
    // Another wallet cannot rebuild it
    assert!(saved.agreed(&SwapSeed::from_wallet_seed(b"another wallet"), &account(1)).is_err());
    assert_ne!(saved.nonce_a(), saved.nonce_b());
}

#[test]
fn offers_that_break_the_rules_are_refused() {
    let mut o = offer(Chain::EthSepolia, Chain::BscTestnet);
    o.leg_b.duration2 = o.leg_a.duration2;
    assert!(o.check().is_err(), "B must close before A");
    let mut o = offer(Chain::EthSepolia, Chain::EthSepolia);
    o.leg_b.contract = o.leg_a.contract;
    assert!(o.check().is_err(), "one contract for both legs");
    let mut o = offer(Chain::EthSepolia, Chain::BscTestnet);
    o.follower = o.leader;
    assert!(o.check().is_err());
    let mut o = offer(Chain::EthSepolia, Chain::BscTestnet);
    o.leg_a.duration1 = 60;
    assert!(o.check().is_err());
    let mut o = offer(Chain::EthSepolia, Chain::BscTestnet);
    o.leg_b.value = U256::ZERO;
    assert!(o.check().is_err());
}

/// A frame with any content, signed by `signer` as `side`
fn forged(o: &DirectOffer, side: Side, signer: &PrivateKeySigner, change: impl FnOnce(&mut DirectFrame)) -> SignedFrame {
    let (ls, _) = seeds();
    let honest = ls.direct_frame(o, Side::Leader, &account(1)).unwrap();
    let mut frame: DirectFrame = serde_json::from_slice(&honest.frame).unwrap();
    frame.side = side;
    change(&mut frame);
    let bytes = serde_json::to_vec(&frame).unwrap();
    let signature = signer.sign_message_sync(signing_message(o.trade_id, side, &bytes).as_bytes()).unwrap();
    SignedFrame { frame: bytes.into(), signature: signature.as_bytes().to_vec().into() }
}

fn follower_exchange(o: &DirectOffer) -> DirectExchange {
    let (_, fs) = seeds();
    DirectExchange::new(o.clone(), Side::Follower, &fs.direct_keys(o, Side::Follower)).unwrap()
}

#[test]
fn a_claim_key_whose_points_hide_different_secrets_is_refused() {
    let o = offer(Chain::EthSepolia, Chain::SolDevnet);
    let other = SecretShare::from_uniform_bytes(&[9u8; 64]).unwrap();
    // The ed25519 point (what Solana checks) swapped for another secret's, proof left as it was
    let bad = forged(&o, Side::Leader, &account(1), |f| f.claim.as_mut().unwrap().ed = B256::from(other.ed25519_public()));
    assert_eq!(follower_exchange(&o).accept(&bad, 1_000), Err(E::InvalidShare("cross-curve proof does not verify")));
}

#[test]
fn frames_must_come_from_the_offer_accounts_and_carry_the_right_keys() {
    let o = offer(Chain::EthSepolia, Chain::BscTestnet);
    // Signed by someone else
    let bad = forged(&o, Side::Leader, &account(9), |_| {});
    assert_eq!(follower_exchange(&o).accept(&bad, 1_000), Err(E::BadSignature(Role::Owner)));
    // A follower frame with a claim key
    let bad = forged(&o, Side::Follower, &account(2), |_| {});
    let (ls, _) = seeds();
    let mut leader_side = DirectExchange::new(o.clone(), Side::Leader, &ls.direct_keys(&o, Side::Leader)).unwrap();
    assert_eq!(leader_side.accept(&bad, 1_000), Err(E::InvalidShare("only the leader sends a claim key")));
    // A leader frame without one
    let bad = forged(&o, Side::Leader, &account(1), |f| f.claim = None);
    assert_eq!(follower_exchange(&o).accept(&bad, 1_000), Err(E::InvalidShare("only the leader sends a claim key")));
    // A Solana account on a trade with no Solana leg
    let bad = forged(&o, Side::Leader, &account(1), |f| f.sol_account = Some(B256::repeat_byte(5)));
    assert_eq!(follower_exchange(&o).accept(&bad, 1_000), Err(E::InvalidShare("Solana account")));
    // Another offer
    let bad = forged(&o, Side::Leader, &account(1), |f| f.offer = B256::repeat_byte(1));
    assert_eq!(follower_exchange(&o).accept(&bad, 1_000), Err(E::WrongContext));
}

#[test]
fn a_second_different_frame_expiry_and_reused_keys_are_refused() {
    let o = offer(Chain::EthSepolia, Chain::BscTestnet);
    let (ls, _) = seeds();
    let honest = ls.direct_frame(&o, Side::Leader, &account(1)).unwrap();
    let mut ex = follower_exchange(&o);
    assert_eq!(ex.accept(&honest, 1_000), Ok(Side::Leader));
    assert_eq!(ex.accept(&honest, 1_000), Ok(Side::Leader), "the same frame again is fine");
    let other = forged(&o, Side::Leader, &account(1), |f| f.refund.ed = f.claim.as_ref().unwrap().ed);
    assert_eq!(ex.accept(&other, 1_000), Err(E::Equivocation(Role::Owner)));
    assert_eq!(follower_exchange(&o).accept(&honest, 2_000), Err(E::Expired));

    // A leader whose refund key is its claim key: refunding A would publish the claim secret
    let reused = forged(&o, Side::Leader, &account(1), |f| {
        let c = f.claim.as_ref().unwrap();
        f.refund = KeyPoints { secp: c.secp, ed: c.ed };
    });
    let mut ex = follower_exchange(&o);
    ex.accept(&reused, 1_000).unwrap();
    let (_, fs) = seeds();
    ex.accept(&fs.direct_frame(&o, Side::Follower, &account(2)).unwrap(), 1_000).unwrap();
    assert_eq!(ex.finish().err(), Some(E::InvalidShare("a key was used twice")));
}
