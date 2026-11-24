//! A mirror of the operator's signed control documents (the kill switch and trade limits every
//! wallet checks before a new trade), so wallets still find a current one when the website is
//! down. A document is signed with the operator's pinned key: a copy from anywhere is as good as
//! the original, and nothing unsigned is ever stored or served.
//!
//! - GET  /v1/control/<network>.json: the newest valid document this relay holds, byte for byte
//! - POST /v1/control: a signed document (the operator's renewal job sends each new one here);
//!   stored when its signature is the pinned key's and it is newer than the one held
//! - every 10 minutes the relay also fetches each network's document from the website
//!
//! Held documents survive a restart (data_dir/control/).

use axum::body::Bytes;
use axum::extract::{Path, State};
use axum::http::{header, StatusCode};
use axum::response::IntoResponse;
use axum::routing::{get, post};
use axum::{Json, Router};
use ed25519_dalek::{Signature, VerifyingKey};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

/// The signed message is this tag, a zero byte, then the payload text (as the wallets check it)
const DOMAIN: &[u8] = b"monzero-dex-control-v1";
/// The largest document a wallet accepts
pub const MAX_DOCUMENT: usize = 4096;
/// A document's longest validity (as the wallets check it)
const MAX_AGE: u64 = 30 * 86400;
/// The networks fetched from the website by default
pub const NETWORKS: [&str; 5] = ["eth-mainnet", "bnb-mainnet", "sol-mainnet", "sol-usdt-v2-mainnet", "bnb-usdt-v2-mainnet"];

#[derive(Clone)]
struct Held {
    raw: Vec<u8>,
    sequence: u64,
    expires: u64,
}

pub struct ControlMirror {
    key: VerifyingKey,
    dir: PathBuf,
    docs: Mutex<HashMap<String, Held>>,
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

fn network_name(n: &str) -> bool {
    !n.is_empty() && n.len() <= 40 && n.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
}

impl ControlMirror {
    /// `key`: the operator's control key (32 bytes hex, the one pinned in the wallets)
    pub fn new(key_hex: &str, dir: PathBuf) -> Result<Arc<Self>, String> {
        let bytes: [u8; 32] = hex::decode(key_hex).ok().and_then(|b| b.try_into().ok()).ok_or("controlKey must be 32 bytes of hex")?;
        let key = VerifyingKey::from_bytes(&bytes).map_err(|e| format!("controlKey: {e}"))?;
        let m = Arc::new(Self { key, dir, docs: Mutex::new(HashMap::new()) });
        // Documents held before a restart, checked again (an expired one is dropped)
        if let Ok(entries) = std::fs::read_dir(&m.dir) {
            for e in entries.flatten() {
                if let Ok(raw) = std::fs::read(e.path()) {
                    let _ = m.accept(&raw, now());
                }
            }
        }
        Ok(m)
    }

    /// Checks a signed document: (network, sequence, expiresAt), or why it is refused
    fn check(&self, raw: &[u8], now: u64) -> Result<(String, u64, u64), String> {
        if raw.len() > MAX_DOCUMENT {
            return Err("too large".into());
        }
        let envelope: Value = serde_json::from_slice(raw).map_err(|_| "not JSON")?;
        let obj = envelope.as_object().ok_or("not an object")?;
        if obj.len() != 2 {
            return Err("an envelope has exactly payload and signature".into());
        }
        let payload_text = obj.get("payload").and_then(Value::as_str).ok_or("no payload")?;
        let sig: [u8; 64] = obj.get("signature").and_then(Value::as_str).and_then(|s| hex::decode(s).ok()).and_then(|b| b.try_into().ok()).ok_or("bad signature")?;
        let mut message = DOMAIN.to_vec();
        message.push(0);
        message.extend_from_slice(payload_text.as_bytes());
        self.key.verify_strict(&message, &Signature::from_bytes(&sig)).map_err(|_| "not signed by the operator's control key")?;
        let p: Value = serde_json::from_str(payload_text).map_err(|_| "payload is not JSON")?;
        if p["schema"] != "monzero-dex-control-v1" {
            return Err("unknown schema".into());
        }
        let network = p["network"].as_str().filter(|n| network_name(n)).ok_or("bad network")?.to_string();
        let (sequence, issued, expires) = (p["sequence"].as_u64(), p["issuedAt"].as_u64(), p["expiresAt"].as_u64());
        let (Some(sequence), Some(issued), Some(expires)) = (sequence, issued, expires) else { return Err("missing sequence or times".into()) };
        if expires <= issued || expires - issued > MAX_AGE || issued > now + 300 {
            return Err("validity window refused".into());
        }
        if expires <= now {
            return Err("expired".into());
        }
        Ok((network, sequence, expires))
    }

    /// Stores a valid document when it is newer than the one held: true when stored
    pub fn accept(&self, raw: &[u8], now: u64) -> Result<bool, String> {
        let (network, sequence, expires) = self.check(raw, now)?;
        let mut docs = self.docs.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(held) = docs.get(&network) {
            if held.expires > now && (sequence < held.sequence || (sequence == held.sequence && held.raw == raw)) {
                return Ok(false);
            }
            if sequence == held.sequence && held.expires > now {
                return Err("another document with the same sequence is held".into());
            }
        }
        docs.insert(network.clone(), Held { raw: raw.to_vec(), sequence, expires });
        drop(docs);
        if std::fs::create_dir_all(&self.dir).is_ok() {
            let tmp = self.dir.join(format!(".{network}.json.tmp"));
            if std::fs::write(&tmp, raw).is_ok() {
                let _ = std::fs::rename(&tmp, self.dir.join(format!("{network}.json")));
            }
        }
        Ok(true)
    }

    /// The held document of a network, while it is valid
    pub fn get(&self, network: &str, now: u64) -> Option<Vec<u8>> {
        let docs = self.docs.lock().unwrap_or_else(|e| e.into_inner());
        docs.get(network).filter(|h| h.expires > now).map(|h| h.raw.clone())
    }

    /// Fetches every network's document from `base` (".../dex/control/") and keeps the valid, newer ones
    pub async fn refresh_from(&self, client: &reqwest::Client, base: &str) {
        for network in NETWORKS {
            let url = format!("{base}{network}.json");
            let Ok(r) = client.get(&url).send().await else { continue };
            if !r.status().is_success() {
                continue;
            }
            if let Ok(raw) = r.bytes().await {
                if let Ok(true) = self.accept(&raw, now()) {
                    eprintln!("control: {network} document updated from {base}");
                }
            }
        }
    }
}

pub fn router(m: Arc<ControlMirror>) -> Router {
    Router::new()
        .route("/v1/control/{file}", get(serve))
        .route("/v1/control", post(receive))
        .with_state(m)
}

async fn serve(State(m): State<Arc<ControlMirror>>, Path(file): Path<String>) -> axum::response::Response {
    let Some(network) = file.strip_suffix(".json").filter(|n| network_name(n)) else {
        return (StatusCode::NOT_FOUND, "no such document").into_response();
    };
    match m.get(network, now()) {
        Some(raw) => ([(header::CONTENT_TYPE, "application/json"), (header::CACHE_CONTROL, "no-store")], raw).into_response(),
        None => (StatusCode::NOT_FOUND, "no current document for this network").into_response(),
    }
}

async fn receive(State(m): State<Arc<ControlMirror>>, body: Bytes) -> (StatusCode, Json<Value>) {
    match m.accept(&body, now()) {
        Ok(stored) => (StatusCode::OK, Json(json!({"ok": true, "stored": stored}))),
        Err(why) => (StatusCode::BAD_REQUEST, Json(json!({"ok": false, "error": why}))),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};

    fn doc(key: &SigningKey, network: &str, sequence: u64, issued: u64, expires: u64) -> Vec<u8> {
        let payload = format!(
            "{{\"expiresAt\":{expires},\"issuedAt\":{issued},\"maxLtcAtomsPerDay\":\"1\",\"maxLtcAtomsPerTrade\":\"1\",\"maxXmzAtomsPerTrade\":\"1\",\"network\":\"{network}\",\"newTradesEnabled\":true,\"schema\":\"monzero-dex-control-v1\",\"sequence\":{sequence}}}"
        );
        let mut message = DOMAIN.to_vec();
        message.push(0);
        message.extend_from_slice(payload.as_bytes());
        let sig = hex::encode(key.sign(&message).to_bytes());
        serde_json::to_vec(&json!({"payload": payload, "signature": sig})).unwrap()
    }

    fn mirror(key: &SigningKey, dir: &std::path::Path) -> Arc<ControlMirror> {
        ControlMirror::new(&hex::encode(key.verifying_key().to_bytes()), dir.to_path_buf()).unwrap()
    }

    #[test]
    fn keeps_the_newest_signed_document_and_serves_it_byte_for_byte() {
        let dir = std::env::temp_dir().join(format!("control-test-{}", std::process::id()));
        let key = SigningKey::from_bytes(&[7; 32]);
        let m = mirror(&key, &dir);
        let t = now();
        let first = doc(&key, "bnb-mainnet", 3, t - 10, t + 86400);
        assert_eq!(m.accept(&first, t), Ok(true));
        assert_eq!(m.accept(&first, t), Ok(false), "the same document again changes nothing");
        assert_eq!(m.get("bnb-mainnet", t), Some(first.clone()));
        // An older one never replaces it; a newer one does
        assert_eq!(m.accept(&doc(&key, "bnb-mainnet", 2, t - 10, t + 86400), t), Ok(false));
        let newer = doc(&key, "bnb-mainnet", 4, t - 5, t + 86400);
        assert_eq!(m.accept(&newer, t), Ok(true));
        assert_eq!(m.get("bnb-mainnet", t), Some(newer.clone()));
        // Held across a restart
        assert_eq!(mirror(&key, &dir).get("bnb-mainnet", t), Some(newer));
        // Gone once expired
        assert_eq!(m.get("bnb-mainnet", t + 86401), None);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn refuses_anything_not_signed_by_the_operator() {
        let dir = std::env::temp_dir().join(format!("control-test-b-{}", std::process::id()));
        let key = SigningKey::from_bytes(&[7; 32]);
        let other = SigningKey::from_bytes(&[8; 32]);
        let m = mirror(&key, &dir);
        let t = now();
        assert!(m.accept(&doc(&other, "bnb-mainnet", 9, t - 10, t + 86400), t).is_err());
        assert!(m.accept(&doc(&key, "bnb-mainnet", 9, t - 10, t - 1), t).is_err(), "expired");
        assert!(m.accept(&doc(&key, "bnb-mainnet", 9, t - 10, t + 31 * 86400), t).is_err(), "longer than 30 days");
        assert!(m.accept(&doc(&key, "../etc", 9, t - 10, t + 86400), t).is_err(), "bad network name");
        let mut tampered = doc(&key, "bnb-mainnet", 9, t - 10, t + 86400);
        let at = tampered.windows(4).position(|w| w == b"true").unwrap();
        tampered.splice(at..at + 4, b"fals".iter().copied());
        assert!(m.accept(&tampered, t).is_err(), "a changed payload fails the signature");
        assert_eq!(m.get("bnb-mainnet", t), None);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
