//! Fee sponsoring for USDT traders (see `monzero_dex_evm::sponsor`): this relay pays Solana / BNB
//! Smart Chain fees and is paid back in USDT. It signs nothing it has not checked:
//! - a seller's relayed claim: the relay builds the whole transaction itself (the seller's signed
//!   permission caps the fee; the program or contract pays the claim to the seller only);
//! - a buyer's Solana lock: exactly "the relay fronts the deposits, the buyer locks USDT in the
//!   pinned program, the buyer pays the relay's USDT fee"; the relay's account appears nowhere else.
//! Each is simulated before it is signed and sent. Fees cover the relay's costs at current prices.

use crate::{ChainView, Relay, TxId};
use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, B256, U256};
use alloy::providers::{DynProvider, Provider, ProviderBuilder};
use alloy::rpc::types::TransactionRequest;
use alloy::signers::local::PrivateKeySigner;
use alloy::sol_types::{SolCall, SolValue};
use axum::extract::{ConnectInfo, State};
use axum::http::{HeaderMap, StatusCode};
use axum::routing::{get, post};
use axum::{Json, Router};
use base64::Engine as _;
use ed25519_dalek::SigningKey;
use monzero_dex_evm::sponsor::{self as sp, EvmClaimRequest, SolClaimRequest, SolLockRequest, SponsorAnswer, SponsorInfo};
use monzero_dex_evm::{sol, Chain, Swap, XmzSwap};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::sync::{Arc, Mutex};
use std::time::Duration;

/// Requests per minute from one address (each one is simulated on a node)
const PER_MINUTE: u32 = 20;
/// A fee a little below the current quote is still taken (prices move between quote and request)
const TOLERANCE: f64 = 0.8;
/// The relay keeps at least this much SOL for its own fees before it sponsors anything
const SOL_FLOOR: u64 = 30_000_000;

pub struct SolSponsor {
    pub chain: Chain,
    pub rpc: String,
    pub program: sol::Pubkey,
    pub mint: sol::Pubkey,
    pub key: SigningKey,
}

pub struct EvmSponsor {
    pub chain: Chain,
    pub contract: Address,
    pub token: Address,
    pub provider: DynProvider,
    pub signer: PrivateKeySigner,
}

#[derive(Default, Clone, Copy)]
struct Prices {
    sol_usd: f64,
    bnb_usd: f64,
    /// wei per gas on BNB Smart Chain
    gas_price: f64,
    sol_balance: u64,
    bnb_balance: f64,
    at: u64,
}

pub struct Sponsor<C> {
    relay: Arc<Relay<C>>,
    sol: Option<SolSponsor>,
    evm: Option<EvmSponsor>,
    trust_proxy: bool,
    prices: Mutex<Prices>,
    /// Recently sent, by swap: the same claim is not paid twice
    recent: Mutex<HashMap<String, (String, u64)>>,
    writes: Mutex<HashMap<IpAddr, (u64, u32)>>,
    /// One transaction at a time from the relay's own accounts (EVM nonces)
    sending: tokio::sync::Mutex<()>,
}

fn now() -> u64 {
    crate::http::now()
}

fn answer(status: StatusCode, a: SponsorAnswer) -> (StatusCode, Json<SponsorAnswer>) {
    (status, Json(a))
}

fn refuse(why: impl std::fmt::Display) -> (StatusCode, Json<SponsorAnswer>) {
    answer(StatusCode::BAD_REQUEST, SponsorAnswer { error: Some(why.to_string()), ..Default::default() })
}

impl<C: ChainView + 'static> Sponsor<C> {
    pub fn new(relay: Arc<Relay<C>>, sol: Option<SolSponsor>, evm: Option<EvmSponsor>, trust_proxy: bool) -> Arc<Self> {
        Arc::new(Sponsor {
            relay,
            sol,
            evm,
            trust_proxy,
            prices: Mutex::default(),
            recent: Mutex::default(),
            writes: Mutex::default(),
            sending: tokio::sync::Mutex::new(()),
        })
    }

    pub fn enabled(&self) -> bool {
        self.sol.is_some() || self.evm.is_some()
    }

    /// Prices, gas price and our balances, every few minutes (no quote without them)
    pub async fn refresh(&self) {
        let mut p = *self.prices.lock().unwrap_or_else(|e| e.into_inner());
        if let Ok(r) = reqwest::Client::builder().timeout(Duration::from_secs(20)).user_agent("monzero-dex-relay").build().expect("client")
            .get("https://monzero.org/markets/api.php?only=coins").send().await
        {
            if let Ok(v) = r.json::<serde_json::Value>().await {
                for c in v["coins"].as_array().into_iter().flatten() {
                    let usd = c["usd"].as_f64().unwrap_or(0.0);
                    match c["symbol"].as_str() {
                        Some("SOL") if usd > 0.0 => p.sol_usd = usd,
                        Some("BNB") if usd > 0.0 => p.bnb_usd = usd,
                        _ => {}
                    }
                }
            }
        }
        if let Some(s) = &self.sol {
            if let Ok(b) = sol::balance(&s.rpc, &s.key.verifying_key().to_bytes()).await {
                p.sol_balance = b;
            }
        }
        if let Some(e) = &self.evm {
            if let Ok(g) = e.provider.get_gas_price().await {
                p.gas_price = g as f64;
            }
            if let Ok(b) = e.provider.get_balance(e.signer.address()).await {
                p.bnb_balance = b.to_string().parse::<f64>().unwrap_or(0.0) / 1e18;
            }
        }
        p.at = now();
        *self.prices.lock().unwrap_or_else(|e| e.into_inner()) = p;
    }

    fn quote(&self) -> SponsorInfo {
        let p = *self.prices.lock().unwrap_or_else(|e| e.into_inner());
        let mut info = SponsorInfo::default();
        let fresh = now() < p.at + 30 * 60;
        if let (Some(s), true) = (&self.sol, fresh && p.sol_usd > 0.0 && p.sol_balance >= SOL_FLOOR) {
            let usdt = |lamports: u64, min: u64| ((lamports as f64 / 1e9 * p.sol_usd * 1.3 * 1e6).ceil() as u64).max(min);
            info.solana = Some(sol::to_base58(&s.key.verifying_key().to_bytes()));
            // The seller's claim: fees, and its token account's deposit when it has none
            info.sol_claim_fee = Some(usdt(10_000 + sol::TOKEN_ACCOUNT_RENT, 50_000));
            // The buyer's lock: the deposits fronted (they go back to the buyer) and the fee
            info.sol_lock_front = Some(sp::SOL_LOCK_FRONT);
            info.sol_lock_fee = Some(usdt(sp::SOL_LOCK_FRONT + 10_000, 100_000));
        }
        if let (Some(e), true) = (&self.evm, fresh && p.bnb_usd > 0.0 && p.gas_price > 0.0 && p.bnb_balance >= 0.002) {
            // A relayed claim costs about 150,000 gas; 1.5x margin; at least 0.05 USDT (18 decimals)
            let usd = 150_000.0 * p.gas_price / 1e18 * p.bnb_usd * 1.5;
            let units = (usd.max(0.05) * 1e6).ceil() as u128 * 1_000_000_000_000u128;
            info.evm = Some(e.signer.address());
            info.bsc_claim_fee = Some(units.to_string());
        }
        info
    }

    fn allow(&self, headers: &HeaderMap, peer: SocketAddr) -> bool {
        let ip = if self.trust_proxy {
            headers.get("x-real-ip").and_then(|v| v.to_str().ok()).and_then(|v| v.parse().ok()).unwrap_or(peer.ip())
        } else {
            peer.ip()
        };
        let t = now();
        let mut w = self.writes.lock().unwrap_or_else(|e| e.into_inner());
        if w.len() > 50_000 {
            w.retain(|_, (start, _)| *start + 60 > t);
        }
        let e = w.entry(ip).or_insert((t, 0));
        if e.0 + 60 <= t {
            *e = (t, 0);
        }
        e.1 += 1;
        e.1 <= PER_MINUTE
    }

    /// The transaction sent for this swap in the last minutes, if any
    fn already(&self, swap: &str) -> Option<String> {
        let mut r = self.recent.lock().unwrap_or_else(|e| e.into_inner());
        r.retain(|_, (_, at)| *at + 300 > now());
        r.get(swap).map(|(tx, _)| tx.clone())
    }

    fn sent(&self, swap: &str, tx: &str) {
        self.recent.lock().unwrap_or_else(|e| e.into_inner()).insert(swap.to_string(), (tx.to_string(), now()));
    }

    /// The trade print (for the price chart): the claim is recorded once it is final
    async fn record(&self, chain_id: u64, tx: &str) {
        if let Ok(id) = serde_json::from_value::<TxId>(serde_json::Value::String(tx.to_string())) {
            let _ = self.relay.report(chain_id, id, now()).await;
        }
    }

    async fn sol_claim(&self, req: SolClaimRequest) -> Result<String, String> {
        let s = self.sol.as_ref().ok_or("this relay does not pay Solana fees")?;
        if req.chain_id != s.chain.id() {
            return Err("another cluster".into());
        }
        let quote = self.quote().sol_claim_fee.ok_or("not sponsoring now")?;
        if (req.fee as f64) < quote as f64 * TOLERANCE {
            return Err(format!("the fee is {}; this relay takes {quote}", req.fee));
        }
        if let Some(tx) = self.already(&req.swap) {
            return Ok(tx);
        }
        let key = |t: &str| sol::from_base58(t).map_err(|e| e.to_string());
        let (swap, claimer, owner) = (key(&req.swap)?, key(&req.claimer)?, key(&req.owner)?);
        let secret: [u8; 32] = hex::decode(&req.secret).ok().and_then(|b| b.try_into().ok()).ok_or("bad secret")?;
        let signature: [u8; 64] = hex::decode(&req.signature).ok().and_then(|b| b.try_into().ok()).ok_or("bad signature")?;
        let _one = self.sending.lock().await;
        let blockhash = sol::latest_blockhash(&s.rpc).await?;
        let raw = sol::sponsored_claim_tx(&s.key, &s.program, &s.mint, &swap, &secret, req.fee, &claimer, &owner, &signature, &blockhash).map_err(|e| e.to_string())?;
        sol::simulate(&s.rpc, &raw).await.map_err(|e| format!("the claim would fail: {e}"))?;
        let tx = sol::send_raw(&s.rpc, &raw).await?;
        self.sent(&req.swap, &tx);
        self.record(req.chain_id, &tx).await;
        eprintln!("sponsor: Solana claim {tx} (fee {})", req.fee);
        Ok(tx)
    }

    async fn sol_lock(&self, req: SolLockRequest) -> Result<(String, String), String> {
        let s = self.sol.as_ref().ok_or("this relay does not pay Solana fees")?;
        if req.chain_id != s.chain.id() {
            return Err("another cluster".into());
        }
        let quote = self.quote().sol_lock_fee.ok_or("not sponsoring now")?;
        let message = base64::engine::general_purpose::STANDARD.decode(&req.message).map_err(|e| e.to_string())?;
        let buyer: [u8; 64] = hex::decode(&req.signature).ok().and_then(|b| b.try_into().ok()).ok_or("bad signature")?;
        let relayer = s.key.verifying_key().to_bytes();
        let min_fee = (quote as f64 * TOLERANCE) as u64;
        sol::check_sponsored_lock(&message, &relayer, &s.program, &s.mint, sp::SOL_LOCK_FRONT, min_fee)?;
        let raw = sol::cosign(&s.key, &message, &buyer);
        let _one = self.sending.lock().await;
        sol::simulate(&s.rpc, &raw).await.map_err(|e| format!("the lock would fail: {e}"))?;
        let tx = sol::send_raw(&s.rpc, &raw).await?;
        eprintln!("sponsor: Solana lock {tx}");
        Ok((tx, base64::engine::general_purpose::STANDARD.encode(&raw)))
    }

    async fn evm_claim(&self, req: EvmClaimRequest) -> Result<String, String> {
        let e = self.evm.as_ref().ok_or("this relay does not pay BNB Smart Chain fees")?;
        if req.chain_id != e.chain.id() {
            return Err("another chain".into());
        }
        let swap = Swap::abi_decode(&req.swap).map_err(|err| format!("bad swap: {err}"))?;
        if swap.asset != e.token {
            return Err("only USDT trades are sponsored".into());
        }
        let quote: U256 = self.quote().bsc_claim_fee.and_then(|f| f.parse().ok()).ok_or("not sponsoring now")?;
        if req.fee * U256::from(10u8) < quote * U256::from(8u8) {
            return Err(format!("the fee is {}; this relay takes {quote}", req.fee));
        }
        let id = monzero_dex_evm::swap_id(&swap).to_string();
        if let Some(tx) = self.already(&id) {
            return Ok(tx);
        }
        if req.signature.len() != 65 {
            return Err("bad signature".into());
        }
        let (r, s_, v) = (B256::from_slice(&req.signature[..32]), B256::from_slice(&req.signature[32..64]), req.signature[64]);
        let v = if v < 27 { v + 27 } else { v };
        let input = XmzSwap::claimRelayerCall { swap: swap.clone(), secret: req.secret, fee: req.fee, v, r, s: s_ }.abi_encode();
        let call = TransactionRequest::default().with_from(e.signer.address()).with_to(e.contract).with_input(input.clone());
        let _one = self.sending.lock().await;
        e.provider.call(call.clone()).await.map_err(|err| format!("the claim would fail: {err}"))?;
        let gas = e.provider.estimate_gas(call).await.map_err(|err| err.to_string())?;
        let sender = ProviderBuilder::new().wallet(EthereumWallet::from(e.signer.clone())).connect_provider(e.provider.clone());
        let pending = sender
            .send_transaction(TransactionRequest::default().with_to(e.contract).with_input(input).with_gas_limit(gas + gas / 5))
            .await
            .map_err(|err| err.to_string())?;
        let tx = pending.tx_hash().to_string();
        self.sent(&id, &tx);
        self.record(req.chain_id, &tx).await;
        eprintln!("sponsor: BNB Smart Chain claim {tx} (fee {})", req.fee);
        Ok(tx)
    }
}

pub fn router<C: ChainView + 'static>(s: Arc<Sponsor<C>>) -> Router {
    Router::new()
        .route(sp::INFO_PATH, get(info::<C>))
        .route(sp::SOL_CLAIM_PATH, post(sol_claim::<C>))
        .route(sp::SOL_LOCK_PATH, post(sol_lock::<C>))
        .route(sp::EVM_CLAIM_PATH, post(evm_claim::<C>))
        .with_state(s)
}

async fn info<C: ChainView + 'static>(State(s): State<Arc<Sponsor<C>>>) -> Json<SponsorInfo> {
    Json(s.quote())
}

async fn sol_claim<C: ChainView + 'static>(State(s): State<Arc<Sponsor<C>>>, ConnectInfo(peer): ConnectInfo<SocketAddr>, headers: HeaderMap, Json(req): Json<SolClaimRequest>) -> (StatusCode, Json<SponsorAnswer>) {
    if !s.allow(&headers, peer) {
        return answer(StatusCode::TOO_MANY_REQUESTS, SponsorAnswer { error: Some("too many requests".into()), ..Default::default() });
    }
    match s.sol_claim(req).await {
        Ok(tx) => answer(StatusCode::OK, SponsorAnswer { tx: Some(tx), ..Default::default() }),
        Err(e) => refuse(e),
    }
}

async fn sol_lock<C: ChainView + 'static>(State(s): State<Arc<Sponsor<C>>>, ConnectInfo(peer): ConnectInfo<SocketAddr>, headers: HeaderMap, Json(req): Json<SolLockRequest>) -> (StatusCode, Json<SponsorAnswer>) {
    if !s.allow(&headers, peer) {
        return answer(StatusCode::TOO_MANY_REQUESTS, SponsorAnswer { error: Some("too many requests".into()), ..Default::default() });
    }
    match s.sol_lock(req).await {
        Ok((tx, raw)) => answer(StatusCode::OK, SponsorAnswer { tx: Some(tx), raw: Some(raw), ..Default::default() }),
        Err(e) => refuse(e),
    }
}

async fn evm_claim<C: ChainView + 'static>(State(s): State<Arc<Sponsor<C>>>, ConnectInfo(peer): ConnectInfo<SocketAddr>, headers: HeaderMap, Json(req): Json<EvmClaimRequest>) -> (StatusCode, Json<SponsorAnswer>) {
    if !s.allow(&headers, peer) {
        return answer(StatusCode::TOO_MANY_REQUESTS, SponsorAnswer { error: Some("too many requests".into()), ..Default::default() });
    }
    match s.evm_claim(req).await {
        Ok(tx) => answer(StatusCode::OK, SponsorAnswer { tx: Some(tx), ..Default::default() }),
        Err(e) => refuse(e),
    }
}
