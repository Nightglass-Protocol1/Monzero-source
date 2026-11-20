//! The Solana side of an XMZ <-> SOL swap, behind the same driver as the EVM chains.
//!
//! The swap program (`dex-contracts/solana/program`) mirrors the EVM contract: New (lock) ->
//! SetReady -> Claim / Refund with two time limits. It differs in what a commitment is: the
//! program checks an ed25519 secret against the point `k·B`, where the EVM contract checks the
//! same scalar `k` against the address of `k·G`. Both traders already hold each other's `k·B`
//! (the XMZ spend share of the key exchange, tied to `k·G` by the cross-curve proof), so the
//! swap machine keeps working on the trade as agreed (accounts and commitments as 20-byte
//! addresses) and this module translates at the edge:
//!
//!  - each trader's Solana account comes from its signed key-exchange frame ([`SolBinding`]);
//!  - a commitment address stands for the ed25519 point the same frame carried;
//!  - the lock is read from the swap's program account and reported as the `New` event the
//!    machine checks against the agreed terms. An account or point that is not the agreed one
//!    is reported as an address that matches nothing, so the check fails as it should.
//!
//! No Solana SDK: a transaction with one signer is a few dozen bytes to lay out, and the node
//! is reached over plain JSON-RPC.

use crate::driver::{DriverError, EvmCall, EvmChain, Funds};
use crate::secret::SecretShare;
use crate::{secret_matches, swap_id, AgreedTerms, Chain, ObservedLock, Revealed, Stage, Swap, XmzSwap};
use alloy::primitives::{Address, Bytes, B256, U256};
use alloy::rpc::types::Log;
use alloy::transports::http::reqwest;
use base64::Engine as _;
use curve25519_dalek::edwards::CompressedEdwardsY;
use ed25519_dalek::{Signer, SigningKey};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

pub type Pubkey = [u8; 32];

/// The system program (all zero bytes: "11111111111111111111111111111111")
pub const SYSTEM_PROGRAM: Pubkey = [0u8; 32];
/// Size of the program's swap account
const SWAP_LEN: usize = 196;
/// A signature costs 5000 lamports; our transactions carry one
pub const FEE_LAMPORTS: u64 = 5_000;
/// "Lamports per gas" for the driver's funding rule (gas for three calls, doubled, for the
/// owner; one call for the claimer). The owner's 4,500,000 lamports cover the swap account's
/// rent (2,255,040, returned when the swap closes), its fees, and the 890,880 an account must
/// keep to stay open (a lock that left less was refused: "InsufficientFundsForRent"). The
/// claimer's 1,500,000 keep its account open while it pays for its claim.
const LAMPORTS_PER_GAS: u64 = 3;
/// What stands for an account or point that is not the agreed one
const NOT_AGREED: Address = Address::repeat_byte(0xEE);

pub fn to_base58(key: &Pubkey) -> String {
    bs58::encode(key).into_string()
}

pub fn from_base58(text: &str) -> Result<Pubkey, DriverError> {
    let bytes = bs58::decode(text.trim()).into_vec().map_err(|_| sol_err("not a Solana address"))?;
    bytes.try_into().map_err(|_| sol_err("not a Solana address"))
}

fn sol_err(e: impl std::fmt::Display) -> DriverError {
    DriverError::Chain(e.to_string())
}

static LOCAL_PROGRAM: std::sync::OnceLock<Pubkey> = std::sync::OnceLock::new();

/// The swap program on a local test validator (chain `SolLocal` only): given with the request,
/// as the local EVM contract is. Real clusters use the pinned programs.
pub fn set_local_program(program: Pubkey) {
    let _ = LOCAL_PROGRAM.set(program);
}

/// The pinned swap program of each Solana cluster. A cluster without one cannot trade.
pub fn program_of(chain: Chain) -> Option<Pubkey> {
    let text = match chain {
        Chain::SolDevnet => "9idXkjjEeyu6nLrYSnCcX6PjmRemKQvuz7VwwwYMtihh",
        // Deployed by the owner 2026-11-11; the same code as on devnet (sha256 0008fca9…7cef)
        Chain::SolMainnet => "GtbWmiCkc4c9nmEbN8tERLovA5CG3FbJQTKisaunEkLK",
        Chain::SolLocal => return LOCAL_PROGRAM.get().copied(),
        _ => return None,
    };
    from_base58(text).ok()
}

static LOCAL_USDT: std::sync::OnceLock<Pubkey> = std::sync::OnceLock::new();

/// The USDT mint on a local test validator (chain `SolLocal` only), given with the request
pub fn set_local_usdt_mint(mint: Pubkey) {
    let _ = LOCAL_USDT.set(mint);
}

/// The SPL token program and the associated token account program
pub const TOKEN_PROGRAM: &str = "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA";
pub const ATA_PROGRAM: &str = "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL";
/// What a token account must hold to stay open (165 bytes); the claimer pays it once per token
pub const TOKEN_ACCOUNT_RENT: u64 = 2_039_280;

/// The pinned USDT mint of each Solana cluster (Tether's on mainnet; none on devnet)
pub fn usdt_mint(chain: Chain) -> Option<Pubkey> {
    match chain {
        Chain::SolMainnet => from_base58("Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB").ok(),
        Chain::SolLocal => LOCAL_USDT.get().copied(),
        _ => None,
    }
}

/// The 20-byte stand-in for a mint in the trade terms (as `contract_address` for the program)
pub fn asset_address(mint: &Pubkey) -> Address {
    Address::from_slice(&mint[..20])
}

/// The mint a trade's asset stands for on this cluster: None for native SOL
pub fn mint_of(chain: Chain, asset: Address) -> Result<Option<Pubkey>, DriverError> {
    if asset == Address::ZERO {
        return Ok(None);
    }
    match usdt_mint(chain) {
        Some(m) if asset_address(&m) == asset => Ok(Some(m)),
        _ => Err(sol_err("this token is not traded on this cluster")),
    }
}

/// The associated token account of `wallet` for `mint`
pub fn token_account(wallet: &Pubkey, mint: &Pubkey) -> Option<Pubkey> {
    let token = from_base58(TOKEN_PROGRAM).ok()?;
    let ata = from_base58(ATA_PROGRAM).ok()?;
    find_program_address(&[wallet, &token, mint], &ata).map(|(k, _)| k)
}

/// Base units of `mint` that `wallet` holds in its associated token account (0 without one)
pub async fn token_balance(url: &str, wallet: &Pubkey, mint: &Pubkey) -> Result<u64, DriverError> {
    let rpc = Rpc::new(url)?;
    let Some(ata) = token_account(wallet, mint) else { return Ok(0) };
    // Read the token account itself (getAccountInfo) rather than getTokenAccountBalance: public
    // nodes have started to refuse that one as an "indexed request" (PublicNode, 19 Nov 2026).
    // An SPL token account is mint (32) | owner (32) | amount (u64, little-endian) | …
    let r = rpc.call("getAccountInfo", json!([to_base58(&ata), {"commitment": "confirmed", "encoding": "base64"}])).await?;
    let v = &r["value"];
    if v.is_null() {
        return Ok(0); // no such account yet: it holds nothing
    }
    let token = from_base58(TOKEN_PROGRAM)?;
    if v["owner"].as_str() != Some(to_base58(&token).as_str()) {
        return Err(sol_err("the token account is not owned by the token program"));
    }
    let data = v["data"][0].as_str().and_then(|b| base64::Engine::decode(&base64::engine::general_purpose::STANDARD, b).ok())
        .ok_or_else(|| sol_err("unreadable token account"))?;
    if data.len() < 72 || data[..32] != mint[..] || data[32..64] != wallet[..] {
        return Err(sol_err("the token account does not belong to this wallet and mint"));
    }
    Ok(u64::from_le_bytes(data[64..72].try_into().expect("8 bytes")))
}

/// The stand-in contract addresses of the Solana clusters that have a program
pub fn pinned_contracts() -> Vec<(u64, Address)> {
    [Chain::SolMainnet, Chain::SolDevnet, Chain::SolLocal].into_iter().filter_map(|c| program_of(c).map(|p| (c.id(), contract_address(&p)))).collect()
}

/// Balance of an account (lamports), the cluster's time and newest slot: what the market
/// commands need before a trade exists.
pub async fn balance(url: &str, account: &Pubkey) -> Result<u64, DriverError> {
    let rpc = Rpc::new(url)?;
    Ok(rpc.call("getBalance", json!([to_base58(account), {"commitment": "confirmed"}])).await?["value"].as_u64().unwrap_or(0))
}

pub async fn clock(url: &str) -> Result<(u64, u64), DriverError> {
    let rpc = Rpc::new(url)?;
    let slot = rpc.call("getSlot", json!([{"commitment": "finalized"}])).await?.as_u64().ok_or_else(|| sol_err("no slot"))?;
    Ok((rpc.block_time(slot).await?, slot))
}

/// The program is there and can run
pub async fn check_program(url: &str, program: &Pubkey) -> Result<(), DriverError> {
    let r = Rpc::new(url)?.call("getAccountInfo", json!([to_base58(program), {"encoding": "base64", "commitment": "confirmed"}])).await?;
    if r["value"]["executable"].as_bool() != Some(true) {
        return Err(sol_err("the swap program is not on this Solana cluster"));
    }
    Ok(())
}

/// Send everything `key` holds (less the fee) to `to`, outside any trade.
/// Sends the account's balance to `to`, less the fee and less `keep` lamports (left for later fees)
pub async fn pay_out(url: &str, key: &SigningKey, to: &Pubkey, keep: u64) -> Result<Option<(String, u64)>, DriverError> {
    pay_out_with(url, key, to, keep, None).await
}

/// As `pay_out`, and first all of `mint` (USDT) the account holds: into `to`'s token account
/// (opened if it has none), then our token account is closed so its deposit comes back with
/// the SOL. One transaction. Returns the SOL sent (0 when only tokens moved).
pub async fn pay_out_with(url: &str, key: &SigningKey, to: &Pubkey, keep: u64, mint: Option<Pubkey>) -> Result<Option<(String, u64)>, DriverError> {
    let rpc = Rpc::new(url)?;
    let me = key.verifying_key().to_bytes();
    if to == &me {
        return Ok(None);
    }
    let lamports_of = |r: Value| r["value"].as_u64().unwrap_or(0);
    let balance = lamports_of(rpc.call("getBalance", json!([to_base58(&me), {"commitment": "confirmed"}])).await?);
    let token = from_base58(TOKEN_PROGRAM)?;
    let mut ixs: Vec<(Pubkey, Vec<Meta>, Vec<u8>)> = vec![];
    let mut available = balance;
    if let Some(mint) = mint {
        let units = token_balance(url, &me, &mint).await?;
        let ours = token_account(&me, &mint).ok_or_else(|| sol_err("no token account address"))?;
        let ours_rent = lamports_of(rpc.call("getBalance", json!([to_base58(&ours), {"commitment": "confirmed"}])).await?);
        if ours_rent > 0 {
            let theirs = token_account(to, &mint).ok_or_else(|| sol_err("no token account address"))?;
            if units > 0 {
                if rpc.account(&theirs, "confirmed").await?.is_none() {
                    // We open it for them, before our own token account's deposit comes back
                    if balance < TOKEN_ACCOUNT_RENT + FEE_LAMPORTS {
                        return Err(sol_err(format!(
                            "sending the USDT opens a USDT account in the receiving wallet, which needs {}.{:09} SOL here first; send a little SOL to this account, or receive at a wallet that already holds USDT",
                            (TOKEN_ACCOUNT_RENT + FEE_LAMPORTS) / 1_000_000_000, (TOKEN_ACCOUNT_RENT + FEE_LAMPORTS) % 1_000_000_000)));
                    }
                    available = available.saturating_sub(TOKEN_ACCOUNT_RENT);
                }
                let ata_program = from_base58(ATA_PROGRAM)?;
                ixs.push((ata_program, vec![meta(me, true, true), meta(theirs, false, true), meta(*to, false, false), meta(mint, false, false), meta(SYSTEM_PROGRAM, false, false), meta(token, false, false)], vec![1u8]));
                let mut data = vec![12u8];
                data.extend_from_slice(&units.to_le_bytes());
                data.push(token_decimals(&mint));
                ixs.push((token, vec![meta(ours, false, true), meta(mint, false, false), meta(theirs, false, true), meta(me, true, true)], data));
            }
            // Close our token account: its deposit comes back to us and goes out with the SOL
            ixs.push((token, vec![meta(ours, false, true), meta(me, false, true), meta(me, true, true)], vec![9u8]));
            available += ours_rent;
        }
    }
    let sol = available.saturating_sub(FEE_LAMPORTS).saturating_sub(keep);
    if sol > 0 && balance > FEE_LAMPORTS.saturating_add(keep) {
        let mut data = 2u32.to_le_bytes().to_vec();
        data.extend_from_slice(&sol.to_le_bytes());
        ixs.push((SYSTEM_PROGRAM, vec![meta(me, true, true), meta(*to, false, true)], data));
    }
    if ixs.is_empty() {
        return Ok(None);
    }
    let list: Vec<(&Pubkey, &[Meta], &[u8])> = ixs.iter().map(|(p, a, d)| (p, a.as_slice(), d.as_slice())).collect();
    let raw = signed(key, message_many(&me, &list, &rpc.blockhash().await?));
    let id = bs58::encode(&raw[1..65]).into_string();
    rpc.send(&raw).await?;
    Ok(Some((id, if ixs.iter().any(|(p, _, _)| p == &SYSTEM_PROGRAM) { sol } else { 0 })))
}

/// Decimals of a pinned token (USDT: 6)
fn token_decimals(_mint: &Pubkey) -> u8 {
    6
}

/// A transaction's id as Solana shows it (its first signature), for a transaction made here
pub fn transaction_id(raw: &[u8]) -> Option<String> {
    parts(raw).map(|(signature, _)| bs58::encode(signature).into_string())
}

/// The 20-byte stand-in for a program in listings and offers (their `contract` field)
pub fn contract_address(program: &Pubkey) -> Address {
    Address::from_slice(&alloy::primitives::keccak256(program)[12..])
}

/// This wallet's Solana trade account: an ed25519 key made from the trade account's EVM key, so
/// the wallet's recovery words restore it and nothing new has to be stored.
pub fn account_key(evm_key: &[u8; 32]) -> SigningKey {
    SigningKey::from_bytes(&monzero_evm_keys::prf(evm_key, &[b"monzero solana trade account v1"]))
}

/// A program address: the first of bump 255, 254, ... whose hash is not a curve point.
pub fn find_program_address(seeds: &[&[u8]], program: &Pubkey) -> Option<(Pubkey, u8)> {
    (0..=255u8).rev().find_map(|bump| {
        let mut h = Sha256::new();
        for s in seeds {
            h.update(s);
        }
        h.update([bump]);
        h.update(program);
        h.update(b"ProgramDerivedAddress");
        let key: Pubkey = h.finalize().into();
        CompressedEdwardsY(key).decompress().is_none().then_some((key, bump))
    })
}

/// Both traders' Solana accounts and commitment points, from their signed frames.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SolBinding {
    pub owner: Pubkey,
    pub claimer: Pubkey,
    /// The claimer's `k·B`
    pub claim_point: [u8; 32],
    /// The owner's `k·B`
    pub refund_point: [u8; 32],
}

impl SolBinding {
    pub fn swap_address(&self, program: &Pubkey, lamports: u64, nonce: u64) -> Option<Pubkey> {
        find_program_address(
            &[b"swap", &self.owner, &self.claimer, &self.claim_point, &self.refund_point, &lamports.to_le_bytes(), &nonce.to_le_bytes()],
            program,
        )
        .map(|(key, _)| key)
    }
}

// ---- transactions

#[derive(Clone)]
struct Meta {
    key: Pubkey,
    signer: bool,
    writable: bool,
}

fn compact(n: usize, out: &mut Vec<u8>) {
    let mut n = n as u32;
    loop {
        let byte = (n & 0x7f) as u8;
        n >>= 7;
        if n == 0 {
            out.push(byte);
            return;
        }
        out.push(byte | 0x80);
    }
}

fn read_compact(bytes: &[u8], at: &mut usize) -> Option<usize> {
    let (mut value, mut shift) = (0usize, 0);
    loop {
        let byte = *bytes.get(*at)?;
        *at += 1;
        value |= ((byte & 0x7f) as usize) << shift;
        if byte & 0x80 == 0 {
            return Some(value);
        }
        shift += 7;
        if shift > 14 {
            return None;
        }
    }
}

/// One instruction, paid for and signed by `payer` alone (the first account).
#[cfg(test)]
fn message(payer: &Pubkey, program: &Pubkey, accounts: &[Meta], data: &[u8], blockhash: &[u8; 32]) -> Vec<u8> {
    message_many(payer, &[(program, accounts, data)], blockhash)
}

/// Several instructions in one transaction, paid for and signed by `payer` alone.
fn message_many(payer: &Pubkey, ixs: &[(&Pubkey, &[Meta], &[u8])], blockhash: &[u8; 32]) -> Vec<u8> {
    message_signed_by(&[*payer], ixs, blockhash)
}

/// Several instructions signed by `signers` (the first pays the fees; all writable).
fn message_signed_by(signers: &[Pubkey], ixs: &[(&Pubkey, &[Meta], &[u8])], blockhash: &[u8; 32]) -> Vec<u8> {
    // Order: the signers, other writable accounts, read-only accounts (programs last). An account
    // writable in any instruction is writable.
    let mut keys: Vec<(Pubkey, bool)> = signers.iter().map(|k| (*k, true)).collect();
    for writable in [true, false] {
        for (_, accounts, _) in ixs {
            for a in accounts.iter().filter(|a| a.writable == writable && !a.signer) {
                if !keys.iter().any(|(k, _)| k == &a.key) {
                    keys.push((a.key, writable));
                }
            }
        }
    }
    for (program, _, _) in ixs {
        if !keys.iter().any(|(k, _)| k == *program) {
            keys.push((**program, false));
        }
    }
    let readonly = keys.iter().filter(|(_, w)| !w).count() as u8;
    let mut m = vec![signers.len() as u8, 0, readonly];
    compact(keys.len(), &mut m);
    for (k, _) in &keys {
        m.extend_from_slice(k);
    }
    m.extend_from_slice(blockhash);
    compact(ixs.len(), &mut m);
    let index = |key: &Pubkey| keys.iter().position(|(k, _)| k == key).expect("listed above") as u8;
    for (program, accounts, data) in ixs {
        m.push(index(program));
        compact(accounts.len(), &mut m);
        for a in accounts.iter() {
            m.push(index(&a.key));
        }
        compact(data.len(), &mut m);
        m.extend_from_slice(data);
    }
    m
}

fn signed(key: &SigningKey, message: Vec<u8>) -> Vec<u8> {
    let mut tx = vec![1];
    tx.extend_from_slice(&key.sign(&message).to_bytes());
    tx.extend_from_slice(&message);
    tx
}

/// The signature (the transaction's id) and where its blockhash sits, for a transaction made here
fn parts(raw: &[u8]) -> Option<([u8; 64], usize)> {
    let n = *raw.first()? as usize;
    if n == 0 || n > 2 || raw.len() < 1 + 64 * n + 3 {
        return None;
    }
    let signature: [u8; 64] = raw[1..65].try_into().ok()?;
    let mut at = 1 + 64 * n + 3;
    let keys = read_compact(raw, &mut at)?;
    let blockhash = at + keys * 32;
    (raw.len() >= blockhash + 32).then_some((signature, blockhash))
}

fn meta(key: Pubkey, signer: bool, writable: bool) -> Meta {
    Meta { key, signer, writable }
}

fn lamports(value: U256) -> Result<u64, DriverError> {
    u64::try_from(value).map_err(|_| sol_err("amount does not fit Solana's 64-bit lamports"))
}

/// The ed25519 secret the program checks: the scalar little-endian (the EVM form is big-endian)
fn program_secret(evm: B256) -> [u8; 32] {
    let mut le = evm.0;
    le.reverse();
    le
}

// ---- the node

struct Rpc {
    url: String,
    http: reqwest::Client,
}

impl Rpc {
    fn new(url: &str) -> Result<Self, DriverError> {
        let http = crate::chain::http_client_builder().timeout(std::time::Duration::from_secs(30)).build().map_err(sol_err)?;
        Ok(Rpc { url: url.to_string(), http })
    }

    /// A slot without a block has no time: step back to one that has
    async fn block_time(&self, slot: u64) -> Result<u64, DriverError> {
        for back in 0..8 {
            if let Ok(t) = self.call("getBlockTime", json!([slot.saturating_sub(back)])).await {
                if let Some(t) = t.as_u64() {
                    return Ok(t);
                }
            }
        }
        Err(sol_err("no block time"))
    }

    async fn send(&self, raw: &[u8]) -> Result<(), DriverError> {
        let text = base64::engine::general_purpose::STANDARD.encode(raw);
        match self.call("sendTransaction", json!([text, {"encoding": "base64", "preflightCommitment": "confirmed"}])).await {
            Ok(_) => Ok(()),
            // Known already, or its blockhash has aged out (a fresh version follows): not failures
            Err(DriverError::Chain(e)) if e.contains("AlreadyProcessed") || e.contains("already been processed") || e.contains("BlockhashNotFound") || e.contains("Blockhash not found") => Ok(()),
            Err(e) => Err(e),
        }
    }

    async fn call(&self, method: &str, params: Value) -> Result<Value, DriverError> {
        let body = json!({"jsonrpc": "2.0", "id": 1, "method": method, "params": params});
        let mut last = String::new();
        for attempt in 0..4u64 {
            if attempt > 0 {
                tokio::time::sleep(std::time::Duration::from_millis(400 * attempt)).await;
            }
            let sent = self.http.post(&self.url).json(&body).send().await;
            let reply = match sent {
                Ok(r) if r.status().is_server_error() || r.status().as_u16() == 429 => {
                    last = format!("HTTP {}", r.status());
                    continue;
                }
                Ok(r) => r.json::<Value>().await.map_err(sol_err)?,
                Err(e) => {
                    last = e.to_string();
                    continue;
                }
            };
            if let Some(e) = reply.get("error") {
                return Err(DriverError::Chain(format!("{method}: {e}")));
            }
            return Ok(reply["result"].clone());
        }
        Err(DriverError::Chain(format!("{method}: {last}")))
    }

    async fn account(&self, key: &Pubkey, commitment: &str) -> Result<Option<(Pubkey, Vec<u8>)>, DriverError> {
        let r = self.call("getAccountInfo", json!([to_base58(key), {"encoding": "base64", "commitment": commitment}])).await?;
        let Some(owner) = r["value"]["owner"].as_str() else {
            return Ok(None);
        };
        let data = r["value"]["data"][0].as_str().ok_or_else(|| sol_err("account without data"))?;
        Ok(Some((from_base58(owner)?, base64::engine::general_purpose::STANDARD.decode(data).map_err(sol_err)?)))
    }

    async fn blockhash(&self) -> Result<[u8; 32], DriverError> {
        let r = self.call("getLatestBlockhash", json!([{"commitment": "confirmed"}])).await?;
        from_base58(r["value"]["blockhash"].as_str().ok_or_else(|| sol_err("no blockhash"))?)
    }

    /// Whether a transaction made it: Some(true) executed, Some(false) executed and failed
    async fn status(&self, signature: &[u8; 64]) -> Result<Option<bool>, DriverError> {
        let id = bs58::encode(signature).into_string();
        let r = self.call("getSignatureStatuses", json!([[id], {"searchTransactionHistory": true}])).await?;
        let s = &r["value"][0];
        Ok((!s.is_null()).then(|| s["err"].is_null()))
    }
}

/// A finished swap, as its claim transaction and the swap's creation prove it
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ClaimedSwap {
    pub owner: Pubkey,
    pub claimer: Pubkey,
    pub lamports: u64,
    pub nonce: u64,
    /// Block time of the claim
    pub at: u64,
    /// The token claimed (USDT); None for native SOL. `lamports` is then the token's base units.
    pub mint: Option<Pubkey>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ClaimSeen {
    /// Unknown to the node yet, or not yet final
    Pending,
    Final(ClaimedSwap),
    NotAClaim(String),
}

/// The pinned program's top-level instructions in a transaction: (accounts, data) each
fn program_calls(tx: &Value, program: &Pubkey) -> Vec<(Vec<Pubkey>, Vec<u8>)> {
    let message = &tx["transaction"]["message"];
    let keys: Vec<Option<Pubkey>> = message["accountKeys"].as_array().into_iter().flatten().map(|k| k.as_str().and_then(|k| from_base58(k).ok())).collect();
    let key = |i: &Value| i.as_u64().and_then(|i| keys.get(i as usize).copied().flatten());
    let mut out = vec![];
    for ix in message["instructions"].as_array().into_iter().flatten() {
        if key(&ix["programIdIndex"]) != Some(*program) {
            continue;
        }
        let accounts: Option<Vec<Pubkey>> = ix["accounts"].as_array().into_iter().flatten().map(key).collect();
        let data = ix["data"].as_str().and_then(|d| bs58::decode(d).into_vec().ok());
        if let (Some(accounts), Some(data)) = (accounts, data) {
            out.push((accounts, data));
        }
    }
    out
}

/// What a transaction reported as a swap's claim really is, read from a node (for the relays'
/// record of trades). A claim counts once it is final, was a direct claim on the pinned program,
/// and the swap it paid out was made with terms that give exactly that swap account's address.
pub async fn claim_lookup(url: &str, program: &Pubkey, signature: &[u8; 64]) -> Result<ClaimSeen, DriverError> {
    let rpc = Rpc::new(url)?;
    let options = json!({"encoding": "json", "commitment": "finalized", "maxSupportedTransactionVersion": 0});
    let tx = rpc.call("getTransaction", json!([bs58::encode(signature).into_string(), options])).await?;
    if tx.is_null() {
        return Ok(ClaimSeen::Pending);
    }
    if !tx["meta"]["err"].is_null() {
        return Ok(ClaimSeen::NotAClaim("the transaction failed".into()));
    }
    // Claim = 2: the secret follows; accounts are the claimer, the swap, the owner.
    // ClaimSpl = 5: accounts are the claimer, the swap, its token account, the vault, the owner, the mint.
    // ClaimRelayedSpl = 8 (a relay's, the fee follows the secret): the relay, the swap, the
    // claimer's token account, the relay's, the vault, the owner, the mint; the claimer comes from
    // the swap's creation below.
    let Some((claim, kind)) = program_calls(&tx, program).into_iter().find_map(|(a, d)| match (d.len(), d.first()) {
        (33, Some(2)) if a.len() >= 3 => Some((a, 2u8)),
        (33, Some(5)) if a.len() >= 6 => Some((a, 5u8)),
        (41, Some(8)) if a.len() >= 7 => Some((a, 8u8)),
        _ => None,
    }) else {
        return Ok(ClaimSeen::NotAClaim("no claim on the pinned program in it".into()));
    };
    let (claimer, swap, owner) = match kind {
        2 => (Some(claim[0]), claim[1], claim[2]),
        5 => (Some(claim[0]), claim[1], claim[4]),
        _ => (None, claim[1], claim[5]),
    };
    let claimed_mint = match kind {
        5 => Some(claim[5]),
        8 => Some(claim[6]),
        _ => None,
    };
    let Some(at) = tx["blockTime"].as_u64() else {
        return Ok(ClaimSeen::Pending);
    };
    // The swap account is closed by now: its terms are in the transaction that made it, among
    // the oldest that touched its address (anyone can send it dust later, never earlier)
    let history = rpc.call("getSignaturesForAddress", json!([to_base58(&swap), {"limit": 1000, "commitment": "finalized"}])).await?;
    let oldest: Vec<&str> = history.as_array().into_iter().flatten().rev().filter(|s| s["err"].is_null()).filter_map(|s| s["signature"].as_str()).take(4).collect();
    for id in oldest {
        let made = rpc.call("getTransaction", json!([id, options])).await?;
        for (accounts, data) in program_calls(&made, program) {
            // NewSwap = 0 (NewSpl = 4): both points, both durations, the amount, the nonce;
            // accounts are the owner, the claimer, the swap (then, for a token, its mint)
            let made_kind = if kind == 2 { 0 } else { 4 };   // a token claim (5 or 8) follows NewSpl
            if data.len() != 97 || data[0] != made_kind || accounts.len() < 3 || accounts[2] != swap {
                continue;
            }
            if made_kind == 4 && accounts.get(3) != claimed_mint.as_ref() {
                continue;
            }
            let point = |at: usize| -> [u8; 32] { data[at..at + 32].try_into().expect("32 bytes") };
            let word = |at: usize| u64::from_le_bytes(data[at..at + 8].try_into().expect("8 bytes"));
            let binding = SolBinding { owner: accounts[0], claimer: accounts[1], claim_point: point(1), refund_point: point(33) };
            let (lamports, nonce) = (word(81), word(89));
            if binding.swap_address(program, lamports, nonce) != Some(swap) || binding.owner != owner || claimer.is_some_and(|c| c != binding.claimer) {
                continue;
            }
            return Ok(ClaimSeen::Final(ClaimedSwap { owner, claimer: binding.claimer, lamports, nonce, at, mint: claimed_mint }));
        }
    }
    Ok(ClaimSeen::NotAClaim("the claimed swap's creation was not found".into()))
}

/// The swap as the program stores it
struct Stored {
    owner: Pubkey,
    claimer: Pubkey,
    claim_point: [u8; 32],
    refund_point: [u8; 32],
    timeout1: u64,
    timeout2: u64,
    amount: u64,
    stage: u8,
    native: bool,
    mint: Pubkey,
    nonce: u64,
}

fn stored(data: &[u8]) -> Option<Stored> {
    if data.len() < SWAP_LEN {
        return None;
    }
    let key = |at: usize| -> Pubkey { data[at..at + 32].try_into().expect("32 bytes") };
    let word = |at: usize| u64::from_le_bytes(data[at..at + 8].try_into().expect("8 bytes"));
    let s = Stored {
        owner: key(0),
        claimer: key(32),
        claim_point: key(64),
        refund_point: key(96),
        timeout1: word(128),
        timeout2: word(136),
        amount: word(144),
        stage: data[152],
        // bump at 153, mint at 154..186, kind at 186
        native: data[186] == 0 && data[154..186] == [0u8; 32],
        mint: key(154),
        nonce: word(188),
    };
    // A closed swap is zeroed
    (s.stage != 0).then_some(s)
}

pub struct SolChain {
    chain: Chain,
    program: Pubkey,
    contract: Address,
    key: SigningKey,
    binding: SolBinding,
    nonce: u64,
    /// The trade's lamports (or token base units): with the binding and the nonce it fixes the
    /// swap account's address
    amount: u64,
    /// The token the swap holds (USDT); None for native SOL
    mint: Option<Pubkey>,
    /// Relays that may pay this token trade's fees (see `sponsor`); none: we pay them in SOL
    sponsors: Vec<String>,
    rpc: Rpc,
}

impl SolChain {
    /// `binding`, `nonce` and `value` belong to one trade: this chain view serves that trade only.
    pub fn new(url: &str, chain: Chain, program: Pubkey, key: SigningKey, binding: SolBinding, nonce: u64, value: U256) -> Result<Self, DriverError> {
        Self::with_asset(url, chain, program, key, binding, nonce, value, Address::ZERO)
    }

    /// As `new`, for a trade paid in `asset` (zero: native SOL; else a pinned token such as USDT)
    #[allow(clippy::too_many_arguments)]
    pub fn with_asset(url: &str, chain: Chain, program: Pubkey, key: SigningKey, binding: SolBinding, nonce: u64, value: U256, asset: Address) -> Result<Self, DriverError> {
        let amount = lamports(value)?;
        if !chain.is_solana() {
            return Err(sol_err("not a Solana chain"));
        }
        let mint = mint_of(chain, asset)?;
        Ok(SolChain { chain, program, contract: contract_address(&program), key, binding, nonce, amount, mint, sponsors: vec![], rpc: Rpc::new(url)? })
    }

    /// Relays that may pay the fees of a token (USDT) trade, paid back in USDT
    pub fn with_sponsors(mut self, relays: Vec<String>) -> Self {
        self.sponsors = relays;
        self
    }

    fn sponsored(&self) -> bool {
        self.mint.is_some() && !self.sponsors.is_empty()
    }

    /// Our lock as a sponsored transaction (we hold USDT but no SOL): the first relay that agrees
    /// signs it as fee payer. The relay's terms are checked here before we sign anything.
    async fn lock_through_sponsor(&self, lock: (Pubkey, Vec<Meta>, Vec<u8>)) -> Result<Vec<u8>, DriverError> {
        let mint = self.mint.ok_or_else(|| sol_err("not a token trade"))?;
        let me = self.account();
        let token = from_base58(TOKEN_PROGRAM)?;
        let ours = token_account(&me, &mint).ok_or_else(|| sol_err("no token account address"))?;
        let mut why = String::from("no relay pays network fees for USDT");
        for relay in &self.sponsors {
            let Some(info) = crate::sponsor::info(relay).await else { continue };
            let (Some(relayer), Some(fee), Some(front)) = (info.solana.as_deref().and_then(|r| from_base58(r).ok()), info.sol_lock_fee, info.sol_lock_front) else { continue };
            if fee > crate::sponsor::MAX_SOL_LOCK_FEE || front > crate::sponsor::SOL_LOCK_FRONT {
                why = format!("{relay} asks {fee} USDT units for the lock; at most {}", crate::sponsor::MAX_SOL_LOCK_FEE);
                continue;
            }
            let theirs = token_account(&relayer, &mint).ok_or_else(|| sol_err("no token account address"))?;
            let mut give = 2u32.to_le_bytes().to_vec();
            give.extend_from_slice(&front.to_le_bytes());
            let mut pay = vec![12u8];
            pay.extend_from_slice(&fee.to_le_bytes());
            pay.push(token_decimals(&mint));
            let ixs: Vec<(Pubkey, Vec<Meta>, Vec<u8>)> = vec![
                (SYSTEM_PROGRAM, vec![meta(relayer, true, true), meta(me, false, true)], give),
                lock.clone(),
                (token, vec![meta(ours, false, true), meta(mint, false, false), meta(theirs, false, true), meta(me, true, false)], pay),
            ];
            let list: Vec<(&Pubkey, &[Meta], &[u8])> = ixs.iter().map(|(p, a, d)| (p, a.as_slice(), d.as_slice())).collect();
            let m = message_signed_by(&[relayer, me], &list, &self.rpc.blockhash().await?);
            match self.sponsor_lock_at(relay, m).await {
                Ok(raw) => return Ok(raw),
                Err(e) => why = format!("{relay}: {e}"),
            }
        }
        Err(sol_err(why))
    }

    /// Sign `m` (a sponsored lock message) and have `relay` sign it as fee payer
    async fn sponsor_lock_at(&self, relay: &str, m: Vec<u8>) -> Result<Vec<u8>, String> {
        let signature = self.key.sign(&m).to_bytes();
        let request = crate::sponsor::SolLockRequest {
            chain_id: self.chain.id(),
            message: base64::engine::general_purpose::STANDARD.encode(&m),
            signature: hex::encode(signature),
        };
        let answer = crate::sponsor::post(relay, crate::sponsor::SOL_LOCK_PATH, &request).await?;
        let raw = base64::engine::general_purpose::STANDARD.decode(answer.raw.ok_or("no transaction in the answer")?).map_err(|e| e.to_string())?;
        // Exactly what we signed, with the relay's signature added
        if raw.len() != 129 + m.len() || raw[0] != 2 || raw[129..] != m[..] || raw[65..129] != signature[..] {
            return Err("the relay returned another transaction".into());
        }
        Ok(raw)
    }

    /// A sponsored lock again with a fresh blockhash, from the relay that paid the first
    async fn sponsor_lock_message(&self, m: Vec<u8>) -> Result<Vec<u8>, DriverError> {
        let relayer = parse_message(&m).and_then(|p| p.keys.first().copied()).ok_or_else(|| sol_err("not a message"))?;
        for relay in &self.sponsors {
            if crate::sponsor::info(relay).await.and_then(|i| i.solana).and_then(|r| from_base58(&r).ok()) == Some(relayer) {
                return self.sponsor_lock_at(relay, m).await.map_err(sol_err);
            }
        }
        Err(sol_err("the relay that paid for the lock is gone"))
    }

    /// Our claim through a relay (we hold no SOL): it pays the fees and takes `fee` of the USDT
    async fn claim_through_sponsor(&self, value: U256, secret: B256) -> Result<bool, DriverError> {
        if !self.sponsored() {
            return Ok(false);
        }
        let address = self.swap_address(value)?;
        let secret = program_secret(secret);
        for relay in &self.sponsors {
            let Some(info) = crate::sponsor::info(relay).await else { continue };
            let (Some(relayer), Some(fee)) = (info.solana.as_deref().and_then(|r| from_base58(r).ok()), info.sol_claim_fee) else { continue };
            if fee > crate::sponsor::MAX_SOL_CLAIM_FEE || fee >= self.amount {
                continue;
            }
            let signature = self.key.sign(&crate::sponsor::sol_claim_message(&address, fee, &relayer)).to_bytes();
            let request = crate::sponsor::SolClaimRequest {
                chain_id: self.chain.id(),
                swap: to_base58(&address),
                secret: hex::encode(secret),
                fee,
                claimer: to_base58(&self.account()),
                owner: to_base58(&self.binding.owner),
                signature: hex::encode(signature),
            };
            if crate::sponsor::post(relay, crate::sponsor::SOL_CLAIM_PATH, &request).await.is_ok() {
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// The swap's token vault (the program's account for the swap's tokens)
    fn vault(&self, swap: &Pubkey) -> Result<Pubkey, DriverError> {
        find_program_address(&[b"vault", swap], &self.program).map(|(k, _)| k).ok_or_else(|| sol_err("no vault address"))
    }

    pub fn account(&self) -> Pubkey {
        self.key.verifying_key().to_bytes()
    }

    /// The trade's swap account. `value` is what the caller believes the swap holds: a swap of
    /// another amount is not this trade's.
    fn swap_address(&self, value: U256) -> Result<Pubkey, DriverError> {
        if lamports(value)? != self.amount {
            return Err(sol_err("a swap of another amount is not this trade's"));
        }
        self.binding.swap_address(&self.program, self.amount, self.nonce).ok_or_else(|| sol_err("no swap address"))
    }

    /// Signatures of the transactions that touched the swap account, oldest last
    async fn history(&self, swap: &Pubkey) -> Result<Vec<String>, DriverError> {
        let r = self.rpc.call("getSignaturesForAddress", json!([to_base58(swap), {"limit": 25, "commitment": "confirmed"}])).await?;
        Ok(r.as_array()
            .into_iter()
            .flatten()
            .filter(|s| s["err"].is_null())
            .filter_map(|s| s["signature"].as_str().map(str::to_string))
            .collect())
    }
}

impl EvmChain for SolChain {
    async fn now(&self) -> Result<u64, DriverError> {
        let slot = self.rpc.call("getSlot", json!([{"commitment": "finalized"}])).await?.as_u64().ok_or_else(|| sol_err("no slot"))?;
        self.rpc.block_time(slot).await
    }

    async fn find_lock(&self, terms: &AgreedTerms) -> Result<Option<ObservedLock>, DriverError> {
        if mint_of(self.chain, terms.asset)? != self.mint {
            return Err(sol_err("the trade's asset is not this chain view's"));
        }
        if lamports(terms.value).ok() != Some(self.amount) {
            return Ok(None);
        }
        let address = self.swap_address(terms.value)?;
        let Some((owner_program, data)) = self.rpc.account(&address, "finalized").await? else {
            return Ok(None);
        };
        let Some(s) = stored(&data) else {
            return Ok(None);
        };
        // The address already commits to accounts, points, amount and nonce; they are compared
        // again so that nothing rests on the derivation alone
        let stands_for = |agreed: bool, address: Address| if agreed { address } else { NOT_AGREED };
        let mut event = XmzSwap::New {
            swapId: B256::ZERO,
            owner: stands_for(s.owner == self.binding.owner, terms.owner),
            claimer: stands_for(s.claimer == self.binding.claimer, terms.claimer),
            claimCommitment: stands_for(s.claim_point == self.binding.claim_point, terms.claim_commitment),
            refundCommitment: stands_for(s.refund_point == self.binding.refund_point, terms.refund_commitment),
            timeout1: U256::from(s.timeout1),
            timeout2: U256::from(s.timeout2),
            asset: match self.mint {
                None => stands_for(s.native, Address::ZERO),
                Some(m) => stands_for(!s.native && s.mint == m, terms.asset),
            },
            value: U256::from(s.amount),
            nonce: U256::from(s.nonce),
        };
        event.swapId = swap_id(&Swap {
            owner: event.owner,
            claimer: event.claimer,
            claimCommitment: event.claimCommitment,
            refundCommitment: event.refundCommitment,
            timeout1: event.timeout1,
            timeout2: event.timeout2,
            asset: event.asset,
            value: event.value,
            nonce: event.nonce,
        });
        // Not this program's account: reported as emitted elsewhere, which the terms check refuses
        let emitter = if owner_program == self.program { self.contract } else { NOT_AGREED };
        Ok(Some(ObservedLock {
            chain_id: self.chain.id(),
            log: Log {
                inner: alloy::primitives::Log { address: emitter, data: event },
                block_hash: None,
                block_number: None,
                block_timestamp: None,
                transaction_hash: None,
                transaction_index: None,
                log_index: None,
                removed: false,
            },
            // The program stores the time limits, not the lock time: the first limit is the truth
            // the trade runs on, and the second must still be exactly `duration2` after it
            block_timestamp: s.timeout1.saturating_sub(terms.duration1),
            stage: s.stage,
        }))
    }

    async fn stage(&self, _id: B256) -> Result<Stage, DriverError> {
        // This view serves one trade; its swap account is the one derived from the binding
        let address = self.swap_address(U256::from(self.amount))?;
        match self.rpc.account(&address, "finalized").await? {
            Some((program, data)) if program == self.program => match stored(&data) {
                Some(s) => Stage::try_from(s.stage).map_err(sol_err),
                None => Ok(Stage::Completed),
            },
            Some(_) => Ok(Stage::Invalid),
            // Closed by a claim or refund (it has a history), or never made
            None => Ok(if self.history(&address).await?.is_empty() { Stage::Invalid } else { Stage::Completed }),
        }
    }

    async fn revealed(&self, swap: &Swap) -> Result<Option<Revealed>, DriverError> {
        let address = self.swap_address(swap.value)?;
        for signature in self.history(&address).await? {
            let tx = self
                .rpc
                .call("getTransaction", json!([signature, {"encoding": "json", "commitment": "confirmed", "maxSupportedTransactionVersion": 0}]))
                .await?;
            let keys: Vec<&str> = tx["transaction"]["message"]["accountKeys"].as_array().into_iter().flatten().filter_map(Value::as_str).collect();
            for ix in tx["transaction"]["message"]["instructions"].as_array().into_iter().flatten() {
                let program = ix["programIdIndex"].as_u64().and_then(|i| keys.get(i as usize));
                if program.map(|p| from_base58(p).ok()) != Some(Some(self.program)) {
                    continue;
                }
                let Some(data) = ix["data"].as_str().and_then(|d| bs58::decode(d).into_vec().ok()) else {
                    continue;
                };
                // Claim = 2, Refund = 3, ClaimRelayed = 7: the secret follows the instruction byte
                let (Some(kind), Some(secret)) = (data.first(), data.get(1..33)) else {
                    continue;
                };
                let Ok(share) = SecretShare::from_ed25519_le(secret.try_into().expect("32 bytes")) else {
                    continue;
                };
                let evm = share.to_evm();
                match kind {
                    2 | 5 | 7 | 8 if secret_matches(&evm, swap.claimCommitment) => return Ok(Some(Revealed::Claimed(evm))),
                    3 | 6 if secret_matches(&evm, swap.refundCommitment) => return Ok(Some(Revealed::Refunded(evm))),
                    _ => {}
                }
            }
        }
        Ok(None)
    }

    async fn head(&self) -> Result<u64, DriverError> {
        self.rpc.call("getSlot", json!([{"commitment": "confirmed"}])).await?.as_u64().ok_or_else(|| sol_err("no slot"))
    }

    async fn sign(&self, call: &EvmCall) -> Result<Bytes, DriverError> {
        let me = self.account();
        let b = &self.binding;
        let token = from_base58(TOKEN_PROGRAM)?;
        let ata_program = from_base58(ATA_PROGRAM)?;
        // "Open this wallet's token account if it has none" (create idempotent), paid by us
        let open_account = |wallet: Pubkey, mint: Pubkey| -> Result<(Pubkey, Vec<Meta>, Vec<u8>), DriverError> {
            let ata = token_account(&wallet, &mint).ok_or_else(|| sol_err("no token account address"))?;
            Ok((ata_program, vec![meta(me, true, true), meta(ata, false, true), meta(wallet, false, false), meta(mint, false, false), meta(SYSTEM_PROGRAM, false, false), meta(token, false, false)], vec![1u8]))
        };
        let mut ixs: Vec<(Pubkey, Vec<Meta>, Vec<u8>)> = vec![];
        match call {
            EvmCall::NewSwap { terms, nonce } => {
                if me != b.owner || mint_of(self.chain, terms.asset)? != self.mint || u64::try_from(*nonce).ok() != Some(self.nonce) {
                    return Err(sol_err("this account does not lock this swap"));
                }
                let amount = lamports(terms.value)?;
                let mut data = vec![if self.mint.is_some() { 4u8 } else { 0u8 }];
                data.extend_from_slice(&b.claim_point);
                data.extend_from_slice(&b.refund_point);
                for word in [terms.duration1, terms.duration2, amount, self.nonce] {
                    data.extend_from_slice(&word.to_le_bytes());
                }
                let swap = self.swap_address(terms.value)?;
                let accounts = match self.mint {
                    None => vec![meta(me, true, true), meta(b.claimer, false, false), meta(swap, false, true), meta(SYSTEM_PROGRAM, false, false)],
                    Some(mint) => {
                        let ours = token_account(&me, &mint).ok_or_else(|| sol_err("no token account address"))?;
                        vec![meta(me, true, true), meta(b.claimer, false, false), meta(swap, false, true), meta(mint, false, false),
                             meta(ours, false, true), meta(self.vault(&swap)?, false, true), meta(token, false, false), meta(SYSTEM_PROGRAM, false, false)]
                    }
                };
                let lamports = self.rpc.call("getBalance", json!([to_base58(&me), {"commitment": "confirmed"}])).await?["value"].as_u64().unwrap_or(0);
                if self.sponsored() && lamports < crate::sponsor::SOL_LOCK_FRONT {
                    // We hold USDT but no SOL: a relay pays the fees and fronts the deposits, for USDT
                    let lock = (self.program, accounts, data);
                    return self.lock_through_sponsor(lock).await.map(Into::into);
                }
                ixs.push((self.program, accounts, data));
            }
            EvmCall::SetReady(swap) => ixs.push((self.program, vec![meta(me, true, true), meta(self.swap_address(swap.value)?, false, true)], vec![1u8])),
            EvmCall::Claim(swap, secret) => {
                let address = self.swap_address(swap.value)?;
                match self.mint {
                    None => {
                        let mut data = vec![2u8];
                        data.extend_from_slice(&program_secret(*secret));
                        ixs.push((self.program, vec![meta(me, true, true), meta(address, false, true), meta(b.owner, false, true)], data));
                    }
                    Some(mint) => {
                        // The tokens go to our own token account, opened first if we have none
                        ixs.push(open_account(me, mint)?);
                        let ours = token_account(&me, &mint).ok_or_else(|| sol_err("no token account address"))?;
                        let mut data = vec![5u8];
                        data.extend_from_slice(&program_secret(*secret));
                        ixs.push((self.program, vec![meta(me, true, true), meta(address, false, true), meta(ours, false, true), meta(self.vault(&address)?, false, true),
                                                    meta(b.owner, false, true), meta(mint, false, false), meta(token, false, false)], data));
                    }
                }
            }
            // A plain system transfer from the owner to the claimer's account, so a claimer
            // holding no SOL can pay for its claim. The claimer is the swap's bound claimer,
            // whatever address the driver names (EVM addresses do not exist here).
            EvmCall::GasAdvance { amount, .. } => {
                if me != b.owner {
                    return Err(sol_err("only the owner advances the claimer's fee"));
                }
                let mut data = 2u32.to_le_bytes().to_vec();
                data.extend_from_slice(&lamports(*amount)?.to_le_bytes());
                ixs.push((SYSTEM_PROGRAM, vec![meta(me, true, true), meta(b.claimer, false, true)], data));
            }
            // Solana token swaps need no approval: the owner signs the lock itself
            EvmCall::Approve { .. } => return Err(sol_err("no token approval on Solana")),
            EvmCall::Refund(swap, secret) => {
                let address = self.swap_address(swap.value)?;
                match self.mint {
                    None => {
                        let mut data = vec![3u8];
                        data.extend_from_slice(&program_secret(*secret));
                        ixs.push((self.program, vec![meta(me, true, true), meta(address, false, true)], data));
                    }
                    Some(mint) => {
                        ixs.push(open_account(me, mint)?);
                        let ours = token_account(&me, &mint).ok_or_else(|| sol_err("no token account address"))?;
                        let mut data = vec![6u8];
                        data.extend_from_slice(&program_secret(*secret));
                        ixs.push((self.program, vec![meta(me, true, true), meta(address, false, true), meta(ours, false, true), meta(self.vault(&address)?, false, true),
                                                    meta(mint, false, false), meta(token, false, false)], data));
                    }
                }
            }
        }
        let list: Vec<(&Pubkey, &[Meta], &[u8])> = ixs.iter().map(|(p, a, d)| (p, a.as_slice(), d.as_slice())).collect();
        let raw = signed(&self.key, message_many(&me, &list, &self.rpc.blockhash().await?));
        // As gas estimation on EVM: a call the program would refuse is not kept
        let text = base64::engine::general_purpose::STANDARD.encode(&raw);
        let sim = self.rpc.call("simulateTransaction", json!([text, {"encoding": "base64", "commitment": "confirmed"}])).await?;
        if !sim["value"]["err"].is_null() {
            return Err(DriverError::Chain(format!("the swap program would refuse this step: {} {}", sim["value"]["err"], sim["value"]["logs"])));
        }
        Ok(raw.into())
    }

    async fn replacement(&self, raw: &Bytes) -> Result<Option<Bytes>, DriverError> {
        // A Solana transaction dies with its blockhash (about a minute). The same instruction
        // with a fresh one replaces it, unless the old one made it after all.
        let Some((signature, at)) = parts(raw) else {
            return Ok(None);
        };
        if self.rpc.status(&signature).await? == Some(true) {
            return Ok(None);
        }
        if raw[0] == 2 {
            // A sponsored lock: the relay signs the fresh one too (it checks it again)
            let mut m = raw[129..].to_vec();
            m[at - 129..at - 129 + 32].copy_from_slice(&self.rpc.blockhash().await?);
            return self.sponsor_lock_message(m).await.map(|r| Some(r.into()));
        }
        let mut m = raw[65..].to_vec();
        m[at - 65..at - 65 + 32].copy_from_slice(&self.rpc.blockhash().await?);
        Ok(Some(signed(&self.key, m).into()))
    }

    async fn broadcast(&self, raw: &Bytes) -> Result<(), DriverError> {
        if let Some((signature, _)) = parts(raw) {
            if self.rpc.status(&signature).await?.is_some() {
                return Ok(());
            }
        }
        self.rpc.send(raw).await
    }

    async fn funds(&self) -> Result<Funds, DriverError> {
        let r = self.rpc.call("getBalance", json!([to_base58(&self.account()), {"commitment": "confirmed"}])).await?;
        Ok(Funds { balance: U256::from(r["value"].as_u64().unwrap_or(0)), fee_per_gas: U256::from(LAMPORTS_PER_GAS) })
    }

    fn relayed_claim(&self) -> bool {
        self.sponsored()
    }

    fn sponsored_lock(&self) -> bool {
        self.sponsored()
    }

    async fn relay_claim(&self, swap: &Swap, secret: B256) -> Result<bool, DriverError> {
        self.claim_through_sponsor(swap.value, secret).await
    }

    fn reserve(&self, owner: bool) -> U256 {
        // A token swap: the owner's vault deposit; the claimer's own token account deposit, and one
        // more for the token account its pay-out opens in the user's wallet (most new sellers have none)
        U256::from(match (self.mint.is_some(), owner) {
            (false, _) => 0,
            (true, true) => TOKEN_ACCOUNT_RENT,
            (true, false) => 2 * TOKEN_ACCOUNT_RENT,
        })
    }

    /// The claimer's balance, for the gas advance: always the swap's bound claimer
    async fn balance_of(&self, _who: Address) -> Result<U256, DriverError> {
        let r = self.rpc.call("getBalance", json!([to_base58(&self.binding.claimer), {"commitment": "confirmed"}])).await?;
        Ok(U256::from(r["value"].as_u64().unwrap_or(0)))
    }
}

/// The chain of a trade: an EVM chain or Solana, behind the one interface the driver uses.
pub enum AnyChain {
    Evm(crate::chain::AlloyChain),
    Sol(SolChain),
}

macro_rules! either {
    ($self:ident, $c:ident => $e:expr) => {
        match $self {
            AnyChain::Evm($c) => $e,
            AnyChain::Sol($c) => $e,
        }
    };
}

impl EvmChain for AnyChain {
    async fn now(&self) -> Result<u64, DriverError> {
        either!(self, c => c.now().await)
    }
    async fn find_lock(&self, terms: &AgreedTerms) -> Result<Option<ObservedLock>, DriverError> {
        either!(self, c => c.find_lock(terms).await)
    }
    async fn stage(&self, id: B256) -> Result<Stage, DriverError> {
        either!(self, c => c.stage(id).await)
    }
    async fn revealed(&self, swap: &Swap) -> Result<Option<Revealed>, DriverError> {
        either!(self, c => c.revealed(swap).await)
    }
    async fn head(&self) -> Result<u64, DriverError> {
        either!(self, c => c.head().await)
    }
    async fn sign(&self, call: &EvmCall) -> Result<Bytes, DriverError> {
        either!(self, c => c.sign(call).await)
    }
    async fn replacement(&self, raw: &Bytes) -> Result<Option<Bytes>, DriverError> {
        either!(self, c => c.replacement(raw).await)
    }
    async fn broadcast(&self, raw: &Bytes) -> Result<(), DriverError> {
        either!(self, c => c.broadcast(raw).await)
    }
    async fn funds(&self) -> Result<Funds, DriverError> {
        either!(self, c => c.funds().await)
    }
    async fn balance_of(&self, who: Address) -> Result<U256, DriverError> {
        either!(self, c => c.balance_of(who).await)
    }
    fn reserve(&self, owner: bool) -> U256 {
        either!(self, c => c.reserve(owner))
    }
    async fn allowance(&self, token: Address) -> Result<U256, DriverError> {
        either!(self, c => c.allowance(token).await)
    }
    fn relayed_claim(&self) -> bool {
        either!(self, c => c.relayed_claim())
    }
    fn sponsored_lock(&self) -> bool {
        either!(self, c => c.sponsored_lock())
    }
    async fn relay_claim(&self, swap: &Swap, secret: B256) -> Result<bool, DriverError> {
        either!(self, c => c.relay_claim(swap, secret).await)
    }
}

impl AnyChain {
    /// Relays that may pay this token trade's fees (a USDT trade); none for a coin trade
    pub fn with_sponsors(self, relays: Vec<String>) -> Self {
        match self {
            AnyChain::Evm(c) => AnyChain::Evm(c.with_sponsors(relays)),
            AnyChain::Sol(c) => AnyChain::Sol(c.with_sponsors(relays)),
        }
    }
}

#[cfg(test)]
#[path = "sol_tests.rs"]
mod tests;


// ---- fee sponsoring (see `sponsor`): the relay's side

const ED25519_PROGRAM: &str = "Ed25519SigVerify111111111111111111111111111";
const INSTRUCTIONS_SYSVAR: &str = "Sysvar1nstructions1111111111111111111111111";

/// The ed25519 program's check of one signature, all data inline (the layout the swap program reads)
fn ed25519_check(signer: &Pubkey, signature: &[u8; 64], message: &[u8]) -> Vec<u8> {
    let (pk, sig, msg) = (16u16, 48u16, 112u16);
    let mut d = vec![1u8, 0];
    for v in [sig, u16::MAX, pk, u16::MAX, msg, message.len() as u16, u16::MAX] {
        d.extend_from_slice(&v.to_le_bytes());
    }
    d.extend_from_slice(signer);
    d.extend_from_slice(signature);
    d.extend_from_slice(message);
    d
}

/// The relay's transaction for a seller's relayed USDT claim: open the seller's token account
/// (if it has none) and the relay's, check the seller's signature, claim; the relay pays the fees.
#[allow(clippy::too_many_arguments)]
pub fn sponsored_claim_tx(relayer: &SigningKey, program: &Pubkey, mint: &Pubkey, swap: &Pubkey, secret: &[u8; 32], fee: u64,
                          claimer: &Pubkey, owner: &Pubkey, signature: &[u8; 64], blockhash: &[u8; 32]) -> Result<Vec<u8>, DriverError> {
    let me = relayer.verifying_key().to_bytes();
    let (token, ata_program) = (from_base58(TOKEN_PROGRAM)?, from_base58(ATA_PROGRAM)?);
    let (ed, sysvar) = (from_base58(ED25519_PROGRAM)?, from_base58(INSTRUCTIONS_SYSVAR)?);
    let theirs = token_account(claimer, mint).ok_or_else(|| sol_err("no token account address"))?;
    let ours = token_account(&me, mint).ok_or_else(|| sol_err("no token account address"))?;
    let vault = find_program_address(&[b"vault", swap], program).map(|(k, _)| k).ok_or_else(|| sol_err("no vault address"))?;
    let open = |wallet: Pubkey, ata: Pubkey| vec![meta(me, true, true), meta(ata, false, true), meta(wallet, false, false), meta(*mint, false, false), meta(SYSTEM_PROGRAM, false, false), meta(token, false, false)];
    let message = crate::sponsor::sol_claim_message(swap, fee, &me);
    let mut claim = vec![8u8];
    claim.extend_from_slice(secret);
    claim.extend_from_slice(&fee.to_le_bytes());
    let ixs: Vec<(Pubkey, Vec<Meta>, Vec<u8>)> = vec![
        (ata_program, open(*claimer, theirs), vec![1u8]),
        (ata_program, open(me, ours), vec![1u8]),
        // The program reads the check just before its own instruction
        (ed, vec![], ed25519_check(claimer, signature, &message)),
        (*program, vec![meta(me, true, true), meta(*swap, false, true), meta(theirs, false, true), meta(ours, false, true), meta(vault, false, true),
                        meta(*owner, false, true), meta(*mint, false, false), meta(token, false, false), meta(sysvar, false, false)], claim),
    ];
    let list: Vec<(&Pubkey, &[Meta], &[u8])> = ixs.iter().map(|(p, a, d)| (p, a.as_slice(), d.as_slice())).collect();
    Ok(signed(relayer, message_many(&me, &list, blockhash)))
}

struct Parsed {
    signers: usize,
    readonly_signed: u8,
    keys: Vec<Pubkey>,
    ixs: Vec<(usize, Vec<usize>, Vec<u8>)>,
}

fn parse_message(m: &[u8]) -> Option<Parsed> {
    let (signers, readonly_signed) = (*m.first()? as usize, *m.get(1)?);
    let mut at = 3;
    let n = read_compact(m, &mut at)?;
    let mut keys = Vec::with_capacity(n);
    for _ in 0..n {
        keys.push(m.get(at..at + 32)?.try_into().ok()?);
        at += 32;
    }
    at += 32; // blockhash
    let count = read_compact(m, &mut at)?;
    let mut ixs = Vec::with_capacity(count);
    for _ in 0..count {
        let program = *m.get(at)? as usize;
        at += 1;
        let k = read_compact(m, &mut at)?;
        let accounts: Vec<usize> = m.get(at..at + k)?.iter().map(|b| *b as usize).collect();
        at += k;
        let len = read_compact(m, &mut at)?;
        let data = m.get(at..at + len)?.to_vec();
        at += len;
        if program >= keys.len() || accounts.iter().any(|a| *a >= keys.len()) {
            return None;
        }
        ixs.push((program, accounts, data));
    }
    (at == m.len()).then_some(Parsed { signers, readonly_signed, keys, ixs })
}

/// A buyer's sponsored lock as the relay accepts it, nothing else: exactly (1) the relay sends the
/// buyer at most `max_front` lamports, (2) the buyer locks USDT in the pinned program (NewSpl), (3)
/// the buyer pays the relay's token account at least `min_fee` USDT. The relay's account is the fee
/// payer and appears in no other instruction. Returns the buyer's account.
pub fn check_sponsored_lock(m: &[u8], relayer: &Pubkey, program: &Pubkey, mint: &Pubkey, max_front: u64, min_fee: u64) -> Result<Pubkey, String> {
    let p = parse_message(m).ok_or("not a transaction message")?;
    if p.signers != 2 || p.readonly_signed != 0 || p.keys.first() != Some(relayer) {
        return Err("the relay must be the fee payer and the buyer the only other signer".into());
    }
    let owner = p.keys[1];
    let key = |i: usize| p.keys[i];
    let (token, ours) = (from_base58(TOKEN_PROGRAM).map_err(|e| e.to_string())?, token_account(relayer, mint).ok_or("no token account")?);
    if p.ixs.len() != 3 {
        return Err("a sponsored lock has exactly three instructions".into());
    }
    let (front, lock, pay) = (&p.ixs[0], &p.ixs[1], &p.ixs[2]);
    // (1) the deposits the relay fronts
    if key(front.0) != SYSTEM_PROGRAM || front.1 != [0, 1] || front.2.len() != 12 || front.2[..4] != [2, 0, 0, 0] {
        return Err("the first instruction must be the relay's transfer to the buyer".into());
    }
    let lamports = u64::from_le_bytes(front.2[4..12].try_into().unwrap());
    if lamports > max_front {
        return Err(format!("asks for {lamports} lamports; at most {max_front}"));
    }
    // (2) the lock, by the buyer, of this token, in the pinned program; the relay not involved
    if key(lock.0) != *program || lock.2.len() != 97 || lock.2[0] != 4 || lock.1.len() != 8 || lock.1[0] != 1 || lock.1.contains(&0) || key(lock.1[3]) != *mint {
        return Err("the second instruction must be the buyer's USDT lock in the swap program".into());
    }
    // (3) the relay's fee, from the buyer, in this token, to the relay's token account
    if key(pay.0) != token || pay.2.len() != 10 || pay.2[0] != 12 || pay.2[9] != 6 || pay.1.len() != 4
        || key(pay.1[1]) != *mint || key(pay.1[2]) != ours || pay.1[3] != 1 || pay.1.contains(&0) {
        return Err("the third instruction must pay the relay's fee in USDT from the buyer".into());
    }
    let fee = u64::from_le_bytes(pay.2[1..9].try_into().unwrap());
    if fee < min_fee {
        return Err(format!("the fee is {fee}; this relay takes {min_fee}"));
    }
    Ok(owner)
}

/// The full transaction of a checked sponsored lock: the relay's signature first (fee payer), the buyer's
pub fn cosign(relayer: &SigningKey, message: &[u8], buyer_signature: &[u8; 64]) -> Vec<u8> {
    let mut tx = vec![2u8];
    tx.extend_from_slice(&relayer.sign(message).to_bytes());
    tx.extend_from_slice(buyer_signature);
    tx.extend_from_slice(message);
    tx
}

/// A transaction's simulation on the node: Ok when it would succeed (signatures checked)
pub async fn simulate(url: &str, raw: &[u8]) -> Result<(), String> {
    let rpc = Rpc::new(url).map_err(|e| e.to_string())?;
    let text = base64::engine::general_purpose::STANDARD.encode(raw);
    let sim = rpc.call("simulateTransaction", json!([text, {"encoding": "base64", "commitment": "confirmed", "sigVerify": true}])).await.map_err(|e| e.to_string())?;
    if sim["value"]["err"].is_null() { Ok(()) } else { Err(format!("{} {}", sim["value"]["err"], sim["value"]["logs"])) }
}

/// Send a signed transaction; its id
pub async fn send_raw(url: &str, raw: &[u8]) -> Result<String, String> {
    let rpc = Rpc::new(url).map_err(|e| e.to_string())?;
    rpc.send(raw).await.map_err(|e| e.to_string())?;
    transaction_id(raw).ok_or_else(|| "no signature".into())
}

pub async fn latest_blockhash(url: &str) -> Result<[u8; 32], String> {
    Rpc::new(url).map_err(|e| e.to_string())?.blockhash().await.map_err(|e| e.to_string())
}

/// Open the relay's own USDT account (once, before it is paid in USDT)
pub async fn open_own_token_account(url: &str, relayer: &SigningKey, mint: &Pubkey) -> Result<(), String> {
    let rpc = Rpc::new(url).map_err(|e| e.to_string())?;
    let me = relayer.verifying_key().to_bytes();
    let ours = token_account(&me, mint).ok_or("no token account")?;
    if rpc.account(&ours, "confirmed").await.map_err(|e| e.to_string())?.is_some() {
        return Ok(());
    }
    let token = from_base58(TOKEN_PROGRAM).map_err(|e| e.to_string())?;
    let ata = from_base58(ATA_PROGRAM).map_err(|e| e.to_string())?;
    let accounts = [meta(me, true, true), meta(ours, false, true), meta(me, false, false), meta(*mint, false, false), meta(SYSTEM_PROGRAM, false, false), meta(token, false, false)];
    let raw = signed(relayer, message_many(&me, &[(&ata, &accounts, &[1u8])], &rpc.blockhash().await.map_err(|e| e.to_string())?));
    rpc.send(&raw).await.map_err(|e| e.to_string())
}

/// The relay's Solana fee-payer key, from a file of 32 bytes in hex or a solana-keygen JSON keypair
pub fn relayer_key(text: &str) -> Result<SigningKey, String> {
    let t = text.trim();
    let seed: [u8; 32] = if t.starts_with('[') {
        let bytes: Vec<u8> = serde_json::from_str(t).map_err(|e| e.to_string())?;
        bytes.get(..32).ok_or("short keypair")?.try_into().map_err(|_| "bad keypair")?
    } else {
        hex::decode(t).map_err(|e| e.to_string())?.try_into().map_err(|_| "a key is 32 bytes")?
    };
    Ok(SigningKey::from_bytes(&seed))
}

#[cfg(test)]
mod sponsor_tests {
    use super::*;

    fn key(n: u8) -> Pubkey {
        SigningKey::from_bytes(&[n; 32]).verifying_key().to_bytes()
    }

    /// A lock message as the buyer's wallet builds it (`SolChain::lock_through_sponsor`)
    fn lock_message(relayer: Pubkey, owner: Pubkey, program: Pubkey, mint: Pubkey, front: u64, fee: u64, tamper: impl Fn(&mut Vec<(Pubkey, Vec<Meta>, Vec<u8>)>)) -> Vec<u8> {
        let token = from_base58(TOKEN_PROGRAM).unwrap();
        let (ours, theirs) = (token_account(&owner, &mint).unwrap(), token_account(&relayer, &mint).unwrap());
        let swap = key(40);
        let mut give = 2u32.to_le_bytes().to_vec();
        give.extend_from_slice(&front.to_le_bytes());
        let mut lock = vec![4u8];
        lock.extend_from_slice(&[7u8; 96]);
        let mut pay = vec![12u8];
        pay.extend_from_slice(&fee.to_le_bytes());
        pay.push(6);
        let mut ixs: Vec<(Pubkey, Vec<Meta>, Vec<u8>)> = vec![
            (SYSTEM_PROGRAM, vec![meta(relayer, true, true), meta(owner, false, true)], give),
            (program, vec![meta(owner, true, true), meta(key(41), false, false), meta(swap, false, true), meta(mint, false, false),
                           meta(ours, false, true), meta(key(42), false, true), meta(token, false, false), meta(SYSTEM_PROGRAM, false, false)], lock),
            (token, vec![meta(ours, false, true), meta(mint, false, false), meta(theirs, false, true), meta(owner, true, false)], pay),
        ];
        tamper(&mut ixs);
        let list: Vec<(&Pubkey, &[Meta], &[u8])> = ixs.iter().map(|(p, a, d)| (p, a.as_slice(), d.as_slice())).collect();
        message_signed_by(&[relayer, owner], &list, &[9u8; 32])
    }

    #[test]
    fn a_relay_signs_only_the_sponsored_lock_it_agreed_to() {
        let (relayer, owner, program, mint) = (key(1), key(2), key(3), key(4));
        let front = crate::sponsor::SOL_LOCK_FRONT;
        let ok = lock_message(relayer, owner, program, mint, front, 500_000, |_| {});
        assert_eq!(check_sponsored_lock(&ok, &relayer, &program, &mint, front, 400_000), Ok(owner));
        // Too small a fee, too much fronted
        assert!(check_sponsored_lock(&ok, &relayer, &program, &mint, front, 600_000).is_err());
        let greedy = lock_message(relayer, owner, program, mint, front + 1, 500_000, |_| {});
        assert!(check_sponsored_lock(&greedy, &relayer, &program, &mint, front, 400_000).is_err());
        // A fourth instruction (say, another transfer out of the relay's account)
        let extra = lock_message(relayer, owner, program, mint, front, 500_000, |ixs| {
            let mut d = 2u32.to_le_bytes().to_vec();
            d.extend_from_slice(&1_000_000u64.to_le_bytes());
            ixs.push((SYSTEM_PROGRAM, vec![meta(relayer, true, true), meta(key(66), false, true)], d));
        });
        assert!(check_sponsored_lock(&extra, &relayer, &program, &mint, front, 400_000).is_err());
        // The relay's account inside the lock, another program, another mint, the fee paid elsewhere
        let inside = lock_message(relayer, owner, program, mint, front, 500_000, |ixs| ixs[1].1[1] = meta(relayer, false, false));
        assert!(check_sponsored_lock(&inside, &relayer, &program, &mint, front, 400_000).is_err());
        let elsewhere_program = lock_message(relayer, owner, key(9), mint, front, 500_000, |_| {});
        assert!(check_sponsored_lock(&elsewhere_program, &relayer, &program, &mint, front, 400_000).is_err());
        let other_mint = lock_message(relayer, owner, program, key(10), front, 500_000, |_| {});
        assert!(check_sponsored_lock(&other_mint, &relayer, &program, &mint, front, 400_000).is_err());
        let fee_elsewhere = lock_message(relayer, owner, program, mint, front, 500_000, |ixs| ixs[2].1[2] = meta(key(77), false, true));
        assert!(check_sponsored_lock(&fee_elsewhere, &relayer, &program, &mint, front, 400_000).is_err());
        // The relay not as fee payer
        let swapped = lock_message(owner, relayer, program, mint, front, 500_000, |_| {});
        assert!(check_sponsored_lock(&swapped, &relayer, &program, &mint, front, 400_000).is_err());
    }

    #[test]
    fn a_relayed_claim_carries_the_sellers_permission_just_before_the_claim() {
        let relayer = SigningKey::from_bytes(&[1; 32]);
        let seller = SigningKey::from_bytes(&[2; 32]);
        let (program, mint, swap, owner) = (key(3), key(4), key(5), key(6));
        let claimer = seller.verifying_key().to_bytes();
        let permission = seller.sign(&crate::sponsor::sol_claim_message(&swap, 150_000, &relayer.verifying_key().to_bytes())).to_bytes();
        let raw = sponsored_claim_tx(&relayer, &program, &mint, &swap, &[8; 32], 150_000, &claimer, &owner, &permission, &[9; 32]).unwrap();
        assert_eq!(raw[0], 1, "only the relay signs");
        let p = parse_message(&raw[65..]).unwrap();
        assert_eq!(p.keys[0], relayer.verifying_key().to_bytes(), "the relay pays");
        let ed = from_base58(ED25519_PROGRAM).unwrap();
        let (check, claim) = (&p.ixs[2], &p.ixs[3]);
        assert_eq!(p.keys[check.0], ed);
        assert_eq!(p.keys[claim.0], program);
        assert_eq!(claim.2[0], 8);
        assert_eq!(u64::from_le_bytes(claim.2[33..41].try_into().unwrap()), 150_000);
        // The check covers the seller's key and the exact message the program rebuilds
        let d = &check.2;
        assert_eq!(&d[16..48], &claimer);
        assert_eq!(&d[48..112], &permission);
        assert_eq!(&d[112..], crate::sponsor::sol_claim_message(&swap, 150_000, &relayer.verifying_key().to_bytes()).as_slice());
        // The payout goes to the seller's own token account
        assert_eq!(p.keys[claim.1[2]], token_account(&claimer, &mint).unwrap());
    }
}
