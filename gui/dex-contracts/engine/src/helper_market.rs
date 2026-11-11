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
use crate::listing::{Cancel, Listing, Rules, Side, SignedListing, CANCEL_SCHEMA, LISTING_SCHEMA, MAX_LIFETIME};
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
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct TakerFile {
    listing: SignedListing,
    /// The chain search starts before our first take: the maker may lock once it answered
    evm_from_block: u64,
    sent_at: u64,
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
    if chain.is_solana() {
        let (now, slot) = crate::sol::clock(&cfg.evm_rpc).await.map_err(|e| e.to_string())?;
        let main = rpc(&cfg.main_wallet)?;
        let xmz = main.call("get_height", json!({})).await.map_err(|e| e.to_string())?["height"].as_u64().ok_or("the XMZ wallet gave no height")?;
        return Ok(Heights { now, evm_from_block: evm_from_block.unwrap_or(slot), xmz_restore_height: xmz.saturating_sub(1) });
    }
    let provider = ProviderBuilder::new().connect_http(cfg.evm_rpc.parse().map_err(|e| format!("evmRpc: {e}"))?);
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
async fn check_funds(role: Role, listing: &Listing, key: &PrivateKeySigner, cfg: &TickConfig) -> Result<(), String> {
    match role {
        Role::Owner if Chain::from_id(listing.chain_id).is_some_and(Chain::is_solana) => {
            let account = crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes();
            let have = crate::sol::balance(&cfg.evm_rpc, &account).await.map_err(|e| e.to_string())?;
            let need = u64::try_from(listing.value).map_err(|_| "the amount does not fit Solana's lamports")?;
            if have < need {
                let sol = |lamports: u64| format!("{}.{:09}", lamports / 1_000_000_000, lamports % 1_000_000_000);
                return Err(format!(
                    "the trade account {} holds {} SOL; this offer needs {} plus network fees. Send at least {} more to it (for example from Phantom), then try again",
                    crate::sol::to_base58(&account), sol(have), sol(need), sol(need - have)
                ));
            }
        }
        Role::Owner => {
            let provider = ProviderBuilder::new().connect_http(cfg.evm_rpc.parse().map_err(|e| format!("evmRpc: {e}"))?);
            let have = provider.get_balance(key.address()).await.map_err(|e| e.to_string())?;
            if have < listing.value {
                // In coins, not wei: this is what the person reads before sending from their wallet
                let coins = alloy::primitives::utils::format_ether;
                return Err(format!(
                    "the trade account {} holds {}; this offer needs {} plus network fees. Send at least {} more to it (for example from MetaMask), then try again",
                    key.address(), coins(have), coins(listing.value), coins(listing.value - have)
                ));
            }
        }
        Role::Claimer => {
            let main = rpc(&cfg.main_wallet)?;
            main.call("refresh", json!({})).await.map_err(|e| e.to_string())?;
            let unlocked = main.call("get_balance", json!({})).await.map_err(|e| e.to_string())?["unlocked_balance"].as_u64().unwrap_or(0);
            let needed = xmz_needed(listing.xmz);
            if unlocked < needed {
                return Err(format!("the XMZ wallet has {unlocked} atoms unlocked; this offer needs {needed} (the amount plus a margin for fees)"));
            }
        }
    }
    Ok(())
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

pub async fn listings(relays: &[String], pair: Option<&str>, local_contract: Option<Address>, now: u64) -> Result<Value, String> {
    let client = RelayClient::new(relays)?;
    let (found, errors) = client.listings(pair, &rules(local_contract), now).await;
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
    if params.lifetime_secs == 0 || params.lifetime_secs > MAX_LIFETIME {
        return Err("an offer stays open for at most 24 hours".into());
    }
    let xmz_atoms: u64 = parse_amount(&params.xmz_atoms, "xmzAtoms")?.try_into().map_err(|_| "xmzAtoms is out of range")?;
    let at = heights(cfg, chain_kind, None).await?;
    let mut id = [0u8; 32];
    OsRng.fill_bytes(&mut id);
    let id = B256::from(id);
    let listing = Listing {
        schema: LISTING_SCHEMA.into(),
        id,
        pair: chain_kind.pair().into(),
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
    check_funds(listing.maker_role(), &listing, &key, cfg).await?;
    let signed = listing.sign(&key);
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    write(&dir.join("listing.json"), &MakerFile { listing: signed.clone(), taken_by: None, answered: BTreeSet::new(), closed: false })?;
    // On Solana the relays also want the maker's Solana trade account, signed (see `SolAccount`)
    let account = chain_kind.is_solana().then(|| {
        let account = B256::from(crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes());
        crate::listing::SolAccount { schema: crate::listing::SOL_ACCOUNT_SCHEMA.into(), id: signed.listing.id, account }.sign(&key)
    });
    let posted = client.post_listing(&signed, account.as_ref()).await;
    Ok(json!({"listing": signed, "posted": posted}))
}

pub async fn serve(dir: &Path, cfg: &TickConfig, relays: &[String]) -> Result<Value, String> {
    let mut file: MakerFile = read(&dir.join("listing.json"))?.ok_or("this folder holds no offer of ours")?;
    if journal_has_trade(dir) && file.taken_by.is_some() {
        close_taken(dir, &mut file, cfg, relays).await;
        let mut v = status(dir)?;
        v["taken"] = json!(true);
        v["takenBy"] = json!(file.taken_by);
        return Ok(v);
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = keys(cfg)?;
    if key.address() != file.listing.listing.maker {
        return Err("these keys are not the offer's maker".into());
    }
    let id = file.listing.listing.id;
    let mailbox = seed.mailbox(id, true);
    let mut maker = Maker::new(file.listing.clone(), file.taken_by);
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
        close_taken(dir, &mut file, cfg, relays).await;
        let mut v = status(dir)?;
        v["taken"] = json!(true);
        v["takenBy"] = json!(file.taken_by);
        v["answered"] = json!(answered);
        return Ok(v);
    }
    Ok(json!({"taken": false, "answered": answered, "expired": now >= file.listing.listing.expires_at, "listing": file.listing}))
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

pub async fn take(dir: &Path, cfg: &TickConfig, relays: &[String], offered: Option<SignedListing>, local_contract: Option<Address>) -> Result<Value, String> {
    if journal_has_trade(dir) {
        let mut v = status(dir)?;
        v["agreed"] = json!(true);
        return Ok(v);
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
            check_chain(cfg, l, at.evm_from_block, &key, &[key.address(), l.maker]).await?;
            check_funds(ours, l, &key, cfg).await?;
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
            let f = TakerFile { listing: signed, evm_from_block: at.evm_from_block, sent_at: 0 };
            write(&path, &f)?;
            f
        }
    };
    let l = file.listing.listing.clone();
    let at = heights(cfg, chain_of_id(l.chain_id)?, Some(file.evm_from_block)).await?;
    let (mut taker, letter) = Taker::start(&l, &seed, &key, at.now).map_err(|e| e.to_string())?;
    // An answer first: it may already be waiting
    let mailbox = seed.mailbox(l.id, false);
    let mut refusal = None;
    for (_, sealed) in client.mail(mailbox.address()).await {
        let Some(Letter::Answer(answer)) = Letter::open(&mailbox, &sealed) else { continue };
        if answer.listing != l.id {
            continue;
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
        if dir.join("cancelled").exists() {
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

/// What a seller's trading wallet must hold unlocked for an offer: the amount, plus room for the
/// lock transaction's fee (about 0.01 XMZ): 1%, and never less than 1 XMZ.
pub fn xmz_needed(atoms: u64) -> u64 {
    atoms.saturating_add((atoms / 100).max(100_000_000_000))
}

/// Send what the trade account holds on this chain back to the user's own wallet (for example
/// MetaMask), when nothing of ours needs it: coin paid in for an offer that was cancelled, never
/// taken, or refused.
pub async fn withdraw(root: &Path, cfg: &TickConfig, chain_id: u64, to: &str, now: u64) -> Result<Value, String> {
    if let Some(why) = funds_in_use(root, now)? {
        return Err(why);
    }
    let (_, key) = keys(cfg)?;
    let kind = Chain::from_id(chain_id).ok_or("unknown chain")?;
    if kind.is_solana() {
        let to_key = crate::sol::from_base58(to).map_err(|e| e.to_string())?;
        let sent = crate::sol::pay_out(&cfg.evm_rpc, &crate::sol::account_key(&key.to_bytes().0), &to_key).await.map_err(|e| e.to_string())?;
        return Ok(match sent {
            Some((tx, amount)) => json!({"withdrawn": {"to": to.trim(), "tx": tx, "amountWei": amount.to_string()}}),
            None => json!({"withdrawn": null, "reason": "nothing to send, or a payment is still pending"}),
        });
    }
    let to = crate::helper::parse_address(to)?;
    let chain = AlloyChain::new(&cfg.evm_rpc, kind, Address::ZERO, 0, key).map_err(|e| e.to_string())?;
    Ok(match chain.pay_out(to).await.map_err(|e| e.to_string())? {
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
    use crate::listing::{Listing, Side, LISTING_SCHEMA};

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
        }
        .sign(&key);
        let dir = root.join(name);
        std::fs::create_dir_all(&dir).unwrap();
        let file = MakerFile { listing, taken_by: taken.then(|| Address::repeat_byte(9)), answered: BTreeSet::new(), closed: false };
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
        offer_folder(&r, "offer-expired", NOW - 1, false);
        assert_eq!(funds_in_use(&r, NOW).unwrap(), None, "expired: free");
        // Taken but the trade is not written yet: the taker may be waiting for our lock
        offer_folder(&r, "offer-taken", NOW - 1, true);
        assert!(funds_in_use(&r, NOW).unwrap().is_some());
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
