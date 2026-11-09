//! The app's DEX XMZ wallet: a wallet the app runs in its own wallet RPC service for trading, so
//! the user's main GUI wallet is never opened twice. The user funds it from the main wallet; XMZ a
//! trade receives lands in it; the user sends it back when done. (`xmzWallet` helper command.)
//!
//!  - `open`:   open the wallet `name` in the service, creating it on first use; its password is in
//!              `passwordFile` (made by the app, private). Answers with the status.
//!  - `status`: address and balances (atoms), and the wallet height.
//!  - `send`:   send `atoms` (or everything, when absent) to `to`, a standard XMZ address.
//!
//! The service must be one the app started for this wallet (its login file), never the GUI's own.

use crate::helper::{rpc, Endpoint};
use serde::Deserialize;
use serde_json::{json, Value};
use std::path::PathBuf;
use zeroize::Zeroizing;

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct XmzWalletRequest {
    pub wallet: Endpoint,
    pub action: String,
    pub name: String,
    pub password_file: PathBuf,
    #[serde(default)]
    pub to: Option<String>,
    /// Decimal atoms; absent for "everything"
    #[serde(default)]
    pub atoms: Option<String>,
}

fn check_name(name: &str) -> Result<(), String> {
    if name.is_empty() || name.len() > 64 || !name.chars().all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_') {
        return Err("the wallet name must be 1-64 letters, digits, - or _".into());
    }
    Ok(())
}

fn password(path: &PathBuf) -> Result<Zeroizing<String>, String> {
    let text = Zeroizing::new(std::fs::read_to_string(path).map_err(|e| format!("password file: {e}"))?);
    let p = Zeroizing::new(text.trim().to_string());
    if p.len() < 16 {
        return Err("the wallet password file is too short".into());
    }
    Ok(p)
}

async fn status(w: &crate::xmz::WalletRpc) -> Result<Value, String> {
    let _ = w.call("refresh", json!({})).await;
    // Coins a swap locked from this wallet (built first, relayed later) stay counted until the
    // wallet asks the node which outputs are spent
    let _ = w.call("rescan_spent", json!({})).await;
    let address = w.call("get_address", json!({"account_index": 0})).await.map_err(|e| e.to_string())?;
    let balance = w.call("get_balance", json!({"account_index": 0})).await.map_err(|e| e.to_string())?;
    let height = w.call("get_height", json!({})).await.map_err(|e| e.to_string())?;
    Ok(json!({
        "address": address["address"],
        "balanceAtoms": balance["balance"].as_u64().unwrap_or(0).to_string(),
        "unlockedAtoms": balance["unlocked_balance"].as_u64().unwrap_or(0).to_string(),
        "height": height["height"],
    }))
}

pub async fn run(r: &XmzWalletRequest) -> Result<Value, String> {
    check_name(&r.name)?;
    let w = rpc(&r.wallet)?;
    match r.action.as_str() {
        "open" => {
            let pass = password(&r.password_file)?;
            if w.call("open_wallet", json!({"filename": r.name, "password": pass.as_str()})).await.is_err() {
                // First use: the wallet does not exist yet
                w.call("create_wallet", json!({"filename": r.name, "password": pass.as_str(), "language": "English"}))
                    .await
                    .map_err(|e| format!("the DEX wallet could not be opened or created: {e}"))?;
            }
            status(&w).await
        }
        "status" => status(&w).await,
        "send" => {
            let to = r.to.as_deref().ok_or("send needs the destination address")?;
            if to.len() < 90 || to.len() > 110 || !to.chars().all(|c| c.is_ascii_alphanumeric()) {
                return Err("that is not an XMZ address".into());
            }
            let sent = match &r.atoms {
                None => w.call("sweep_all", json!({"address": to, "ring_size": 16})).await.map_err(|e| e.to_string())?,
                Some(a) => {
                    let atoms: u64 = a.parse().map_err(|_| "atoms must be a whole number".to_string())?;
                    if atoms == 0 {
                        return Err("nothing to send".into());
                    }
                    w.call("transfer", json!({"destinations": [{"amount": atoms, "address": to}], "ring_size": 16}))
                        .await
                        .map_err(|e| e.to_string())?
                }
            };
            let mut v = status(&w).await?;
            v["sent"] = json!({"txHashes": sent.get("tx_hash_list").cloned().unwrap_or_else(|| json!([sent["tx_hash"]]))});
            Ok(v)
        }
        _ => Err("unknown wallet action".into()),
    }
}
