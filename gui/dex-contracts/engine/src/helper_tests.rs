use super::*;
use crate::driver::Record;
use crate::exchange::Session;
use crate::machine::{OwnerState, Timing};
use crate::negotiation::{Offer, Role};
use crate::xmz::XmzNetwork;
use alloy::primitives::{Address, U256};
use alloy::signers::local::PrivateKeySigner;

/// A trade folder whose journal holds the owner's saved trade, as `Driver::start` writes it.
fn trade_dir(name: &str) -> (PathBuf, B256) {
    let dir = std::env::temp_dir().join(format!("monzero-helper-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    let (owner, claimer) = (PrivateKeySigner::random(), PrivateKeySigner::random());
    let (owner_seed, claimer_seed) = (SwapSeed::from_wallet_seed(b"owner"), SwapSeed::from_wallet_seed(b"claimer"));
    let offer = Offer {
        trade_id: B256::repeat_byte(0x42),
        chain_id: 97,
        contract: Address::repeat_byte(0xc0),
        owner: owner.address(),
        claimer: claimer.address(),
        asset: Address::ZERO,
        value: U256::from(10u64.pow(17)),
        duration1: 3600,
        duration2: 3600,
        xmz_amount: 5_000_000_000_000,
        xmz_network: XmzNetwork::Stagenet,
        expires_at: 2_000,
    };
    let mut maker = Session::start(&owner_seed, offer.clone(), Role::Owner, &owner, 1_000).unwrap();
    let mut taker = Session::start(&claimer_seed, offer, Role::Claimer, &claimer, 1_000).unwrap();
    taker.on_response(&maker.respond(&taker.request(), 1_000), 1_000).unwrap();
    let saved = maker.saved(Timing::BSC, 1_800, 100, 200).unwrap();
    let transcript = saved.transcript;
    FileJournal(dir.join("journal.json")).save(&Record { trade: Some(saved), ..Record::new(RoleState::Owner(OwnerState::Start)) }).unwrap();
    (dir, transcript)
}

async fn call(request: Value) -> Value {
    handle(&serde_json::to_vec(&request).unwrap()).await
}

#[tokio::test]
async fn status_shows_the_trade_without_secrets() {
    let (dir, transcript) = trade_dir("status");
    let v = call(json!({"schema": REQUEST_SCHEMA, "command": "status", "tradeDir": dir})).await;
    assert_eq!(v["ok"], true, "{v}");
    assert_eq!((v["role"].as_str(), v["stage"].as_str(), v["done"].as_bool(), v["approved"].as_bool()), (Some("owner"), Some("Start"), Some(false), Some(false)));
    assert_eq!(v["commitment"], transcript.to_string());
    assert_eq!(v["offer"]["value"], "100000000000000000");
    // Nothing secret leaks: no frames, view shares or proofs
    let text = v.to_string();
    for word in ["frame", "viewShare", "proof", "theirFrame", "signature"] {
        assert!(!text.contains(word), "{word} in {text}");
    }
    let _ = std::fs::remove_dir_all(dir);
}

#[tokio::test]
async fn approval_needs_this_trades_commitment() {
    let (dir, transcript) = trade_dir("approve");
    let wrong = call(json!({"schema": REQUEST_SCHEMA, "command": "approve", "tradeDir": dir, "commitment": B256::repeat_byte(1)})).await;
    assert_eq!(wrong["ok"], false);
    let missing = call(json!({"schema": REQUEST_SCHEMA, "command": "approve", "tradeDir": dir})).await;
    assert_eq!(missing["ok"], false);
    let status = call(json!({"schema": REQUEST_SCHEMA, "command": "status", "tradeDir": dir})).await;
    assert_eq!(status["approved"], false, "refused approvals changed nothing");
    let ok = call(json!({"schema": REQUEST_SCHEMA, "command": "approve", "tradeDir": dir, "commitment": transcript})).await;
    assert_eq!((ok["ok"].as_bool(), ok["approved"].as_bool()), (Some(true), Some(true)), "{ok}");
    // Approving again is harmless
    let again = call(json!({"schema": REQUEST_SCHEMA, "command": "approve", "tradeDir": dir, "commitment": transcript})).await;
    assert_eq!(again["approved"], true);
    let _ = std::fs::remove_dir_all(dir);
}

#[tokio::test]
async fn malformed_requests_are_refused() {
    let (dir, _) = trade_dir("bad");
    for request in [
        json!({"schema": "something-else", "command": "status", "tradeDir": dir}),
        json!({"schema": REQUEST_SCHEMA, "command": "launch", "tradeDir": dir}),
        json!({"schema": REQUEST_SCHEMA, "command": "status", "tradeDir": dir, "extra": 1}),
        json!({"schema": REQUEST_SCHEMA, "command": "tick", "tradeDir": dir}),
        json!({"schema": REQUEST_SCHEMA, "command": "status", "tradeDir": dir.join("missing")}),
    ] {
        let v = call(request.clone()).await;
        assert_eq!(v["ok"], false, "accepted {request}");
        assert_eq!(v["schema"], RESPONSE_SCHEMA);
    }
    assert_eq!(handle(b"not json").await["ok"], false);
    let _ = std::fs::remove_dir_all(dir);
}

#[tokio::test]
async fn tick_takes_vault_keys_or_the_prototype_seed_but_not_both() {
    let (dir, _) = trade_dir("keys");
    let endpoint = json!({"url": "http://127.0.0.1:1"});
    let base = json!({"evmRpc": "http://127.0.0.1:1", "mainWallet": endpoint, "swapWallet": endpoint});
    let with = |extra: Value| {
        let mut t = base.clone();
        for (k, v) in extra.as_object().unwrap() {
            t[k] = v.clone();
        }
        json!({"schema": REQUEST_SCHEMA, "command": "tick", "tradeDir": dir, "tick": t})
    };
    // Keys derived by the vault for another wallet: they do not rebuild this trade
    let seed = [5u8; 64];
    let vault = json!({"swapSeedHex": hex::encode(*monzero_evm_keys::swap_seed(&seed)), "evmKeyHex": hex::encode(*monzero_evm_keys::evm_private_key(&seed, 0).unwrap())});
    let v = call(with(json!({"swapKeys": vault}))).await;
    assert_eq!(v["ok"], false);
    // Another wallet's account cannot sign this trade's frame, nor its seed rebuild it
    let e = v["error"].as_str().unwrap();
    assert!(e.contains("do not rebuild the saved trade") || e.contains("not signed by the Owner"), "{v}");
    // Neither, both, or malformed keys are refused before anything else
    for extra in [
        json!({}),
        json!({"swapKeys": vault, "walletSeedHex": hex::encode(seed), "accountIndex": 0}),
        json!({"swapKeys": {"swapSeedHex": "00", "evmKeyHex": "00"}}),
        json!({"walletSeedHex": hex::encode(seed)}),
    ] {
        let v = call(with(extra.clone())).await;
        assert_eq!(v["ok"], false, "accepted {extra}");
        let e = v["error"].as_str().unwrap();
        assert!(!e.contains("rebuild") && !e.contains("not signed"), "refused for the right reason: {v}");
    }
    let _ = std::fs::remove_dir_all(dir);
}

#[tokio::test]
async fn payout_needs_a_finished_trade_and_a_valid_address() {
    let (dir, _) = trade_dir("payout");
    let endpoint = json!({"url": "http://127.0.0.1:1"});
    let seed = [5u8; 64];
    let tick = json!({"evmRpc": "http://127.0.0.1:1", "mainWallet": endpoint, "swapWallet": endpoint,
                      "walletSeedHex": hex::encode(seed), "accountIndex": 0});
    let call_with = |addr: Value| json!({"schema": REQUEST_SCHEMA, "command": "payout", "tradeDir": dir, "tick": tick, "payoutAddress": addr});
    // A trade that has not finished keeps its funds where the swap needs them
    let v = call(call_with(json!("0x4b7c0a922d663ec68bc6d5d5f9fa77e420c28faf"))).await;
    assert_eq!(v["ok"], false);
    assert!(v["error"].as_str().unwrap().contains("not finished"), "{v}");
    // Address mistakes are refused before anything else
    for bad in ["4b7c0a922d663ec68bc6d5d5f9fa77e420c28faf", "0x4b7c0a922d663ec68bc6d5d5f9fa77e420c28fa", "0x4B7c0a922d663ec68Bc6d5D5f9fa77E420C28FAa"] {
        let v = call(call_with(json!(bad))).await;
        assert_eq!(v["ok"], false, "accepted {bad}");
        assert!(!v["error"].as_str().unwrap().contains("not finished"), "{bad}: {v}");
    }
    let v = call(json!({"schema": REQUEST_SCHEMA, "command": "payout", "tradeDir": dir, "tick": tick})).await;
    assert!(v["error"].as_str().unwrap().contains("payoutAddress"), "{v}");
    let _ = std::fs::remove_dir_all(dir);
}

#[test]
fn addresses_as_metamask_shows_them_are_accepted() {
    let a = parse_address("0x4b7c0a922d663ec68Bc6d5D5f9fa77E420C28FAF").unwrap();
    assert_eq!(a, parse_address("0x4b7c0a922d663ec68bc6d5d5f9fa77e420c28faf").unwrap());
    assert!(parse_address("0x4b7c0a922d663ec68Bc6d5D5f9fa77E420C28FAf").is_err(), "one wrong case is a typo");
}
