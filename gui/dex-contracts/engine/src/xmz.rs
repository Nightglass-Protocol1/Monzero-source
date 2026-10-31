//! The XMZ side of the swap over `monzero-wallet-rpc`.
//!
//! The shared XMZ address has spend key (a + b)·B, where a and b are the two traders'
//! [`SecretShare`]s, and a view key both traders know. Each trader runs:
//!  - its own funds wallet (the `main` wallet RPC), which sends the lock and receives the sweep;
//!  - a dedicated wallet RPC for the shared address (the `swap` wallet RPC). It holds a
//!    view-only wallet of the shared address until the combined spend key is known, then a
//!    spending wallet restored from that key.
//!
//! As on the EVM side, every XMZ transaction is built once without relaying it, kept in the
//! XMZ record **before** it is relayed, and later ticks relay the identical transaction again.
//! The record holds transaction metadata only; the spend key is in the swap wallet file,
//! which is protected by its own password.

use crate::driver::{DriverError, XmzSide};
use crate::machine::XmzLock;
use crate::secret::SecretShare;
use alloy::primitives::keccak256;
use alloy::transports::http::reqwest;
use curve25519_dalek::constants::ED25519_BASEPOINT_TABLE;
use curve25519_dalek::edwards::CompressedEdwardsY;
use curve25519_dalek::Scalar;
use md5::{Digest, Md5};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::path::PathBuf;
use std::sync::Mutex;

fn xmz_err(e: impl std::fmt::Display) -> DriverError {
    DriverError::Xmz(e.to_string())
}

// ---------- addresses ----------

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum XmzNetwork {
    /// Also used by the local fakechain (`monzerod --regtest`)
    Mainnet,
    Testnet,
    Stagenet,
}

impl XmzNetwork {
    /// Varint prefix of a standard address (86, 111, 131)
    fn prefix(self) -> &'static [u8] {
        match self {
            XmzNetwork::Mainnet => &[86],
            XmzNetwork::Testnet => &[111],
            XmzNetwork::Stagenet => &[131, 1],
        }
    }
}

const ALPHABET: &[u8; 58] = b"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
/// Characters for a block of 0..=8 bytes
const BLOCK_CHARS: [usize; 9] = [0, 2, 3, 5, 6, 7, 9, 10, 11];

/// Monero-style base58: 8-byte blocks, each written as a fixed number of characters.
fn base58(data: &[u8]) -> String {
    let mut out = Vec::with_capacity(data.len() / 8 * 11 + 11);
    for block in data.chunks(8) {
        let mut num = block.iter().fold(0u64, |n, &b| (n << 8) | u64::from(b));
        let mut chars = vec![b'1'; BLOCK_CHARS[block.len()]];
        for c in chars.iter_mut().rev() {
            *c = ALPHABET[(num % 58) as usize];
            num /= 58;
        }
        out.extend(chars);
    }
    String::from_utf8(out).expect("alphabet is ASCII")
}

/// A standard (primary) address: prefix, public spend key, public view key, checksum.
pub fn standard_address(network: XmzNetwork, spend_public: &[u8; 32], view_public: &[u8; 32]) -> String {
    let mut data = network.prefix().to_vec();
    data.extend_from_slice(spend_public);
    data.extend_from_slice(view_public);
    let check = keccak256(&data);
    data.extend_from_slice(&check[..4]);
    base58(&data)
}

/// The shared address both traders watch.
#[derive(Clone, PartialEq, Eq)]
pub struct SharedXmz {
    pub network: XmzNetwork,
    /// (a + b)·B
    pub spend_public: [u8; 32],
    /// Private view key (both traders know it)
    pub view_key: [u8; 32],
}

impl std::fmt::Debug for SharedXmz {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SharedXmz").field("network", &self.network).field("address", &self.address()).finish_non_exhaustive()
    }
}

impl SharedXmz {
    /// From our share and the other trader's public spend share and view share. The other
    /// trader's point must be a valid point of the prime-order subgroup.
    pub fn from_shares(
        network: XmzNetwork,
        mine: &SecretShare,
        their_spend_public: [u8; 32],
        my_view_share: [u8; 32],
        their_view_share: [u8; 32],
    ) -> Result<Self, DriverError> {
        let theirs = CompressedEdwardsY(their_spend_public)
            .decompress()
            .filter(|p| p.is_torsion_free() && !p.is_small_order())
            .ok_or_else(|| xmz_err("the other trader's spend share is not a valid point"))?;
        let ours = CompressedEdwardsY(mine.ed25519_public()).decompress().expect("our own public share");
        let scalar = |bytes: [u8; 32]| Option::<Scalar>::from(Scalar::from_canonical_bytes(bytes)).ok_or_else(|| xmz_err("view share is not a canonical scalar"));
        let view = scalar(my_view_share)? + scalar(their_view_share)?;
        if view == Scalar::ZERO {
            return Err(xmz_err("shared view key is zero"));
        }
        Ok(SharedXmz { network, spend_public: (ours + theirs).compress().to_bytes(), view_key: view.to_bytes() })
    }

    pub fn view_public(&self) -> [u8; 32] {
        let view = Scalar::from_canonical_bytes(self.view_key).expect("canonical by construction");
        (&view * ED25519_BASEPOINT_TABLE).compress().to_bytes()
    }

    pub fn address(&self) -> String {
        standard_address(self.network, &self.spend_public, &self.view_public())
    }

    /// The combined private spend key opens this address.
    pub fn opens(&self, spend_key: &[u8; 32]) -> bool {
        Option::<Scalar>::from(Scalar::from_canonical_bytes(*spend_key))
            .is_some_and(|k| (&k * ED25519_BASEPOINT_TABLE).compress().to_bytes() == self.spend_public)
    }
}

// ---------- wallet RPC client ----------

/// JSON-RPC client for one `monzero-wallet-rpc`, with its HTTP digest login.
#[derive(Clone)]
pub struct WalletRpc {
    url: String,
    login: Option<(String, String)>,
    http: reqwest::Client,
}

impl WalletRpc {
    /// `url` is the wallet RPC root, e.g. `http://127.0.0.1:18083`.
    pub fn new(url: &str, login: Option<(String, String)>) -> Self {
        WalletRpc { url: format!("{}/json_rpc", url.trim_end_matches('/')), login, http: reqwest::Client::new() }
    }

    /// Login from the `monzero-wallet-rpc.<port>.login` file the RPC writes at start.
    pub fn with_login_file(url: &str, file: &std::path::Path) -> Result<Self, DriverError> {
        let text = std::fs::read_to_string(file).map_err(xmz_err)?;
        let (user, pass) = text.trim().split_once(':').ok_or_else(|| xmz_err("malformed wallet RPC login file"))?;
        Ok(Self::new(url, Some((user.to_string(), pass.to_string()))))
    }

    pub async fn call(&self, method: &str, params: Value) -> Result<Value, DriverError> {
        let body = json!({"jsonrpc": "2.0", "id": "0", "method": method, "params": params}).to_string();
        let send = |auth: Option<String>| {
            let mut req = self.http.post(&self.url).header("Content-Type", "application/json").body(body.clone());
            if let Some(auth) = auth {
                req = req.header("Authorization", auth);
            }
            req.send()
        };
        let mut resp = send(None).await.map_err(xmz_err)?;
        // A 401 to a signed request means the server's nonce changed under us (it rotates
        // them); the request was not processed, so answering the new challenge is safe.
        for _ in 0..3 {
            if resp.status() != reqwest::StatusCode::UNAUTHORIZED {
                break;
            }
            let (user, pass) = self.login.as_ref().ok_or_else(|| xmz_err("wallet RPC asks for a login"))?;
            let challenges: Vec<String> =
                resp.headers().get_all("www-authenticate").iter().filter_map(|h| h.to_str().ok()).map(str::to_string).collect();
            let auth = digest_authorization(&challenges, user, pass, "/json_rpc")?;
            resp = send(Some(auth)).await.map_err(xmz_err)?;
        }
        if !resp.status().is_success() {
            return Err(xmz_err(format!("{method}: HTTP {}", resp.status())));
        }
        let bytes = resp.bytes().await.map_err(xmz_err)?;
        let mut reply: Value = serde_json::from_slice(&bytes).map_err(xmz_err)?;
        if let Some(error) = reply.get("error") {
            return Err(xmz_err(format!("{method}: {}", error.get("message").and_then(Value::as_str).unwrap_or("error"))));
        }
        Ok(reply["result"].take())
    }
}

/// HTTP digest (RFC 2617, MD5, qop=auth) as the wallet RPC's login expects.
fn digest_authorization(challenges: &[String], user: &str, pass: &str, uri: &str) -> Result<String, DriverError> {
    let parsed: Vec<Vec<(String, String)>> =
        challenges.iter().filter_map(|c| c.trim().strip_prefix("Digest ")).map(digest_params).collect();
    let get = |p: &[(String, String)], k: &str| p.iter().find(|(key, _)| key.eq_ignore_ascii_case(k)).map(|(_, v)| v.clone());
    let params = parsed
        .iter()
        .find(|p| get(p, "algorithm").is_none_or(|a| a.eq_ignore_ascii_case("MD5")))
        .ok_or_else(|| xmz_err("wallet RPC sent no MD5 digest challenge"))?;
    let realm = get(params, "realm").unwrap_or_default();
    let nonce = get(params, "nonce").ok_or_else(|| xmz_err("digest challenge without a nonce"))?;
    let hex_md5 = |s: String| hex::encode(Md5::digest(s.as_bytes()));
    let cnonce = hex_md5(format!("{nonce}{:?}", std::time::SystemTime::now()))[..16].to_string();
    let ha1 = hex_md5(format!("{user}:{realm}:{pass}"));
    let ha2 = hex_md5(format!("POST:{uri}"));
    let response = hex_md5(format!("{ha1}:{nonce}:00000001:{cnonce}:auth:{ha2}"));
    let mut header = format!(
        "Digest username=\"{user}\", realm=\"{realm}\", nonce=\"{nonce}\", uri=\"{uri}\", algorithm=MD5, \
         response=\"{response}\", qop=auth, nc=00000001, cnonce=\"{cnonce}\""
    );
    if let Some(opaque) = get(params, "opaque") {
        header.push_str(&format!(", opaque=\"{opaque}\""));
    }
    Ok(header)
}

/// `key="value", key=value` pairs (commas inside quotes kept).
fn digest_params(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut rest = text;
    while let Some(eq) = rest.find('=') {
        let key = rest[..eq].trim().trim_start_matches(',').trim().to_string();
        rest = &rest[eq + 1..];
        let value;
        if let Some(quoted) = rest.strip_prefix('"') {
            let end = quoted.find('"').unwrap_or(quoted.len());
            value = quoted[..end].to_string();
            rest = quoted.get(end + 1..).unwrap_or("");
        } else {
            let end = rest.find(',').unwrap_or(rest.len());
            value = rest[..end].trim().to_string();
            rest = &rest[end..];
        }
        out.push((key, value));
    }
    out
}

// ---------- the swap's XMZ side ----------

/// A built transaction: its hash and the metadata `relay_tx` takes.
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Retained {
    pub tx_hash: String,
    pub metadata: String,
}

impl std::fmt::Debug for Retained {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Retained({})", self.tx_hash)
    }
}

/// What the XMZ side keeps for one swap (JSON file, replaced atomically).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct XmzRecord {
    pub lock: Option<Retained>,
    /// The spending wallet of the shared address has been created
    pub spend_wallet: bool,
    pub sweep: Option<Vec<Retained>>,
    /// Times the shared-address wallets were rebuilt under a new file name (a power cut can
    /// leave a wallet file empty; these wallets hold nothing that is not made from our keys)
    #[serde(default)]
    pub view_generation: u32,
    #[serde(default)]
    pub spend_generation: u32,
}

pub struct WalletRpcConfig {
    pub shared: SharedXmz,
    /// Height to scan the shared address from (just below the chain height at agreement)
    pub restore_height: u64,
    /// Swap wallet file names are `<wallet_name>-view` and `<wallet_name>-spend`
    pub wallet_name: String,
    pub wallet_password: String,
    /// Confirmations before our sweep counts as done
    pub sweep_confirmations: u64,
    pub record: PathBuf,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Opened {
    View,
    Spend,
}

/// [`XmzSide`] over two wallet RPCs: `main` holds the trader's funds wallet (already open),
/// `swap` is dedicated to the shared address.
pub struct WalletRpcXmz {
    cfg: WalletRpcConfig,
    main: WalletRpc,
    swap: WalletRpc,
    opened: Mutex<Option<Opened>>,
}

impl WalletRpcXmz {
    pub fn new(cfg: WalletRpcConfig, main: WalletRpc, swap: WalletRpc) -> Self {
        WalletRpcXmz { cfg, main, swap, opened: Mutex::new(None) }
    }

    pub fn record(&self) -> Result<XmzRecord, DriverError> {
        match std::fs::read_to_string(&self.cfg.record) {
            Ok(text) => serde_json::from_str(&text).map_err(xmz_err),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(XmzRecord::default()),
            Err(e) => Err(xmz_err(e)),
        }
    }

    fn save(&self, record: &XmzRecord) -> Result<(), DriverError> {
        let text = serde_json::to_string_pretty(record).map_err(xmz_err)?;
        crate::driver::durable_write(&self.cfg.record, text.as_bytes()).map_err(xmz_err)
    }

    /// Open the shared wallet: the spending one once it exists, else the view-only one.
    /// `spend_key` is needed only the first time the spending wallet is made.
    async fn open(&self, want: Opened, spend_key: Option<&[u8; 32]>) -> Result<(), DriverError> {
        if *self.opened.lock().unwrap() == Some(want) {
            return Ok(());
        }
        let kind = if want == Opened::View { "view" } else { "spend" };
        let name = |generation: u32| match generation {
            0 => format!("{}-{kind}", self.cfg.wallet_name),
            g => format!("{}-{kind}-{g}", self.cfg.wallet_name),
        };
        let generation = |r: &XmzRecord| if want == Opened::View { r.view_generation } else { r.spend_generation };
        let filename = name(generation(&self.record()?));
        let _ = self.swap.call("close_wallet", json!({})).await;
        let opened = self.swap.call("open_wallet", json!({"filename": filename, "password": self.cfg.wallet_password})).await;
        if opened.is_err() {
            let mut params = json!({
                "filename": filename,
                "password": self.cfg.wallet_password,
                "address": self.cfg.shared.address(),
                "viewkey": hex::encode(self.cfg.shared.view_key),
                "restore_height": self.cfg.restore_height,
                "autosave_current": true,
            });
            if want == Opened::Spend {
                let key = spend_key.ok_or_else(|| xmz_err("the spending wallet does not exist yet"))?;
                params["spendkey"] = json!(hex::encode(key));
            }
            // generate_from_keys leaves the new wallet open
            if let Err(e) = self.swap.call("generate_from_keys", params.clone()).await {
                if !e.to_string().contains("already exists") {
                    return Err(e);
                }
                // The files are there but do not open: rebuild from our keys under the next name
                let mut record = self.record()?;
                let next = generation(&record) + 1;
                if want == Opened::View { record.view_generation = next } else { record.spend_generation = next }
                self.save(&record)?;
                params["filename"] = json!(name(next));
                self.swap.call("generate_from_keys", params).await?;
            }
        }
        *self.opened.lock().unwrap() = Some(want);
        Ok(())
    }

    async fn open_current(&self) -> Result<(), DriverError> {
        let want = if self.record()?.spend_wallet { Opened::Spend } else { Opened::View };
        self.open(want, None).await
    }

    /// Relay a kept transaction. Relaying again is fine; if the node refuses it, the
    /// transaction counts as relayed when the wallet already knows it.
    async fn relay(rpc: &WalletRpc, tx: &Retained) -> Result<(), DriverError> {
        match rpc.call("relay_tx", json!({"hex": tx.metadata})).await {
            Ok(_) => Ok(()),
            Err(e) => {
                // Already sent: the wallet knows it, possibly only after a refresh (a wallet
                // killed before saving reopens behind and refuses to relay its own spent inputs)
                if rpc.call("get_transfer_by_txid", json!({"txid": tx.tx_hash})).await.is_ok() {
                    return Ok(());
                }
                rpc.call("refresh", json!({})).await?;
                rpc.call("get_transfer_by_txid", json!({"txid": tx.tx_hash})).await.map(|_| ()).map_err(|_| e)
            }
        }
    }
}

impl XmzSide for WalletRpcXmz {
    async fn lock_status(&self) -> Result<Option<XmzLock>, DriverError> {
        self.open_current().await?;
        self.swap.call("refresh", json!({})).await?;
        let transfers = self.swap.call("get_transfers", json!({"in": true})).await?;
        let mut amount = 0u64;
        let mut confirmations = u64::MAX;
        for t in transfers["in"].as_array().into_iter().flatten() {
            // An output with an unlock time could not be swept in time: it does not count
            if t["unlock_time"].as_u64() != Some(0) || t["subaddr_index"]["minor"].as_u64() != Some(0) {
                continue;
            }
            amount = amount.checked_add(t["amount"].as_u64().ok_or_else(|| xmz_err("transfer without an amount"))?).ok_or_else(|| xmz_err("amount overflow"))?;
            confirmations = confirmations.min(t["confirmations"].as_u64().unwrap_or(0));
        }
        Ok((amount > 0).then_some(XmzLock { amount, confirmations }))
    }

    async fn ensure_lock(&self, amount: u64) -> Result<(), DriverError> {
        let mut record = self.record()?;
        let tx = match &record.lock {
            Some(tx) => tx.clone(),
            None => {
                self.main.call("refresh", json!({})).await?;
                let built = self
                    .main
                    .call(
                        "transfer",
                        json!({
                            "destinations": [{"amount": amount, "address": self.cfg.shared.address()}],
                            "do_not_relay": true,
                            "get_tx_metadata": true,
                        }),
                    )
                    .await?;
                let tx = Retained {
                    tx_hash: built["tx_hash"].as_str().ok_or_else(|| xmz_err("transfer: no tx_hash"))?.to_string(),
                    metadata: built["tx_metadata"].as_str().ok_or_else(|| xmz_err("transfer: no tx_metadata"))?.to_string(),
                };
                record.lock = Some(tx.clone());
                self.save(&record)?;
                tx
            }
        };
        Self::relay(&self.main, &tx).await
    }

    async fn ensure_sweep(&self, spend_key: [u8; 32]) -> Result<(), DriverError> {
        if !self.cfg.shared.opens(&spend_key) {
            return Err(xmz_err("combined spend key does not open the shared address"));
        }
        let mut record = self.record()?;
        self.open(Opened::Spend, Some(&spend_key)).await?;
        if !record.spend_wallet {
            record.spend_wallet = true;
            self.save(&record)?;
        }
        let txs = match &record.sweep {
            Some(txs) => txs.clone(),
            None => {
                self.swap.call("refresh", json!({})).await?;
                let balance = self.swap.call("get_balance", json!({})).await?;
                let (total, unlocked) = (balance["balance"].as_u64().unwrap_or(0), balance["unlocked_balance"].as_u64().unwrap_or(0));
                // Sweep everything at once, after it has all unlocked; until then, wait
                if unlocked == 0 || unlocked != total {
                    return Ok(());
                }
                let to = self.main.call("get_address", json!({})).await?;
                let to = to["address"].as_str().ok_or_else(|| xmz_err("get_address: no address"))?;
                let built = self
                    .swap
                    .call("sweep_all", json!({"address": to, "do_not_relay": true, "get_tx_metadata": true}))
                    .await?;
                let hashes = built["tx_hash_list"].as_array().ok_or_else(|| xmz_err("sweep_all: no tx_hash_list"))?;
                let metas = built["tx_metadata_list"].as_array().ok_or_else(|| xmz_err("sweep_all: no tx_metadata_list"))?;
                if hashes.is_empty() || hashes.len() != metas.len() {
                    return Err(xmz_err("sweep_all: unexpected transaction list"));
                }
                let txs: Vec<Retained> = hashes
                    .iter()
                    .zip(metas)
                    .map(|(h, m)| Retained { tx_hash: h.as_str().unwrap_or_default().to_string(), metadata: m.as_str().unwrap_or_default().to_string() })
                    .collect();
                record.sweep = Some(txs.clone());
                self.save(&record)?;
                txs
            }
        };
        for tx in &txs {
            Self::relay(&self.swap, tx).await?;
        }
        Ok(())
    }

    async fn swept(&self) -> Result<bool, DriverError> {
        let Some(txs) = self.record()?.sweep else {
            return Ok(false);
        };
        self.open(Opened::Spend, None).await?;
        self.swap.call("refresh", json!({})).await?;
        for tx in &txs {
            let found = self.swap.call("get_transfer_by_txid", json!({"txid": tx.tx_hash})).await?;
            if found["transfer"]["confirmations"].as_u64().unwrap_or(0) < self.cfg.sweep_confirmations {
                return Ok(false);
            }
        }
        Ok(true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const VECTORS: &str = include_str!("../tests/xmz_address_vectors.txt");

    fn basepoint() -> [u8; 32] {
        curve25519_dalek::constants::ED25519_BASEPOINT_POINT.compress().to_bytes()
    }

    #[test]
    fn standard_addresses_match_the_core_vectors() {
        // Lines: mainnet, testnet, stagenet x (standard, integrated, subaddress); keys = 1·B
        let lines: Vec<&str> = VECTORS.lines().collect();
        let b = basepoint();
        assert_eq!(standard_address(XmzNetwork::Mainnet, &b, &b), lines[0]);
        assert_eq!(standard_address(XmzNetwork::Testnet, &b, &b), lines[3]);
        assert_eq!(standard_address(XmzNetwork::Stagenet, &b, &b), lines[6]);
    }

    #[test]
    fn base58_block_sizes() {
        assert_eq!(base58(&[]), "");
        assert_eq!(base58(&[0]), "11");
        assert_eq!(base58(&[0xff; 8]), "jpXCZedGfVQ");
        assert_eq!(base58(&[0; 9]).len(), 13);
    }

    fn share(byte: u8) -> SecretShare {
        SecretShare::from_uniform_bytes(&[byte; 64]).unwrap()
    }

    #[test]
    fn both_traders_derive_the_same_shared_address() {
        let (a, b) = (share(1), share(2));
        let (va, vb) = (share(3).to_ed25519_le(), share(4).to_ed25519_le());
        let alice = SharedXmz::from_shares(XmzNetwork::Stagenet, &a, b.ed25519_public(), va, vb).unwrap();
        let bob = SharedXmz::from_shares(XmzNetwork::Stagenet, &b, a.ed25519_public(), vb, va).unwrap();
        assert_eq!(alice, bob);
        assert!(alice.address().starts_with('N'));
        assert!(alice.opens(&crate::secret::combined_spend_key(&a, &b)));
        assert!(!alice.opens(&a.to_ed25519_le()));
    }

    #[test]
    fn rejects_bad_points_and_view_shares() {
        let a = share(1);
        let v = share(3).to_ed25519_le();
        // identity point (small order) and a non-point
        let mut identity = [0u8; 32];
        identity[0] = 1;
        assert!(SharedXmz::from_shares(XmzNetwork::Mainnet, &a, identity, v, v).is_err());
        assert!(SharedXmz::from_shares(XmzNetwork::Mainnet, &a, [0xff; 32], v, v).is_err());
        // a non-canonical view share
        assert!(SharedXmz::from_shares(XmzNetwork::Mainnet, &a, share(2).ed25519_public(), [0xff; 32], v).is_err());
    }

    #[test]
    fn digest_header_answers_the_md5_challenge() {
        let challenges = vec![
            "Digest qop=\"auth\",algorithm=MD5-sess,realm=\"monero-rpc\",nonce=\"abc\",stale=false".to_string(),
            "Digest qop=\"auth\",algorithm=MD5,realm=\"monero-rpc\",nonce=\"n,1\",stale=false".to_string(),
        ];
        let h = digest_authorization(&challenges, "u", "p", "/json_rpc").unwrap();
        assert!(h.contains("nonce=\"n,1\""), "{h}");
        assert!(h.contains("realm=\"monero-rpc\""));
        assert!(h.contains("algorithm=MD5,"));
    }
}
