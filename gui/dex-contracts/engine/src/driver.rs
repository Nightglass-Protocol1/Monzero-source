//! Runs one XMZ <-> BNB swap: polls both chains into a [`Snapshot`], steps the swap states,
//! and carries out the actions.
//!
//! Recovery rules, as in the other pairs' journals:
//!  - the next state is saved before any action runs, so a crash replays the same step;
//!  - every EVM transaction is signed once and kept in the journal **before** it is
//!    broadcast; later ticks rebroadcast the identical transaction, never a new call;
//!  - a transaction still unmined after `Chain::bump_after_blocks` is replaced by the same
//!    call with the same nonce and higher fees (kept before broadcast too). Any version may be
//!    mined; the nonce makes sure only one is;
//!  - signing goes through gas estimation, which runs the call first. A claim or refund
//!    that would fail on chain is therefore never signed, so it cannot leak our secret.
//!
//! The XMZ side (`XmzSide`) is the existing XMZ wallet code: it keeps its own record of
//! the lock and sweep it sent.

use crate::machine::{claimer_step, owner_step, Action, ClaimerState, OwnerState, Snapshot, SwapData, Trade, XmzLock};
use crate::exchange::SavedTrade;
use crate::secret::{combined_spend_key, SecretShare};
use crate::swap_seed::SwapSeed;
use alloy::signers::local::PrivateKeySigner;
use crate::{check_lock, swap_id, AgreedTerms, ObservedLock, Revealed, Stage, Swap};
use alloy::primitives::{Address, Bytes, B256, U256};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, thiserror::Error)]
pub enum DriverError {
    #[error("EVM chain: {0}")]
    Chain(String),
    #[error("XMZ side: {0}")]
    Xmz(String),
    #[error("journal: {0}")]
    Journal(String),
    #[error("our secret does not match the agreed commitment")]
    SecretMismatch,
    #[error("saved trade: {0}")]
    Agreement(String),
    /// The next step would lock our funds and the user has not approved this trade yet
    #[error("waiting for the user to approve locking funds for this trade")]
    AwaitingApproval,
    /// The next step would lock funds, but our EVM account cannot yet pay for it and for the
    /// later transactions that protect those funds (wei). Nothing was locked; send at least
    /// `needed - have` to the account (e.g. from MetaMask) and step again.
    #[error("waiting for funding: the account holds {have} wei and needs {needed} wei")]
    AwaitingFunding { needed: U256, have: U256 },
    #[error(transparent)]
    Swap(#[from] crate::Error),
}

/// What our EVM account holds and what gas costs now (wei).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Funds {
    pub balance: U256,
    pub fee_per_gas: U256,
}

/// Generous gas per contract call (newSwap, setReady, claim, refund each use well under this);
/// the reserve also doubles it for fee bumps on stuck transactions.
pub const GAS_PER_CALL: u64 = 250_000;

/// What our account must hold before a step that locks funds: the owner's own BNB/ETH lock
/// (native asset) plus gas for newSwap, setReady and a refund; the claimer, gas for its claim,
/// checked before it locks XMZ so it can always collect what it is owed.
pub fn funding_needed(terms: &AgreedTerms, owner: bool, fee_per_gas: U256) -> U256 {
    let calls: u64 = if owner { 3 } else { 1 };
    let gas = U256::from(GAS_PER_CALL * calls * 2) * fee_per_gas;
    if owner && terms.asset == Address::ZERO { terms.value + gas } else { gas }
}

/// A contract call the driver asks the chain to sign.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EvmCall {
    NewSwap { terms: AgreedTerms, nonce: U256 },
    SetReady(Swap),
    Claim(Swap, B256),
    Refund(Swap, B256),
    /// A plain payment from the owner to the claimer's account, so a claimer who holds none
    /// of the chain's coin can pay for its claim (see `GasAdvance`)
    GasAdvance { to: Address, amount: U256 },
    /// Let the swap contract take `amount` of `token` from us (a BEP-20 USDT lock needs it first)
    Approve { token: Address, amount: U256 },
}

/// Who pays the gas for the claimer's claim when the claimer's account is empty.
/// `Give` (owner): before `setReady`, top the claimer's account up to the cost of a claim.
/// `Expect` (claimer): lock XMZ without holding gas, relying on the owner's advance. If no
/// advance comes the claimer cannot claim, the owner refunds after the second deadline and
/// the claimer takes its XMZ back: time is lost, funds are not.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum GasAdvance {
    #[default]
    Off,
    Give,
    Expect,
}

/// The EVM chain as the driver needs it.
#[allow(async_fn_in_trait)]
pub trait EvmChain {
    /// Timestamp of the newest block: the clock the contract's deadlines run on.
    async fn now(&self) -> Result<u64, DriverError>;
    /// The owner's `New` event for these terms, once it has enough confirmations.
    async fn find_lock(&self, terms: &AgreedTerms) -> Result<Option<ObservedLock>, DriverError>;
    /// `swaps(id)` at the newest block with enough confirmations.
    async fn stage(&self, id: B256) -> Result<Stage, DriverError>;
    /// A secret revealed for this swap, at any depth (knowing it is enough).
    async fn revealed(&self, swap: &Swap) -> Result<Option<Revealed>, DriverError>;
    /// Number of the newest block.
    async fn head(&self) -> Result<u64, DriverError>;
    /// Sign the call (after gas estimation has run it successfully).
    async fn sign(&self, call: &EvmCall) -> Result<Bytes, DriverError>;
    /// The same transaction (same call and nonce) with higher fees, while it is unmined.
    /// None once its nonce is used, or when the fee cap leaves no room for a replacement.
    async fn replacement(&self, raw: &Bytes) -> Result<Option<Bytes>, DriverError>;
    /// Broadcast a signed transaction; already known or mined is not an error.
    async fn broadcast(&self, raw: &Bytes) -> Result<(), DriverError>;
    /// Our account's balance and the current maximum fee per gas (wei)
    async fn funds(&self) -> Result<Funds, DriverError>;
    /// What another account holds (wei). Chains without gas advances need not answer.
    async fn balance_of(&self, _who: Address) -> Result<U256, DriverError> {
        Err(DriverError::Chain("this chain has no gas advance".into()))
    }
    /// What a side needs besides gas, in the chain's coin: on Solana a token swap opens a token
    /// vault (the owner) and a token account (the claimer), each holding a deposit
    fn reserve(&self, _owner: bool) -> U256 {
        U256::ZERO
    }
    /// How much of `token` the swap contract may take from us (no approval needed: everything)
    async fn allowance(&self, _token: Address) -> Result<U256, DriverError> {
        Ok(U256::MAX)
    }
    /// A relay can claim for us, paid in the swap's token (a USDT trade): we need none of the coin
    fn relayed_claim(&self) -> bool {
        false
    }
    /// A relay can pay for our lock, paid in the swap's token (USDT on Solana)
    fn sponsored_lock(&self) -> bool {
        false
    }
    /// Have a relay claim for us; Ok(false) when none would
    async fn relay_claim(&self, _swap: &Swap, _secret: B256) -> Result<bool, DriverError> {
        Ok(false)
    }
}

/// The XMZ wallet side of the swap.
#[allow(async_fn_in_trait)]
pub trait XmzSide {
    /// XMZ received at the shared address (watched with the shared view key).
    async fn lock_status(&self) -> Result<Option<XmzLock>, DriverError>;
    /// Make sure `amount` has been sent to the shared address (sent once, then retried).
    async fn ensure_lock(&self, amount: u64) -> Result<(), DriverError>;
    /// Make sure the shared output is swept to our wallet with this private spend key.
    async fn ensure_sweep(&self, spend_key: [u8; 32]) -> Result<(), DriverError>;
    /// Our sweep is confirmed.
    async fn swept(&self) -> Result<bool, DriverError>;
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub enum RoleState {
    Owner(OwnerState),
    Claimer(ClaimerState),
}

impl RoleState {
    /// Short stage name for the app, e.g. "Locked" or "Done(Swapped)": never the data a
    /// state carries (a revealed share, swap terms)
    pub fn stage(&self) -> String {
        let debug = match self {
            RoleState::Owner(OwnerState::Done(o)) | RoleState::Claimer(ClaimerState::Done(o)) => return format!("Done({o:?})"),
            RoleState::Owner(s) => format!("{s:?}"),
            RoleState::Claimer(s) => format!("{s:?}"),
        };
        debug.split([' ', '{', '(']).next().unwrap_or_default().to_string()
    }

    pub fn is_done(&self) -> bool {
        matches!(self, RoleState::Owner(OwnerState::Done(_)) | RoleState::Claimer(ClaimerState::Done(_)))
    }
}

/// One signed version of a transaction.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Signed {
    pub raw: Bytes,
    /// Newest block when it was signed
    pub block: u64,
}

/// What the journal keeps for one swap.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Record {
    pub state: RoleState,
    /// Signed EVM transactions by kind ("new_swap", "set_ready", "claim", "refund"): every
    /// version, the newest last
    pub retained: BTreeMap<String, Vec<Signed>>,
    /// The agreed trade, written before anything is locked (`Driver::start`)
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub trade: Option<SavedTrade>,
    /// The saved trade's transcript, once the user approved locking our funds for it
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub approved: Option<B256>,
}

impl Record {
    pub fn new(state: RoleState) -> Self {
        Record { state, retained: BTreeMap::new(), trade: None, approved: None }
    }
}

pub trait Journal {
    fn load(&self) -> Result<Record, DriverError>;
    fn save(&mut self, record: &Record) -> Result<(), DriverError>;
}

/// Record the user's approval of the journal's saved trade (see `Driver::approve`); works on
/// the journal alone, so the app can approve without a chain connection.
pub fn approve_journal<J: Journal>(journal: &mut J, transcript: B256) -> Result<(), DriverError> {
    let mut record = journal.load()?;
    let saved = record.trade.as_ref().ok_or_else(|| DriverError::Journal("no saved trade to approve".into()))?;
    if saved.transcript != transcript {
        return Err(DriverError::Agreement("approval for another trade".into()));
    }
    record.approved = Some(transcript);
    journal.save(&record)
}

/// Journal kept in memory (tests).
#[derive(Clone, Debug)]
pub struct MemoryJournal(pub Record);

impl Journal for MemoryJournal {
    fn load(&self) -> Result<Record, DriverError> {
        Ok(self.0.clone())
    }

    fn save(&mut self, record: &Record) -> Result<(), DriverError> {
        self.0 = record.clone();
        Ok(())
    }
}

/// Journal in a JSON file, replaced atomically (write to a temporary file, then rename).
/// It holds signed transactions but no secret: the secret share stays in the wallet's vault.
pub struct FileJournal(pub std::path::PathBuf);

impl Journal for FileJournal {
    fn load(&self) -> Result<Record, DriverError> {
        let text = std::fs::read_to_string(&self.0).map_err(|e| DriverError::Journal(e.to_string()))?;
        serde_json::from_str(&text).map_err(|e| DriverError::Journal(e.to_string()))
    }

    fn save(&mut self, record: &Record) -> Result<(), DriverError> {
        let text = serde_json::to_string_pretty(record).map_err(|e| DriverError::Journal(e.to_string()))?;
        durable_write(&self.0, text.as_bytes()).map_err(|e| DriverError::Journal(e.to_string()))
    }
}

/// Replace `path` with `bytes` so that a power cut leaves either the old or the new file:
/// write a temporary file, flush it to disk, rename it over `path`, then flush the directory
/// so the rename itself is on disk before we act on the new state.
pub fn durable_write(path: &std::path::Path, bytes: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    let tmp = path.with_extension("tmp");
    let mut file = std::fs::File::create(&tmp)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    drop(file);
    std::fs::rename(&tmp, path)?;
    let dir = path.parent().filter(|d| !d.as_os_str().is_empty()).unwrap_or(std::path::Path::new("."));
    std::fs::File::open(dir)?.sync_all()
}

pub struct Driver<C, X, J> {
    trade: Trade,
    secret: SecretShare,
    /// The owner's `newSwap` nonce (unused by the claimer)
    swap_nonce: U256,
    chain: C,
    xmz: X,
    journal: J,
    gas_advance: GasAdvance,
}

impl<C: EvmChain, X: XmzSide, J: Journal> Driver<C, X, J> {
    /// Low level (tests): the app uses `start` and `resume`, which keep the agreed trade in
    /// the journal. The journal must already hold the starting state (`OwnerState::Start` or
    /// `ClaimerState::WaitingForEvmLock`). Our secret must open our side's commitment.
    pub fn new(trade: Trade, secret: SecretShare, swap_nonce: U256, chain: C, xmz: X, journal: J) -> Result<Self, DriverError> {
        let ours = match journal.load()?.state {
            RoleState::Owner(_) => trade.terms.refund_commitment,
            RoleState::Claimer(_) => trade.terms.claim_commitment,
        };
        if secret.commitment() != ours {
            return Err(DriverError::SecretMismatch);
        }
        Ok(Driver { trade, secret, swap_nonce, chain, xmz, journal, gas_advance: GasAdvance::Off })
    }

    /// A new trade: the saved trade goes into the journal before the driver can act, so a
    /// restart can always rebuild it (`resume`). Starting again with the same saved trade is
    /// a resume; a journal holding another trade is refused.
    pub fn start(saved: SavedTrade, seed: &SwapSeed, account: &PrivateKeySigner, chain: C, xmz: X, mut journal: J) -> Result<Self, DriverError> {
        let agreed = saved.agreed(seed, account).map_err(|e| DriverError::Agreement(e.to_string()))?;
        match journal.load() {
            Ok(record) if record.trade.as_ref() == Some(&saved) => {}
            Ok(_) => return Err(DriverError::Journal("the journal holds another trade".into())),
            Err(_) => {
                let state = match saved.role {
                    crate::negotiation::Role::Owner => RoleState::Owner(OwnerState::Start),
                    crate::negotiation::Role::Claimer => RoleState::Claimer(ClaimerState::WaitingForEvmLock),
                };
                journal.save(&Record { trade: Some(saved.clone()), ..Record::new(state) })?;
            }
        }
        Driver::new(saved.trade(&agreed), saved.secret(seed), saved.swap_nonce(), chain, xmz, journal)
    }

    /// The trade in the journal, rebuilt from it and the swap seed after a restart. The chain
    /// must search logs from the saved `evm_from_block` (read it with `journal.load()`).
    pub fn resume(seed: &SwapSeed, account: &PrivateKeySigner, chain: C, xmz: X, journal: J) -> Result<Self, DriverError> {
        let saved = journal.load()?.trade.ok_or_else(|| DriverError::Journal("no saved trade in the journal".into()))?;
        let agreed = saved.agreed(seed, account).map_err(|e| DriverError::Agreement(e.to_string()))?;
        Driver::new(saved.trade(&agreed), saved.secret(seed), saved.swap_nonce(), chain, xmz, journal)
    }

    /// The user approved locking our funds for this trade: `transcript` must be the saved
    /// trade's (what the app showed). Steps that protect funds (setReady, claim, refund,
    /// sweep) never wait for approval; only our first lock does.
    pub fn approve(&mut self, transcript: B256) -> Result<(), DriverError> {
        approve_journal(&mut self.journal, transcript)
    }

    /// How the claimer's claim gas is paid on this trade (a setting of this run, not saved)
    pub fn with_gas_advance(mut self, mode: GasAdvance) -> Self {
        self.gas_advance = mode;
        self
    }

    pub fn journal(&self) -> &J {
        &self.journal
    }

    /// One round: observe, step, save, act. Returns the new state.
    pub async fn tick(&mut self) -> Result<RoleState, DriverError> {
        let mut record = self.journal.load()?;
        let (snap, xmz_down) = self.snapshot(&record.state).await?;
        let (next, actions) = match &record.state {
            RoleState::Owner(s) => {
                let (n, a) = owner_step(s, &self.trade, &snap);
                (RoleState::Owner(n), a)
            }
            RoleState::Claimer(s) => {
                let (n, a) = claimer_step(s, &self.trade, &snap);
                (RoleState::Claimer(n), a)
            }
        };
        // The private coin's wallet or node did not answer. The owner's refund does not depend on
        // it: the times and the contract alone decide it, and the step takes the same refund with
        // or without a view of the other coin. Everything else waits for the wallet.
        if let Some(e) = xmz_down {
            let refunding = matches!(next, RoleState::Owner(OwnerState::RefundSent { .. } | OwnerState::Done(crate::machine::Outcome::Refunded)))
                && actions.iter().all(|a| matches!(a, Action::SendRefund(_)));
            if !refunding {
                return Err(e);
            }
        }
        // A trade started with `start` locks our funds only after the user approved it
        let locks = actions.iter().any(|a| matches!(a, Action::SendNewSwap | Action::LockXmz { .. }));
        if locks && record.trade.as_ref().is_some_and(|t| record.approved != Some(t.transcript)) {
            return Err(DriverError::AwaitingApproval);
        }
        // Nothing is locked until our account can pay for the lock and for what protects it. Only
        // before the first lock is signed: afterwards the value is already spent, and the lock is
        // only rebroadcast until it has enough confirmations (on Ethereum, about 19 minutes).
        let first_lock = actions.iter().any(|a| match a {
            Action::SendNewSwap => !record.retained.contains_key("new_swap"),
            Action::LockXmz { .. } => true,
            _ => false,
        });
        if first_lock {
            let funds = self.chain.funds().await?;
            let owner = matches!(record.state, RoleState::Owner(_));
            let claim_gas = funding_needed(&self.trade.terms, false, funds.fee_per_gas) + self.chain.reserve(false);
            let needed = match (owner, self.gas_advance) {
                // USDT with a relay paying the fees: the coin is not needed (the tokens are checked
                // before the offer is made or taken)
                (true, _) if self.chain.sponsored_lock() => U256::ZERO,
                (false, _) if self.chain.relayed_claim() => U256::ZERO,
                // The owner also holds what it may have to advance
                (true, GasAdvance::Give) => funding_needed(&self.trade.terms, true, funds.fee_per_gas) + self.chain.reserve(true) + claim_gas,
                // The claimer counts on the advance
                (false, GasAdvance::Expect) => U256::ZERO,
                _ => funding_needed(&self.trade.terms, owner, funds.fee_per_gas) + self.chain.reserve(owner),
            };
            if funds.balance < needed {
                return Err(DriverError::AwaitingFunding { needed, have: funds.balance });
            }
        }
        record.state = next.clone();
        self.journal.save(&record)?;
        for action in actions {
            self.execute(action).await?;
        }
        Ok(next)
    }

    /// What both chains show now. When the private coin's side fails, the snapshot is still
    /// returned, without a lock or sweep seen, together with the error.
    async fn snapshot(&self, state: &RoleState) -> Result<(Snapshot, Option<DriverError>), DriverError> {
        let now = self.chain.now().await?;
        let evm_lock = self.chain.find_lock(&self.trade.terms).await?;
        // The swap: from our state once known, else from the observed lock. A lock that does
        // not match the terms is still read, so an owner can refund its own mistaken lock.
        let swap: Option<Swap> = known_swap(state).map(|s| (&s).into()).or_else(|| {
            evm_lock.as_ref().map(|lock| check_lock(&self.trade.terms, lock).unwrap_or_else(|_| raw_swap(lock)))
        });
        let (stage, revealed) = match &swap {
            Some(swap) => (Some(self.chain.stage(swap_id(swap)).await?), self.chain.revealed(swap).await?),
            None => (None, None),
        };
        let xmz = async { Ok::<_, DriverError>((self.xmz.lock_status().await?, self.xmz.swept().await?)) }.await;
        let (xmz_lock, xmz_swept, down) = match xmz {
            Ok((lock, swept)) => (lock, swept, None),
            Err(e) => (None, false, Some(e)),
        };
        Ok((Snapshot { now, evm_lock, stage, revealed, xmz_lock, xmz_swept }, down))
    }

    async fn execute(&mut self, action: Action) -> Result<(), DriverError> {
        match action {
            Action::SendNewSwap => {
                // A token lock: the contract must be allowed to take the tokens first. The approval
                // is kept and sent again like any step; the lock follows on a later tick, once the
                // chain shows the allowance (the lock's own gas estimate needs it)
                let terms = &self.trade.terms;
                if terms.asset != Address::ZERO && !self.journal.load()?.retained.contains_key("new_swap") && self.chain.allowance(terms.asset).await? < terms.value {
                    return self.evm("approve", EvmCall::Approve { token: terms.asset, amount: terms.value }).await;
                }
                let call = EvmCall::NewSwap { terms: self.trade.terms.clone(), nonce: self.swap_nonce };
                self.evm("new_swap", call).await
            }
            Action::SendSetReady(swap) => {
                if self.gas_advance == GasAdvance::Give {
                    // An advance that cannot be sent now (fees jumped past what we hold, the
                    // node refused it) must not hold `setReady` back: the swap goes on, and the
                    // claimer can still pay for its own claim. Tried again on the next round.
                    let _ = self.advance_gas().await;
                }
                self.evm("set_ready", EvmCall::SetReady((&swap).into())).await
            }
            Action::SendRefund(swap) => self.evm("refund", EvmCall::Refund((&swap).into(), self.secret.to_evm())).await,
            Action::SendClaim(swap) if self.chain.relayed_claim() => {
                let swap: Swap = (&swap).into();
                match self.claim_route().await? {
                    ClaimRoute::Own => self.evm_claim(swap).await,
                    // A relay was asked a moment ago: its claim may be on its way
                    ClaimRoute::Wait => Ok(()),
                    ClaimRoute::Relay => {
                        if self.chain.relay_claim(&swap, self.secret.to_evm()).await? {
                            self.note_relayed_claim()
                        } else {
                            // No relay would: claim ourselves, which asks for the coin to be paid in
                            self.evm_claim(swap).await
                        }
                    }
                }
            }
            Action::SendClaim(swap) => self.evm_claim((&swap).into()).await,
            Action::LockXmz { amount } => self.xmz.ensure_lock(amount).await,
            Action::SweepXmz { their_secret } => {
                let theirs = SecretShare::from_evm(their_secret)?;
                self.xmz.ensure_sweep(combined_spend_key(&self.secret, &theirs)).await
            }
        }
    }

    /// How to claim: ourselves when our account can pay for it, else through a relay (asked again
    /// only a minute after the last time, while its claim may still be on its way)
    async fn claim_route(&mut self) -> Result<ClaimRoute, DriverError> {
        let funds = self.chain.funds().await?;
        let needed = funding_needed(&self.trade.terms, false, funds.fee_per_gas) + self.chain.reserve(false);
        if funds.balance >= needed {
            return Ok(ClaimRoute::Own);
        }
        let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
        let asked = self.journal.load()?.retained.get("relayed_claim").and_then(|v| v.last()).map(|r| r.block);
        Ok(if asked.is_some_and(|at| now < at + 60) { ClaimRoute::Wait } else { ClaimRoute::Relay })
    }

    fn note_relayed_claim(&mut self) -> Result<(), DriverError> {
        let mut record = self.journal.load()?;
        let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
        record.retained.insert("relayed_claim".into(), vec![Signed { raw: Bytes::new(), block: now }]);
        self.journal.save(&record)
    }

    async fn evm_claim(&mut self, swap: Swap) -> Result<(), DriverError> {
        match self.evm("claim", EvmCall::Claim(swap, self.secret.to_evm())).await {
            // A claim the account cannot pay for (no advance came) asks for the coin to be paid
            // in, as the lock's funding rule does, rather than failing with the chain's error
            Err(e) => {
                let funds = self.chain.funds().await?;
                let needed = funding_needed(&self.trade.terms, false, funds.fee_per_gas) + self.chain.reserve(false);
                if funds.balance < needed { Err(DriverError::AwaitingFunding { needed, have: funds.balance }) } else { Err(e) }
            }
            ok => ok,
        }
    }

    /// The claimer's account topped up to the cost of its claim, once per trade: decided when
    /// first reached (nothing is sent to an account that already holds enough), then kept and
    /// rebroadcast like every other transaction of the trade.
    async fn advance_gas(&mut self) -> Result<(), DriverError> {
        let to = self.trade.terms.claimer;
        let record = self.journal.load()?;
        if record.retained.contains_key("gas_advance") {
            return self.evm("gas_advance", EvmCall::GasAdvance { to, amount: U256::ZERO }).await;
        }
        if record.retained.contains_key("set_ready") {
            return Ok(());
        }
        let needed = funding_needed(&self.trade.terms, false, self.chain.funds().await?.fee_per_gas) + self.chain.reserve(false);
        let have = self.chain.balance_of(to).await?;
        if have >= needed {
            return Ok(());
        }
        self.evm("gas_advance", EvmCall::GasAdvance { to, amount: needed - have }).await
    }

    /// Sign once and keep it, replace it with higher fees when it is stuck, then
    /// (re)broadcast the newest kept version.
    async fn evm(&mut self, kind: &str, call: EvmCall) -> Result<(), DriverError> {
        let mut record = self.journal.load()?;
        let head = self.chain.head().await?;
        let newest = match record.retained.get(kind).and_then(|v| v.last()) {
            None => Some(self.chain.sign(&call).await?),
            Some(last) if head >= last.block + self.trade.terms.chain.bump_after_blocks() => self.chain.replacement(&last.raw).await?,
            Some(_) => None,
        };
        if let Some(raw) = newest {
            record.retained.entry(kind.to_string()).or_default().push(Signed { raw, block: head });
            self.journal.save(&record)?;
        }
        let raw = &record.retained[kind].last().expect("kept above").raw;
        self.chain.broadcast(raw).await
    }
}

fn known_swap(state: &RoleState) -> Option<SwapData> {
    use ClaimerState as C;
    use OwnerState as O;
    match state {
        RoleState::Owner(O::Locked { swap } | O::ReadySent { swap } | O::Ready { swap } | O::RefundSent { swap }) => Some(swap.clone()),
        RoleState::Claimer(C::XmzLockSent { swap } | C::XmzLocked { swap } | C::ClaimSent { swap } | C::WaitingForRefund { swap }) => {
            Some(swap.clone())
        }
        _ => None,
    }
}

fn raw_swap(lock: &ObservedLock) -> Swap {
    let e = &lock.log.inner.data;
    Swap {
        owner: e.owner,
        claimer: e.claimer,
        claimCommitment: e.claimCommitment,
        refundCommitment: e.refundCommitment,
        timeout1: e.timeout1,
        timeout2: e.timeout2,
        asset: e.asset,
        value: e.value,
        nonce: e.nonce,
    }
}

/// How the claimer's claim goes out this tick
enum ClaimRoute {
    /// From our own account (it can pay the fee)
    Own,
    /// Through a relay, paid in the swap's token
    Relay,
    /// A relay was asked a moment ago
    Wait,
}
