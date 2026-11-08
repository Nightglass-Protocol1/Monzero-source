//! The relay's HTTP API (v1). Reads are open to any site (CORS); writes are signed or checked on chain.

use crate::{ChainView, PrintStatus, Relay, RelayError, INTERVALS};
use alloy::primitives::B256;
use axum::extract::{ConnectInfo, DefaultBodyLimit, Path, Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use monzero_dex_evm::listing::{SignedCancel, SignedListing};
use serde::Deserialize;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::net::{IpAddr, SocketAddr};
use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

/// Writes one address may make per minute
pub const WRITES_PER_MINUTE: u32 = 30;
const MAX_BODY: usize = 16 * 1024;

pub fn now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

pub struct Api<C> {
    pub relay: Arc<Relay<C>>,
    pub peers: Vec<String>,
    /// Behind a local reverse proxy: take the client address from X-Real-IP
    pub trust_proxy: bool,
    writes: Mutex<HashMap<IpAddr, (u64, u32)>>,
}

impl<C> Api<C> {
    pub fn new(relay: Arc<Relay<C>>, peers: Vec<String>, trust_proxy: bool) -> Self {
        Api { relay, peers, trust_proxy, writes: Mutex::default() }
    }

    fn allow_write(&self, headers: &HeaderMap, peer: SocketAddr, now: u64) -> bool {
        let ip = if self.trust_proxy {
            headers.get("x-real-ip").and_then(|v| v.to_str().ok()).and_then(|v| v.parse().ok()).unwrap_or(peer.ip())
        } else {
            peer.ip()
        };
        let mut w = self.writes.lock().unwrap_or_else(|e| e.into_inner());
        if w.len() > 50_000 {
            w.retain(|_, (start, _)| *start + 60 > now);
        }
        let entry = w.entry(ip).or_insert((now, 0));
        if entry.0 + 60 <= now {
            *entry = (now, 0);
        }
        entry.1 += 1;
        entry.1 <= WRITES_PER_MINUTE
    }
}

struct Failure(StatusCode, String);

impl IntoResponse for Failure {
    fn into_response(self) -> Response {
        (self.0, Json(json!({ "error": self.1 }))).into_response()
    }
}

impl From<RelayError> for Failure {
    fn from(e: RelayError) -> Self {
        let status = match e {
            RelayError::NotFound => StatusCode::NOT_FOUND,
            RelayError::MakerLimit | RelayError::Full => StatusCode::TOO_MANY_REQUESTS,
            RelayError::ChainRead(_) => StatusCode::SERVICE_UNAVAILABLE,
            _ => StatusCode::BAD_REQUEST,
        };
        Failure(status, e.to_string())
    }
}

fn limited() -> Failure {
    Failure(StatusCode::TOO_MANY_REQUESTS, "too many requests; try again in a minute".into())
}

#[derive(Deserialize)]
struct PairQuery {
    pair: Option<String>,
    since: Option<u64>,
}

#[derive(Deserialize)]
struct CandleQuery {
    pair: String,
    interval: Option<u64>,
    since: Option<u64>,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Report {
    chain_id: u64,
    tx: B256,
}

pub fn router<C: ChainView + 'static>(api: Arc<Api<C>>) -> Router {
    let cors = tower_http::cors::CorsLayer::new()
        .allow_origin(tower_http::cors::Any)
        .allow_methods([axum::http::Method::GET, axum::http::Method::POST])
        .allow_headers([axum::http::header::CONTENT_TYPE]);
    Router::new()
        .route("/v1/info", get(info::<C>))
        .route("/v1/listings", get(listings::<C>).post(post_listing::<C>))
        .route("/v1/listings/{id}", get(listing::<C>))
        .route("/v1/cancel", post(cancel::<C>))
        .route("/v1/prints", get(prints::<C>).post(report::<C>))
        .route("/v1/candles", get(candles::<C>))
        .layer(DefaultBodyLimit::max(MAX_BODY))
        .layer(cors)
        .with_state(api)
}

async fn info<C: ChainView>(State(api): State<Arc<Api<C>>>) -> Json<Value> {
    let rules = api.relay.rules();
    let chains: Vec<Value> = rules
        .contracts
        .iter()
        .map(|(id, contract)| {
            let pair = monzero_dex_evm::Chain::from_id(*id).map(|c| c.pair());
            json!({ "chainId": id, "pair": pair, "contract": contract, "maxValue": rules.max_value.get(id) })
        })
        .collect();
    Json(json!({
        "relay": "monzero-dex-relay",
        "version": env!("CARGO_PKG_VERSION"),
        "api": 1,
        "chains": chains,
        "peers": api.peers,
        "intervals": INTERVALS,
        "limits": { "openOffersPerMaker": crate::MAX_PER_MAKER, "writesPerMinute": WRITES_PER_MINUTE },
    }))
}

async fn listings<C: ChainView>(State(api): State<Arc<Api<C>>>, Query(q): Query<PairQuery>) -> Json<Value> {
    Json(json!({ "listings": api.relay.listings(q.pair.as_deref(), now()) }))
}

async fn listing<C: ChainView>(State(api): State<Arc<Api<C>>>, Path(id): Path<B256>) -> Result<Json<SignedListing>, Failure> {
    api.relay.listing(id).map(Json).ok_or(Failure(StatusCode::NOT_FOUND, "no such offer".into()))
}

async fn post_listing<C: ChainView>(
    State(api): State<Arc<Api<C>>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(signed): Json<SignedListing>,
) -> Result<Json<Value>, Failure> {
    let now = now();
    if !api.allow_write(&headers, peer, now) {
        return Err(limited());
    }
    let id = signed.listing.id;
    let new = api.relay.post(signed, now).await?;
    Ok(Json(json!({ "id": id, "new": new })))
}

async fn cancel<C: ChainView>(
    State(api): State<Arc<Api<C>>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(signed): Json<SignedCancel>,
) -> Result<Json<Value>, Failure> {
    let now = now();
    if !api.allow_write(&headers, peer, now) {
        return Err(limited());
    }
    api.relay.cancel(&signed, now)?;
    Ok(Json(json!({ "id": signed.cancel.id, "cancelled": true })))
}

async fn prints<C: ChainView>(State(api): State<Arc<Api<C>>>, Query(q): Query<PairQuery>) -> Json<Value> {
    Json(json!({ "prints": api.relay.prints(q.pair.as_deref(), q.since.unwrap_or(0)) }))
}

async fn report<C: ChainView>(
    State(api): State<Arc<Api<C>>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(r): Json<Report>,
) -> Result<Json<Value>, Failure> {
    let now = now();
    if !api.allow_write(&headers, peer, now) {
        return Err(limited());
    }
    let status = api.relay.report(r.chain_id, r.tx, now).await?;
    let code = match status {
        PrintStatus::Recorded => "recorded",
        PrintStatus::Pending => "pending",
    };
    Ok(Json(json!({ "tx": r.tx, "status": code })))
}

async fn candles<C: ChainView>(State(api): State<Arc<Api<C>>>, Query(q): Query<CandleQuery>) -> Result<Json<Value>, Failure> {
    let interval = q.interval.unwrap_or(3600);
    if !INTERVALS.contains(&interval) {
        return Err(Failure(StatusCode::BAD_REQUEST, format!("interval must be one of {INTERVALS:?}")));
    }
    Ok(Json(json!({ "pair": q.pair, "interval": interval, "candles": api.relay.candles(&q.pair, interval, q.since.unwrap_or(0)) })))
}
