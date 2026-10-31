use super::*;
use crate::machine::Timing;
use crate::xmz::XmzNetwork;
use alloy::primitives::{Address, U256};
use std::time::Duration;

const NOW: u64 = 1_000;

struct Trader {
    seed: SwapSeed,
    account: PrivateKeySigner,
}

fn trader(name: &[u8]) -> Trader {
    Trader { seed: SwapSeed::from_wallet_seed(name), account: PrivateKeySigner::random() }
}

fn offer(owner: &Trader, claimer: &Trader) -> Offer {
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

fn session(t: &Trader, offer: &Offer, role: Role) -> Session {
    Session::start(&t.seed, offer.clone(), role, &t.account, NOW).unwrap()
}

#[test]
fn one_round_trip_agrees_on_both_sides() {
    let (owner, claimer) = (trader(b"owner"), trader(b"claimer"));
    let offer = offer(&owner, &claimer);
    let (mut asks, mut answers) = (session(&claimer, &offer, Role::Claimer), session(&owner, &offer, Role::Owner));
    assert!(matches!(answers.agreed(), Err(ExchangeError::Negotiation(NegotiationError::Incomplete))));

    let response = answers.respond(&asks.request(), NOW);
    let at_claimer = asks.on_response(&response, NOW).unwrap();
    let at_owner = answers.agreed().unwrap();
    assert_eq!(at_claimer.terms, at_owner.terms);
    assert_eq!(at_claimer.shared, at_owner.shared);
    assert_eq!(at_claimer.transcript, at_owner.transcript);

    // Asking again (a lost answer, a restart on either side) gets the same answer
    assert_eq!(answers.respond(&asks.request(), NOW), response);
    let mut restarted = session(&owner, &offer, Role::Owner);
    assert_eq!(restarted.respond(&session(&claimer, &offer, Role::Claimer).request(), NOW), response);
    assert_eq!(restarted.agreed().unwrap().transcript, at_owner.transcript);
}

#[test]
fn a_stranger_gets_no_frame() {
    let (owner, claimer, stranger) = (trader(b"owner"), trader(b"claimer"), trader(b"stranger"));
    let offer = offer(&owner, &claimer);
    let mut answers = session(&owner, &offer, Role::Owner);
    // The stranger's frame for the claimer's role, signed by the stranger's account
    let forged = KeyRequest { trade_id: offer.trade_id, frame: stranger.seed.frame(&offer, Role::Claimer, &stranger.account).unwrap() };
    let KeyResponse::Refused(why) = answers.respond(&forged, NOW) else { panic!("answered a stranger") };
    assert!(why.contains("not signed"), "{why}");
    // Another trade id
    let mut other = session(&claimer, &offer, Role::Claimer).request();
    other.trade_id = B256::repeat_byte(0x43);
    assert_eq!(answers.respond(&other, NOW), KeyResponse::Refused("unknown trade".into()));
    // Our own frame sent back to us
    assert!(matches!(answers.respond(&answers.request(), NOW), KeyResponse::Refused(_)));
    // After the offer expired
    let late = session(&claimer, &offer, Role::Claimer).request();
    assert!(matches!(answers.respond(&late, offer.expires_at), KeyResponse::Refused(_)));
    // None of that took the claimer's slot
    assert!(matches!(answers.respond(&late, NOW), KeyResponse::Frame(_)));
}

#[test]
fn answers_must_come_from_the_other_role() {
    let (owner, claimer) = (trader(b"owner"), trader(b"claimer"));
    let offer = offer(&owner, &claimer);
    let mut asks = session(&claimer, &offer, Role::Claimer);
    assert_eq!(asks.on_response(&KeyResponse::Frame(asks.request().frame), NOW).err(), Some(ExchangeError::Reflected));
    assert_eq!(asks.on_response(&KeyResponse::Refused("busy".into()), NOW).err(), Some(ExchangeError::Refused("busy".into())));
    let wrong = trader(b"wrong").seed.frame(&offer, Role::Owner, &trader(b"x").account).unwrap();
    assert!(matches!(asks.on_response(&KeyResponse::Frame(wrong), NOW), Err(ExchangeError::Negotiation(NegotiationError::BadSignature(Role::Owner)))));
    // Still open for the real answer
    let response = session(&owner, &offer, Role::Owner).respond(&asks.request(), NOW);
    asks.on_response(&response, NOW).unwrap();
}

#[test]
fn only_the_offers_account_can_start() {
    let (owner, claimer) = (trader(b"owner"), trader(b"claimer"));
    let offer = offer(&owner, &claimer);
    assert!(matches!(
        Session::start(&owner.seed, offer.clone(), Role::Claimer, &owner.account, NOW),
        Err(ExchangeError::NotOurAccount(Role::Claimer))
    ));
}

#[tokio::test]
async fn the_exchange_runs_over_libp2p() {
    use futures::StreamExt;
    use libp2p::request_response::{Event, Message};
    use libp2p::swarm::SwarmEvent;
    use libp2p::{noise, tcp, yamux, Swarm, SwarmBuilder};

    fn node() -> Swarm<p2p::Behaviour> {
        SwarmBuilder::with_new_identity()
            .with_tokio()
            .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)
            .unwrap()
            .with_behaviour(|_| p2p::behaviour())
            .unwrap()
            .with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(30)))
            .build()
    }

    let (owner, claimer) = (trader(b"owner"), trader(b"claimer"));
    let offer = offer(&owner, &claimer);
    let (mut maker_session, mut taker_session) = (session(&owner, &offer, Role::Owner), session(&claimer, &offer, Role::Claimer));
    let (mut maker, mut taker) = (node(), node());
    let maker_id = *maker.local_peer_id();

    let run = async {
        maker.listen_on("/ip4/127.0.0.1/tcp/0".parse().unwrap()).unwrap();
        let address = loop {
            if let SwarmEvent::NewListenAddr { address, .. } = maker.select_next_some().await {
                break address;
            }
        };
        taker.dial(address).unwrap();
        loop {
            tokio::select! {
                e = taker.select_next_some() => if let SwarmEvent::ConnectionEstablished { peer_id, .. } = e {
                    if peer_id == maker_id { break; }
                },
                _ = maker.select_next_some() => {}
            }
        }
        taker.behaviour_mut().send_request(&maker_id, taker_session.request());
        loop {
            tokio::select! {
                e = maker.select_next_some() => {
                    if let SwarmEvent::Behaviour(Event::Message { message: Message::Request { request, channel, .. }, .. }) = e {
                        let response = maker_session.respond(&request, NOW);
                        maker.behaviour_mut().send_response(channel, response).unwrap();
                    }
                }
                e = taker.select_next_some() => match e {
                    SwarmEvent::Behaviour(Event::Message { message: Message::Response { response, .. }, .. }) => {
                        break taker_session.on_response(&response, NOW).unwrap();
                    }
                    SwarmEvent::Behaviour(Event::OutboundFailure { error, .. }) => panic!("request failed: {error}"),
                    _ => {}
                }
            }
        }
    };
    let at_taker = tokio::time::timeout(Duration::from_secs(60), run).await.expect("exchange timed out");
    let at_maker = maker_session.agreed().unwrap();
    assert_eq!(at_taker.transcript, at_maker.transcript);
    assert_eq!(at_taker.shared, at_maker.shared);
}

#[test]
fn a_saved_trade_rebuilds_the_agreement_after_the_offer_expired() {
    let (owner, claimer, stranger) = (trader(b"owner"), trader(b"claimer"), trader(b"stranger"));
    let offer = offer(&owner, &claimer);
    let (mut maker, mut taker) = (session(&owner, &offer, Role::Owner), session(&claimer, &offer, Role::Claimer));
    assert!(taker.saved(Timing::BSC, 0, 0, 0).is_none());
    let at_claimer = taker.on_response(&maker.respond(&taker.request(), NOW), NOW).unwrap();
    let saved = taker.saved(Timing::BSC, 7, 8, 9).unwrap();
    assert_eq!((saved.role, saved.lock_deadline, saved.evm_from_block, saved.xmz_restore_height), (Role::Claimer, 7, 8, 9));
    assert_eq!(saved.their_frame, maker.request().frame);
    // KeyExchange::accept would now refuse every frame (expired); the saved trade rebuilds
    let again = saved.agreed(&claimer.seed, &claimer.account).unwrap();
    assert_eq!((again.transcript, again.shared.clone()), (at_claimer.transcript, at_claimer.shared.clone()));
    assert_eq!(saved.secret(&claimer.seed).commitment(), again.terms.claim_commitment);
    assert_eq!(saved.swap_nonce(), U256::from(u64::from_be_bytes([0x42; 8])));
    // The owner's side too
    let owner_saved = maker.saved(Timing::BSC, 7, 8, 9).unwrap();
    assert_eq!(owner_saved.agreed(&owner.seed, &owner.account).unwrap().transcript, at_claimer.transcript);
    // A swapped-in frame, our own frame, or another seed: refused
    let forged = SavedTrade { their_frame: stranger.seed.frame(&offer, Role::Owner, &stranger.account).unwrap(), ..saved.clone() };
    assert!(forged.agreed(&claimer.seed, &claimer.account).is_err());
    let reflected = SavedTrade { their_frame: taker.request().frame, ..saved.clone() };
    assert!(reflected.agreed(&claimer.seed, &claimer.account).is_err());
    assert_eq!(saved.agreed(&stranger.seed, &claimer.account).err(), Some(ExchangeError::NotThisTrade), "another seed, same account");
    // It survives the journal's JSON
    let json = serde_json::to_string(&saved).unwrap();
    assert_eq!(serde_json::from_str::<SavedTrade>(&json).unwrap(), saved);
}
