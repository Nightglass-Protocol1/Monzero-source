//! The app's side of one XMZ <-> EVM trade: a one-shot JSON request on stdin, a JSON response
//! on stdout, as the GUI runs the helpers of the other pairs (`monzero-dex-evm-helper`).
//!
//!  - `status`: the trade as its journal holds it (no network, no secrets);
//!  - `approve`: record the user's approval; `commitment` must be the trade's transcript, as
//!    `status` showed it, so an approval cannot land on another trade;
//!  - `tick`: one step of the trade; needs the wallet seed (from the app's vault) and the
//!    endpoints. A step that would lock our funds before approval reports `awaitingApproval`.
//!
//! The trade folder holds `journal.json` (with the saved trade) and `xmz.json`, private to the
//! wallet. Responses never contain keys, frames or secrets.

use crate::chain::AlloyChain;
use crate::driver::{approve_journal, Driver, DriverError, FileJournal, Journal, RoleState};
use crate::evm_key::evm_account;
use crate::swap_seed::SwapSeed;
use crate::xmz::{WalletRpc, WalletRpcConfig, WalletRpcXmz, XmzRecord};
use crate::Chain;
use alloy::primitives::B256;
use serde::Deserialize;
use serde_json::{json, Value};
use std::path::{Path, PathBuf};
use zeroize::Zeroizing;

pub const REQUEST_SCHEMA: &str = "monzero-xmz-evm-helper-request-v1";
pub const RESPONSE_SCHEMA: &str = "monzero-xmz-evm-helper-response-v1";

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Request {
    pub schema: String,
    pub command: Command,
    pub trade_dir: PathBuf,
    #[serde(default)]
    pub commitment: Option<B256>,
    #[serde(default)]
    pub tick: Option<TickConfig>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum Command {
    Status,
    Approve,
    Tick,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct TickConfig {
    pub evm_rpc: String,
    #[serde(default)]
    pub evm_log_chunk: Option<u64>,
    /// The keys this trade needs, as the vault derives them (the wallet seed stays in the
    /// vault). Prototype alternative: `walletSeedHex` with `accountIndex`. Exactly one of the two.
    #[serde(default)]
    pub swap_keys: Option<SwapKeys>,
    /// The wallet's BIP-39 seed, hex (prototype only; never logged or echoed)
    #[serde(default)]
    pub wallet_seed_hex: Option<Zeroizing<String>>,
    #[serde(default)]
    pub account_index: Option<u32>,
    pub main_wallet: Endpoint,
    pub swap_wallet: Endpoint,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct SwapKeys {
    /// `monzero_evm_keys::swap_seed` of the wallet seed, hex
    pub swap_seed_hex: Zeroizing<String>,
    /// `monzero_evm_keys::evm_private_key` of the wallet seed for the trade's account, hex
    pub evm_key_hex: Zeroizing<String>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Endpoint {
    pub url: String,
    #[serde(default)]
    pub login_file: Option<PathBuf>,
}

/// Handle one request; errors become `{"ok": false, "error": ...}`.
pub async fn handle(request: &[u8]) -> Value {
    let result = match serde_json::from_slice::<Request>(request) {
        Err(e) => Err(format!("bad request: {e}")),
        Ok(r) if r.schema != REQUEST_SCHEMA => Err(format!("unknown schema {}", r.schema)),
        Ok(r) => match r.command {
            Command::Status => status(&r.trade_dir),
            Command::Approve => approve(&r.trade_dir, r.commitment),
            Command::Tick => tick(&r.trade_dir, r.tick).await,
        },
    };
    match result {
        Ok(mut v) => {
            v["schema"] = json!(RESPONSE_SCHEMA);
            v["ok"] = json!(true);
            v
        }
        Err(e) => json!({"schema": RESPONSE_SCHEMA, "ok": false, "error": e}),
    }
}

fn journal(dir: &Path) -> FileJournal {
    FileJournal(dir.join("journal.json"))
}

fn status(dir: &Path) -> Result<Value, String> {
    let record = journal(dir).load().map_err(|e| e.to_string())?;
    let saved = record.trade.as_ref().ok_or("the journal holds no saved trade")?;
    let o = &saved.offer;
    let xmz: XmzRecord = std::fs::read_to_string(dir.join("xmz.json")).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
    let evm_txs: serde_json::Map<String, Value> = record
        .retained
        .iter()
        .map(|(k, v)| (k.clone(), json!(v.iter().map(|s| alloy::primitives::keccak256(&s.raw).to_string()).collect::<Vec<_>>())))
        .collect();
    let deadlines = match &record.state {
        RoleState::Owner(s) => swap_of_owner(s),
        RoleState::Claimer(s) => swap_of_claimer(s),
    }
    .map(|s| json!({"timeout1": s.timeout1.to_string(), "timeout2": s.timeout2.to_string()}));
    Ok(json!({
        "role": format!("{:?}", saved.role).to_lowercase(),
        "stage": record.state.stage(),
        "done": record.state.is_done(),
        "approved": record.approved == Some(saved.transcript),
        "commitment": saved.transcript.to_string(),
        "offer": {
            "tradeId": o.trade_id.to_string(),
            "chainId": o.chain_id,
            "contract": o.contract.to_string(),
            "owner": o.owner.to_string(),
            "claimer": o.claimer.to_string(),
            "asset": o.asset.to_string(),
            "value": o.value.to_string(),
            "xmzAmount": o.xmz_amount.to_string(),
            "duration1": o.duration1,
            "duration2": o.duration2,
        },
        "deadlines": deadlines,
        "evmTxs": evm_txs,
        "xmzLock": xmz.lock.as_ref().map(|r| r.tx_hash.clone()),
        "xmzSweeps": xmz.sweep.as_ref().map(|v| v.iter().map(|r| r.tx_hash.clone()).collect::<Vec<_>>()),
    }))
}

fn swap_of_owner(s: &crate::machine::OwnerState) -> Option<&crate::machine::SwapData> {
    use crate::machine::OwnerState::*;
    match s {
        Locked { swap } | ReadySent { swap } | Ready { swap } | RefundSent { swap } => Some(swap),
        _ => None,
    }
}

fn swap_of_claimer(s: &crate::machine::ClaimerState) -> Option<&crate::machine::SwapData> {
    use crate::machine::ClaimerState::*;
    match s {
        XmzLockSent { swap } | XmzLocked { swap } | ClaimSent { swap } | WaitingForRefund { swap } => Some(swap),
        _ => None,
    }
}

fn approve(dir: &Path, commitment: Option<B256>) -> Result<Value, String> {
    let commitment = commitment.ok_or("approve needs the commitment that status showed")?;
    approve_journal(&mut journal(dir), commitment).map_err(|e| e.to_string())?;
    status(dir)
}

async fn tick(dir: &Path, cfg: Option<TickConfig>) -> Result<Value, String> {
    let cfg = cfg.ok_or("tick needs its configuration")?;
    let (swap_seed, key) = keys(&cfg)?;
    let saved = journal(dir).load().map_err(|e| e.to_string())?.trade.ok_or("the journal holds no saved trade")?;
    let agreed = saved.agreed(&swap_seed, &key).map_err(|e| e.to_string())?;
    let chain_kind = Chain::from_id(saved.offer.chain_id).ok_or("unknown chain")?;
    let mut chain = AlloyChain::new(&cfg.evm_rpc, chain_kind, saved.offer.contract, saved.evm_from_block, key.clone()).map_err(|e| e.to_string())?;
    if let Some(n) = cfg.evm_log_chunk {
        chain = chain.with_log_chunk(n);
    }
    let rpc = |e: &Endpoint| match &e.login_file {
        Some(f) => WalletRpc::with_login_file(&e.url, f).map_err(|e| e.to_string()),
        None => Ok(WalletRpc::new(&e.url, None)),
    };
    let xmz = WalletRpcXmz::new(
        WalletRpcConfig {
            shared: agreed.shared.clone(),
            restore_height: saved.xmz_restore_height,
            wallet_name: "shared".into(),
            wallet_password: swap_seed.wallet_password(&saved.offer, saved.role).to_string(),
            sweep_confirmations: 10,
            record: dir.join("xmz.json"),
        },
        rpc(&cfg.main_wallet)?,
        rpc(&cfg.swap_wallet)?,
    );
    let mut driver = Driver::resume(&swap_seed, &key, chain, xmz, journal(dir)).map_err(|e| e.to_string())?;
    let awaiting = match driver.tick().await {
        Ok(_) => false,
        Err(DriverError::AwaitingApproval) => true,
        Err(e) => return Err(e.to_string()),
    };
    let mut v = status(dir)?;
    v["awaitingApproval"] = json!(awaiting);
    Ok(v)
}

/// The swap seed and the EVM account key: derived by the vault (`swapKeys`), or from the
/// wallet seed in the prototype (`walletSeedHex` + `accountIndex`).
fn keys(cfg: &TickConfig) -> Result<(SwapSeed, alloy::signers::local::PrivateKeySigner), String> {
    let bytes32 = |hex_text: &str, what: &str| -> Result<Zeroizing<[u8; 32]>, String> {
        let v = Zeroizing::new(hex::decode(hex_text.trim()).map_err(|_| format!("{what} is not hex"))?);
        let mut out = Zeroizing::new([0u8; 32]);
        if v.len() != 32 {
            return Err(format!("{what} must be 32 bytes"));
        }
        out.copy_from_slice(&v);
        Ok(out)
    };
    match (&cfg.swap_keys, &cfg.wallet_seed_hex, cfg.account_index) {
        (Some(k), None, None) => {
            let seed = bytes32(&k.swap_seed_hex, "swapSeedHex")?;
            let key = bytes32(&k.evm_key_hex, "evmKeyHex")?;
            let signer = alloy::signers::local::PrivateKeySigner::from_bytes(&B256::from(*key)).map_err(|_| "evmKeyHex is not a valid key")?;
            Ok((SwapSeed::from_bytes(*seed), signer))
        }
        (None, Some(hex_seed), Some(index)) => {
            let seed = Zeroizing::new(hex::decode(hex_seed.trim()).map_err(|_| "walletSeedHex is not hex")?);
            Ok((SwapSeed::from_wallet_seed(&seed), evm_account(&seed, index).map_err(|e| e.to_string())?))
        }
        _ => Err("tick needs either swapKeys, or walletSeedHex with accountIndex".into()),
    }
}

#[cfg(test)]
#[path = "helper_tests.rs"]
mod tests;
