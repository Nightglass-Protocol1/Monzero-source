//! The user's trade history as CSV (for their records and taxes): one row per trade the wallet
//! agreed, from the trade folders it keeps (`evm-trades/<market>/<folder>`), read the same way as
//! the trade's own status. Nothing is fetched: everything is in the folders already.
use serde_json::{json, Value};
use std::path::{Path, PathBuf};

const HEADER: &str = "started_utc,finished_utc,pair,side,amount,amount_coin,total,total_coin,price,outcome,network,trade_id,coin_transactions,private_transactions";

/// Every trade under `root` (searched three folders deep; a trade folder holds `journal.json`),
/// oldest first, as CSV text with a header line
pub fn history(root: &Path) -> Result<Value, String> {
    if !root.is_dir() {
        return Ok(json!({"csv": format!("{HEADER}\n"), "rows": 0}));
    }
    let mut found = Vec::new();
    trade_folders(root, 0, &mut found);
    let mut rows: Vec<(u64, String)> = found.iter().filter_map(|dir| row(dir)).collect();
    rows.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)));
    let mut csv = format!("{HEADER}\n");
    for (_, line) in &rows {
        csv.push_str(line);
        csv.push('\n');
    }
    Ok(json!({"csv": csv, "rows": rows.len()}))
}

fn trade_folders(dir: &Path, depth: usize, out: &mut Vec<PathBuf>) {
    if dir.join("journal.json").is_file() {
        out.push(dir.to_path_buf());
        return;
    }
    if depth >= 3 {
        return;
    }
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    let mut subdirs: Vec<PathBuf> = entries.flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect();
    subdirs.sort();
    for d in subdirs {
        // A trade's own wallet folders hold no trades
        let name = d.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if matches!(name, "swap-service" | "wallets" | "main" | "swap") {
            continue;
        }
        trade_folders(&d, depth + 1, out);
    }
}

/// The signed listing's pair ("BNB/XMR") and when the trade began (the take was sent, or our offer made)
fn listing_info(dir: &Path) -> (String, u64) {
    for name in ["take.json", "listing.json"] {
        let Some(v) = std::fs::read_to_string(dir.join(name)).ok().and_then(|t| serde_json::from_str::<Value>(&t).ok()) else { continue };
        let l = &v["listing"]["listing"];
        let pair = l["pair"].as_str().unwrap_or("").to_string();
        let at = v["sentAt"].as_u64().or_else(|| l["createdAt"].as_u64()).unwrap_or(0);
        return (pair, at);
    }
    (String::new(), 0)
}

fn mtime(p: &Path) -> Option<u64> {
    std::fs::metadata(p).ok()?.modified().ok()?.duration_since(std::time::UNIX_EPOCH).ok().map(|d| d.as_secs())
}

/// The pair coin's decimal places and name: USDT on Solana 6, USDT on BNB Chain 18, SOL 9, ETH/BNB 18
fn coin_of(pair: &str) -> (u32, &'static str) {
    match pair.split('/').next().unwrap_or("") {
        "USDT" => (6, "USDT (Solana)"),
        "USDT-BSC" => (18, "USDT (BNB Chain)"),
        "SOL" => (9, "SOL"),
        "BNB" => (18, "BNB"),
        _ => (18, "ETH"),
    }
}

fn network(chain_id: u64) -> &'static str {
    match chain_id {
        1 => "Ethereum",
        56 => "BNB Smart Chain",
        11155111 => "Sepolia (test)",
        97 => "BNB Smart Chain Testnet",
        _ => match crate::Chain::from_id(chain_id) {
            Some(c) if c.is_solana() => "Solana",
            _ => "other",
        },
    }
}

fn row(dir: &Path) -> Option<(u64, String)> {
    let s = crate::helper::status(dir).ok()?;
    let o = &s["offer"];
    let (mut pair, began) = listing_info(dir);
    // An early trade folder without its listing: the pair from the trade's own chain and asset
    if pair.is_empty() {
        let chain = o["chainId"].as_u64().unwrap_or(0);
        let token = o["asset"].as_str().is_some_and(|a| a.trim_start_matches("0x").chars().any(|c| c != '0'));
        let solana = crate::Chain::from_id(chain).is_some_and(|c| c.is_solana());
        let coin = match (solana, token, chain) {
            (true, true, _) => "USDT",
            (true, false, _) => "SOL",
            (false, true, _) => "USDT-BSC",
            (false, false, 56 | 97) => "BNB",
            _ => "ETH",
        };
        pair = format!("{coin}/{}", o["privateCoin"].as_str().unwrap_or("XMZ"));
    }
    let (coin_places, coin) = coin_of(&pair);
    let private = o["privateCoin"].as_str().unwrap_or("XMZ").to_string();
    let private_places = o["privateDecimals"].as_u64().unwrap_or(11) as u32;
    let value = o["value"].as_str().unwrap_or("0");
    let atoms = o["xmzAmount"].as_str().unwrap_or("0");
    let (total, amount) = (units(value, coin_places), units(atoms, private_places));
    let price = match (total.parse::<f64>(), amount.parse::<f64>()) {
        (Ok(t), Ok(a)) if a > 0.0 => format!("{:.10}", t / a).trim_end_matches('0').trim_end_matches('.').to_string(),
        _ => String::new(),
    };
    // The owner pays the coin and receives the private coin; the claimer sells it
    let side = if s["role"] == "owner" { "buy" } else { "sell" };
    let stage = s["stage"].as_str().unwrap_or("");
    let outcome = if dir.join("abandoned").exists() {
        "cancelled before approval".to_string()
    } else if stage.contains("Swapped") {
        "swapped".to_string()
    } else if stage.contains("Refunded") {
        "refunded".to_string()
    } else if stage.starts_with("Done") {
        stage.to_lowercase()
    } else {
        format!("in progress ({stage})")
    };
    let finished = if s["done"].as_bool() == Some(true) {
        mtime(&dir.join("finished")).or_else(|| mtime(&dir.join("journal.json"))).map(utc).unwrap_or_default()
    } else {
        String::new()
    };
    let began = if began > 0 { began } else { mtime(&dir.join("journal.json")).unwrap_or(0) };
    let coin_txs: Vec<String> = s["evmTxs"]
        .as_object()
        .map(|m| m.iter().flat_map(|(k, v)| v.as_array().into_iter().flatten().filter_map(|x| x.as_str()).map(move |x| format!("{k}:{x}"))).collect())
        .unwrap_or_default();
    let mut private_txs: Vec<String> = s["xmzLock"].as_str().map(|t| vec![format!("lock:{t}")]).unwrap_or_default();
    private_txs.extend(s["xmzSweeps"].as_array().into_iter().flatten().filter_map(|x| x.as_str()).map(|x| format!("sweep:{x}")));
    let fields = [
        utc(began),
        finished,
        pair.clone(),
        side.to_string(),
        amount,
        private,
        total,
        coin.to_string(),
        price,
        outcome,
        network(o["chainId"].as_u64().unwrap_or(0)).to_string(),
        o["tradeId"].as_str().unwrap_or("").to_string(),
        coin_txs.join(" "),
        private_txs.join(" "),
    ];
    Some((began, fields.iter().map(|f| field(f)).collect::<Vec<_>>().join(",")))
}

/// A CSV field: quoted when it holds a comma, quote or line break
pub fn field(text: &str) -> String {
    if text.contains([',', '"', '\n', '\r']) {
        format!("\"{}\"", text.replace('"', "\"\""))
    } else {
        text.to_string()
    }
}

/// Whole atoms (decimal text) as plain decimals with `places` places, trailing zeros cut ("0.044")
pub fn units(atoms: &str, places: u32) -> String {
    let digits: String = atoms.chars().filter(|c| c.is_ascii_digit()).collect();
    let digits = digits.trim_start_matches('0');
    let p = places as usize;
    let padded = format!("{:0>width$}", digits, width = p + 1);
    let (whole, part) = padded.split_at(padded.len() - p);
    let part = part.trim_end_matches('0');
    if part.is_empty() { whole.to_string() } else { format!("{whole}.{part}") }
}

/// Unix seconds as "2026-11-22 21:27:05" (UTC)
pub fn utc(secs: u64) -> String {
    if secs == 0 {
        return String::new();
    }
    let days = (secs / 86400) as i64;
    let rem = secs % 86400;
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm)
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    format!("{y:04}-{m:02}-{d:02} {:02}:{:02}:{:02}", rem / 3600, rem % 3600 / 60, rem % 60)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn amounts_dates_and_fields() {
        assert_eq!(units("44000000000000000", 18), "0.044");
        assert_eq!(units("50000000000", 12), "0.05");
        assert_eq!(units("200000000000000", 11), "2000");
        assert_eq!(units("0", 9), "0");
        assert_eq!(units("1", 6), "0.000001");
        assert_eq!(utc(0), "");
        assert_eq!(utc(1795383600), "2026-11-22 21:40:00");
        assert_eq!(utc(951782400), "2000-02-29 00:00:00");
        assert_eq!(field("a,b"), "\"a,b\"");
        assert_eq!(field("say \"x\""), "\"say \"\"x\"\"\"");
        assert_eq!(field("plain"), "plain");
    }

    #[test]
    fn an_empty_or_missing_folder_is_a_header_only() {
        let dir = std::env::temp_dir().join(format!("history-test-{}", std::process::id()));
        let v = history(&dir.join("none")).unwrap();
        assert_eq!(v["rows"], 0);
        assert_eq!(v["csv"].as_str().unwrap().lines().count(), 1);
    }
}
