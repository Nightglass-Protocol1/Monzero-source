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
        // Set when the owner has deployed the program for the public opening
        Chain::SolMainnet => return None,
        Chain::SolLocal => return LOCAL_PROGRAM.get().copied(),
        _ => return None,
    };
    from_base58(text).ok()
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
pub async fn pay_out(url: &str, key: &SigningKey, to: &Pubkey) -> Result<Option<(String, u64)>, DriverError> {
    let rpc = Rpc::new(url)?;
    let me = key.verifying_key().to_bytes();
    let balance = rpc.call("getBalance", json!([to_base58(&me), {"commitment": "confirmed"}])).await?["value"].as_u64().unwrap_or(0);
    if balance <= FEE_LAMPORTS || to == &me {
        return Ok(None);
    }
    let amount = balance - FEE_LAMPORTS;
    let mut data = 2u32.to_le_bytes().to_vec();
    data.extend_from_slice(&amount.to_le_bytes());
    let raw = signed(key, message(&me, &SYSTEM_PROGRAM, &[meta(me, true, true), meta(*to, false, true)], &data, &rpc.blockhash().await?));
    let id = bs58::encode(&raw[1..65]).into_string();
    rpc.send(&raw).await?;
    Ok(Some((id, amount)))
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
fn message(payer: &Pubkey, program: &Pubkey, accounts: &[Meta], data: &[u8], blockhash: &[u8; 32]) -> Vec<u8> {
    // Order: the signer, other writable accounts, read-only accounts (the program last)
    let mut keys: Vec<(Pubkey, bool)> = vec![(*payer, true)];
    for writable in [true, false] {
        for a in accounts.iter().filter(|a| a.writable == writable && !a.signer) {
            if !keys.iter().any(|(k, _)| k == &a.key) {
                keys.push((a.key, writable));
            }
        }
    }
    if !keys.iter().any(|(k, _)| k == program) {
        keys.push((*program, false));
    }
    let readonly = keys.iter().filter(|(_, w)| !w).count() as u8;
    let mut m = vec![1, 0, readonly];
    compact(keys.len(), &mut m);
    for (k, _) in &keys {
        m.extend_from_slice(k);
    }
    m.extend_from_slice(blockhash);
    compact(1, &mut m);
    let index = |key: &Pubkey| keys.iter().position(|(k, _)| k == key).expect("listed above") as u8;
    m.push(index(program));
    compact(accounts.len(), &mut m);
    for a in accounts {
        m.push(index(&a.key));
    }
    compact(data.len(), &mut m);
    m.extend_from_slice(data);
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
    if raw.first() != Some(&1) || raw.len() < 65 + 3 {
        return None;
    }
    let signature: [u8; 64] = raw[1..65].try_into().ok()?;
    let mut at = 65 + 3;
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
        let http = reqwest::Client::builder().timeout(std::time::Duration::from_secs(30)).build().map_err(sol_err)?;
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
        // bump at 153, mint at 154..186
        native: data[186] == 0 && data[154..186] == [0u8; 32],
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
    /// The trade's lamports: with the binding and the nonce it fixes the swap account's address
    amount: u64,
    rpc: Rpc,
}

impl SolChain {
    /// `binding`, `nonce` and `value` belong to one trade: this chain view serves that trade only.
    pub fn new(url: &str, chain: Chain, program: Pubkey, key: SigningKey, binding: SolBinding, nonce: u64, value: U256) -> Result<Self, DriverError> {
        let amount = lamports(value)?;
        if !chain.is_solana() {
            return Err(sol_err("not a Solana chain"));
        }
        Ok(SolChain { chain, program, contract: contract_address(&program), key, binding, nonce, amount, rpc: Rpc::new(url)? })
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
        if terms.asset != Address::ZERO {
            return Err(sol_err("only native SOL swaps"));
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
            asset: stands_for(s.native, Address::ZERO),
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
                    2 | 7 if secret_matches(&evm, swap.claimCommitment) => return Ok(Some(Revealed::Claimed(evm))),
                    3 if secret_matches(&evm, swap.refundCommitment) => return Ok(Some(Revealed::Refunded(evm))),
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
        let (accounts, data) = match call {
            EvmCall::NewSwap { terms, nonce } => {
                if me != b.owner || terms.asset != Address::ZERO || u64::try_from(*nonce).ok() != Some(self.nonce) {
                    return Err(sol_err("this account does not lock this swap"));
                }
                let amount = lamports(terms.value)?;
                let mut data = vec![0u8];
                data.extend_from_slice(&b.claim_point);
                data.extend_from_slice(&b.refund_point);
                for word in [terms.duration1, terms.duration2, amount, self.nonce] {
                    data.extend_from_slice(&word.to_le_bytes());
                }
                let swap = self.swap_address(terms.value)?;
                (vec![meta(me, true, true), meta(b.claimer, false, false), meta(swap, false, true), meta(SYSTEM_PROGRAM, false, false)], data)
            }
            EvmCall::SetReady(swap) => (vec![meta(me, true, true), meta(self.swap_address(swap.value)?, false, true)], vec![1u8]),
            EvmCall::Claim(swap, secret) => {
                let mut data = vec![2u8];
                data.extend_from_slice(&program_secret(*secret));
                (vec![meta(me, true, true), meta(self.swap_address(swap.value)?, false, true), meta(b.owner, false, true)], data)
            }
            EvmCall::Refund(swap, secret) => {
                let mut data = vec![3u8];
                data.extend_from_slice(&program_secret(*secret));
                (vec![meta(me, true, true), meta(self.swap_address(swap.value)?, false, true)], data)
            }
        };
        let raw = signed(&self.key, message(&me, &self.program, &accounts, &data, &self.rpc.blockhash().await?));
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
}

#[cfg(test)]
#[path = "sol_tests.rs"]
mod tests;
