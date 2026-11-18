//! Fee sponsoring for USDT traders: the relays pay the Solana / BNB Smart Chain network fees and
//! are paid back in USDT, so a USDT trader needs no SOL or BNB.
//!
//! - A seller's claim (both chains) goes through the deployed contracts' relayed claim: the seller
//!   signs (swap, fee, relayer); the relay submits it and the contract pays the relay `fee` out of
//!   the USDT, the seller the rest. The seller caps the fee it signs.
//! - A buyer's lock on Solana: the relay is the fee payer of one transaction that also fronts the
//!   lock's account deposits to the buyer and takes its USDT fee from the buyer, all or nothing.
//!   The deposits come back to the buyer's account when the swap closes.
//!
//! The relay never holds the trader's funds; a relay that does not answer only costs time (the
//! app asks the other relay, or for a little SOL/BNB as before).

use alloy::primitives::{Address, Bytes, B256, U256};
use serde::{Deserialize, Serialize};
use serde_json::Value;

pub const INFO_PATH: &str = "/v1/sponsor";

static RELAYS: std::sync::OnceLock<Vec<String>> = std::sync::OnceLock::new();

/// The relays of this request (one helper run serves one request)
pub fn set_relays(relays: Vec<String>) {
    let _ = RELAYS.set(relays);
}

pub fn relays() -> &'static [String] {
    RELAYS.get().map(Vec::as_slice).unwrap_or(&[])
}

/// The lock fee of the first relay that pays Solana fees for USDT buyers, if any does
pub async fn sol_lock_fee() -> Option<u64> {
    for relay in relays() {
        if let Some(fee) = info(relay).await.and_then(|i| i.sol_lock_fee.filter(|f| *f <= MAX_SOL_LOCK_FEE)) {
            return Some(fee);
        }
    }
    None
}
pub const SOL_CLAIM_PATH: &str = "/v1/sponsor/sol-claim";
pub const SOL_LOCK_PATH: &str = "/v1/sponsor/sol-lock";
pub const EVM_CLAIM_PATH: &str = "/v1/sponsor/evm-claim";

/// The most a seller lets a relay take for its claim: 1 USDT (Solana: 6 decimals)
pub const MAX_SOL_CLAIM_FEE: u64 = 1_000_000;
/// The most a buyer pays a relay for its lock: 1.5 USDT. Most of it is the deposits the relay
/// fronts, which come back to the buyer as SOL when the swap closes.
pub const MAX_SOL_LOCK_FEE: u64 = 1_500_000;
/// The most a seller lets a relay take for its claim on BNB Smart Chain: 1 USDT (18 decimals)
pub fn max_evm_claim_fee() -> U256 {
    U256::from(10u64).pow(U256::from(18u64))
}
/// Lamports a relay fronts for a sponsored lock: the swap account's and the token vault's
/// deposits, the buyer account's minimum balance, and three later fees (set ready, refund, spare)
pub const SOL_LOCK_FRONT: u64 = 2_255_040 + 2_039_280 + 890_880 + 15_000;

/// What a relay offers (GET /v1/sponsor). Absent fields: not offered by this relay.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SponsorInfo {
    /// The relay's Solana fee-payer account (base58)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub solana: Option<String>,
    /// USDT base units (6 decimals) the relay takes for a seller's claim on Solana
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sol_claim_fee: Option<u64>,
    /// USDT base units the relay takes for a buyer's lock on Solana
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sol_lock_fee: Option<u64>,
    /// Lamports the relay fronts for that lock
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub sol_lock_front: Option<u64>,
    /// The relay's EVM account on BNB Smart Chain
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub evm: Option<Address>,
    /// USDT base units (18 decimals, decimal text) the relay takes for a claim on BNB Smart Chain
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub bsc_claim_fee: Option<String>,
}

/// A seller's relayed claim on Solana
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SolClaimRequest {
    pub chain_id: u64,
    /// The swap account (base58)
    pub swap: String,
    /// The claim secret as the program takes it (32 bytes, hex)
    pub secret: String,
    pub fee: u64,
    /// The seller's account (base58): it signed `message` below
    pub claimer: String,
    /// The buyer's account (base58), where the swap's deposits go back
    pub owner: String,
    /// The seller's ed25519 signature of `sol_claim_message(swap, fee, relayer)` (64 bytes, hex)
    pub signature: String,
}

/// A buyer's sponsored lock on Solana: the whole message (the relay is its fee payer) and the
/// buyer's signature of it; the relay checks it, adds its own and sends it
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct SolLockRequest {
    pub chain_id: u64,
    /// base64
    pub message: String,
    /// hex
    pub signature: String,
}

/// A seller's relayed claim on BNB Smart Chain (the contract's claimRelayer)
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct EvmClaimRequest {
    pub chain_id: u64,
    /// The swap as the contract takes it, ABI-encoded
    pub swap: Bytes,
    pub secret: B256,
    pub fee: U256,
    /// The seller's signature of `relay_digest(chain, contract, id, fee, relayer)` (65 bytes)
    pub signature: Bytes,
}

/// A relay's answer: the transaction it sent (`tx`), the signed transaction for the caller to
/// keep and send again (`raw`, base64, a sponsored lock), or why not (`error`)
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SponsorAnswer {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub tx: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub raw: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

/// What a seller signs for a relayed claim on Solana (the program checks exactly these bytes)
pub fn sol_claim_message(swap: &[u8; 32], fee: u64, relayer: &[u8; 32]) -> Vec<u8> {
    let mut m = b"monzero-sol-relay-v1".to_vec();
    m.extend_from_slice(swap);
    m.extend_from_slice(&fee.to_le_bytes());
    m.extend_from_slice(relayer);
    m
}

use alloy::transports::http::reqwest;

fn client() -> Result<reqwest::Client, String> {
    // The same TLS setup as the chain calls (Android carries its own roots)
    crate::chain::http_client_builder().timeout(std::time::Duration::from_secs(30)).build().map_err(|e| e.to_string())
}

/// The relay's offer, or None when it offers nothing (or does not answer)
pub async fn info(relay: &str) -> Option<SponsorInfo> {
    let r = client().ok()?.get(format!("{}{INFO_PATH}", relay.trim_end_matches('/'))).send().await.ok()?;
    if !r.status().is_success() {
        return None;
    }
    serde_json::from_slice(&r.bytes().await.ok()?).ok()
}

/// Ask `relay` to sponsor something; Err with its reason when it will not
pub async fn post<T: Serialize>(relay: &str, path: &str, body: &T) -> Result<SponsorAnswer, String> {
    let body = serde_json::to_vec(body).map_err(|e| e.to_string())?;
    let r = client()?
        .post(format!("{}{path}", relay.trim_end_matches('/')))
        .header("content-type", "application/json")
        .body(body)
        .send()
        .await
        .map_err(|e| e.to_string())?;
    let status = r.status();
    let v: Value = serde_json::from_slice(&r.bytes().await.map_err(|e| e.to_string())?).unwrap_or(Value::Null);
    let answer: SponsorAnswer = serde_json::from_value(v).unwrap_or_default();
    match (&answer.error, status.is_success()) {
        (Some(e), _) => Err(e.clone()),
        (None, true) => Ok(answer),
        (None, false) => Err(format!("HTTP {status}")),
    }
}
