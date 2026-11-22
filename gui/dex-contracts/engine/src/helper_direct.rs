//! The helper's commands for direct (coin <-> coin) trades: ETH, BNB and SOL against each other.
//!
//! As with the XMZ pairs, each command is one short request the app repeats:
//!  - `directListings`: open direct offers from the relays, each checked;
//!  - `directPost` (maker): make, sign and post an offer;
//!  - `directServe` (maker, repeated): answer takes by relay mail; the first good take becomes
//!    the trade, and the offer is closed on the relays;
//!  - `directTake` (taker, repeated until agreed): send our frame, read the maker's answer;
//!  - `directStatus`, `directApprove`, `directTick`: the trade, as `status` / `approve` / `tick`;
//!  - `directPayout`: after the trade, send what it brought in to the user's own wallet;
//!  - `directCancel` (maker): withdraw an offer nobody took.
//!
//! The trade folder holds `direct-listing.json` (maker) or `direct-take.json` (taker), and once
//! agreed `direct-journal.json`. A trade needs a node for each of its two chains (`rpcs`).
//!
//! The maker's posting of an offer is its approval of the trade it becomes (an offer can be taken
//! at any time; a maker that then waited for a click would leave the taker's coin locked for
//! hours). The taker approves its trade in the app, from the terms `directStatus` shows.

use crate::chain::AlloyChain;
use crate::direct::{DirectSession, DirectTiming, SavedDirect, Side};
use crate::direct_driver::{approve_direct, DirectDriver, DirectError, DirectFileJournal, DirectJournal, DirectRecord, DirectRole};
use crate::direct_listing::{coin_decimals, coin_ticker, coins_pair, usdt_asset, Coin, DirectListing, DirectRules, SignedDirectListing, DIRECT_LISTING_SCHEMA, LOCK_WINDOW};
use crate::direct_machine::{FollowerState, LeaderState};
use crate::driver::{durable_write, DriverError};
use crate::exchange::KeyResponse;
use crate::helper::{keys_from, SwapKeys};
use crate::listing::{Cancel, CANCEL_SCHEMA, MAX_LIFETIME_V2};
use crate::mail::{DirectAnswer, DirectTake, Envelope, Letter};
use crate::relay_client::{pinned_contracts, RelayClient};
use crate::sol::AnyChain;
use crate::swap_seed::SwapSeed;
use crate::{AgreedTerms, Chain};
use alloy::primitives::{keccak256, Address, B256, U256};
use alloy::providers::{Provider, ProviderBuilder};
use alloy::signers::local::PrivateKeySigner;
use rand_core::{OsRng, RngCore};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;
use zeroize::Zeroizing;

/// A taker sends its take again after this long without an answer
pub const RESEND_SECS: u64 = 300;

/// What every direct command needs: the keys and a node for each chain it touches
#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct DirectConfig {
    /// Node (RPC URL) per chain id, e.g. {"1": "https://…", "56": "https://…"}
    pub rpcs: BTreeMap<String, String>,
    #[serde(default)]
    pub swap_keys: Option<SwapKeys>,
    #[serde(default)]
    pub wallet_seed_hex: Option<Zeroizing<String>>,
    #[serde(default)]
    pub account_index: Option<u32>,
    #[serde(default)]
    pub evm_log_chunk: Option<u64>,
}

impl DirectConfig {
    fn rpc(&self, chain: Chain) -> Result<&str, String> {
        self.rpcs.get(&chain.id().to_string()).map(String::as_str).ok_or_else(|| format!("no node given for {} (chain {})", chain.ticker(), chain.id()))
    }

    fn keys(&self) -> Result<(SwapSeed, PrivateKeySigner), String> {
        keys_from(&self.swap_keys, &self.wallet_seed_hex, self.account_index)
    }
}

/// `directPost`: the offer to make
#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct DirectOfferParams {
    /// What we give (lock): chain and amount in its base units (wei, lamports), decimal
    pub gives_chain_id: u64,
    pub gives_value: String,
    /// What we want in return
    pub wants_chain_id: u64,
    pub wants_value: String,
    /// "usdt": the chain's pinned USDT instead of its own coin (absent: the coin)
    #[serde(default)]
    pub gives_token: Option<String>,
    #[serde(default)]
    pub wants_token: Option<String>,
    /// How long the offer stays open (at most 31 days)
    pub lifetime_secs: u64,
}

/// What a request accepts besides the pinned contracts
#[derive(Clone, Default)]
pub struct DirectAccept {
    /// Real coins: off until direct pairs open
    pub mainnet: bool,
    /// Tests only: swap contracts on the local chain (31337)
    pub local_contracts: Vec<Address>,
}

pub fn rules(accept: &DirectAccept) -> DirectRules {
    DirectRules { contracts: pinned_contracts(), max_value: BTreeMap::new(), max_token_value: BTreeMap::new(), mainnet: accept.mainnet, local_contracts: accept.local_contracts.clone() }
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct MakerFile {
    listing: SignedDirectListing,
    taken_by: Option<Address>,
    /// keccak256 of every take letter already answered
    answered: BTreeSet<B256>,
    /// The relays were told the offer is closed
    closed: bool,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
struct TakerFile {
    listing: SignedDirectListing,
    /// Where both legs' searches start: read before our first take was sent
    from_block_a: u64,
    from_block_b: u64,
    /// When our take first went out (leg A's clock): the maker's deadline is no earlier than
    /// this plus `LOCK_WINDOW`, so ours is exactly that
    first_sent_at: u64,
    sent_at: u64,
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

fn journal(dir: &Path) -> DirectFileJournal {
    DirectFileJournal(dir.join("direct-journal.json"))
}

fn has_trade(dir: &Path) -> bool {
    journal(dir).load().is_ok_and(|r| r.trade.is_some())
}

fn parse_amount(text: &str, what: &str) -> Result<U256, String> {
    let t = text.trim();
    if t.is_empty() || t.len() > 78 || !t.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("{what} must be a whole number of base units"));
    }
    t.parse().map_err(|_| format!("{what} is out of range"))
}

fn chain_of(id: u64) -> Result<Chain, String> {
    Chain::from_id(id).ok_or_else(|| format!("unknown chain {id}"))
}

fn sol_account(key: &PrivateKeySigner) -> [u8; 32] {
    crate::sol::account_key(&key.to_bytes().0).verifying_key().to_bytes()
}

/// Why the trade account's coin on `chain_id` must stay where it is, if it must: a direct trade
/// on that chain still running, an open offer of ours that gives it, or a take of ours waiting
/// for its maker that would lock it. `root` holds the direct trade folders. The XMZ markets'
/// `withdraw` asks this too: both kinds of trade share one trade account per chain.
pub fn funds_in_use(root: &Path, chain_id: u64, now: u64) -> Result<Option<String>, String> {
    let entries = match std::fs::read_dir(root) {
        Ok(e) => e,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(e) => return Err(format!("the direct trades folder could not be read: {e}")),
    };
    for entry in entries {
        let dir = entry.map_err(|e| e.to_string())?.path();
        if !dir.is_dir() || dir.join("abandoned").exists() {
            continue;
        }
        if dir.join("direct-journal.json").exists() {
            // A journal we cannot read is treated as a running trade
            let record = journal(&dir).load().map_err(|e| format!("a direct trade's record could not be read: {e}"))?;
            if let Some(saved) = &record.trade {
                let on_chain = saved.offer.leg_a.chain_id == chain_id || saved.offer.leg_b.chain_id == chain_id;
                if on_chain && !record.state.is_done() {
                    return Ok(Some("a direct trade is still running: its account needs its funds until it finishes or refunds".into()));
                }
                continue;
            }
        }
        if let Some(file) = read::<MakerFile>(&dir.join("direct-listing.json"))? {
            let l = &file.listing.listing;
            if l.gives.chain_id == chain_id && !file.closed && (file.taken_by.is_some() || l.expires_at > now) {
                return Ok(Some("a direct offer of yours is still open: cancel it first".into()));
            }
        }
        if let Some(file) = read::<TakerFile>(&dir.join("direct-take.json"))? {
            let l = &file.listing.listing;
            if l.wants.chain_id == chain_id && !dir.join("refused").exists() && l.expires_at > now {
                return Ok(Some("a direct offer you took is waiting for its maker: its account needs its funds".into()));
            }
        }
    }
    Ok(None)
}

/// The chain's clock and head (EVM block; Solana finalized slot)
async fn clock(cfg: &DirectConfig, chain: Chain) -> Result<(u64, u64), String> {
    crate::helper::step("reading the coins' networks");
    let rpc = cfg.rpc(chain)?;
    if chain.is_solana() {
        return crate::sol::clock(rpc).await.map_err(|e| e.to_string());
    }
    let provider = ProviderBuilder::new().connect_reqwest(crate::chain::http_client(), rpc.parse().map_err(|e| format!("node for chain {}: {e}", chain.id()))?);
    let head = provider.get_block_number().await.map_err(|e| e.to_string())?;
    let block = provider.get_block_by_number(alloy::eips::BlockNumberOrTag::Number(head)).await.map_err(|e| e.to_string())?.ok_or("the head block is not available")?;
    Ok((block.header.timestamp, head))
}

/// What our trade account holds on `chain` of `asset` (zero: the chain's coin), in base units
async fn balance(cfg: &DirectConfig, chain: Chain, asset: Address, key: &PrivateKeySigner) -> Result<U256, String> {
    let rpc = cfg.rpc(chain)?;
    if chain.is_solana() {
        if asset != Address::ZERO {
            let mint = crate::sol::mint_of(chain, asset).map_err(|e| e.to_string())?.ok_or("unknown token")?;
            return crate::sol::token_balance(rpc, &sol_account(key), &mint).await.map(U256::from).map_err(|e| e.to_string());
        }
        return crate::sol::balance(rpc, &sol_account(key)).await.map(U256::from).map_err(|e| e.to_string());
    }
    if asset != Address::ZERO {
        let view = AlloyChain::new(rpc, chain, Address::ZERO, 0, key.clone()).map_err(|e| e.to_string())?;
        return view.token_balance(asset, key.address()).await.map_err(|e| e.to_string());
    }
    let provider = ProviderBuilder::new().connect_reqwest(crate::chain::http_client(), rpc.parse().map_err(|e| format!("node for chain {}: {e}", chain.id()))?);
    provider.get_balance(key.address()).await.map_err(|e| e.to_string())
}

/// Our trade account's address on `chain`, as its wallets show it
fn account_text(chain: Chain, key: &PrivateKeySigner) -> String {
    if chain.is_solana() { crate::sol::to_base58(&sol_account(key)) } else { key.address().to_string() }
}

fn amount_text(chain: Chain, v: U256) -> String {
    coin_text(chain, Address::ZERO, v)
}

/// An amount of a chain's coin or token as people read it ("1.5 ETH", "25 USDT")
fn coin_text(chain: Chain, asset: Address, v: U256) -> String {
    let places = coin_decimals(chain, asset) as usize;
    let ticker = match coin_ticker(chain, asset) {
        Some("USDT-BSC") | Some("USDT") => "USDT",
        Some(t) => t,
        None => "units",
    };
    let s = v.to_string();
    let s = format!("{s:0>width$}", width = places + 1);
    let (whole, part) = s.split_at(s.len() - places);
    let part = part.trim_end_matches('0');
    if part.is_empty() { format!("{whole} {ticker}") } else { format!("{whole}.{part} {ticker}") }
}

/// Can our account lock `lock` on its chain, and pay for a claim on the other (`claim_on`)? None
/// when it can; else what to send where. Errors: a balance could not be read.
async fn funds_short(cfg: &DirectConfig, key: &PrivateKeySigner, lock: &Coin, claim_on: Chain) -> Result<Option<String>, String> {
    let chain = lock.chain().map_err(|e| e.to_string())?;
    let have = balance(cfg, chain, lock.asset, key).await?;
    if have < lock.value {
        return Ok(Some(format!(
            "the trade account {} holds {}; this trade needs {} plus network fees. Send at least {} more to it, then try again",
            account_text(chain, key), coin_text(chain, lock.asset, have), coin_text(chain, lock.asset, lock.value), coin_text(chain, lock.asset, lock.value - have)
        )));
    }
    // A token lock is paid for in the chain's coin
    if lock.asset != Address::ZERO && balance(cfg, chain, Address::ZERO, key).await?.is_zero() {
        return Ok(Some(format!(
            "the trade account {} holds no {}: locking the token costs a small network fee. Send a little {} to it, then try again",
            account_text(chain, key), chain.ticker(), chain.ticker()
        )));
    }
    if balance(cfg, claim_on, Address::ZERO, key).await?.is_zero() {
        return Ok(Some(format!(
            "the trade account {} holds no {}: collecting your coins there costs a small network fee. Send a little {} to it, then try again",
            account_text(claim_on, key), claim_on.ticker(), claim_on.ticker()
        )));
    }
    Ok(None)
}

/// The node is the chain it claims to be and, on EVM chains, these accounts can receive coins
/// (no code); on Solana, the pinned program is there.
async fn check_chain(cfg: &DirectConfig, coin: &Coin, key: &PrivateKeySigner, accounts: &[Address]) -> Result<(), String> {
    crate::helper::step("checking the coins' networks");
    let chain = coin.chain().map_err(|e| e.to_string())?;
    let rpc = cfg.rpc(chain)?;
    if chain.is_solana() {
        let program = crate::sol::program_of(chain).ok_or("no swap program is pinned for this Solana cluster")?;
        if coin.contract != crate::sol::contract_address(&program) {
            return Err("the offer names another swap program".into());
        }
        return crate::sol::check_program(rpc, &program).await.map_err(|e| e.to_string());
    }
    let view = AlloyChain::new(rpc, chain, coin.contract, 0, key.clone()).map_err(|e| e.to_string())?;
    view.check_network().await.map_err(|e| e.to_string())?;
    view.check_plain_accounts(accounts).await.map_err(|e| e.to_string())
}

// ---------- offers ----------

pub async fn listings(relays: &[String], pair: Option<&str>, accept: &DirectAccept, now: u64) -> Result<Value, String> {
    let client = RelayClient::new(relays)?;
    let (found, errors) = client.direct_listings(pair, &rules(accept), now).await;
    let out: Vec<Value> = found.into_iter().map(|(signed, from)| json!({"signed": signed, "relays": from})).collect();
    Ok(json!({"listings": out, "relayErrors": errors}))
}

pub async fn post(dir: &Path, cfg: &DirectConfig, relays: &[String], params: &DirectOfferParams, accept: &DirectAccept, local: (Option<Address>, Option<Address>)) -> Result<Value, String> {
    if dir.join("direct-listing.json").exists() || dir.join("direct-take.json").exists() || has_trade(dir) {
        return Err("this trade folder is already in use".into());
    }
    if params.lifetime_secs == 0 || params.lifetime_secs > MAX_LIFETIME_V2 {
        return Err("an offer stays open for at most 31 days".into());
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = cfg.keys()?;
    let rules = rules(accept);
    let (give, want) = (chain_of(params.gives_chain_id)?, chain_of(params.wants_chain_id)?);
    let contract = |chain: Chain, local: Option<Address>| -> Result<Address, String> {
        match local {
            Some(c) if chain == Chain::Local => Ok(c),
            _ => rules.contracts.get(&chain.id()).copied().ok_or_else(|| format!("no pinned swap contract for chain {}", chain.id())),
        }
    };
    let asset = |chain: Chain, token: &Option<String>| -> Result<Address, String> {
        match token.as_deref() {
            None | Some("") => Ok(Address::ZERO),
            Some("usdt") => usdt_asset(chain).ok_or_else(|| format!("USDT is not traded on chain {}", chain.id())),
            Some(other) => Err(format!("unknown token {other}")),
        }
    };
    let gives = Coin { chain_id: give.id(), contract: contract(give, local.0)?, asset: asset(give, &params.gives_token)?, value: parse_amount(&params.gives_value, "givesValue")? };
    let wants = Coin { chain_id: want.id(), contract: contract(want, local.1)?, asset: asset(want, &params.wants_token)?, value: parse_amount(&params.wants_value, "wantsValue")? };
    let (now, _) = clock(cfg, give).await?;
    let mut id = [0u8; 32];
    OsRng.fill_bytes(&mut id);
    let id = B256::from(id);
    let listing = DirectListing {
        schema: DIRECT_LISTING_SCHEMA.into(),
        id,
        pair: coins_pair(&gives, &wants).map_err(|e| e.to_string())?,
        maker: key.address(),
        maker_sol: (give.is_solana() || want.is_solana()).then(|| B256::from(sol_account(&key))),
        mailbox: seed.mailbox(id, true).address(),
        gives,
        wants,
        created_at: now,
        expires_at: now + params.lifetime_secs,
    };
    listing.check(&rules, now).map_err(|e| e.to_string())?;
    check_chain(cfg, &listing.gives, &key, &[key.address()]).await?;
    check_chain(cfg, &listing.wants, &key, &[key.address()]).await?;
    if let Some(why) = funds_short(cfg, &key, &listing.gives, want).await? {
        return Err(why);
    }
    let signed = listing.sign(&key);
    std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    write(&dir.join("direct-listing.json"), &MakerFile { listing: signed.clone(), taken_by: None, answered: BTreeSet::new(), closed: false })?;
    crate::helper::step("sending the offer to the relays");
    let posted = client.post_direct_listing(&signed).await;
    if posted.iter().any(|p| p["ok"] == true) {
        let _ = std::fs::write(dir.join("posted"), b"");
    }
    Ok(json!({"listing": signed, "posted": posted}))
}

/// Our answer to one take: the agreed trade (written to the journal) and our frame, or a refusal
async fn answer(dir: &Path, file: &MakerFile, cfg: &DirectConfig, seed: &SwapSeed, key: &PrivateKeySigner, take: &DirectTake) -> Result<KeyResponse, String> {
    let l = &file.listing.listing;
    if let Some(by) = file.taken_by {
        // The taker of the trade asking again (our answer was lost): the same frame, remade
        return Ok(if by == take.taker {
            KeyResponse::Frame(seed.direct_frame(&l.offer(by), Side::Follower, key).map_err(|e| e.to_string())?)
        } else {
            KeyResponse::Refused("the offer was taken".into())
        });
    }
    if take.taker == l.maker {
        return Ok(KeyResponse::Refused("a maker cannot take its own offer".into()));
    }
    let (want, give) = (l.wants.chain().map_err(|e| e.to_string())?, l.gives.chain().map_err(|e| e.to_string())?);
    // The taker's EVM account must be able to receive what it claims (no code at the address)
    for coin in [&l.gives, &l.wants] {
        if !coin.chain().is_ok_and(Chain::is_solana) && check_chain(cfg, coin, key, &[take.taker]).await.is_err() {
            return Ok(KeyResponse::Refused("the taker's account cannot receive coins on this chain".into()));
        }
    }
    let (now_a, head_a) = clock(cfg, want).await?;
    let (_, head_b) = clock(cfg, give).await?;
    let mut session = match DirectSession::start(seed, l.offer(take.taker), Side::Follower, key, now_a) {
        Ok(s) => s,
        Err(e) => return Ok(KeyResponse::Refused(e.to_string())),
    };
    if let Err(e) = session.accept_theirs(&take.frame, now_a) {
        return Ok(KeyResponse::Refused(e.to_string()));
    }
    let saved = session.saved(DirectTiming::DEFAULT, now_a + LOCK_WINDOW, head_a, head_b).ok_or("the exchange did not complete")?;
    let transcript = saved.transcript;
    let mut record = DirectRecord::new(DirectRole::Follower(FollowerState::WaitingForA));
    record.trade = Some(saved);
    // Posting the offer was the maker's approval
    record.approved = Some(transcript);
    journal(dir).save(&record).map_err(|e| e.to_string())?;
    Ok(KeyResponse::Frame(session.frame().clone()))
}

pub async fn serve(dir: &Path, cfg: &DirectConfig, relays: &[String]) -> Result<Value, String> {
    let mut file: MakerFile = read(&dir.join("direct-listing.json"))?.ok_or("this folder holds no direct offer of ours")?;
    let client = RelayClient::new(relays)?;
    let (seed, key) = cfg.keys()?;
    let l = file.listing.listing.clone();
    if key.address() != l.maker {
        return Err("these keys are not the offer's maker".into());
    }
    if file.taken_by.is_none() && !dir.join("posted").exists() && !file.closed {
        if client.post_direct_listing(&file.listing).await.iter().any(|p| p["ok"] == true) {
            let _ = std::fs::write(dir.join("posted"), b"");
        }
    }
    crate::helper::step("reading the offer's mail on the relays");
    let mailbox = seed.mailbox(l.id, true);
    let mut answered = 0;
    for (_, sealed) in client.mail(mailbox.address()).await {
        let digest = keccak256(&sealed);
        if file.answered.contains(&digest) {
            continue;
        }
        let Some(Letter::DirectTake(take)) = Letter::open(&mailbox, &sealed) else { continue };
        if take.listing != l.id {
            continue;
        }
        // Before agreeing: we must still be able to pay for our side. Short: refuse (the offer
        // stays as it is; the app shows why). Unknown (a node down): leave the letter for later.
        let response = if file.taken_by.is_none() {
            match funds_short(cfg, &key, &l.gives, l.wants.chain().map_err(|e| e.to_string())?).await {
                Ok(Some(why)) => KeyResponse::Refused(format!("the maker cannot pay for this offer now: {why}")),
                Ok(None) => answer(dir, &file, cfg, &seed, &key, &take).await?,
                Err(_) => continue,
            }
        } else {
            answer(dir, &file, cfg, &seed, &key, &take).await?
        };
        if matches!(response, KeyResponse::Frame(_)) && file.taken_by.is_none() {
            file.taken_by = Some(take.taker);
        }
        file.answered.insert(digest);
        write(&dir.join("direct-listing.json"), &file)?;
        if let Some(sealed) = Letter::DirectAnswer(DirectAnswer { listing: l.id, response }).seal(take.reply_to, &mut OsRng) {
            client.send_mail(&Envelope { to: take.reply_to, sealed }).await;
            answered += 1;
        }
    }
    if file.taken_by.is_some() && !file.closed {
        file.closed = close(&client, &l, &key).await;
        write(&dir.join("direct-listing.json"), &file)?;
    }
    if file.taken_by.is_some() && has_trade(dir) {
        let mut v = status(dir, Some(&key))?;
        v["taken"] = json!(true);
        v["takenBy"] = json!(file.taken_by);
        v["answered"] = json!(answered);
        return Ok(v);
    }
    let (now, _) = clock(cfg, l.gives.chain().map_err(|e| e.to_string())?).await.unwrap_or((0, 0));
    Ok(json!({"taken": false, "answered": answered, "expired": now >= l.expires_at, "listing": file.listing}))
}

/// A signed cancel to every relay; true once one has it (or no longer has the offer)
async fn close(client: &RelayClient, l: &DirectListing, key: &PrivateKeySigner) -> bool {
    let at = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(l.created_at);
    let signed = Cancel { schema: CANCEL_SCHEMA.into(), id: l.id, maker: key.address(), at }.sign(key);
    let told = client.direct_cancel(&signed).await;
    told.iter().any(|v| v["ok"] == true || v["error"].as_str().is_some_and(|e| e.contains("no such offer") || e.contains("404")))
}

pub async fn cancel(dir: &Path, cfg: &DirectConfig, relays: &[String]) -> Result<Value, String> {
    let mut file: MakerFile = read(&dir.join("direct-listing.json"))?.ok_or("this folder holds no direct offer of ours")?;
    if file.taken_by.is_some() {
        return Err("the offer was taken: the trade runs to its end, or refunds".into());
    }
    let client = RelayClient::new(relays)?;
    let (_, key) = cfg.keys()?;
    file.closed = close(&client, &file.listing.listing, &key).await;
    std::fs::write(dir.join("withdrawn"), b"cancelled by the maker").map_err(|e| e.to_string())?;
    write(&dir.join("direct-listing.json"), &file)?;
    Ok(json!({"cancelled": file.closed}))
}

pub async fn take(dir: &Path, cfg: &DirectConfig, relays: &[String], offered: Option<SignedDirectListing>, accept: &DirectAccept) -> Result<Value, String> {
    if has_trade(dir) {
        let mut v = status(dir, None)?;
        v["agreed"] = json!(true);
        return Ok(v);
    }
    if let Ok(why) = std::fs::read_to_string(dir.join("refused")) {
        return Ok(json!({"agreed": false, "refusedFinal": true, "refused": why.trim()}));
    }
    let client = RelayClient::new(relays)?;
    let (seed, key) = cfg.keys()?;
    let path = dir.join("direct-take.json");
    let mut file: TakerFile = match read(&path)? {
        Some(f) => f,
        None => {
            let signed = offered.ok_or("directTake needs the offer the first time")?;
            let l = &signed.listing;
            let (want, give) = (l.wants.chain().map_err(|e| e.to_string())?, l.gives.chain().map_err(|e| e.to_string())?);
            let (now_a, head_a) = clock(cfg, want).await?;
            let (_, head_b) = clock(cfg, give).await?;
            signed.verify(&rules(accept), now_a).map_err(|e| e.to_string())?;
            if l.maker == key.address() {
                return Err("this is our own offer".into());
            }
            // We lead: we lock what the maker wants, and claim what it gives
            check_chain(cfg, &l.wants, &key, &[key.address(), l.maker]).await?;
            check_chain(cfg, &l.gives, &key, &[key.address(), l.maker]).await?;
            if let Some(why) = funds_short(cfg, &key, &l.wants, give).await? {
                return Err(why);
            }
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
            let f = TakerFile { listing: signed, from_block_a: head_a, from_block_b: head_b, first_sent_at: now_a, sent_at: 0 };
            write(&path, &f)?;
            f
        }
    };
    let l = file.listing.listing.clone();
    let offer = l.offer(key.address());
    let want = l.wants.chain().map_err(|e| e.to_string())?;
    let (now_a, _) = clock(cfg, want).await?;
    let mailbox = seed.mailbox(l.id, false);
    let mut refusal = None;
    for (_, sealed) in client.mail(mailbox.address()).await {
        let Some(Letter::DirectAnswer(a)) = Letter::open(&mailbox, &sealed) else { continue };
        if a.listing != l.id {
            continue;
        }
        match a.response {
            KeyResponse::Refused(why) => {
                let why: String = why.chars().filter(|c| !c.is_control()).take(300).collect();
                durable_write(&dir.join("refused"), why.as_bytes()).map_err(|e| e.to_string())?;
                return Ok(json!({"agreed": false, "refusedFinal": true, "refused": why}));
            }
            KeyResponse::Frame(frame) => {
                // The expiry only bounds when keys can be agreed: an answer to a take sent in time
                // still counts, so the session is checked as of when we first sent it
                let at = file.first_sent_at.min(now_a);
                let mut session = DirectSession::start(&seed, offer.clone(), Side::Leader, &key, at).map_err(|e| e.to_string())?;
                match session.accept_theirs(&frame, at) {
                    Ok(_) => {
                        let saved = session.saved(DirectTiming::DEFAULT, file.first_sent_at + LOCK_WINDOW, file.from_block_a, file.from_block_b).ok_or("the exchange did not complete")?;
                        let mut record = DirectRecord::new(DirectRole::Leader(LeaderState::Start));
                        record.trade = Some(saved);
                        journal(dir).save(&record).map_err(|e| e.to_string())?;
                        let mut v = status(dir, Some(&key))?;
                        v["agreed"] = json!(true);
                        return Ok(v);
                    }
                    Err(e) => refusal = Some(e.to_string()),
                }
            }
        }
    }
    if now_a >= l.expires_at && file.sent_at == 0 {
        return Ok(json!({"agreed": false, "expired": true}));
    }
    // Past the window the maker would wait for our lock: a late answer could not be traded on
    if now_a >= file.first_sent_at + LOCK_WINDOW - crate::direct_listing::LEADER_LOCK_BUDGET {
        durable_write(&dir.join("refused"), b"the maker did not answer in time").map_err(|e| e.to_string())?;
        return Ok(json!({"agreed": false, "refusedFinal": true, "refused": "the maker did not answer in time"}));
    }
    let mut sent = vec![];
    if now_a >= file.sent_at + RESEND_SECS {
        let frame = seed.direct_frame(&offer, Side::Leader, &key).map_err(|e| e.to_string())?;
        let letter = Letter::DirectTake(DirectTake { listing: l.id, taker: key.address(), reply_to: mailbox.address(), frame });
        let sealed = letter.seal(l.mailbox, &mut OsRng).ok_or("the offer's mailbox key cannot be used")?;
        sent = client.send_mail(&Envelope { to: l.mailbox, sealed }).await;
        if file.sent_at == 0 {
            file.first_sent_at = now_a;
        }
        file.sent_at = now_a;
        write(&path, &file)?;
    }
    Ok(json!({"agreed": false, "refused": refusal, "sent": sent}))
}

// ---------- the trade ----------

fn tx_ids(chain: Chain, signed: &[crate::driver::Signed]) -> Vec<String> {
    signed.iter().map(|s| if chain.is_solana() { crate::sol::transaction_id(&s.raw).unwrap_or_default() } else { keccak256(&s.raw).to_string() }).collect()
}

/// The trade as its journal holds it (no network, no secrets). `key`: to show our accounts.
pub fn status(dir: &Path, key: Option<&PrivateKeySigner>) -> Result<Value, String> {
    let record = journal(dir).load().map_err(|e| e.to_string())?;
    let saved = record.trade.as_ref().ok_or("the journal holds no saved trade")?;
    let o = &saved.offer;
    let (ca, cb) = (chain_of(o.leg_a.chain_id)?, chain_of(o.leg_b.chain_id)?);
    let mut txs = serde_json::Map::new();
    for (kind, signed) in &record.retained {
        let chain = if kind.starts_with("a:") { ca } else { cb };
        txs.insert(kind.clone(), json!(tx_ids(chain, signed)));
    }
    let leg = |l: &crate::direct::Leg, chain: Chain| json!({"chainId": l.chain_id, "coin": coin_ticker(chain, l.asset).unwrap_or("?"), "asset": l.asset.to_string(), "decimals": coin_decimals(chain, l.asset), "contract": l.contract.to_string(), "value": l.value.to_string(), "duration1": l.duration1, "duration2": l.duration2});
    // What we give and what we get, in the user's terms
    let (gives, gets) = match saved.side {
        Side::Leader => (leg(&o.leg_a, ca), leg(&o.leg_b, cb)),
        Side::Follower => (leg(&o.leg_b, cb), leg(&o.leg_a, ca)),
    };
    let mut v = json!({
        "direct": true,
        "side": format!("{:?}", saved.side).to_lowercase(),
        "role": match saved.side { Side::Leader => "taker", Side::Follower => "maker" },
        "pair": crate::direct_listing::coin_ticker(ca, o.leg_a.asset).zip(coin_ticker(cb, o.leg_b.asset)).map(|(x, y)| if x <= y { format!("{x}/{y}") } else { format!("{y}/{x}") }).unwrap_or_default(),
        "stage": record.state.stage(),
        "done": record.state.is_done(),
        "approved": record.approved == Some(saved.transcript),
        "commitment": saved.transcript.to_string(),
        "tradeId": o.trade_id.to_string(),
        "gives": gives,
        "gets": gets,
        "legA": leg(&o.leg_a, ca),
        "legB": leg(&o.leg_b, cb),
        "lockDeadline": saved.lock_deadline,
        "txs": txs,
        "payout": read::<Value>(&dir.join("direct-payout.json")).ok().flatten(),
    });
    if let Some(key) = key {
        v["accounts"] = json!({ ca.id().to_string(): account_text(ca, key), cb.id().to_string(): account_text(cb, key) });
    }
    Ok(v)
}

pub fn approve(dir: &Path, commitment: Option<B256>) -> Result<Value, String> {
    let commitment = commitment.ok_or("directApprove needs the commitment that directStatus showed")?;
    approve_direct(&mut journal(dir), commitment).map_err(|e| e.to_string())?;
    status(dir, None)
}

/// One leg's chain view for this trade
fn leg_chain(cfg: &DirectConfig, saved: &SavedDirect, terms: &AgreedTerms, binding: Option<crate::sol::SolBinding>, nonce: u64, from_block: u64, key: &PrivateKeySigner) -> Result<AnyChain, String> {
    let rpc = cfg.rpc(terms.chain)?;
    if !terms.chain.is_solana() {
        let mut c = AlloyChain::new(rpc, terms.chain, terms.contract, from_block, key.clone()).map_err(|e| e.to_string())?;
        if let Some(n) = cfg.evm_log_chunk {
            c = c.with_log_chunk(n);
        }
        return Ok(AnyChain::Evm(c));
    }
    let program = crate::sol::program_of(terms.chain).ok_or("no swap program is pinned for this Solana cluster")?;
    if terms.contract != crate::sol::contract_address(&program) {
        return Err("the trade names another swap program".into());
    }
    let binding = binding.ok_or("the key exchange did not carry the Solana accounts")?;
    let ours = sol_account(key);
    if ours != binding.owner && ours != binding.claimer {
        return Err("the trade's Solana account is not this wallet's".into());
    }
    let _ = saved;
    crate::sol::SolChain::with_asset(rpc, terms.chain, program, crate::sol::account_key(&key.to_bytes().0), binding, nonce, terms.value, terms.asset)
        .map(AnyChain::Sol)
        .map_err(|e| e.to_string())
}

pub async fn tick(dir: &Path, cfg: &DirectConfig) -> Result<Value, String> {
    let (seed, key) = cfg.keys()?;
    let saved = journal(dir).load().map_err(|e| e.to_string())?.trade.ok_or("the journal holds no saved trade")?;
    let agreed = saved.agreed(&seed, &key).map_err(|e| e.to_string())?;
    let a = leg_chain(cfg, &saved, &agreed.leg_a, agreed.sol_a, saved.nonce_a(), saved.from_block_a, &key)?;
    let b = leg_chain(cfg, &saved, &agreed.leg_b, agreed.sol_b, saved.nonce_b(), saved.from_block_b, &key)?;
    let mut driver = DirectDriver::resume(&seed, &key, a, b, journal(dir)).map_err(|e| e.to_string())?;
    crate::helper::step("stepping the trade");
    let (awaiting, funding) = match driver.tick().await {
        Ok(_) => (false, None),
        Err(DirectError::Driver(DriverError::AwaitingApproval)) => (true, None),
        Err(DirectError::AwaitingFunding { chain_id, asset, needed, have }) => (false, Some((chain_id, asset, needed, have))),
        Err(e) => return Err(e.to_string()),
    };
    let mut v = status(dir, Some(&key))?;
    v["awaitingApproval"] = json!(awaiting);
    v["awaitingFunding"] = match funding {
        Some((chain_id, asset, needed, have)) => {
            let chain = chain_of(chain_id)?;
            json!({"chainId": chain_id, "asset": asset.to_string(), "coin": coin_ticker(chain, asset).unwrap_or("?"), "account": account_text(chain, &key), "needed": needed.to_string(), "have": have.to_string(), "missing": (needed - have).to_string(), "text": format!("send at least {} to {}", coin_text(chain, asset, needed - have), account_text(chain, &key))})
        }
        None => Value::Null,
    };
    Ok(v)
}

/// After the trade: send what it brought in on `chain_id` (the coin we got, or our own coin back
/// after a refund) to `to`, the user's own wallet. Once: the payout is recorded before and after.
pub async fn payout(dir: &Path, cfg: &DirectConfig, chain_id: u64, to: &str) -> Result<Value, String> {
    let (seed, key) = cfg.keys()?;
    let record = journal(dir).load().map_err(|e| e.to_string())?;
    let saved = record.trade.clone().ok_or("the journal holds no saved trade")?;
    saved.agreed(&seed, &key).map_err(|e| e.to_string())?;
    let outcome = match &record.state {
        DirectRole::Leader(LeaderState::Done(o)) | DirectRole::Follower(FollowerState::Done(o)) => *o,
        _ => return Err("the trade is not finished: its accounts still need their funds".into()),
    };
    use crate::direct_machine::DirectOutcome as O;
    let (o, side) = (&saved.offer, saved.side);
    // What came in, and on which chain
    let (leg, amount) = match (side, outcome) {
        (Side::Leader, O::Swapped) => (&o.leg_b, o.leg_b.value),
        (Side::Follower, O::Swapped) => (&o.leg_a, o.leg_a.value),
        (Side::Leader, O::Refunded) => (&o.leg_a, o.leg_a.value),
        (Side::Follower, O::Refunded) => (&o.leg_b, o.leg_b.value),
        _ => return Err("nothing came in from this trade".into()),
    };
    if leg.chain_id != chain_id {
        return Err(format!("this trade paid in on chain {}, not {chain_id}", leg.chain_id));
    }
    if let Some(done) = read::<Value>(&dir.join("direct-payout.json"))? {
        return Ok(json!({"payout": done}));
    }
    let chain = chain_of(chain_id)?;
    let have = balance(cfg, chain, leg.asset, &key).await?;
    let intent_path = dir.join("direct-payout-intent.json");
    match read::<Value>(&intent_path)? {
        // A payout may already have gone out before a restart: if the balance fell by about the
        // amount since, it did
        Some(intent) => {
            let before: U256 = intent["balanceBefore"].as_str().and_then(|t| t.parse().ok()).unwrap_or(U256::MAX);
            if have.saturating_add(amount / U256::from(2)) <= before {
                let done = json!({"to": to.trim(), "amount": amount.to_string(), "tx": Value::Null, "note": "sent before a restart"});
                write(&dir.join("direct-payout.json"), &done)?;
                return Ok(json!({"payout": done}));
            }
        }
        None => write(&intent_path, &json!({"balanceBefore": have.to_string(), "to": to.trim()}))?,
    }
    // Leave everything else in the account: other trades or offers may need it
    let keep = have.saturating_sub(amount);
    let sent = if leg.asset != Address::ZERO {
        // A token: exactly the trade's amount (or what is left of it), paid for in the chain's coin
        let units = amount.min(have);
        if chain.is_solana() {
            let to_key = crate::sol::from_base58(to).map_err(|e| e.to_string())?;
            let mint = crate::sol::mint_of(chain, leg.asset).map_err(|e| e.to_string())?.ok_or("unknown token")?;
            let units = u64::try_from(units).map_err(|_| "amount out of range")?;
            crate::sol::send_token(cfg.rpc(chain)?, &crate::sol::account_key(&key.to_bytes().0), &to_key, &mint, units)
                .await
                .map_err(|e| e.to_string())?
                .map(|tx| (tx, U256::from(units)))
        } else {
            let to_addr = crate::helper::parse_address(to)?;
            let view = AlloyChain::new(cfg.rpc(chain)?, chain, leg.contract, saved.from_block_a.min(saved.from_block_b), key.clone()).map_err(|e| e.to_string())?;
            view.send_token(leg.asset, to_addr, units).await.map_err(|e| e.to_string())?.map(|tx| (tx.to_string(), units))
        }
    } else if chain.is_solana() {
        let to_key = crate::sol::from_base58(to).map_err(|e| e.to_string())?;
        let keep = u64::try_from(keep).map_err(|_| "amount out of range")?;
        crate::sol::pay_out_with(cfg.rpc(chain)?, &crate::sol::account_key(&key.to_bytes().0), &to_key, keep, None)
            .await
            .map_err(|e| e.to_string())?
            .map(|(tx, amount)| (tx, U256::from(amount)))
    } else {
        let to_addr = crate::helper::parse_address(to)?;
        let view = AlloyChain::new(cfg.rpc(chain)?, chain, leg.contract, saved.from_block_a.min(saved.from_block_b), key.clone()).map_err(|e| e.to_string())?;
        view.pay_out(to_addr, keep).await.map_err(|e| e.to_string())?.map(|(tx, amount)| (tx.to_string(), amount))
    };
    let v = match sent {
        Some((tx, amount)) => {
            let done = json!({"to": to.trim(), "tx": tx, "amount": amount.to_string()});
            write(&dir.join("direct-payout.json"), &done)?;
            done
        }
        None => json!({"to": to.trim(), "sent": false, "reason": "nothing to send, or a payout is still pending"}),
    };
    Ok(json!({"payout": v}))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn amounts_read_as_coins() {
        assert_eq!(amount_text(Chain::EthMainnet, U256::from(15u64) * U256::from(10u64.pow(17))), "1.5 ETH");
        assert_eq!(amount_text(Chain::SolMainnet, U256::from(2_000_000_000u64)), "2 SOL");
        assert_eq!(amount_text(Chain::BscMainnet, U256::from(1u64)), "0.000000000000000001 BNB");
    }

    fn listing(gives: u64, wants: u64, expires_at: u64) -> SignedDirectListing {
        let coin = |chain_id| Coin { chain_id, contract: Address::repeat_byte(1), asset: Address::ZERO, value: U256::from(1u64) };
        let key = PrivateKeySigner::random();
        DirectListing {
            schema: DIRECT_LISTING_SCHEMA.into(),
            id: B256::repeat_byte(7),
            pair: "BNB/ETH".into(),
            maker: key.address(),
            maker_sol: None,
            mailbox: B256::repeat_byte(9),
            gives: coin(gives),
            wants: coin(wants),
            created_at: 1,
            expires_at,
        }
        .sign(&key)
    }

    #[test]
    fn direct_offers_and_takes_keep_the_funds_they_need() {
        const NOW: u64 = 1_800_000_000;
        let root = std::env::temp_dir().join(format!("monzero-direct-funds-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&root);
        assert_eq!(funds_in_use(&root, 1, NOW).unwrap(), None, "no folder: nothing in use");
        // Our open offer gives ETH (1) for BNB (56): ETH is held, BNB is not
        let offer = root.join("offer-1");
        std::fs::create_dir_all(&offer).unwrap();
        let mut maker = MakerFile { listing: listing(1, 56, NOW + 60), taken_by: None, answered: BTreeSet::new(), closed: false };
        write(&offer.join("direct-listing.json"), &maker).unwrap();
        assert!(funds_in_use(&root, 1, NOW).unwrap().is_some());
        assert_eq!(funds_in_use(&root, 56, NOW).unwrap(), None);
        // Expired or closed: free again
        assert_eq!(funds_in_use(&root, 1, NOW + 61).unwrap(), None);
        maker.closed = true;
        write(&offer.join("direct-listing.json"), &maker).unwrap();
        assert_eq!(funds_in_use(&root, 1, NOW).unwrap(), None);
        // A take of a SOL-for-BNB offer: we would lock BNB (what the maker wants)
        let take = root.join("take-1");
        std::fs::create_dir_all(&take).unwrap();
        let taker = TakerFile { listing: listing(Chain::SolMainnet.id(), 56, NOW + 60), from_block_a: 0, from_block_b: 0, first_sent_at: NOW, sent_at: NOW };
        write(&take.join("direct-take.json"), &taker).unwrap();
        assert!(funds_in_use(&root, 56, NOW).unwrap().is_some());
        assert_eq!(funds_in_use(&root, Chain::SolMainnet.id(), NOW).unwrap(), None);
        std::fs::write(take.join("refused"), "no").unwrap();
        assert_eq!(funds_in_use(&root, 56, NOW).unwrap(), None);
        // A journal we cannot read counts as a running trade
        std::fs::write(take.join("direct-journal.json"), "not json").unwrap();
        assert!(funds_in_use(&root, 56, NOW).is_err());
        let _ = std::fs::remove_dir_all(&root);
    }
}
