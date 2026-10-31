//! EVM side of the XMZ <-> BNB / ETH atomic swap (see DEX_XMZ_EVM_SWAP_DESIGN.md).
//!
//! This crate knows the swap contract (`dex-contracts/contracts/XmzSwap.sol`) byte for byte:
//! its interface, how it hashes a swap and a relayed claim, and how it checks a secret.
//! It gives the swap machine the checks it must run before acting:
//!  - [`check_lock`]: the XMZ seller checks the owner's lock against the agreed terms
//!    before locking any XMZ;
//!  - [`claim_window`] / [`refund_window`]: whether a claim or refund can be sent now;
//!  - [`revealed_secret`]: the other side's secret, read from the contract's events.
//!
//! It does not hold keys or send transactions itself; the engine's journal does that.

use alloy::primitives::{eip191_hash_message, keccak256, Address, B256, U256};
use alloy::rpc::types::Log;
use alloy::sol;
use alloy::sol_types::SolValue;
use k256::elliptic_curve::sec1::ToEncodedPoint;
use k256::SecretKey;

sol! {
    #[sol(rpc, all_derives)]
    contract XmzSwap {
        struct Swap {
            address owner;
            address claimer;
            address claimCommitment;
            address refundCommitment;
            uint256 timeout1;
            uint256 timeout2;
            address asset;
            uint256 value;
            uint256 nonce;
        }

        event New(
            bytes32 indexed swapId,
            address indexed owner,
            address indexed claimer,
            address claimCommitment,
            address refundCommitment,
            uint256 timeout1,
            uint256 timeout2,
            address asset,
            uint256 value,
            uint256 nonce
        );
        event Ready(bytes32 indexed swapId);
        event Claimed(bytes32 indexed swapId, bytes32 secret);
        event Refunded(bytes32 indexed swapId, bytes32 secret);

        function swaps(bytes32 id) external view returns (uint8);
        function swapId(Swap memory swap) external pure returns (bytes32);
        function secretMatches(bytes32 secret, address commitment) external pure returns (bool);
        function newSwap(
            address claimer,
            address claimCommitment,
            address refundCommitment,
            uint256 duration1,
            uint256 duration2,
            address asset,
            uint256 value,
            uint256 nonce
        ) external payable returns (bytes32 id);
        function setReady(Swap calldata swap) external;
        function claim(Swap calldata swap, bytes32 secret) external;
        function claimRelayer(Swap calldata swap, bytes32 secret, uint256 fee, uint8 v, bytes32 r, bytes32 s) external;
        function refund(Swap calldata swap, bytes32 secret) external;
    }
}

pub use XmzSwap::Swap;

/// Domain tag of a relayed claim; must match `claimRelayer` in the contract.
pub const RELAY_TAG: &str = "XMZ-SWAP-RELAY-V1";

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum Error {
    #[error("the secret is zero or not below the secp256k1 group order")]
    InvalidSecret,
    #[error("the lock does not match the agreed terms: {0}")]
    TermsMismatch(&'static str),
    #[error("the lock is not in the expected stage (found {0})")]
    WrongStage(u8),
}

/// Chains the swap runs on. Contract and token addresses are pinned in the app for each
/// chain, never taken from a quote.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Chain {
    BscMainnet,
    BscTestnet,
    /// Hardhat / anvil test node
    Local,
}

impl Chain {
    pub fn from_id(id: u64) -> Option<Chain> {
        [Chain::BscMainnet, Chain::BscTestnet, Chain::Local].into_iter().find(|c| c.id() == id)
    }

    pub fn id(self) -> u64 {
        match self {
            Chain::BscMainnet => 56,
            Chain::BscTestnet => 97,
            Chain::Local => 31337,
        }
    }

    /// Blocks an EVM transaction may stay unmined before it is replaced with higher fees
    /// (about a minute on BNB Chain).
    pub fn bump_after_blocks(self) -> u64 {
        match self {
            Chain::BscMainnet | Chain::BscTestnet => 40,
            Chain::Local => 3,
        }
    }

    /// Highest fee per gas a replacement may offer (wei). Each replacement adds 12.5%, so
    /// this bounds what a fee spike can cost.
    pub fn max_fee_cap(self) -> u128 {
        match self {
            Chain::BscMainnet | Chain::BscTestnet => 50_000_000_000,
            Chain::Local => 1_000_000_000_000,
        }
    }

    /// Blocks after a transaction before the app acts on it. BNB Chain has fast finality
    /// (about 2 blocks); this keeps a margin for testnet reorganisations and lagging RPCs.
    pub fn confirmations(self) -> u64 {
        match self {
            Chain::BscMainnet => 15,
            Chain::BscTestnet => 15,
            Chain::Local => 1,
        }
    }
}

/// Stage of a swap in the contract's `swaps` mapping.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum Stage {
    Invalid = 0,
    Pending = 1,
    Ready = 2,
    Completed = 3,
}

impl TryFrom<u8> for Stage {
    type Error = Error;

    fn try_from(value: u8) -> Result<Self, Error> {
        Ok(match value {
            0 => Stage::Invalid,
            1 => Stage::Pending,
            2 => Stage::Ready,
            3 => Stage::Completed,
            other => return Err(Error::WrongStage(other)),
        })
    }
}

/// The commitment to a secret, as the contract stores it: the address of `secret * G`.
/// The same scalar is the trader's ed25519 key share on the XMZ side (proved to the other
/// trader with the engine's cross-curve DLEQ proof).
pub fn commitment(secret: &B256) -> Result<Address, Error> {
    let key = SecretKey::from_slice(secret.as_slice()).map_err(|_| Error::InvalidSecret)?;
    let point = key.public_key().to_encoded_point(false);
    Ok(Address::from_raw_public_key(&point.as_bytes()[1..]))
}

/// True when `secret` opens `commitment`, as `secretMatches` in the contract.
pub fn secret_matches(secret: &B256, commitment_address: Address) -> bool {
    commitment(secret).is_ok_and(|address| address == commitment_address)
}

/// The swap id: `keccak256(abi.encode(swap))`.
pub fn swap_id(swap: &Swap) -> B256 {
    keccak256(swap.abi_encode())
}

/// The message hash a claimer signs to let `relayer` submit the claim for `fee`.
pub fn relay_digest(chain_id: u64, contract: Address, id: B256, fee: U256, relayer: Address) -> B256 {
    let inner = keccak256((RELAY_TAG.to_string(), U256::from(chain_id), contract, id, fee, relayer).abi_encode_params());
    eip191_hash_message(inner)
}

/// What the two traders agreed before the owner locks anything.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AgreedTerms {
    pub chain: Chain,
    pub contract: Address,
    pub owner: Address,
    pub claimer: Address,
    pub claim_commitment: Address,
    pub refund_commitment: Address,
    /// `Address::ZERO` for the native coin
    pub asset: Address,
    pub value: U256,
    /// Seconds from the lock block to timeout1, and from timeout1 to timeout2
    pub duration1: u64,
    pub duration2: u64,
}

/// The owner's lock as the claimer observed it on chain.
#[derive(Clone, Debug)]
pub struct ObservedLock {
    pub chain_id: u64,
    /// The `New` event, with the address that emitted it
    pub log: Log<XmzSwap::New>,
    /// Timestamp of the block that holds the event
    pub block_timestamp: u64,
    /// `swaps(id)` read from the contract after enough confirmations
    pub stage: u8,
}

/// The XMZ seller's check before locking XMZ: the owner's lock must match the agreed terms
/// exactly. Returns the swap as the contract hashes it, for the later claim.
pub fn check_lock(terms: &AgreedTerms, lock: &ObservedLock) -> Result<Swap, Error> {
    let mismatch = |what| Err(Error::TermsMismatch(what));
    if lock.chain_id != terms.chain.id() {
        return mismatch("chain id");
    }
    if lock.log.address() != terms.contract {
        return mismatch("emitted by another contract");
    }
    let event = &lock.log.inner.data;
    let swap = Swap {
        owner: event.owner,
        claimer: event.claimer,
        claimCommitment: event.claimCommitment,
        refundCommitment: event.refundCommitment,
        timeout1: event.timeout1,
        timeout2: event.timeout2,
        asset: event.asset,
        value: event.value,
        nonce: event.nonce,
    };
    if swap_id(&swap) != event.swapId {
        return mismatch("swap id");
    }
    if swap.owner != terms.owner {
        return mismatch("owner");
    }
    if swap.claimer != terms.claimer {
        return mismatch("claimer");
    }
    if swap.claimCommitment != terms.claim_commitment {
        return mismatch("claim commitment");
    }
    if swap.refundCommitment != terms.refund_commitment {
        return mismatch("refund commitment");
    }
    if swap.asset != terms.asset {
        return mismatch("asset");
    }
    if swap.value != terms.value {
        return mismatch("value");
    }
    let timeout1 = U256::from(lock.block_timestamp) + U256::from(terms.duration1);
    if swap.timeout1 != timeout1 {
        return mismatch("timeout1");
    }
    if swap.timeout2 != timeout1 + U256::from(terms.duration2) {
        return mismatch("timeout2");
    }
    match Stage::try_from(lock.stage)? {
        Stage::Pending => Ok(swap),
        _ => Err(Error::WrongStage(lock.stage)),
    }
}

/// Whether the claimer may send a claim at `now`. `margin` keeps the claim clear of timeout2:
/// a claim that lands after timeout2 fails yet still reveals the secret, after which the
/// owner can both sweep the XMZ and refund.
pub fn claim_window(swap: &Swap, stage: Stage, now: u64, margin: u64) -> bool {
    let now = U256::from(now);
    let ready_or_late = stage == Stage::Ready || (stage == Stage::Pending && now >= swap.timeout1);
    ready_or_late && now + U256::from(margin) < swap.timeout2
}

/// Whether the owner may refund at `now`, as `refund` in the contract.
pub fn refund_window(swap: &Swap, stage: Stage, now: u64) -> bool {
    let now = U256::from(now);
    match stage {
        Stage::Pending => now < swap.timeout1 || now >= swap.timeout2,
        Stage::Ready => now >= swap.timeout2,
        Stage::Invalid | Stage::Completed => false,
    }
}

/// A secret the other trader revealed for this swap.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Revealed {
    /// The claimer's secret: the owner can now sweep the XMZ.
    Claimed(B256),
    /// The owner's secret: the claimer can now sweep the XMZ back.
    Refunded(B256),
}

/// Finds the revealed secret for `id` among `logs` emitted by `contract`, and checks it
/// against the expected commitment before returning it.
pub fn revealed_secret(logs: &[Log], contract: Address, swap: &Swap) -> Option<Revealed> {
    let id = swap_id(swap);
    logs.iter().filter(|log| log.address() == contract).find_map(|log| {
        if let Ok(claimed) = log.log_decode::<XmzSwap::Claimed>() {
            let event = claimed.inner.data;
            if event.swapId == id && secret_matches(&event.secret, swap.claimCommitment) {
                return Some(Revealed::Claimed(event.secret));
            }
        }
        if let Ok(refunded) = log.log_decode::<XmzSwap::Refunded>() {
            let event = refunded.inner.data;
            if event.swapId == id && secret_matches(&event.secret, swap.refundCommitment) {
                return Some(Revealed::Refunded(event.secret));
            }
        }
        None
    })
}

#[cfg(test)]
mod tests;
pub mod machine;
pub mod secret;
pub mod driver;
pub mod chain;
pub mod xmz;
pub mod negotiation;
pub mod swap_seed;
pub mod evm_key;
pub mod exchange;
pub mod helper;
