//! The app's side of the open relays: read and post offers, mail and trades (see `relay/`).
//!
//! Nothing a relay returns is trusted: offers are checked by signature and rules, letters must
//! open with our mailbox key, and trades are only reported, never read back as facts.

use crate::listing::{Rules, SignedCancel, SignedListing};
use crate::mail::Envelope;
use alloy::primitives::{Address, Bytes, B256};
use serde::Deserialize;
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::time::Duration;

/// The XmzSwap deployments the app trades on, pinned in the build (never taken from a relay)
pub fn pinned_contracts() -> BTreeMap<u64, Address> {
    [
        (1, "0xF2b4e38a4d507Cd29F1d4697f3E25D73123ED187"),
        (56, "0x9073290952DCDF3225D8f65d787cF9148a490feb"),
        (11_155_111, "0xcFc3b72EA8E5fa41905a2119c457c43162d9Ab30"),
        (97, "0xf7c19f735C3494b356091eFBeFf3ab53617190b7"),
    ]
    .into_iter()
    .map(|(id, a)| (id, a.parse().expect("pinned address")))
    // Solana clusters: the stand-in address of each pinned swap program
    .chain(crate::sol::pinned_contracts())
    .collect()
}

pub fn pinned_rules() -> Rules {
    Rules { contracts: pinned_contracts(), max_value: BTreeMap::new() }
}

/// Relay base URLs: https, or plain http to this machine only
pub fn check_relay(url: &str) -> Result<String, String> {
    let u = url.trim().trim_end_matches('/');
    let ok_scheme = u.starts_with("https://") || u.starts_with("http://127.0.0.1:") || u.starts_with("http://localhost:");
    if !ok_scheme || u.len() > 200 || u.chars().any(|c| c.is_whitespace() || c == '?' || c == '#') {
        return Err(format!("not a relay address: {url}"));
    }
    Ok(u.to_string())
}

pub struct RelayClient {
    http: reqwest::Client,
    pub relays: Vec<String>,
}

#[derive(Deserialize)]
struct Listings {
    listings: Vec<SignedListing>,
}

#[derive(Deserialize)]
struct MailItem {
    at: u64,
    sealed: Bytes,
}

#[derive(Deserialize)]
struct Mail {
    mail: Vec<MailItem>,
}

impl RelayClient {
    pub fn new(relays: &[String]) -> Result<Self, String> {
        let relays = relays.iter().map(|r| check_relay(r)).collect::<Result<Vec<_>, _>>()?;
        if relays.is_empty() {
            return Err("no relay given".into());
        }
        let http = reqwest::Client::builder()
            .timeout(Duration::from_secs(20))
            .user_agent("monzero-app")
            .build()
            .map_err(|e| e.to_string())?;
        Ok(RelayClient { http, relays })
    }

    async fn get<T: serde::de::DeserializeOwned>(&self, url: &str) -> Result<T, String> {
        let r = self.http.get(url).send().await.map_err(|e| e.to_string())?;
        if !r.status().is_success() {
            return Err(format!("HTTP {}", r.status()));
        }
        let bytes = r.bytes().await.map_err(|e| e.to_string())?;
        if bytes.len() > 16 * 1024 * 1024 {
            return Err("response too large".into());
        }
        serde_json::from_slice(&bytes).map_err(|e| e.to_string())
    }

    async fn post(&self, url: &str, body: &Value) -> Result<Value, String> {
        let r = self.http.post(url).json(body).send().await.map_err(|e| e.to_string())?;
        let status = r.status();
        let v: Value = r.json().await.unwrap_or(Value::Null);
        if status.is_success() {
            Ok(v)
        } else {
            Err(v.get("error").and_then(Value::as_str).map(str::to_string).unwrap_or_else(|| format!("HTTP {status}")))
        }
    }

    /// Posts to every relay; the result per relay
    async fn post_all(&self, path: &str, body: &Value) -> Vec<Value> {
        let mut out = vec![];
        for r in &self.relays {
            out.push(match self.post(&format!("{r}{path}"), body).await {
                Ok(_) => json!({"relay": r, "ok": true}),
                Err(e) => json!({"relay": r, "ok": false, "error": e}),
            });
        }
        out
    }

    /// Open offers from every relay that answers, each checked; with the relays that carry it
    pub async fn listings(&self, pair: Option<&str>, rules: &Rules, now: u64) -> (Vec<(SignedListing, Vec<String>)>, Vec<Value>) {
        let mut found: BTreeMap<B256, (SignedListing, Vec<String>)> = BTreeMap::new();
        let mut errors = vec![];
        for r in &self.relays {
            let url = match pair {
                Some(p) => format!("{r}/v1/listings?pair={}", p.replace('/', "%2F")),
                None => format!("{r}/v1/listings"),
            };
            match self.get::<Listings>(&url).await {
                Ok(l) => {
                    for signed in l.listings.into_iter().take(5000) {
                        if signed.verify(rules, now).is_err() || pair.is_some_and(|p| signed.listing.pair != p) {
                            continue;
                        }
                        let entry = found.entry(signed.listing.id).or_insert_with(|| (signed.clone(), vec![]));
                        // Two relays with different offers under one id: keep the first, signed one
                        if entry.0 == signed {
                            entry.1.push(r.clone());
                        }
                    }
                }
                Err(e) => errors.push(json!({"relay": r, "error": e})),
            }
        }
        (found.into_values().collect(), errors)
    }

    pub async fn post_listing(&self, signed: &SignedListing) -> Vec<Value> {
        self.post_all("/v1/listings", &serde_json::to_value(signed).expect("serializes")).await
    }

    pub async fn cancel(&self, signed: &SignedCancel) -> Vec<Value> {
        self.post_all("/v1/cancel", &serde_json::to_value(signed).expect("serializes")).await
    }

    pub async fn send_mail(&self, envelope: &Envelope) -> Vec<Value> {
        self.post_all("/v1/mail", &serde_json::to_value(envelope).expect("serializes")).await
    }

    /// Every letter for a mailbox, from every relay (duplicates removed)
    pub async fn mail(&self, to: B256) -> Vec<(u64, Bytes)> {
        let mut out: Vec<(u64, Bytes)> = vec![];
        for r in &self.relays {
            if let Ok(m) = self.get::<Mail>(&format!("{r}/v1/mail?to={to}")).await {
                for item in m.mail.into_iter().take(64) {
                    if !out.iter().any(|(_, s)| s == &item.sealed) {
                        out.push((item.at, item.sealed));
                    }
                }
            }
        }
        out.sort_by_key(|(at, _)| *at);
        out
    }

    pub async fn report(&self, chain_id: u64, tx: B256) -> Vec<Value> {
        self.post_all("/v1/prints", &json!({"chainId": chain_id, "tx": tx})).await
    }
}
