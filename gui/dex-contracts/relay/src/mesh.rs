//! The relay mesh: Monzero boxes run relays of their own and announce them, so the project's
//! relays (and anyone reading `/v1/relays`) can pull their offers and fall back on them.
//!
//! A box relay (config `announce`) signs `monzero-relay-announce/1\n<url>\n<unix time>` with its
//! own Ed25519 key every ten minutes and posts it to its hubs. A hub (config `mesh`) checks the
//! signature, the time, and the address (a Tor v3 onion, or a public IP/name on plain HTTP),
//! then reads the relay's `/v1/info` (through Tor for onions) and keeps it only if it serves the
//! same swap contracts. Listed relays are pulled like configured peers; every offer is checked
//! again as always, so a relay can leave offers out but never change or invent them.

use axum::extract::{ConnectInfo, State};
use axum::http::StatusCode;
use axum::routing::{get, post};
use axum::{Json, Router};
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::{BTreeMap, HashMap};
use std::net::{IpAddr, SocketAddr};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

pub const ANNOUNCE_DOMAIN: &str = "monzero-relay-announce/1";
/// An announcement older or newer than this is refused (clock skew allowance included)
const FRESH_SECS: u64 = 600;
/// A listed relay that has not announced and answered within this is dropped
const STALE_SECS: u64 = 3600;
const MAX_RELAYS: usize = 500;
const MAX_BODY: usize = 8 * 1024 * 1024;

pub fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

fn message(url: &str, time: u64) -> String {
    format!("{ANNOUNCE_DOMAIN}\n{url}\n{time}")
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Announcement {
    pub url: String,
    pub key: String,
    pub time: u64,
    pub signature: String,
}

impl Announcement {
    pub fn sign(key: &SigningKey, url: &str, time: u64) -> Self {
        let sig = key.sign(message(url, time).as_bytes());
        Announcement { url: url.to_string(), key: hex::encode(key.verifying_key().to_bytes()), time, signature: hex::encode(sig.to_bytes()) }
    }

    pub fn verify(&self, now: u64) -> Result<VerifyingKey, String> {
        if self.time + FRESH_SECS < now || self.time > now + FRESH_SECS {
            return Err("announcement too old or from the future".into());
        }
        let key: [u8; 32] = hex::decode(&self.key).ok().and_then(|k| k.try_into().ok()).ok_or("bad key")?;
        let key = VerifyingKey::from_bytes(&key).map_err(|_| "bad key")?;
        let sig: [u8; 64] = hex::decode(&self.signature).ok().and_then(|s| s.try_into().ok()).ok_or("bad signature")?;
        key.verify(message(&self.url, self.time).as_bytes(), &Signature::from_bytes(&sig)).map_err(|_| "signature does not match")?;
        check_url(&self.url)?;
        Ok(key)
    }
}

/// What an announced address may be: `http://<56 base32>.onion[:port]`, or `http(s)://` with a
/// public IP literal and a port. Never loopback, private, link-local or other local ranges,
/// so a hub cannot be turned against its own network.
pub fn check_url(url: &str) -> Result<(), String> {
    if url.len() > 200 || url.contains(['@', '?', '#', ' ', '\\']) {
        return Err("bad relay address".into());
    }
    let (scheme, rest) = url.split_once("://").ok_or("bad relay address")?;
    let host_port = rest.trim_end_matches('/');
    if host_port.contains('/') {
        return Err("relay address must not have a path".into());
    }
    let (host, port) = match host_port.rsplit_once(':') {
        Some((h, p)) if !h.ends_with(']') || h.starts_with('[') => (h, p.parse::<u16>().map_err(|_| "bad port")?),
        _ => (host_port, if scheme == "https" { 443 } else { 80 }),
    };
    if port == 0 {
        return Err("bad port".into());
    }
    if let Some(name) = host.strip_suffix(".onion") {
        if scheme != "http" || name.len() != 56 || !name.bytes().all(|b| b.is_ascii_lowercase() || (b'2'..=b'7').contains(&b)) {
            return Err("not a Tor v3 onion address".into());
        }
        return Ok(());
    }
    if scheme != "http" && scheme != "https" {
        return Err("relay address must be http or https".into());
    }
    let bare = host.trim_start_matches('[').trim_end_matches(']');
    if let Ok(ip) = bare.parse::<IpAddr>() {
        return if public_ip(ip) { Ok(()) } else { Err("relay address is not a public IP".into()) };
    }
    // No DNS names: a name could later resolve to the hub's own network (DNS rebinding)
    Err("a direct relay address must be a public IP (or use a Tor onion)".into())
}

pub fn public_ip(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => {
            let o = v4.octets();
            !(v4.is_private() || v4.is_loopback() || v4.is_link_local() || v4.is_broadcast() || v4.is_documentation() || v4.is_unspecified() || v4.is_multicast()
                || o[0] == 0 || (o[0] == 100 && (64..128).contains(&o[1])) || (o[0] == 192 && o[1] == 0 && o[2] == 0) || o[0] >= 240 || (o[0] == 198 && (o[1] == 18 || o[1] == 19)))
        }
        IpAddr::V6(v6) => {
            if let Some(v4) = v6.to_ipv4_mapped() {
                return public_ip(IpAddr::V4(v4));
            }
            let s = v6.segments();
            !(v6.is_loopback() || v6.is_unspecified() || v6.is_multicast() || (s[0] & 0xfe00) == 0xfc00 || (s[0] & 0xffc0) == 0xfe80 || s[0] == 0x2001 && s[1] == 0x0db8)
        }
    }
}

/// HTTP GET for relay reads: plain addresses through reqwest, onions through Tor's SOCKS port.
#[derive(Clone)]
pub struct Fetcher {
    pub client: reqwest::Client,
    pub tor_socks: Option<SocketAddr>,
}

impl Fetcher {
    pub fn new(tor_socks: Option<SocketAddr>) -> Self {
        let client = reqwest::Client::builder()
            .timeout(Duration::from_secs(20))
            .user_agent("monzero-dex-relay")
            // An announced relay must not bounce a hub to somewhere else (its own network)
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .expect("client");
        Fetcher { client, tor_socks }
    }

    pub async fn get_bytes(&self, url: &str) -> Result<Vec<u8>, String> {
        let onion = url.split_once("://").map(|(_, r)| r.split(['/', ':']).next().unwrap_or("")).is_some_and(|h| h.ends_with(".onion"));
        if onion {
            let socks = self.tor_socks.ok_or("no Tor configured for onion relays")?;
            return tokio::time::timeout(Duration::from_secs(60), socks_get(socks, url)).await.map_err(|_| format!("{url}: timed out"))?;
        }
        let resp = self.client.get(url).send().await.map_err(|e| e.to_string())?;
        if !resp.status().is_success() {
            return Err(format!("{url}: HTTP {}", resp.status()));
        }
        let bytes = resp.bytes().await.map_err(|e| e.to_string())?;
        if bytes.len() > MAX_BODY {
            return Err(format!("{url}: response too large"));
        }
        Ok(bytes.to_vec())
    }

    pub async fn get_json<T: serde::de::DeserializeOwned>(&self, url: &str) -> Result<T, String> {
        let bytes = self.get_bytes(url).await?;
        serde_json::from_slice(&bytes).map_err(|e| format!("{url}: {e}"))
    }
}

/// A plain HTTP/1.1 GET through a SOCKS5 proxy (Tor), with the host name resolved by the proxy.
async fn socks_get(socks: SocketAddr, url: &str) -> Result<Vec<u8>, String> {
    let rest = url.strip_prefix("http://").ok_or("onion relays are plain http (Tor encrypts them)")?;
    let (host_port, path) = rest.split_once('/').map(|(h, p)| (h, format!("/{p}"))).unwrap_or((rest, "/".into()));
    let (host, port) = host_port.rsplit_once(':').map(|(h, p)| (h, p.parse::<u16>().unwrap_or(80))).unwrap_or((host_port, 80));
    if host.len() > 255 {
        return Err("host too long".into());
    }
    let e = |e: std::io::Error| format!("{url}: {e}");
    let mut s = tokio::net::TcpStream::connect(socks).await.map_err(e)?;
    s.write_all(&[5, 1, 0]).await.map_err(e)?;
    let mut hello = [0u8; 2];
    s.read_exact(&mut hello).await.map_err(e)?;
    if hello != [5, 0] {
        return Err("SOCKS proxy refused".into());
    }
    let mut req = vec![5, 1, 0, 3, host.len() as u8];
    req.extend_from_slice(host.as_bytes());
    req.extend_from_slice(&port.to_be_bytes());
    s.write_all(&req).await.map_err(e)?;
    let mut head = [0u8; 4];
    s.read_exact(&mut head).await.map_err(e)?;
    if head[1] != 0 {
        return Err(format!("{url}: Tor could not connect (SOCKS {})", head[1]));
    }
    let skip = match head[3] {
        1 => 4 + 2,
        4 => 16 + 2,
        3 => {
            let mut n = [0u8; 1];
            s.read_exact(&mut n).await.map_err(e)?;
            n[0] as usize + 2
        }
        _ => return Err("bad SOCKS reply".into()),
    };
    let mut junk = vec![0u8; skip];
    s.read_exact(&mut junk).await.map_err(e)?;
    let request = format!("GET {path} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: monzero-dex-relay\r\nAccept: application/json\r\nConnection: close\r\n\r\n");
    s.write_all(request.as_bytes()).await.map_err(e)?;
    let mut buf = Vec::new();
    s.take((MAX_BODY + 64 * 1024) as u64).read_to_end(&mut buf).await.map_err(e)?;
    parse_response(&buf).map_err(|m| format!("{url}: {m}"))
}

/// Status line, headers, then the body by Content-Length, chunks, or to the end
fn parse_response(buf: &[u8]) -> Result<Vec<u8>, String> {
    let end = buf.windows(4).position(|w| w == b"\r\n\r\n").ok_or("no HTTP headers")?;
    let head = std::str::from_utf8(&buf[..end]).map_err(|_| "bad headers")?;
    let mut lines = head.split("\r\n");
    let status = lines.next().unwrap_or("");
    if !status.starts_with("HTTP/1.") || status.split(' ').nth(1) != Some("200") {
        return Err(format!("HTTP {}", status.split(' ').nth(1).unwrap_or("?")));
    }
    let mut length = None;
    let mut chunked = false;
    for l in lines {
        let (k, v) = l.split_once(':').unwrap_or((l, ""));
        match k.trim().to_ascii_lowercase().as_str() {
            "content-length" => length = v.trim().parse::<usize>().ok(),
            "transfer-encoding" => chunked = v.to_ascii_lowercase().contains("chunked"),
            _ => {}
        }
    }
    let body = &buf[end + 4..];
    let out = if chunked {
        let mut out = Vec::new();
        let mut rest = body;
        loop {
            let nl = rest.windows(2).position(|w| w == b"\r\n").ok_or("bad chunk")?;
            let size = usize::from_str_radix(std::str::from_utf8(&rest[..nl]).map_err(|_| "bad chunk")?.split(';').next().unwrap_or("").trim(), 16).map_err(|_| "bad chunk")?;
            rest = &rest[nl + 2..];
            if size == 0 {
                break;
            }
            if rest.len() < size || out.len() + size > MAX_BODY {
                return Err("chunk too large or cut short".into());
            }
            out.extend_from_slice(&rest[..size]);
            rest = rest.get(size + 2..).ok_or("bad chunk")?;
        }
        out
    } else if let Some(n) = length {
        if n > MAX_BODY || body.len() < n {
            return Err("body too large or cut short".into());
        }
        body[..n].to_vec()
    } else {
        body.to_vec()
    };
    if out.len() > MAX_BODY {
        return Err("response too large".into());
    }
    Ok(out)
}

#[derive(Clone, Debug, Serialize)]
pub struct Listed {
    pub url: String,
    #[serde(skip)]
    pub key: String,
    #[serde(rename = "lastOk")]
    pub last_ok: u64,
    pub kind: &'static str,
}

/// A hub's list of announced relays
pub struct Mesh {
    relays: Mutex<BTreeMap<String, Listed>>,     // by key
    last_from: Mutex<HashMap<IpAddr, u64>>,       // announce throttle per sender
    fetcher: Fetcher,
    /// chainId -> contract (lowercase) this relay serves; an announced relay must agree
    contracts: BTreeMap<u64, String>,
    pub peers: Vec<String>,
    /// Behind a reverse proxy: the sender is in X-Real-IP (as for the relay's other writes)
    pub trust_proxy: bool,
}

impl Mesh {
    pub fn new(fetcher: Fetcher, contracts: BTreeMap<u64, String>, peers: Vec<String>, trust_proxy: bool) -> Self {
        Mesh { relays: Mutex::new(BTreeMap::new()), last_from: Mutex::new(HashMap::new()), fetcher, contracts, peers, trust_proxy }
    }

    pub fn fetcher(&self) -> &Fetcher {
        &self.fetcher
    }

    /// Healthy announced relays, newest first
    pub fn listed(&self, now: u64) -> Vec<Listed> {
        let mut v: Vec<Listed> = self.relays.lock().unwrap().values().filter(|r| r.last_ok + STALE_SECS > now).cloned().collect();
        v.sort_by(|a, b| b.last_ok.cmp(&a.last_ok));
        v
    }

    pub fn prune(&self, now: u64) {
        self.relays.lock().unwrap().retain(|_, r| r.last_ok + STALE_SECS > now);
        self.last_from.lock().unwrap().retain(|_, t| *t + 60 > now);
    }

    /// The relay's own /v1/info must name the same swap contracts for every chain both serve
    pub async fn probe(&self, url: &str) -> Result<(), String> {
        let info: Value = self.fetcher.get_json(&format!("{}/v1/info", url.trim_end_matches('/'))).await?;
        if info["api"] != json!(1) {
            return Err("not a Monzero relay (api 1)".into());
        }
        let mut shared = 0;
        for c in info["chains"].as_array().ok_or("no chains")? {
            let (Some(id), Some(contract)) = (c["chainId"].as_u64(), c["contract"].as_str()) else { continue };
            if let Some(ours) = self.contracts.get(&id) {
                if *ours != contract.to_lowercase() {
                    return Err(format!("chain {id}: another swap contract"));
                }
                shared += 1;
            }
        }
        if shared == 0 {
            return Err("serves none of our chains".into());
        }
        Ok(())
    }

    pub async fn accept(&self, a: Announcement, from: IpAddr, now: u64) -> Result<(), String> {
        {
            let mut last = self.last_from.lock().unwrap();
            if last.get(&from).is_some_and(|t| t + 20 > now) {
                return Err("too many announcements; wait".into());
            }
            last.insert(from, now);
        }
        a.verify(now)?;
        {
            let relays = self.relays.lock().unwrap();
            if relays.len() >= MAX_RELAYS && !relays.contains_key(&a.key) {
                return Err("relay list is full".into());
            }
            // The same address under another key: keep the first owner
            if relays.values().any(|r| r.url == a.url && r.key != a.key && r.last_ok + STALE_SECS > now) {
                return Err("address already announced by another relay".into());
            }
        }
        if self.peers.iter().any(|p| p.trim_end_matches('/') == a.url.trim_end_matches('/')) {
            return Ok(());
        }
        self.probe(&a.url).await?;
        let kind = if a.url.contains(".onion") { "onion" } else { "direct" };
        self.relays.lock().unwrap().insert(a.key.clone(), Listed { url: a.url.trim_end_matches('/').to_string(), key: a.key, last_ok: now, kind });
        Ok(())
    }
}

pub fn router(mesh: Arc<Mesh>) -> Router {
    Router::new().route("/v1/relays", get(list)).route("/v1/relays/announce", post(announce)).with_state(mesh)
}

async fn list(State(mesh): State<Arc<Mesh>>) -> Json<Value> {
    let listed = mesh.listed(now());
    Json(json!({"peers": mesh.peers, "relays": listed.into_iter().take(200).collect::<Vec<_>>()}))
}

async fn announce(State(mesh): State<Arc<Mesh>>, ConnectInfo(addr): ConnectInfo<SocketAddr>, headers: axum::http::HeaderMap, Json(a): Json<Announcement>) -> (StatusCode, Json<Value>) {
    let ip = if mesh.trust_proxy { headers.get("x-real-ip").and_then(|v| v.to_str().ok()).and_then(|v| v.parse().ok()).unwrap_or(addr.ip()) } else { addr.ip() };
    match mesh.accept(a, ip, now()).await {
        Ok(()) => (StatusCode::OK, Json(json!({"ok": true}))),
        Err(e) => (StatusCode::BAD_REQUEST, Json(json!({"ok": false, "error": e}))),
    }
}

/// The box side: this relay's key (made on first start) and its address (a file the box writes:
/// the onion name from Tor, or the public address the owner chose)
pub struct Announcer {
    pub key: SigningKey,
    pub url_file: PathBuf,
    pub to: Vec<String>,
}

impl Announcer {
    pub fn load(key_file: &std::path::Path, url_file: PathBuf, to: Vec<String>) -> Result<Self, String> {
        let key = match std::fs::read(key_file) {
            Ok(b) if b.len() == 32 => SigningKey::from_bytes(&b.try_into().expect("32")),
            _ => {
                let mut seed = [0u8; 32];
                std::fs::File::open("/dev/urandom").and_then(|mut f| std::io::Read::read_exact(&mut f, &mut seed)).map_err(|e| format!("random: {e}"))?;
                let tmp = key_file.with_extension("tmp");
                {
                    use std::io::Write;
                    let mut o = std::fs::OpenOptions::new();
                    o.write(true).create(true).truncate(true);
                    #[cfg(unix)]
                    std::os::unix::fs::OpenOptionsExt::mode(&mut o, 0o600);
                    o.open(&tmp).and_then(|mut f| f.write_all(&seed)).map_err(|e| format!("{}: {e}", key_file.display()))?;
                }
                std::fs::rename(&tmp, key_file).map_err(|e| e.to_string())?;
                SigningKey::from_bytes(&seed)
            }
        };
        Ok(Announcer { key, url_file, to })
    }

    /// The address to announce, if the box has one now (public relay on)
    pub fn url(&self) -> Option<String> {
        let mut url = std::fs::read_to_string(&self.url_file).ok()?.trim().to_string();
        // Tor's hostname file holds the bare onion name
        if url.ends_with(".onion") && !url.contains("://") {
            url = format!("http://{url}");
        }
        check_url(&url).ok().map(|_| url)
    }

    pub async fn announce_once(&self, client: &reqwest::Client) -> Vec<String> {
        let Some(url) = self.url() else { return vec![] };
        let a = Announcement::sign(&self.key, &url, now());
        let mut results = vec![];
        for hub in &self.to {
            let r = client.post(format!("{}/v1/relays/announce", hub.trim_end_matches('/'))).json(&a).send().await;
            results.push(match r {
                Ok(resp) if resp.status().is_success() => format!("{hub}: listed as {url}"),
                Ok(resp) => format!("{hub}: {}", resp.text().await.unwrap_or_default()),
                Err(e) => format!("{hub}: {e}"),
            });
        }
        results
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn addresses() {
        let onion = format!("http://{}.onion", "a".repeat(56));
        assert!(check_url(&onion).is_ok());
        assert!(check_url(&format!("{onion}:8790")).is_ok());
        assert!(check_url(&format!("https://{}.onion", "a".repeat(56))).is_err(), "onions are plain http");
        assert!(check_url(&format!("http://{}.onion", "a".repeat(55))).is_err());
        assert!(check_url("http://8.8.8.8:8790").is_ok());
        assert!(check_url("http://relay.example.org").is_err(), "no DNS names (rebinding)");
        for bad in ["http://127.0.0.1:8790", "http://192.168.1.5:8790", "http://10.0.0.1", "http://169.254.169.254", "http://[::1]:8790", "http://[fd00::1]", "http://100.64.1.1",
                    "http://localhost:8790", "http://box.local", "ftp://8.8.8.8", "http://8.8.8.8/v1/x", "http://user@8.8.8.8", "http://8.8.8.8?x=1", "http://8.8.8.8:0", "http://[::ffff:127.0.0.1]"] {
            assert!(check_url(bad).is_err(), "{bad} must be refused");
        }
    }

    #[test]
    fn signatures() {
        let key = SigningKey::from_bytes(&[7u8; 32]);
        let url = format!("http://{}.onion", "b".repeat(56));
        let a = Announcement::sign(&key, &url, 1_000_000);
        assert!(a.verify(1_000_000).is_ok());
        assert!(a.verify(1_000_000 + FRESH_SECS + 1).is_err(), "too old");
        let mut moved = a.clone();
        moved.url = format!("http://{}.onion", "c".repeat(56));
        assert!(moved.verify(1_000_000).is_err(), "another address under the same signature");
        let mut later = a.clone();
        later.time += 1;
        assert!(later.verify(1_000_000).is_err());
    }

    #[test]
    fn responses() {
        assert_eq!(parse_response(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}extra").unwrap(), b"{}");
        assert_eq!(parse_response(b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n").unwrap(), b"{}");
        assert!(parse_response(b"HTTP/1.1 404 Not Found\r\n\r\n").is_err());
        assert!(parse_response(b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n{}").is_err(), "cut short");
    }
}
