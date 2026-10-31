//! The wallet RPC XMZ side against a real local XMZ chain (monzerod --regtest fakechain).
//!
//!   MONZERO_XMZ_BIN=/path/to/dir/with/monzerod-and-monzero-wallet-rpc cargo test --test xmz_local
//!
//! Skipped (passes trivially) when MONZERO_XMZ_BIN is not set.

mod common;

use monzero_dex_evm::driver::XmzSide;
use monzero_dex_evm::machine::XmzLock;
use monzero_dex_evm::secret::{combined_spend_key, SecretShare};
use monzero_dex_evm::xmz::*;
use rand::RngCore;

const AMOUNT: u64 = 2_000_000_000_000;

fn random_share() -> SecretShare {
    let mut bytes = [0u8; 64];
    rand::thread_rng().fill_bytes(&mut bytes);
    SecretShare::from_uniform_bytes(&bytes).unwrap()
}

#[tokio::test]
async fn lock_watch_and_sweep_on_a_real_xmz_chain() {
    let Some(mut chain) = common::XmzChain::start("local").await else {
        eprintln!("MONZERO_XMZ_BIN not set: skipped");
        return;
    };
    let (main, trader) = chain.trader("trader").await;
    let swap = chain.wallet_rpc("swap").await;
    chain.mine(&trader, 80).await;
    let height = chain.height().await;

    // Both traders' shares; this test plays the trader who locks and later sweeps back
    let (ours, theirs) = (random_share(), random_share());
    let (view_ours, view_theirs) = (random_share().to_ed25519_le(), random_share().to_ed25519_le());
    let shared = SharedXmz::from_shares(XmzNetwork::Mainnet, &ours, theirs.ed25519_public(), view_ours, view_theirs).unwrap();
    let cfg = || WalletRpcConfig {
        shared: shared.clone(),
        restore_height: height - 1,
        wallet_name: "swap-1".into(),
        wallet_password: "test-only".into(),
        sweep_confirmations: 10,
        record: chain.root.join("xmz-record.json"),
    };
    let (main_handle, swap_handle) = (main.clone(), swap.clone());
    let mut xmz = WalletRpcXmz::new(cfg(), main, swap);

    assert_eq!(xmz.lock_status().await.unwrap(), None);

    // Lock: built once, relayed, and a second call relays the same transaction
    xmz.ensure_lock(AMOUNT).await.unwrap();
    let lock = xmz.record().unwrap().lock.unwrap();
    xmz.ensure_lock(AMOUNT).await.unwrap();
    assert_eq!(xmz.record().unwrap().lock.unwrap(), lock);
    chain.mine(&trader, 1).await;
    assert_eq!(xmz.lock_status().await.unwrap(), Some(XmzLock { amount: AMOUNT, confirmations: 1 }), "exactly one lock");

    // A power cut can leave the watching wallet's file empty: it is rebuilt from our keys
    // under a new name and still sees the lock
    swap_handle.call("close_wallet", serde_json::json!({})).await.unwrap();
    std::fs::write(chain.root.join("swap").join("swap-1-view"), b"").unwrap();
    xmz = WalletRpcXmz::new(cfg(), main_handle.clone(), swap_handle.clone()); // the restarted app
    assert_eq!(xmz.lock_status().await.unwrap().map(|l| l.amount), Some(AMOUNT), "seen again after the wallet file was emptied");
    assert_eq!(xmz.record().unwrap().view_generation, 1, "rebuilt once under a new name");

    // Only the combined key opens the shared address
    assert!(xmz.ensure_sweep(ours.to_ed25519_le()).await.is_err());
    let key = combined_spend_key(&ours, &theirs);

    // Before the output unlocks nothing is built
    xmz.ensure_sweep(key).await.unwrap();
    assert!(xmz.record().unwrap().spend_wallet);
    assert_eq!(xmz.record().unwrap().sweep, None);
    assert!(!xmz.swept().await.unwrap());

    chain.mine(&trader, 10).await;
    xmz.ensure_sweep(key).await.unwrap();
    let sweep = xmz.record().unwrap().sweep.expect("sweep built once unlocked");
    xmz.ensure_sweep(key).await.unwrap();
    assert_eq!(xmz.record().unwrap().sweep.unwrap(), sweep);
    // The spending wallet still sees the lock it received
    assert_eq!(xmz.lock_status().await.unwrap().map(|l| l.amount), Some(AMOUNT));
    assert!(!xmz.swept().await.unwrap());

    chain.mine(&trader, 10).await;
    assert!(xmz.swept().await.unwrap());
    eprintln!("lock {lock:?} swept by {sweep:?}");
}
