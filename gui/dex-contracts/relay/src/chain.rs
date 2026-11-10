//! Read-only chain access for the relay: balances (spam guard) and claim checks.

use crate::{ChainView, ClaimLookup};
use alloy::consensus::Transaction;
use alloy::eips::BlockNumberOrTag;
use alloy::primitives::{Address, B256, U256};
use alloy::providers::{DynProvider, Provider, ProviderBuilder};
use alloy::sol_types::{SolCall, SolEvent};
use monzero_dex_evm::{swap_id, Chain, XmzSwap};

pub struct AlloyView {
    chain: Chain,
    contract: Address,
    reader: DynProvider,
}

/// Retries rate limits and HTTP 5xx from public endpoints, as the trader's client does
fn retrying_client(url: alloy::transports::http::reqwest::Url) -> alloy::rpc::client::RpcClient {
    use alloy::transports::layers::{RateLimitRetryPolicy, RetryBackoffLayer};
    let policy = RateLimitRetryPolicy::default()
        .or(|e: &alloy::transports::TransportError| e.as_transport_err().and_then(|t| t.as_http_error()).is_some_and(|h| h.status >= 500 || h.status == 429));
    alloy::rpc::client::ClientBuilder::default().layer(RetryBackoffLayer::new_with_policy(5, 400, 10_000, policy)).http(url)
}

impl AlloyView {
    pub fn new(url: &str, chain: Chain, contract: Address) -> Result<Self, String> {
        let url = url.parse().map_err(|e| format!("{url}: {e}"))?;
        Ok(AlloyView { chain, contract, reader: ProviderBuilder::new().connect_client(retrying_client(url)).erased() })
    }

    /// The endpoint must be on the chain it is configured for
    pub async fn check(&self) -> Result<(), String> {
        let id = self.reader.get_chain_id().await.map_err(|e| e.to_string())?;
        if id != self.chain.id() {
            return Err(format!("endpoint is on chain {id}, configured for {}", self.chain.id()));
        }
        let code = self.reader.get_code_at(self.contract).await.map_err(|e| e.to_string())?;
        if code.is_empty() {
            return Err(format!("no contract at {} on chain {id}", self.contract));
        }
        Ok(())
    }
}

impl ChainView for AlloyView {
    async fn balance(&self, account: Address) -> Result<U256, String> {
        self.reader.get_balance(account).await.map_err(|e| e.to_string())
    }

    async fn claim(&self, tx: B256) -> Result<ClaimLookup, String> {
        let Some(receipt) = self.reader.get_transaction_receipt(tx).await.map_err(|e| e.to_string())? else {
            return Ok(ClaimLookup::Pending);
        };
        let Some(number) = receipt.block_number else { return Ok(ClaimLookup::Pending) };
        if !receipt.status() {
            return Ok(ClaimLookup::NotAClaim("the transaction reverted".into()));
        }
        let claimed = receipt.inner.logs().iter().find(|l| l.address() == self.contract && l.topic0() == Some(&XmzSwap::Claimed::SIGNATURE_HASH));
        let Some(id) = claimed.and_then(|l| l.topics().get(1).copied()) else {
            return Ok(ClaimLookup::NotAClaim("no claim from the pinned contract in it".into()));
        };
        let head = self.reader.get_block_number().await.map_err(|e| e.to_string())?;
        if head + 1 < number + self.chain.confirmations() {
            return Ok(ClaimLookup::Pending);
        }
        let Some(sent) = self.reader.get_transaction_by_hash(tx).await.map_err(|e| e.to_string())? else {
            return Ok(ClaimLookup::Pending);
        };
        if sent.to() != Some(self.contract) {
            return Ok(ClaimLookup::NotAClaim("not a direct call to the contract".into()));
        }
        let input = sent.input();
        let swap = if let Ok(c) = XmzSwap::claimCall::abi_decode(input) {
            c.swap
        } else if let Ok(c) = XmzSwap::claimRelayerCall::abi_decode(input) {
            c.swap
        } else {
            return Ok(ClaimLookup::NotAClaim("not a claim call".into()));
        };
        if swap_id(&swap) != id {
            return Ok(ClaimLookup::NotAClaim("the claimed swap does not match the call".into()));
        }
        let block = self
            .reader
            .get_block_by_number(BlockNumberOrTag::Number(number))
            .await
            .map_err(|e| e.to_string())?
            .ok_or_else(|| format!("block {number} not found"))?;
        Ok(ClaimLookup::Final { swap, at: block.header.timestamp })
    }
}

/// The chain behind a served pair: an EVM chain the relay reads, or Solana.
///
/// On Solana the relay reads nothing yet. An offer there is signed by the maker's EVM-style
/// trade account, which says nothing about the maker's Solana account, so the balance that
/// backs an offer cannot be checked: those offers rest on the per-maker and rate limits alone.
/// Claims there are not recorded as trades for the price chart either.
pub enum View {
    Evm(AlloyView),
    Solana,
}

impl ChainView for View {
    async fn balance(&self, account: Address) -> Result<U256, String> {
        match self {
            View::Evm(v) => v.balance(account).await,
            View::Solana => Ok(U256::MAX),
        }
    }

    async fn claim(&self, tx: B256) -> Result<ClaimLookup, String> {
        match self {
            View::Evm(v) => v.claim(tx).await,
            View::Solana => Ok(ClaimLookup::NotAClaim("claims on Solana are not recorded yet".into())),
        }
    }
}
