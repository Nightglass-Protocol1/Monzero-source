use super::*;
use alloy::primitives::LogData;
use alloy::sol_types::SolEvent;
use std::str::FromStr;

/// Values computed by XmzSwap.sol itself (dex-contracts/scripts/vectors.js).
fn vectors() -> serde_json::Value {
    serde_json::from_str(include_str!("../tests/vectors.json")).expect("vectors.json to be valid JSON")
}

fn b256(s: &serde_json::Value) -> B256 {
    B256::from_str(s.as_str().unwrap()).unwrap()
}

fn address(s: &serde_json::Value) -> Address {
    Address::from_str(s.as_str().unwrap()).unwrap()
}

fn u256(s: &serde_json::Value) -> U256 {
    U256::from_str(s.as_str().unwrap()).unwrap()
}

fn vector_swap() -> Swap {
    let s = &vectors()["swap"];
    Swap {
        owner: address(&s[0]),
        claimer: address(&s[1]),
        claimCommitment: address(&s[2]),
        refundCommitment: address(&s[3]),
        timeout1: u256(&s[4]),
        timeout2: u256(&s[5]),
        asset: address(&s[6]),
        value: u256(&s[7]),
        nonce: u256(&s[8]),
    }
}

#[test]
fn commitments_match_the_contract() {
    for c in vectors()["commitments"].as_array().unwrap() {
        let secret = b256(&c["secret"]);
        assert_eq!(commitment(&secret).unwrap(), address(&c["commitment"]));
        assert!(secret_matches(&secret, address(&c["commitment"])));
    }
}

#[test]
fn rejects_zero_and_out_of_range_secrets() {
    let n = B256::from_str("0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141").unwrap();
    assert_eq!(commitment(&B256::ZERO), Err(Error::InvalidSecret));
    assert_eq!(commitment(&n), Err(Error::InvalidSecret));
    assert!(!secret_matches(&B256::ZERO, Address::ZERO));
}

#[test]
fn swap_id_matches_the_contract() {
    assert_eq!(swap_id(&vector_swap()), b256(&vectors()["swapId"]));
}

#[test]
fn relay_digest_matches_the_contract() {
    let v = vectors();
    let r = &v["relay"];
    let digest = relay_digest(
        r["chainId"].as_str().unwrap().parse().unwrap(),
        address(&r["contract"]),
        b256(&v["swapId"]),
        u256(&r["fee"]),
        address(&r["relayer"]),
    );
    assert_eq!(digest, b256(&r["digest"]));
}

// ---------- check_lock ----------

const CONTRACT: Address = Address::repeat_byte(0xc0);
const LOCK_TIME: u64 = 1_793_300_000;

fn terms() -> AgreedTerms {
    let swap = vector_swap();
    AgreedTerms {
        chain: Chain::Local,
        contract: CONTRACT,
        owner: swap.owner,
        claimer: swap.claimer,
        claim_commitment: swap.claimCommitment,
        refund_commitment: swap.refundCommitment,
        asset: swap.asset,
        value: swap.value,
        duration1: 3 * 3600,
        duration2: 6 * 3600,
    }
}

/// The `New` event the contract emits for `terms`, locked at LOCK_TIME, optionally altered.
fn lock_with(change: impl FnOnce(&mut XmzSwap::New)) -> ObservedLock {
    let t = terms();
    let timeout1 = U256::from(LOCK_TIME + t.duration1);
    let mut event = XmzSwap::New {
        swapId: B256::ZERO,
        owner: t.owner,
        claimer: t.claimer,
        claimCommitment: t.claim_commitment,
        refundCommitment: t.refund_commitment,
        timeout1,
        timeout2: timeout1 + U256::from(t.duration2),
        asset: t.asset,
        value: t.value,
        nonce: U256::from(7),
    };
    event.swapId = swap_id(&Swap {
        owner: event.owner,
        claimer: event.claimer,
        claimCommitment: event.claimCommitment,
        refundCommitment: event.refundCommitment,
        timeout1: event.timeout1,
        timeout2: event.timeout2,
        asset: event.asset,
        value: event.value,
        nonce: event.nonce,
    });
    change(&mut event);
    ObservedLock {
        chain_id: Chain::Local.id(),
        log: rpc_log(CONTRACT, event),
        block_timestamp: LOCK_TIME,
        stage: Stage::Pending as u8,
    }
}

fn rpc_log<T>(address: Address, data: T) -> Log<T> {
    Log {
        inner: alloy::primitives::Log { address, data },
        block_hash: None,
        block_number: None,
        block_timestamp: None,
        transaction_hash: None,
        transaction_index: None,
        log_index: None,
        removed: false,
    }
}

/// Recomputes the id after a change, so only the changed field is wrong
fn rehash(event: &mut XmzSwap::New) {
    event.swapId = swap_id(&Swap {
        owner: event.owner,
        claimer: event.claimer,
        claimCommitment: event.claimCommitment,
        refundCommitment: event.refundCommitment,
        timeout1: event.timeout1,
        timeout2: event.timeout2,
        asset: event.asset,
        value: event.value,
        nonce: event.nonce,
    });
}

#[test]
fn accepts_a_lock_that_matches_the_terms() {
    let swap = check_lock(&terms(), &lock_with(|_| {})).unwrap();
    assert_eq!(swap.timeout1, U256::from(LOCK_TIME + 3 * 3600));
}

#[test]
fn rejects_every_field_that_differs_from_the_terms() {
    let other = Address::repeat_byte(0x99);
    let cases: Vec<(&str, Box<dyn Fn(&mut XmzSwap::New)>)> = vec![
        ("owner", Box::new(move |e| e.owner = other)),
        ("claimer", Box::new(move |e| e.claimer = other)),
        ("claim commitment", Box::new(move |e| e.claimCommitment = other)),
        ("refund commitment", Box::new(move |e| e.refundCommitment = other)),
        ("asset", Box::new(move |e| e.asset = other)),
        ("value", Box::new(|e| e.value -= U256::from(1))),
        ("timeout1", Box::new(|e| e.timeout1 -= U256::from(1))),
        ("timeout2", Box::new(|e| e.timeout2 -= U256::from(1))),
    ];
    for (field, change) in cases {
        let lock = lock_with(|e| {
            change(e);
            rehash(e);
        });
        assert_eq!(check_lock(&terms(), &lock), Err(Error::TermsMismatch(field)), "{field}");
    }
}

#[test]
fn rejects_a_wrong_id_contract_chain_or_stage() {
    let lock = lock_with(|e| e.swapId = B256::repeat_byte(1));
    assert_eq!(check_lock(&terms(), &lock), Err(Error::TermsMismatch("swap id")));

    let mut lock = lock_with(|_| {});
    lock.log.inner.address = Address::repeat_byte(0x77);
    assert_eq!(check_lock(&terms(), &lock), Err(Error::TermsMismatch("emitted by another contract")));

    let mut lock = lock_with(|_| {});
    lock.chain_id = Chain::BscMainnet.id();
    assert_eq!(check_lock(&terms(), &lock), Err(Error::TermsMismatch("chain id")));

    for stage in [Stage::Invalid, Stage::Ready, Stage::Completed] {
        let mut lock = lock_with(|_| {});
        lock.stage = stage as u8;
        assert_eq!(check_lock(&terms(), &lock), Err(Error::WrongStage(stage as u8)));
    }
    let mut lock = lock_with(|_| {});
    lock.stage = 9;
    assert_eq!(check_lock(&terms(), &lock), Err(Error::WrongStage(9)));
}

#[test]
fn rejects_a_lock_with_shifted_timeouts_that_keep_their_gap() {
    // An owner locking with a shorter duration1 must not pass, even if timeout2 - timeout1 is right
    let lock = lock_with(|e| {
        e.timeout1 -= U256::from(3600);
        e.timeout2 -= U256::from(3600);
        rehash(e);
    });
    assert_eq!(check_lock(&terms(), &lock), Err(Error::TermsMismatch("timeout1")));
}

// ---------- windows ----------

#[test]
fn claim_window_follows_the_contract_with_a_margin() {
    let swap = vector_swap(); // timeout1 1_793_300_000, timeout2 1_793_321_600
    let t1 = 1_793_300_000;
    let t2 = 1_793_321_600;
    assert!(!claim_window(&swap, Stage::Pending, t1 - 1, 0), "pending before timeout1");
    assert!(claim_window(&swap, Stage::Ready, t1 - 1, 0), "ready before timeout1");
    assert!(claim_window(&swap, Stage::Pending, t1, 0), "pending from timeout1");
    assert!(claim_window(&swap, Stage::Ready, t2 - 1801, 1800), "just outside the margin");
    assert!(!claim_window(&swap, Stage::Ready, t2 - 1800, 1800), "inside the margin");
    assert!(!claim_window(&swap, Stage::Ready, t2, 0), "at timeout2");
    assert!(!claim_window(&swap, Stage::Completed, t1, 0));
    assert!(!claim_window(&swap, Stage::Invalid, t1, 0));
}

#[test]
fn refund_window_follows_the_contract() {
    let swap = vector_swap();
    let t1 = 1_793_300_000;
    let t2 = 1_793_321_600;
    assert!(refund_window(&swap, Stage::Pending, t1 - 1));
    assert!(!refund_window(&swap, Stage::Pending, t1));
    assert!(!refund_window(&swap, Stage::Ready, t1 - 1));
    assert!(!refund_window(&swap, Stage::Ready, t2 - 1));
    assert!(refund_window(&swap, Stage::Ready, t2));
    assert!(refund_window(&swap, Stage::Pending, t2));
    assert!(!refund_window(&swap, Stage::Completed, t2));
}

// ---------- revealed secrets ----------

fn event_log(address: Address, data: LogData) -> Log {
    rpc_log(address, data)
}

#[test]
fn finds_a_revealed_secret_only_for_this_swap_and_contract() {
    let v = vectors();
    let swap = vector_swap();
    let id = swap_id(&swap);
    let claim_secret = b256(&v["commitments"][1]["secret"]); // opens claimCommitment
    let refund_secret = b256(&v["commitments"][0]["secret"]); // opens refundCommitment

    let claimed = XmzSwap::Claimed { swapId: id, secret: claim_secret }.encode_log_data();
    let refunded = XmzSwap::Refunded { swapId: id, secret: refund_secret }.encode_log_data();
    let other_swap = XmzSwap::Claimed { swapId: B256::repeat_byte(5), secret: claim_secret }.encode_log_data();
    let wrong_secret = XmzSwap::Claimed { swapId: id, secret: refund_secret }.encode_log_data();

    assert_eq!(revealed_secret(&[event_log(CONTRACT, claimed.clone())], CONTRACT, &swap), Some(Revealed::Claimed(claim_secret)));
    assert_eq!(revealed_secret(&[event_log(CONTRACT, refunded)], CONTRACT, &swap), Some(Revealed::Refunded(refund_secret)));
    assert_eq!(revealed_secret(&[event_log(CONTRACT, other_swap)], CONTRACT, &swap), None, "another swap");
    assert_eq!(revealed_secret(&[event_log(CONTRACT, wrong_secret)], CONTRACT, &swap), None, "secret that does not open the commitment");
    assert_eq!(revealed_secret(&[event_log(Address::repeat_byte(0x77), claimed)], CONTRACT, &swap), None, "another contract");
}
