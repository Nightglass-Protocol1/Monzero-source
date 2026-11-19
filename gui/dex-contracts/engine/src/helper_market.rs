//! The helper's market commands: find, post, take and cancel public offers (see `market`).
//!
//! Each is one short request, as `tick` is: the app calls `serve` (maker) and `take` (taker)
//! again every few seconds until the keys are agreed. Once they are, the trade folder holds the
//! saved trade in `journal.json` and the usual `status` / `approve` / `tick` take over.
//!
//! The trade folder also keeps `listing.json` (maker: the signed offer, who took it, letters
//! answered) or `take.json` (taker: the offer and where its chain search starts).

use crate::chain::AlloyChain;
use crate::driver::{durable_write, Driver, FileJournal, Journal};
use crate::exchange::SavedTrade;
use crate::helper::{keys, rpc, status, xmz_parts, TickConfig};
use crate::listing::{Cancel, Listing, Rules, Side, SignedListing, CANCEL_SCHEMA, LISTING_SCHEMA, MAX_LIFETIME, MAX_LIFETIME_V2, PARTIAL_SCHEMA};
use crate::mail::{Envelope, Letter};
use crate::market::{Heights, Maker, Taker};
use crate::negotiation::Role;
use crate::relay_client::{pinned_rules, RelayClient};
use crate::swap_seed::SwapSeed;
use crate::xmz::XmzNetwork;
use crate::Chain;
use alloy::primitives::{keccak256, Address, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::signers::local::PrivateKeySigner;
use rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::BTreeSet;
use std::path::Path;

/// A taker sends its take again after this long without an answer
pub const RESEND_SECS: u64 = 300;

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct OfferParams {
    pub chain_id: u64,
    pub side: Side,
    /// Coin amount in wei, decimal
    pub value_wei: String,
    /// XMZ in atoms, decimal
    pub xmz_atoms: String,
    /// How long the offer stays open (at most 24 h)
    pub lifetime_secs: u64,
    /// A partial offer: the least XMZ (atoms, decimal) one take may be. Absent: whole offers only.
    #[serde(default)]
    pub min_xmz_atoms: Option<String>,
    /// Keep the offer open: shortly before it expires, `serve` posts it again at the same price
    /// for the same time (while the wallet runs and can still pay for it)
    #[serde(default)]
    pub renew: bool,
    /// What the offer trades: "USDT/XMZ" on Solana; absent, the chain's own coin
    #[serde(default)]
    pub pair: Option<String>,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct MakerFile {
    listing: SignedListing,
    taken_by: Option<Address>,
    /// keccak256 of every take letter already answered
    answered: BTreeSet<B256>,
    /// The relays were told the taken offer is closed (so it leaves the board before it expires)
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    closed: bool,
    /// Partial offers: the XMZ (atoms) the taker took
    #[serde(default, skip_serializing_if = "Option::is_none")]
    taken_xmz: Option<u64>,
    /// Partial offers: the folder of the offer for what was left (once it is written)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    remainder: Option<std::path::PathBuf>,
}

// Kept beside listing.json, so the offer file stays as older helpers read it:
// `withdrawn` holds why we took the open offer off the board ourselves (our side could no
// longer pay for the smallest take), `funds-checked` when we last saw that it still could.
fn withdrawn_reason(dir: &Path) -> Option<String> {
    std::fs::read_to_string(dir.join("withdrawn")).ok()
}

// `renew`: the owner asked to keep this offer open (copied to each renewal and to the rest of a
// partly taken offer). `renewed`: the folder of the offer that replaced this one.
fn renewed_into(dir: &Path) -> Option<std::path::PathBuf> {
    std::fs::read_to_string(dir.join("renewed")).ok().map(std::path::PathBuf::from)
}

/// How long before it expires a kept-open offer is posted again: a tenth of its life, from one
/// minute to ten
fn renew_margin(l: &Listing) -> u64 {
    (l.expires_at.saturating_sub(l.created_at) / 10).clamp(60, 600)
}

fn funds_checked_at(dir: &Path) -> u64 {
    std::fs::read_to_string(dir.join("funds-checked")).ok().and_then(|t| t.trim().parse().ok()).unwrap_or(0)
}

/// SOL a USDT buyer's trade account needs besides the USDT: the swap state and token vault
/// rent (returned when the swap closes), fees, and the claimer's fee advance
const SOL_TOKEN_OWNER_RESERVE: u64 = 13_000_000;

/// How often an open offer's funds are checked again while no take arrives
const FUNDS_RECHECK_SECS: u64 = 600;

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct TakerFile {
    listing: SignedListing,
    /// The chain search starts before our first take: the maker may lock once it answered
    evm_from_block: u64,
    sent_at: u64,
    /// Partial offers: the XMZ (atoms) we take. Absent: all of it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    xmz: Option<u64>,
}

fn rules(local_contract: Option<Address>) -> Rules {
    let mut r = pinned_rules();
    if let Some(c) = local_contract {
        r.contracts.insert(Chain::Local.id(), c);
    }
    r
}

fn read<T: serde::de::DeserializeOwned>(path: &Path) -> Result<Option<T>, String> {
    match std::fs::read(path) {
        Ok(b) => serde_json::from_slice(&b).map(Some).map_err(|e| format!("{}: {e}", path.display())),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(format!("{}: {e}", path.display())),
    }
}

fn write<T: Serialize>(path: &Path, v: &T) -> Result<(), String> {
    durable_write(path, &serde_json::to_vec_pretty(v).expect("serializes")).map_err(|e| e.to_string())
}

fn journal_has_trade(dir: &Path) -> bool {
    FileJournal(dir.join("journal.json")).load().is_ok_and(|r| r.trade.is_some())
}

fn chain_of_id(id: u64) -> Result<Chain, String> {
    Chain::from_id(id).ok_or_else(|| format!("unknown chain {id}"))
}

fn parse_amount(text: &str, what: &str) -> Result<U256, String> {
    let t = text.trim();
    if t.is_empty() || t.len() > 78 || !t.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("{what} must be a whole number"));
    }
    t.parse().map_err(|_| format!("{what} is out of range"))
}

/// The EVM clock and head, and the XMZ height: what a saved trade records
async fn heights(cfg: &TickConfig, chain: Chain, evm_from_block: Option<u64>) -> Result<Heights, String> {
    crate::helper::step("reading the coin's network");
    if chain.is_solana() {
        let (now, slot) = crate::sol::clock(&cfg.evm_rpc).await.map_err(|e| e.to_string())?;
        let main = rpc(&cfg.main_wallet)?;
        let xmz = main.call("get_height", json!({})).await.map_err(|e| e.to_string())?["height"].as_u64().ok_or("the XMZ wallet gave no height")?;
        return Ok(Heights { now, evm_from_block: evm_from_block.unwrap_or(slot), xmz_restore_height: xmz.saturating_sub(1) });
    }
    let provider = ProviderBuilder::new().connect_reqwest(crate::chain::http_client(), cfg.evm_rpc.parse().map_err(|e| format!("evmRpc: {e}"))?);
    let head = provider.get_block_number().await.map_err(|e| e.to_string())?;
    let block = provider
        .get_block_by_number(alloy::eips::BlockNumberOrTag::Number(head))
        .await
        .map_err(|e| e.to_string())?
        .ok_or("the EVM head block is not available")?;
    let main = rpc(&cfg.main_wallet)?;
    let xmz = main.call("get_height", json!({})).await.map_err(|e| e.to_string())?["height"].as_u64().ok_or("the XMZ wallet gave no height")?;
    Ok(Heights { now: block.header.timestamp, evm_from_block: evm_from_block.unwrap_or(head), xmz_restore_height: xmz.saturating_sub(1) })
}

/// Our side can pay for its part before we offer or take: the coin (owner) or XMZ (claimer)
/// for `xmz` atoms of the listing (all of it when we make the offer).
async fn check_funds(role: Role, listing: &Listing, xmz: u64, key: &PrivateKeySigner, cfg: &TickConfig) -> Result<(), String> {
    match funds_short(role, listing, xmz, key, cfg).await? {
        Some(why) => Err(why),
        None => Ok(()),
    }
}

/// Whether our side can pay for its part of `xmz` atoms of the listing: `Ok(None)` when it can,
/// `Ok(Some(why))` when it cannot, `Err` when the balance could not be read (unknown, not short).
async fn funds_short(role: Role, listing: &Listing, xmz: u64, key: &PrivateKeySigner, cfg: &TickConfig) -> Result<Option<String>, String> {
    let value = listing.fill_value(xmz);
    match role {
        Role::Owner if Chain::from_id(listing.chain_id).is_some_and(Chain::is_solana) && listing.asset() != Address::ZERO => {
            let kind = Chain::from_id(listing.chain_id).expect("checked");
            let account = crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes();
            let mint = crate::sol::mint_of(kind, listing.asset()).map_err(|e| e.to_string())?.ok_or("no token")?;
            let have = crate::sol::token_balance(&cfg.evm_rpc, &account, &mint).await.map_err(|e| e.to_string())?;
            // A relay that pays the fees takes its fee in USDT from the buyer: then no SOL is needed
            let sponsor = crate::sponsor::sol_lock_fee().await;
            let need = u64::try_from(value).map_err(|_| "the amount does not fit")? + sponsor.unwrap_or(0);
            let usdt = |units: u64| format!("{}.{:06}", units / 1_000_000, units % 1_000_000);
            if have < need {
                return Ok(Some(format!(
                    "the trade account {} holds {} USDT; this offer needs {}{}. Send at least {} USDT more to it (for example from Phantom), then try again",
                    crate::sol::to_base58(&account), usdt(have), usdt(need), if sponsor.is_some() { " (with the network fee, paid in USDT)" } else { "" }, usdt(need - have)
                )));
            }
            if sponsor.is_some() {
                return Ok(None);
            }
            // And SOL for the network fees and the swap's two accounts (returned when it closes)
            let sol = crate::sol::balance(&cfg.evm_rpc, &account).await.map_err(|e| e.to_string())?;
            let fees = SOL_TOKEN_OWNER_RESERVE;
            if sol < fees {
                return Ok(Some(format!(
                    "the trade account {} needs about {}.{:09} SOL for network fees and the swap's accounts (returned afterwards); send a little SOL to it too",
                    crate::sol::to_base58(&account), fees / 1_000_000_000, fees % 1_000_000_000
                )));
            }
        }
        Role::Owner if Chain::from_id(listing.chain_id).is_some_and(Chain::is_solana) => {
            let account = crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes();
            let have = crate::sol::balance(&cfg.evm_rpc, &account).await.map_err(|e| e.to_string())?;
            let need = u64::try_from(value).map_err(|_| "the amount does not fit Solana's lamports")?;
            if have < need {
                let sol = |lamports: u64| format!("{}.{:09}", lamports / 1_000_000_000, lamports % 1_000_000_000);
                return Ok(Some(format!(
                    "the trade account {} holds {} SOL; this offer needs {} plus network fees. Send at least {} more to it (for example from Phantom), then try again",
                    crate::sol::to_base58(&account), sol(have), sol(need), sol(need - have)
                )));
            }
        }
        // USDT (BEP-20) on an EVM chain: the tokens; the coin for gas is checked when the lock is due
        Role::Owner if listing.asset() != Address::ZERO => {
            let provider = ProviderBuilder::new().connect_reqwest(crate::chain::http_client(), cfg.evm_rpc.parse().map_err(|e| format!("evmRpc: {e}"))?);
            let have = crate::IErc20::new(listing.asset(), provider).balanceOf(key.address()).call().await.map_err(|e| e.to_string())?;
            if have < value {
                let coins = alloy::primitives::utils::format_ether; // BSC-USD has 18 decimals, as BNB
                return Ok(Some(format!(
                    "the trade account {} holds {} USDT; this offer needs {}. Send at least {} more USDT (BEP-20, on BNB Smart Chain) to it, plus a little BNB for fees, then try again",
                    key.address(), coins(have), coins(value), coins(value - have)
                )));
            }
        }
        Role::Owner => {
            let provider = ProviderBuilder::new().connect_reqwest(crate::chain::http_client(), cfg.evm_rpc.parse().map_err(|e| format!("evmRpc: {e}"))?);
            let have = provider.get_balance(key.address()).await.map_err(|e| e.to_string())?;
            if have < value {
                // In coins, not wei: this is what the person reads before sending from their wallet
                let coins = alloy::primitives::utils::format_ether;
                return Ok(Some(format!(
                    "the trade account {} holds {}; this offer needs {} plus network fees. Send at least {} more to it (for example from MetaMask), then try again",
                    key.address(), coins(have), coins(value), coins(value - have)
                )));
            }
        }
        Role::Claimer => {
            crate::helper::step("checking the XMZ trading wallet (it may still be syncing with the network)");
            let main = rpc(&cfg.main_wallet)?;
            main.call("refresh", json!({})).await.map_err(|e| e.to_string())?;
            let unlocked = main.call("get_balance", json!({})).await.map_err(|e| e.to_string())?["unlocked_balance"].as_u64().unwrap_or(0);
            let needed = xmz_needed(xmz);
            if unlocked < needed {
                return Ok(Some(format!("the XMZ wallet has {unlocked} atoms unlocked; this offer needs {needed} (the amount plus a margin for fees)")));
            }
        }
    }
    Ok(None)
}

fn evm_chain(cfg: &TickConfig, listing: &Listing, from_block: u64, key: &PrivateKeySigner) -> Result<AlloyChain, String> {
    let kind = Chain::from_id(listing.chain_id).ok_or("unknown chain")?;
    let mut c = AlloyChain::new(&cfg.evm_rpc, kind, listing.contract, from_block, key.clone()).map_err(|e| e.to_string())?;
    if let Some(n) = cfg.evm_log_chunk {
        c = c.with_log_chunk(n);
    }
    Ok(c)
}

/// Before offering or taking: the node is the listing's chain and the accounts can be paid (EVM:
/// no code at the address); on Solana, the pinned swap program is there.
async fn check_chain(cfg: &TickConfig, listing: &Listing, from_block: u64, key: &PrivateKeySigner, accounts: &[Address]) -> Result<(), String> {
    crate::helper::step("checking the coin's network");
    let kind = Chain::from_id(listing.chain_id).ok_or("unknown chain")?;
    if kind.is_solana() {
        let program = crate::sol::program_of(kind).ok_or("no swap program is pinned for this Solana cluster")?;
        return crate::sol::check_program(&cfg.evm_rpc, &program).await.map_err(|e| e.to_string());
    }
    let chain = evm_chain(cfg, listing, from_block, key)?;
    chain.check_network().await.map_err(|e| e.to_string())?;
    chain.check_plain_accounts(accounts).await.map_err(|e| e.to_string())
}

/// Writes the agreed trade to the journal, before anything is locked
fn start_trade(dir: &Path, saved: SavedTrade, seed: &SwapSeed, key: &PrivateKeySigner, cfg: &TickConfig) -> Result<(), String> {
    let agreed = saved.agreed(seed, key).map_err(|e| e.to_string())?;
    let chain = crate::helper::trade_chain(cfg, &saved, &agreed, key)?;
    let xmz = xmz_parts(dir, cfg, &saved, &agreed, seed)?;
    Driver::start(saved, seed, key, chain, xmz, FileJournal(dir.join("journal.json"))).map_err(|e| e.to_string())?;
    Ok(())
}

fn listing_json(s: &SignedListing, relays: &[String]) -> Value {
    json!({"signed": s, "relays": relays})
}

/// `partial`: also offers that may be partly taken (the caller can take part of an offer)
pub async fn listings(relays: &[String], pair: Option<&str>, partial: bool, local_contract: Option<Address>, now: u64) -> Result<Value, String> {
    let client = RelayClient::new(relays)?;
    let (found, errors) = client.listings(pair, partial, &rules(local_contract), now).await;
    Ok(json!({"listings": found.iter().map(|(s, r)| listing_json(s, r)).collect::<Vec<_>>(), "relayErrors": errors}))
}

pub async fn post(dir: &Path, cfg: &TickConfig, relays: &[String], params: &OfferParams, local_contract: Option<Address>) -> Result<Value, String> {
    if dir.join("listing.json").exists() || dir.join("take.json").exists() || journal_has_trade(dir) {
        return Err("this trade folder is already in use".into());
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = keys(cfg)?;
    let chain_kind = Chain::from_id(params.chain_id).ok_or("unknown chain")?;
    let rules = rules(local_contract);
    let contract = *rules.contracts.get(&params.chain_id).ok_or("no pinned contract for this chain")?;
    if params.lifetime_secs == 0 || params.lifetime_secs > MAX_LIFETIME_V2 {
        return Err("an offer stays open for at most 31 days".into());
    }
    let xmz_atoms: u64 = parse_amount(&params.xmz_atoms, "xmzAtoms")?.try_into().map_err(|_| "xmzAtoms is out of range")?;
    let min_xmz: Option<u64> = match &params.min_xmz_atoms {
        Some(m) => Some(parse_amount(m, "minXmzAtoms")?.try_into().map_err(|_| "minXmzAtoms is out of range")?),
        // Open longer than a day: a version-2 listing (only apps that accept the longer life see
        // it), taken whole: its minimum is all of it
        None if params.lifetime_secs > MAX_LIFETIME => Some(xmz_atoms),
        None => None,
    };
    let at = heights(cfg, chain_kind, None).await?;
    let mut id = [0u8; 32];
    OsRng.fill_bytes(&mut id);
    let id = B256::from(id);
    let listing = Listing {
        schema: if min_xmz.is_some() { PARTIAL_SCHEMA } else { LISTING_SCHEMA }.into(),
        min_xmz,
        id,
        pair: match &params.pair {
            Some(p) if chain_kind.asset_of(p).is_some() => p.clone(),
            Some(p) => return Err(format!("{p} is not traded on this chain")),
            None => chain_kind.pair().into(),
        },
        chain_id: params.chain_id,
        contract,
        side: params.side,
        value: parse_amount(&params.value_wei, "valueWei")?,
        xmz: xmz_atoms,
        maker: key.address(),
        mailbox: seed.mailbox(id, true).address(),
        xmz_network: match chain_kind {
            Chain::BscMainnet | Chain::EthMainnet | Chain::SolMainnet => XmzNetwork::Mainnet,
            _ => XmzNetwork::Testnet,
        },
        created_at: at.now,
        expires_at: at.now + params.lifetime_secs,
    };
    listing.check(&rules, at.now).map_err(|e| e.to_string())?;
    check_chain(cfg, &listing, at.evm_from_block, &key, &[key.address()]).await?;
    check_funds(listing.maker_role(), &listing, listing.xmz, &key, cfg).await?;
    let signed = listing.sign(&key);
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    if params.renew {
        std::fs::write(dir.join("renew"), b"").map_err(|e| e.to_string())?;
    }
    write(&dir.join("listing.json"), &MakerFile::open(signed.clone()))?;
    let posted = send_offer(&client, &signed, &key).await;
    mark_posted(dir, &posted);
    Ok(json!({"listing": signed, "posted": posted}))
}

impl MakerFile {
    fn open(listing: SignedListing) -> Self {
        MakerFile { listing, taken_by: None, answered: BTreeSet::new(), closed: false, taken_xmz: None, remainder: None }
    }
}

/// Sends a signed offer of ours to the relays; on Solana with our signed Solana trade account
/// (see `SolAccount`), which the relays want next to it
async fn send_offer(client: &RelayClient, signed: &SignedListing, key: &PrivateKeySigner) -> Vec<Value> {
    let account = Chain::from_id(signed.listing.chain_id).is_some_and(Chain::is_solana).then(|| {
        let account = B256::from(crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes());
        crate::listing::SolAccount { schema: crate::listing::SOL_ACCOUNT_SCHEMA.into(), id: signed.listing.id, account }.sign(key)
    });
    crate::helper::step("sending the offer to the relays");
    client.post_listing(signed, account.as_ref()).await
}

/// After part of a partial offer was taken: what is left becomes a new offer of ours, at the
/// same price, minimum and expiry, in a folder next to this one (the apps find it as they find
/// every offer of ours). Its id and folder follow from this offer's id, so it is made once however
/// often `serve` runs or stops part way; the new folder's own `serve` sends it again until a
/// relay has it. None when nothing is left or the offer has expired.
fn write_remainder(dir: &Path, file: &mut MakerFile, seed: &SwapSeed, key: &PrivateKeySigner, now: u64) -> Result<Option<(std::path::PathBuf, SignedListing)>, String> {
    let l = &file.listing.listing;
    let Some(taken) = file.taken_xmz.filter(|x| l.is_partial() && *x < l.xmz) else { return Ok(None) };
    let id = keccak256([b"monzero-dex-rest/1".as_slice(), l.id.as_slice()].concat());
    let name = format!("{}-rest-{}", dir.file_name().and_then(|n| n.to_str()).unwrap_or("offer"), hex::encode(&id[..4]));
    let path = dir.parent().ok_or("the offer's folder has no parent")?.join(name);
    if let Some(made) = read::<MakerFile>(&path.join("listing.json"))? {
        return Ok(Some((path, made.listing)));
    }
    if now >= l.expires_at {
        return Ok(None);
    }
    let Some(rest) = l.remainder(taken, id, seed.mailbox(id, true).address(), now) else { return Ok(None) };
    let signed = rest.sign(key);
    std::fs::create_dir_all(&path).map_err(|e| e.to_string())?;
    write(&path.join("listing.json"), &MakerFile::open(signed.clone()))?;
    if dir.join("renew").exists() {
        std::fs::write(path.join("renew"), b"").map_err(|e| e.to_string())?;
    }
    file.remainder = Some(path.clone());
    write(&dir.join("listing.json"), file)?;
    Ok(Some((path, signed)))
}

/// `posted` in the offer's folder once a relay has it. Until then `serve` sends the offer again,
/// so an offer whose first sending was cut short (no network, the helper stopped) still opens.
fn mark_posted(dir: &Path, posted: &[Value]) {
    if posted.iter().any(|p| p["ok"] == true) {
        let _ = std::fs::write(dir.join("posted"), b"");
    }
}

pub async fn serve(dir: &Path, cfg: &TickConfig, relays: &[String]) -> Result<Value, String> {
    let mut file: MakerFile = read(&dir.join("listing.json"))?.ok_or("this folder holds no offer of ours")?;
    if journal_has_trade(dir) && file.taken_by.is_some() {
        return taken(dir, &mut file, cfg, relays, None, 0).await;
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = keys(cfg)?;
    if key.address() != file.listing.listing.maker {
        return Err("these keys are not the offer's maker".into());
    }
    let id = file.listing.listing.id;
    if let Some(why) = withdrawn_reason(dir) {
        // Still answer late takes with a refusal, and tell the relays if they had not heard yet
        return withdraw_offer(dir, &mut file, &client, &seed, &key, why).await;
    }
    if let Some(next) = renewed_into(dir) {
        let mut v = retire(dir, &mut file, &client, &seed, &key, "the offer was renewed: take the new one").await?;
        v["renewed"] = json!({"folder": next});
        return Ok(v);
    }
    let role = file.listing.listing.maker_role();
    let smallest = file.listing.listing.min_xmz.unwrap_or(file.listing.listing.xmz);
    let mut renew_error: Option<String> = None;
    // Every so often while the offer waits: can we still pay for the smallest take? An offer
    // whose coins were spent or moved is taken off the board, not left for someone to take
    let clock = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    if file.taken_by.is_none() && clock >= funds_checked_at(dir) + FUNDS_RECHECK_SECS {
        crate::helper::step("checking that the offer can still be paid for");
        match funds_short(role, &file.listing.listing, smallest, &key, cfg).await {
            Ok(Some(why)) => return withdraw_offer(dir, &mut file, &client, &seed, &key, why).await,
            Ok(None) => {
                let _ = std::fs::write(dir.join("funds-checked"), clock.to_string());
            }
            Err(_) => {} // balance unreadable now: unknown, not short; checked again on a take
        }
    }
    if !dir.join("posted").exists() && file.taken_by.is_none() && !file.closed {
        mark_posted(dir, &send_offer(&client, &file.listing, &key).await);
    }
    crate::helper::step("reading the offer's mail on the relays");
    let mailbox = seed.mailbox(id, true);
    let mut maker = Maker::new(file.listing.clone(), file.taken_by, file.taken_xmz);
    let mut at: Option<Heights> = None;
    let mut answered = 0;
    for (_, sealed) in client.mail(mailbox.address()).await {
        let digest = keccak256(&sealed);
        if file.answered.contains(&digest) {
            continue;
        }
        let Some(Letter::Take(take)) = Letter::open(&mailbox, &sealed) else { continue };
        if at.is_none() {
            at = Some(heights(cfg, chain_of_id(file.listing.listing.chain_id)?, None).await?);
        }
        let heights = at.expect("set");
        // Before agreeing to a trade: our side must still be able to pay for this take. Short:
        // refuse and take the offer off the board. Unknown (node unreachable): leave the letter
        // unanswered, so it is answered on a later call, rather than start a trade we may not fill
        if file.taken_by.is_none() {
            match funds_short(role, &file.listing.listing, take.xmz.unwrap_or(file.listing.listing.xmz), &key, cfg).await {
                Ok(None) => {}
                Ok(Some(why)) => return withdraw_offer(dir, &mut file, &client, &seed, &key, why).await,
                Err(_) => continue,
            }
        }
        // A taker with code on chain could not receive the payout: refuse before answering
        let plain = check_chain(cfg, &file.listing.listing, heights.evm_from_block, &key, &[take.taker]).await.is_ok();
        let (answer, saved) = if plain {
            maker.answer(&take, &seed, &key, heights)
        } else {
            (crate::mail::Answer { listing: id, response: crate::exchange::KeyResponse::Refused("the taker's account has code on chain".into()) }, None)
        };
        if let Some(saved) = saved {
            if !journal_has_trade(dir) {
                start_trade(dir, saved, &seed, &key, cfg)?;
            }
            file.taken_by = maker.taken_by;
            file.taken_xmz = maker.taken_xmz;
        }
        file.answered.insert(digest);
        write(&dir.join("listing.json"), &file)?;
        if let Some(sealed) = Letter::Answer(answer).seal(take.reply_to, &mut OsRng) {
            client.send_mail(&Envelope { to: take.reply_to, sealed }).await;
            answered += 1;
        }
    }
    let now = match at {
        Some(h) => h.now,
        None => heights(cfg, chain_of_id(file.listing.listing.chain_id)?, None).await.map(|h| h.now).unwrap_or(0),
    };
    if file.taken_by.is_some() && journal_has_trade(dir) {
        return taken(dir, &mut file, cfg, relays, Some(now), answered).await;
    }
    // Takes are answered first: a renewal never refuses a buyer who came in its last minutes
    if dir.join("renew").exists() && file.taken_by.is_none() && clock + renew_margin(&file.listing.listing) >= file.listing.listing.expires_at {
        match renew(dir, &mut file, cfg, relays, &client, &seed, &key).await {
            Ok(v) => return Ok(v),
            // Not now (limits paused, a relay or node down): the offer stays as it is, tried again
            Err(e) => renew_error = Some(e),
        }
    }
    let mut v = json!({"taken": false, "answered": answered, "expired": now >= file.listing.listing.expires_at, "listing": file.listing});
    if dir.join("renew").exists() {
        v["renew"] = json!(true);
    }
    if let Some(e) = renew_error {
        v["renewError"] = json!(e);
    }
    Ok(v)
}

/// Post a kept-open offer again: same side, amounts, smallest part and life, as a new offer in
/// a sibling folder (with its own `renew`), then retire this one so it cannot be taken as well.
/// Idempotent: a renewal already made (the helper stopped half way) is used, not made twice.
async fn renew(dir: &Path, file: &mut MakerFile, cfg: &TickConfig, relays: &[String], client: &RelayClient, seed: &SwapSeed, key: &PrivateKeySigner) -> Result<Value, String> {
    let l = file.listing.listing.clone();
    let base = dir.file_name().and_then(|n| n.to_str()).unwrap_or("offer");
    let base = base.split("-renew-").next().unwrap_or(base);
    let next = dir.parent().ok_or("the offer's folder has no parent")?.join(format!("{base}-renew-{}", hex::encode(&l.id[..4])));
    if read::<MakerFile>(&next.join("listing.json"))?.is_none() {
        // Coins spent since: not renewed but withdrawn, as the funds re-check would
        if let Ok(Some(why)) = funds_short(l.maker_role(), &l, l.min_xmz.unwrap_or(l.xmz), key, cfg).await {
            return withdraw_offer(dir, file, client, seed, key, why).await;
        }
        let params = OfferParams {
            chain_id: l.chain_id,
            side: l.side,
            value_wei: l.value.to_string(),
            xmz_atoms: l.xmz.to_string(),
            lifetime_secs: l.expires_at.saturating_sub(l.created_at).clamp(1, MAX_LIFETIME_V2),
            min_xmz_atoms: if l.is_partial() { l.min_xmz.map(|m| m.to_string()) } else { None },
            renew: true,
            pair: Some(l.pair.clone()),
        };
        let local = (Chain::from_id(l.chain_id) == Some(Chain::Local)).then_some(l.contract);
        post(&next, cfg, relays, &params, local).await?;
    }
    std::fs::write(dir.join("renewed"), next.to_string_lossy().as_bytes()).map_err(|e| e.to_string())?;
    let mut v = retire(dir, file, client, seed, key, "the offer was renewed: take the new one").await?;
    v["renewed"] = json!({"folder": next, "listing": read::<MakerFile>(&next.join("listing.json"))?.map(|f| f.listing)});
    Ok(v)
}

/// Take a renewed offer off the board: a signed cancel (until a relay accepts it) and a refusal
/// to every take still waiting or arriving later
async fn retire(dir: &Path, file: &mut MakerFile, client: &RelayClient, seed: &SwapSeed, key: &PrivateKeySigner, why: &str) -> Result<Value, String> {
    refuse_waiting(file, client, seed, why).await;
    if !file.closed {
        cancel_on_relays(file, client, key).await;
    }
    write(&dir.join("listing.json"), file)?;
    Ok(json!({"taken": false, "expired": true, "listing": file.listing}))
}

async fn refuse_waiting(file: &mut MakerFile, client: &RelayClient, seed: &SwapSeed, why: &str) {
    let id = file.listing.listing.id;
    let mailbox = seed.mailbox(id, true);
    for (_, sealed) in client.mail(mailbox.address()).await {
        let digest = keccak256(&sealed);
        if file.answered.contains(&digest) {
            continue;
        }
        let Some(Letter::Take(take)) = Letter::open(&mailbox, &sealed) else { continue };
        let refusal = crate::mail::Answer { listing: id, response: crate::exchange::KeyResponse::Refused(why.into()) };
        if let Some(sealed) = Letter::Answer(refusal).seal(take.reply_to, &mut OsRng) {
            client.send_mail(&Envelope { to: take.reply_to, sealed }).await;
        }
        file.answered.insert(digest);
    }
}

async fn cancel_on_relays(file: &mut MakerFile, client: &RelayClient, key: &PrivateKeySigner) {
    let l = &file.listing.listing;
    let at = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(l.created_at);
    let signed = Cancel { schema: CANCEL_SCHEMA.into(), id: l.id, maker: key.address(), at }.sign(key);
    let told = client.cancel(&signed).await;
    let gone = |v: &Value| v["ok"] == true || v["error"].as_str().is_some_and(|e| e.contains("not found") || e.contains("404"));
    file.closed = told.iter().any(gone);
}

/// Take our open offer off the board because our side can no longer pay for it: a signed cancel
/// to the relays (tried again on later calls until one accepts it) and the reason kept with it.
async fn withdraw_offer(dir: &Path, file: &mut MakerFile, client: &RelayClient, seed: &SwapSeed, key: &PrivateKeySigner, why: String) -> Result<Value, String> {
    // Every take still waiting gets a refusal, so no taker waits for an offer that is gone
    refuse_waiting(file, client, seed, "the maker can no longer pay for this offer; it was withdrawn").await;
    if !file.closed {
        cancel_on_relays(file, client, key).await;
    }
    std::fs::write(dir.join("withdrawn"), &why).map_err(|e| e.to_string())?;
    write(&dir.join("listing.json"), file)?;
    Ok(withdrawn_status(file, &why))
}

fn withdrawn_status(file: &MakerFile, why: &str) -> Value {
    json!({"taken": false, "offerWithdrawn": true, "reason": format!("Offer withdrawn: {why}"), "expired": true, "listing": file.listing})
}

/// The answer for a taken offer: the trade's status, and for a partial offer the new offer for
/// what was left (`remainder`: its folder and listing, sent to the relays here the first time)
async fn taken(dir: &Path, file: &mut MakerFile, cfg: &TickConfig, relays: &[String], now: Option<u64>, answered: usize) -> Result<Value, String> {
    close_taken(dir, file, cfg, relays).await;
    let mut v = status(dir)?;
    v["taken"] = json!(true);
    v["takenBy"] = json!(file.taken_by);
    v["answered"] = json!(answered);
    if let Some(x) = file.taken_xmz {
        v["takenXmz"] = json!(x.to_string());
    }
    if file.listing.listing.is_partial() && file.taken_xmz.is_some_and(|x| x < file.listing.listing.xmz) {
        let fresh = file.remainder.is_none();
        let now = match now {
            Some(n) => n,
            None if fresh => heights(cfg, chain_of_id(file.listing.listing.chain_id)?, None).await?.now,
            None => u64::MAX,
        };
        let (seed, key) = keys(cfg)?;
        if let Some((path, signed)) = write_remainder(dir, file, &seed, &key, now)? {
            if fresh {
                let posted = send_offer(&RelayClient::new(relays)?, &signed, &key).await;
                mark_posted(&path, &posted);
            }
            v["remainder"] = json!({"folder": path, "listing": signed});
        }
    }
    Ok(v)
}

/// A taken offer is no longer on offer: the maker closes it on the relays with a signed cancel,
/// or it would stay on the board until it expires. The relays keep a closed offer on record, so
/// the trade's claim is still matched to it. Best effort; tried again on the next call.
async fn close_taken(dir: &Path, file: &mut MakerFile, cfg: &TickConfig, relays: &[String]) {
    if file.closed {
        return;
    }
    let (Ok(client), Ok((_, key))) = (RelayClient::new(relays), keys(cfg)) else { return };
    let l = &file.listing.listing;
    let at = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(l.created_at);
    let signed = Cancel { schema: CANCEL_SCHEMA.into(), id: l.id, maker: key.address(), at }.sign(&key);
    let told = client.cancel(&signed).await;
    // Gone already counts (another relay passed it on, or it expired there)
    let gone = |v: &Value| v["ok"] == true || v["error"].as_str().is_some_and(|e| e.contains("not found") || e.contains("404"));
    if !told.is_empty() && told.iter().any(gone) {
        file.closed = true;
        let _ = write(&dir.join("listing.json"), file);
    }
}

/// `xmz`: the XMZ (atoms) to take from a partial offer (first call only; None: all of it)
pub async fn take(dir: &Path, cfg: &TickConfig, relays: &[String], offered: Option<SignedListing>, xmz: Option<u64>, local_contract: Option<Address>) -> Result<Value, String> {
    if journal_has_trade(dir) {
        let mut v = status(dir)?;
        v["agreed"] = json!(true);
        return Ok(v);
    }
    // A take the user stopped waiting for, or one the maker refused, never starts again
    if dir.join("abandoned").exists() {
        return Ok(json!({"agreed": false, "stopped": true}));
    }
    if let Ok(why) = std::fs::read_to_string(dir.join("refused")) {
        return Ok(json!({"agreed": false, "refusedFinal": true, "refused": why.trim()}));
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = keys(cfg)?;
    let path = dir.join("take.json");
    let mut file: TakerFile = match read(&path)? {
        Some(f) => f,
        None => {
            let signed = offered.ok_or("take needs the offer the first time")?;
            let at = heights(cfg, chain_of_id(signed.listing.chain_id)?, None).await?;
            signed.verify(&rules(local_contract), at.now).map_err(|e| e.to_string())?;
            let l = &signed.listing;
            if l.maker == key.address() {
                return Err("this is our own offer".into());
            }
            let ours = match l.maker_role() {
                Role::Owner => Role::Claimer,
                Role::Claimer => Role::Owner,
            };
            if xmz.is_some_and(|x| x != l.xmz) && !l.is_partial() || !l.fill_ok(xmz.unwrap_or(l.xmz)) {
                return Err(match l.min_xmz {
                    None => "this offer can only be taken whole".into(),
                    Some(min) => format!("take at least {} XMZ and at most all {} XMZ, leaving none or at least {} XMZ", xmz_text(min), xmz_text(l.xmz), xmz_text(min)),
                });
            }
            check_chain(cfg, l, at.evm_from_block, &key, &[key.address(), l.maker]).await?;
            check_funds(ours, l, xmz.unwrap_or(l.xmz), &key, cfg).await?;
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
            let f = TakerFile { listing: signed, evm_from_block: at.evm_from_block, sent_at: 0, xmz };
            write(&path, &f)?;
            f
        }
    };
    let l = file.listing.listing.clone();
    let at = heights(cfg, chain_of_id(l.chain_id)?, Some(file.evm_from_block)).await?;
    let (mut taker, letter) = Taker::start(&l, file.xmz, &seed, &key, at.now).map_err(|e| e.to_string())?;
    // An answer first: it may already be waiting
    let mailbox = seed.mailbox(l.id, false);
    let mut refusal = None;
    for (_, sealed) in client.mail(mailbox.address()).await {
        let Some(Letter::Answer(answer)) = Letter::open(&mailbox, &sealed) else { continue };
        if answer.listing != l.id {
            continue;
        }
        // The maker refused (the offer was taken by someone else, withdrawn, expired...): nothing
        // was locked for a take, so it ends here instead of waiting until the offer's end time
        if let crate::exchange::KeyResponse::Refused(why) = &answer.response {
            let why: String = why.chars().filter(|c| !c.is_control()).take(200).collect();
            durable_write(&dir.join("refused"), why.as_bytes()).map_err(|e| e.to_string())?;
            return Ok(json!({"agreed": false, "refusedFinal": true, "refused": why}));
        }
        match taker.on_answer(&answer, at) {
            Ok(saved) => {
                start_trade(dir, saved, &seed, &key, cfg)?;
                let mut v = status(dir)?;
                v["agreed"] = json!(true);
                return Ok(v);
            }
            Err(e) => refusal = Some(e.to_string()),
        }
    }
    if at.now >= l.expires_at {
        return Ok(json!({"agreed": false, "expired": true, "refused": refusal}));
    }
    let mut sent = vec![];
    if at.now >= file.sent_at + RESEND_SECS {
        let sealed = Letter::Take(letter).seal(l.mailbox, &mut OsRng).ok_or("the offer's mailbox key cannot be used")?;
        sent = client.send_mail(&Envelope { to: l.mailbox, sealed }).await;
        file.sent_at = at.now;
        write(&path, &file)?;
    }
    Ok(json!({"agreed": false, "refused": refusal, "sent": sent}))
}

pub async fn cancel(dir: &Path, cfg: &TickConfig, relays: &[String]) -> Result<Value, String> {
    let file: MakerFile = read(&dir.join("listing.json"))?.ok_or("this folder holds no offer of ours")?;
    if file.taken_by.is_some() {
        return Err("the offer was taken: the trade runs to its end, or refunds".into());
    }
    let client = RelayClient::new(relays)?;
    let (_, key) = keys(cfg)?;
    let at = heights(cfg, chain_of_id(file.listing.listing.chain_id)?, None).await?;
    let signed = Cancel { schema: CANCEL_SCHEMA.into(), id: file.listing.listing.id, maker: key.address(), at: at.now }.sign(&key);
    Ok(json!({"cancelled": client.cancel(&signed).await}))
}

/// Why the trade account's coin must stay where it is, if anything in `root` (the folder of
/// all our offers and trades on one chain) still needs it: a trade that has not finished (its
/// lock, its claim or its refund pays from this account) or an offer of ours that is still open.
pub fn funds_in_use(root: &Path, now: u64) -> Result<Option<String>, String> {
    let entries = match std::fs::read_dir(root) {
        Ok(e) => e,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(format!("the trades folder could not be read: {e}")),
    };
    for entry in entries {
        let dir = entry.map_err(|e| e.to_string())?.path();
        if !dir.is_dir() {
            continue;
        }
        // A trade the user gave up before approving it: nothing of it was ever locked
        if dir.join("abandoned").exists() {
            continue;
        }
        let journal = dir.join("journal.json");
        if journal.exists() {
            // A journal we cannot read is treated as a running trade
            let record = FileJournal(journal).load().map_err(|e| format!("a trade's record could not be read: {e}"))?;
            if record.trade.is_some() {
                if !record.state.is_done() {
                    return Ok(Some("a trade is still running: its account needs its funds until it finishes or refunds".into()));
                }
                continue;
            }
        }
        if dir.join("cancelled").exists() || dir.join("withdrawn").exists() || dir.join("renewed").exists() {
            continue;
        }
        if let Ok(Some(file)) = read::<MakerFile>(&dir.join("listing.json")) {
            if file.taken_by.is_some() || file.listing.listing.expires_at > now {
                return Ok(Some("an offer of yours is still open: cancel it first".into()));
            }
        }
    }
    Ok(None)
}

/// XMZ atoms as text in whole XMZ ("12.5")
fn xmz_text(atoms: u64) -> String {
    let (whole, part) = (atoms / 100_000_000_000, atoms % 100_000_000_000);
    if part == 0 { whole.to_string() } else { format!("{whole}.{:011}", part).trim_end_matches('0').to_string() }
}

/// What a seller's trading wallet must hold unlocked for an offer: the amount, plus room for the
/// lock transaction's fee (about 0.01 XMZ): 1%, and never less than 1 XMZ.
pub fn xmz_needed(atoms: u64) -> u64 {
    atoms.saturating_add((atoms / 100).max(100_000_000_000))
}

/// Send what the trade account holds on this chain back to the user's own wallet (for example
/// MetaMask), when nothing of ours needs it: coin paid in for an offer that was cancelled, never
/// taken, or refused.
pub async fn withdraw(root: &Path, cfg: &TickConfig, chain_id: u64, to: &str, now: u64, keep: U256) -> Result<Value, String> {
    if let Some(why) = funds_in_use(root, now)? {
        return Err(why);
    }
    // On Solana the SOL and USDT markets share one trade account (folders <chain id> and the app's
    // USDT market <chain id + 100>): an open offer or trade in either keeps its funds
    if Chain::from_id(chain_id).is_some_and(Chain::is_solana) {
        let name = root.file_name().and_then(|n| n.to_str()).and_then(|n| n.parse::<u64>().ok());
        for sibling in [name.map(|n| n + 100), name.and_then(|n| n.checked_sub(100))].into_iter().flatten() {
            if let Some(why) = funds_in_use(&root.with_file_name(sibling.to_string()), now)? {
                return Err(why);
            }
        }
    }
    let (_, key) = keys(cfg)?;
    let kind = Chain::from_id(chain_id).ok_or("unknown chain")?;
    if kind.is_solana() {
        let to_key = crate::sol::from_base58(to).map_err(|e| e.to_string())?;
        let keep = u64::try_from(keep).map_err(|_| "keepWei is too large for Solana")?;
        let sent = crate::sol::pay_out_with(&cfg.evm_rpc, &crate::sol::account_key(&key.to_bytes().0), &to_key, keep, crate::sol::usdt_mint(kind)).await.map_err(|e| e.to_string())?;
        return Ok(match sent {
            Some((tx, amount)) => json!({"withdrawn": {"to": to.trim(), "tx": tx, "amountWei": amount.to_string()}}),
            None => json!({"withdrawn": null, "reason": "nothing to send, or a payment is still pending"}),
        });
    }
    let to = crate::helper::parse_address(to)?;
    let chain = AlloyChain::new(&cfg.evm_rpc, kind, Address::ZERO, 0, key).map_err(|e| e.to_string())?;
    Ok(match chain.pay_out_all(to, keep).await.map_err(|e| e.to_string())? {
        Some((tx, amount)) => json!({"withdrawn": {"to": to.to_string(), "tx": tx.to_string(), "amountWei": amount.to_string()}}),
        None => json!({"withdrawn": null, "reason": "nothing to send, or a payment is still pending"}),
    })
}

/// After a swap, the claimer tells the relays where its claim is, for the price chart
pub async fn report(dir: &Path, relays: &[String]) -> Result<Value, String> {
    let record = FileJournal(dir.join("journal.json")).load().map_err(|e| e.to_string())?;
    let saved = record.trade.ok_or("the journal holds no saved trade")?;
    if saved.role != Role::Claimer || record.state.stage() != "Done(Swapped)" {
        return Err("only a claimer's finished swap has a claim to report".into());
    }
    let claim = record.retained.get("claim").and_then(|v| v.last()).ok_or("no claim transaction in the journal")?;
    // The claim's id as its chain shows it: the relays read the claim there themselves
    let tx = if Chain::from_id(saved.offer.chain_id).is_some_and(Chain::is_solana) {
        crate::sol::transaction_id(&claim.raw).ok_or("the saved claim is not a Solana transaction")?
    } else {
        keccak256(&claim.raw).to_string()
    };
    let client = RelayClient::new(relays)?;
    Ok(json!({"tx": tx, "reported": client.report(saved.offer.chain_id, &tx).await}))
}


#[cfg(test)]
mod withdraw_tests {
    use super::*;
    use crate::listing::{Listing, Side, LISTING_SCHEMA, PARTIAL_SCHEMA};

    const NOW: u64 = 1_794_300_000;

    fn offer_folder(root: &Path, name: &str, expires_at: u64, taken: bool) -> std::path::PathBuf {
        let key = PrivateKeySigner::random();
        let listing = Listing {
            schema: LISTING_SCHEMA.into(),
            id: B256::repeat_byte(7),
            pair: "ETH/XMZ".into(),
            chain_id: 1,
            contract: Address::repeat_byte(1),
            side: Side::BuyXmz,
            value: U256::from(5_000_000_000_000_000u64),
            xmz: 300_000_000_000_000,
            maker: key.address(),
            mailbox: B256::repeat_byte(0x5a),
            xmz_network: XmzNetwork::Mainnet,
            created_at: NOW - 60,
            expires_at,
            min_xmz: None,
        }
        .sign(&key);
        let dir = root.join(name);
        std::fs::create_dir_all(&dir).unwrap();
        let file = MakerFile { taken_by: taken.then(|| Address::repeat_byte(9)), ..MakerFile::open(listing) };
        write(&dir.join("listing.json"), &file).unwrap();
        dir
    }

    fn root(name: &str) -> std::path::PathBuf {
        let dir = std::env::temp_dir().join(format!("monzero-withdraw-{name}-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn coin_stays_while_an_offer_is_open_and_is_free_once_it_is_cancelled_or_expired() {
        let r = root("offers");
        assert_eq!(funds_in_use(&r, NOW).unwrap(), None, "nothing of ours: free");
        assert_eq!(funds_in_use(&r.join("missing"), NOW).unwrap(), None, "no trades folder yet: free");
        let open = offer_folder(&r, "offer-open", NOW + 3600, false);
        assert!(funds_in_use(&r, NOW).unwrap().unwrap().contains("still open"));
        std::fs::write(open.join("cancelled"), b"").unwrap();
        assert_eq!(funds_in_use(&r, NOW).unwrap(), None, "cancelled: free");
        let gone = offer_folder(&r, "offer-withdrawn", NOW + 3600, false);
        assert!(funds_in_use(&r, NOW).unwrap().is_some());
        std::fs::write(gone.join("withdrawn"), b"the XMZ wallet has 0 atoms unlocked").unwrap();
        assert_eq!(funds_in_use(&r, NOW).unwrap(), None, "withdrawn (its funds were gone): free");
        offer_folder(&r, "offer-expired", NOW - 1, false);
        assert_eq!(funds_in_use(&r, NOW).unwrap(), None, "expired: free");
        // Taken but the trade is not written yet: the taker may be waiting for our lock
        offer_folder(&r, "offer-taken", NOW - 1, true);
        assert!(funds_in_use(&r, NOW).unwrap().is_some());
        std::fs::remove_dir_all(&r).unwrap();
    }

    #[test]
    fn the_rest_of_a_partly_taken_offer_is_one_new_offer_of_ours_however_often_it_is_made() {
        let r = root("rest");
        let seed = SwapSeed::from_bytes([3; 32]);
        let key = PrivateKeySigner::random();
        let xmz = 100_000_000_000u64;
        let listing = Listing {
            schema: PARTIAL_SCHEMA.into(),
            id: B256::repeat_byte(7),
            pair: "SOL/XMZ".into(),
            chain_id: 501000101,
            contract: Address::ZERO,
            side: Side::SellXmz,
            value: U256::from(10_000_000u64),
            xmz: 1_500 * xmz,
            maker: key.address(),
            mailbox: seed.mailbox(B256::repeat_byte(7), true).address(),
            xmz_network: XmzNetwork::Mainnet,
            created_at: NOW - 60,
            expires_at: NOW + 3600,
            min_xmz: Some(100 * xmz),
        }
        .sign(&key);
        let dir = r.join("offer-abc");
        std::fs::create_dir_all(&dir).unwrap();
        let mut file = MakerFile { taken_by: Some(Address::repeat_byte(9)), taken_xmz: Some(400 * xmz), ..MakerFile::open(listing.clone()) };
        write(&dir.join("listing.json"), &file).unwrap();
        let (path, rest) = write_remainder(&dir, &mut file, &seed, &key, NOW).unwrap().unwrap();
        assert_eq!(path.parent(), Some(r.as_path()), "next to the offer, where the apps look");
        let l = &rest.listing;
        assert_eq!((l.xmz, l.value), (1_100 * xmz, U256::from(10_000_000u64) - listing.listing.fill_value(400 * xmz)));
        assert_eq!((l.min_xmz, l.expires_at, l.side, l.created_at), (Some(100 * xmz), NOW + 3600, Side::SellXmz, NOW));
        assert_ne!(l.id, listing.listing.id);
        assert_eq!(l.mailbox, seed.mailbox(l.id, true).address(), "takes of the rest reach us");
        rest.verify(&{ let mut x = Rules::default(); x.contracts.insert(501000101, Address::ZERO); x }, NOW).unwrap();
        // Made again (serve runs again, or stopped part way before recording it): the same offer
        let mut again = MakerFile { remainder: None, ..read::<MakerFile>(&dir.join("listing.json")).unwrap().unwrap() };
        let (path2, rest2) = write_remainder(&dir, &mut again, &seed, &key, NOW + 100).unwrap().unwrap();
        assert_eq!((path2, rest2), (path.clone(), rest));
        assert_eq!(std::fs::read_dir(&r).unwrap().count(), 2, "one offer and its one rest");
        // The rest is an open offer of ours: the coin stays for it
        let rest_file: MakerFile = read(&path.join("listing.json")).unwrap().unwrap();
        assert!(rest_file.taken_by.is_none() && rest_file.remainder.is_none());
        // Nothing left, or expired before it was made: no new offer
        let mut all = MakerFile { taken_xmz: Some(1_500 * xmz), remainder: None, ..MakerFile::open(listing.clone()) };
        assert!(write_remainder(&r.join("offer-all"), &mut all, &seed, &key, NOW).unwrap().is_none());
        let mut late = MakerFile { taken_xmz: Some(400 * xmz), ..MakerFile::open(Listing { id: B256::repeat_byte(8), ..listing.listing.clone() }.sign(&key)) };
        assert!(write_remainder(&r.join("offer-late"), &mut late, &seed, &key, NOW + 3600).unwrap().is_none());
        std::fs::remove_dir_all(&r).unwrap();
    }

    #[test]
    fn a_trade_record_that_cannot_be_read_keeps_the_coin() {
        let r = root("journal");
        let dir = r.join("take-1");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("journal.json"), b"not a journal").unwrap();
        assert!(funds_in_use(&r, NOW).is_err());
        std::fs::remove_dir_all(&r).unwrap();
    }
}
