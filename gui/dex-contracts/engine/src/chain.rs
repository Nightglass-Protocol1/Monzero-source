//! [`EvmChain`] over a JSON-RPC node with alloy.

use crate::driver::{DriverError, EvmCall, EvmChain};
use crate::{revealed_secret, AgreedTerms, Chain, ObservedLock, Revealed, Stage, Swap, XmzSwap};
use alloy::consensus::{SignableTransaction, Transaction, TxEnvelope};
use alloy::eips::{BlockId, BlockNumberOrTag, Decodable2718, Encodable2718};
use alloy::network::{EthereumWallet, TransactionBuilder};
use alloy::primitives::{Address, Bytes, B256, U256};
use alloy::providers::{DynProvider, Provider, ProviderBuilder};
use alloy::rpc::types::{Filter, Log, TransactionRequest};
use alloy::signers::local::PrivateKeySigner;
use alloy::sol_types::{SolCall, SolEvent, SolValue};

/// Blocks per log query by default: public endpoints refuse or cap wide ranges. Set a
/// smaller chunk for endpoints with lower caps (`with_log_chunk`).
const LOG_CHUNK: u64 = 2_000;

/// One log search: the next block to query and the logs found so far.
#[derive(Default)]
struct Scan {
    next: u64,
    logs: Vec<Log>,
}

pub struct AlloyChain {
    chain: Chain,
    contract: Address,
    /// Logs are searched from this block (the block before the trade was agreed)
    from_block: u64,
    reader: DynProvider,
    wallet: EthereumWallet,
    from: Address,
    log_chunk: u64,
    /// Searches by key, so each tick queries only blocks it has not seen
    scans: std::sync::Mutex<std::collections::HashMap<B256, Scan>>,
}

fn chain_err(e: impl std::fmt::Display) -> DriverError {
    DriverError::Chain(e.to_string())
}

/// An RPC client that retries what public endpoints fail now and then: rate limits and HTTP
/// 5xx ("temporary internal error"), up to 5 times with backoff. Answers from the node itself
/// (a call that would revert, a known transaction) are not retried.
fn retrying_client(url: alloy::transports::http::reqwest::Url) -> alloy::rpc::client::RpcClient {
    use alloy::transports::layers::{RateLimitRetryPolicy, RetryBackoffLayer};
    let policy = RateLimitRetryPolicy::default()
        .or(|e: &alloy::transports::TransportError| e.as_transport_err().and_then(|t| t.as_http_error()).is_some_and(|h| h.status >= 500 || h.status == 429));
    alloy::rpc::client::ClientBuilder::default().layer(RetryBackoffLayer::new_with_policy(5, 400, 10_000, policy)).http(url)
}

impl AlloyChain {
    pub fn new(url: &str, chain: Chain, contract: Address, from_block: u64, key: PrivateKeySigner) -> Result<Self, DriverError> {
        let url: alloy::transports::http::reqwest::Url = url.parse().map_err(chain_err)?;
        let from = key.address();
        Ok(AlloyChain {
            chain,
            contract,
            from_block,
            reader: ProviderBuilder::new().connect_client(retrying_client(url)).erased(),
            wallet: EthereumWallet::from(key),
            from,
            log_chunk: LOG_CHUNK,
            scans: Default::default(),
        })
    }

    pub fn with_log_chunk(mut self, blocks: u64) -> Self {
        self.log_chunk = blocks.max(1);
        self
    }

    /// All logs of `filter` from the trade's start block up to `to`, under `key`: only blocks
    /// not queried before are fetched, in chunks the endpoint accepts.
    async fn logs(&self, key: B256, filter: &Filter, to: u64) -> Result<Vec<Log>, DriverError> {
        let mut start = self.scans.lock().unwrap().get(&key).map_or(self.from_block, |s| s.next);
        while start <= to {
            let end = to.min(start + self.log_chunk - 1);
            let found = self.reader.get_logs(&filter.clone().from_block(start).to_block(end)).await.map_err(chain_err)?;
            let mut scans = self.scans.lock().unwrap();
            let scan = scans.entry(key).or_insert_with(|| Scan { next: self.from_block, logs: vec![] });
            if scan.next == start {
                scan.logs.extend(found);
                scan.next = end + 1;
            }
            start = scan.next;
        }
        Ok(self.scans.lock().unwrap().get(&key).map(|s| s.logs.clone()).unwrap_or_default())
    }

    /// Check before trading that this endpoint answers log queries for the contract: some
    /// public endpoints (BNB Chain's own testnet seeds among them) refuse them, and a trader
    /// that cannot read logs cannot see the other side's lock or secret.
    pub async fn check_logs(&self) -> Result<(), DriverError> {
        // History, not only the newest blocks: some endpoints serve only recent logs, and a
        // trade reads back to the block where it started
        let to = self.reader.get_block_number().await.map_err(chain_err)?.saturating_sub(1_000);
        let filter = Filter::new().address(self.contract);
        self.reader
            .get_logs(&filter.from_block(to.saturating_sub(self.log_chunk - 1)).to_block(to))
            .await
            .map(|_| ())
            .map_err(|e| chain_err(format!("endpoint does not serve contract logs: {e}")))
    }

    /// The newest block with enough confirmations.
    async fn confirmed_block(&self) -> Result<u64, DriverError> {
        let head = self.reader.get_block_number().await.map_err(chain_err)?;
        Ok((head + 1).saturating_sub(self.chain.confirmations()))
    }

    async fn block_timestamp(&self, number: u64) -> Result<u64, DriverError> {
        let block = self
            .reader
            .get_block_by_number(BlockNumberOrTag::Number(number))
            .await
            .map_err(chain_err)?
            .ok_or_else(|| chain_err(format!("block {number} not found")))?;
        Ok(block.header.timestamp)
    }
}

impl AlloyChain {
    /// After a trade: send everything our account holds, less the fee of this transfer, to
    /// `to` (the user's own wallet, e.g. MetaMask). Returns the transaction and the amount, or
    /// None when a payout is still pending or nothing is left to send. Repeating it is safe.
    pub async fn pay_out(&self, to: Address) -> Result<Option<(B256, U256)>, DriverError> {
        let latest = self.reader.get_transaction_count(self.from).block_id(BlockId::latest()).await.map_err(chain_err)?;
        let pending = self.reader.get_transaction_count(self.from).pending().await.map_err(chain_err)?;
        if pending > latest {
            return Ok(None);
        }
        let balance = self.reader.get_balance(self.from).await.map_err(chain_err)?;
        let fees = self.reader.estimate_eip1559_fees().await.map_err(chain_err)?;
        let gas: u64 = 21_000;
        // Pay the full max fee up front so the amount plus fee never exceeds the balance
        let fee = U256::from(gas) * U256::from(fees.max_fee_per_gas);
        if balance <= fee {
            return Ok(None);
        }
        let amount = balance - fee;
        let tx = TransactionRequest::default()
            .with_from(self.from)
            .with_to(to)
            .with_value(amount)
            .with_nonce(latest)
            .with_chain_id(self.reader.get_chain_id().await.map_err(chain_err)?)
            .with_gas_limit(gas)
            .with_max_fee_per_gas(fees.max_fee_per_gas)
            .with_max_priority_fee_per_gas(fees.max_priority_fee_per_gas.min(fees.max_fee_per_gas));
        let envelope = tx.build(&self.wallet).await.map_err(chain_err)?;
        let raw = Bytes::from(envelope.encoded_2718());
        self.broadcast(&raw).await?;
        Ok(Some((*envelope.tx_hash(), amount)))
    }
}

impl EvmChain for AlloyChain {
    async fn now(&self) -> Result<u64, DriverError> {
        let head = self.reader.get_block_number().await.map_err(chain_err)?;
        self.block_timestamp(head).await
    }

    async fn find_lock(&self, terms: &AgreedTerms) -> Result<Option<ObservedLock>, DriverError> {
        let confirmed = self.confirmed_block().await?;
        if confirmed < self.from_block {
            return Ok(None);
        }
        let filter = Filter::new()
            .address(self.contract)
            .event_signature(XmzSwap::New::SIGNATURE_HASH)
            .topic2(terms.owner.into_word())
            .topic3(terms.claimer.into_word());
        let key = alloy::primitives::keccak256((terms.owner, terms.claimer, terms.claim_commitment, terms.refund_commitment).abi_encode());
        let logs = self.logs(key, &filter, confirmed).await?;
        // Our swap is the one with our commitments (fresh for every trade)
        let Some(log) = logs.iter().filter_map(|l| l.log_decode::<XmzSwap::New>().ok()).find(|l| {
            l.inner.data.claimCommitment == terms.claim_commitment && l.inner.data.refundCommitment == terms.refund_commitment
        }) else {
            return Ok(None);
        };
        let number = log.block_number.ok_or_else(|| chain_err("lock log without a block number"))?;
        let stage = self.stage(log.inner.data.swapId).await? as u8;
        Ok(Some(ObservedLock {
            chain_id: self.reader.get_chain_id().await.map_err(chain_err)?,
            block_timestamp: self.block_timestamp(number).await?,
            log,
            stage,
        }))
    }

    async fn stage(&self, id: B256) -> Result<Stage, DriverError> {
        let block = BlockId::number(self.confirmed_block().await?);
        let raw = XmzSwap::new(self.contract, &self.reader).swaps(id).block(block).call().await.map_err(chain_err)?;
        Ok(Stage::try_from(raw)?)
    }

    async fn revealed(&self, swap: &Swap) -> Result<Option<Revealed>, DriverError> {
        let filter = Filter::new()
            .address(self.contract)
            .event_signature(vec![XmzSwap::Claimed::SIGNATURE_HASH, XmzSwap::Refunded::SIGNATURE_HASH])
            .topic1(crate::swap_id(swap));
        let head = self.reader.get_block_number().await.map_err(chain_err)?;
        let logs = self.logs(crate::swap_id(swap), &filter, head).await?;
        Ok(revealed_secret(&logs, self.contract, swap))
    }

    async fn sign(&self, call: &EvmCall) -> Result<Bytes, DriverError> {
        let (input, value) = match call {
            EvmCall::NewSwap { terms, nonce } => (
                XmzSwap::newSwapCall {
                    claimer: terms.claimer,
                    claimCommitment: terms.claim_commitment,
                    refundCommitment: terms.refund_commitment,
                    duration1: U256::from(terms.duration1),
                    duration2: U256::from(terms.duration2),
                    asset: terms.asset,
                    value: terms.value,
                    nonce: *nonce,
                }
                .abi_encode(),
                if terms.asset == Address::ZERO { terms.value } else { U256::ZERO },
            ),
            EvmCall::SetReady(swap) => (XmzSwap::setReadyCall { swap: swap.clone() }.abi_encode(), U256::ZERO),
            EvmCall::Claim(swap, secret) => (XmzSwap::claimCall { swap: swap.clone(), secret: *secret }.abi_encode(), U256::ZERO),
            EvmCall::Refund(swap, secret) => (XmzSwap::refundCall { swap: swap.clone(), secret: *secret }.abi_encode(), U256::ZERO),
        };
        let tx = TransactionRequest::default().with_from(self.from).with_to(self.contract).with_input(input).with_value(value);
        // Gas estimation executes the call: a call that would revert fails here, before
        // anything is signed or published.
        let gas = self.reader.estimate_gas(tx.clone()).await.map_err(chain_err)?;
        let fees = self.reader.estimate_eip1559_fees().await.map_err(chain_err)?;
        let tx = tx
            .with_nonce(self.reader.get_transaction_count(self.from).pending().await.map_err(chain_err)?)
            .with_chain_id(self.reader.get_chain_id().await.map_err(chain_err)?)
            .with_gas_limit(gas + gas / 5)
            .with_max_fee_per_gas(fees.max_fee_per_gas)
            .with_max_priority_fee_per_gas(fees.max_priority_fee_per_gas);
        let envelope = tx.build(&self.wallet).await.map_err(chain_err)?;
        Ok(Bytes::from(envelope.encoded_2718()))
    }

    async fn head(&self) -> Result<u64, DriverError> {
        self.reader.get_block_number().await.map_err(chain_err)
    }

    async fn funds(&self) -> Result<crate::driver::Funds, DriverError> {
        let balance = self.reader.get_balance(self.from).await.map_err(chain_err)?;
        let fees = self.reader.estimate_eip1559_fees().await.map_err(chain_err)?;
        Ok(crate::driver::Funds { balance, fee_per_gas: U256::from(fees.max_fee_per_gas) })
    }

    async fn replacement(&self, raw: &Bytes) -> Result<Option<Bytes>, DriverError> {
        let envelope = TxEnvelope::decode_2718(&mut raw.as_ref()).map_err(chain_err)?;
        let TxEnvelope::Eip1559(signed) = &envelope else {
            return Err(chain_err("kept transaction is not EIP-1559"));
        };
        if signed.signature().recover_address_from_prehash(&signed.tx().signature_hash()).map_err(chain_err)? != self.from {
            return Err(chain_err("kept transaction is not ours"));
        }
        let tx = signed.tx();
        // Mined (this or another version): the account's nonce has moved past it
        let used = self.reader.get_transaction_count(self.from).block_id(BlockId::latest()).await.map_err(chain_err)?;
        if used > tx.nonce {
            return Ok(None);
        }
        // Nodes accept a replacement only with at least 10% more on both fees
        let bump = |old: u128| old + old / 8 + 1;
        let (min_fee, min_tip) = (bump(tx.max_fee_per_gas), bump(tx.max_priority_fee_per_gas));
        let now = self.reader.estimate_eip1559_fees().await.map_err(chain_err)?;
        let max_fee = min_fee.max(now.max_fee_per_gas).min(self.chain.max_fee_cap());
        let tip = min_tip.max(now.max_priority_fee_per_gas).min(max_fee);
        if max_fee < min_fee || tip < min_tip {
            return Ok(None);
        }
        let to = tx.to().ok_or_else(|| chain_err("kept transaction has no recipient"))?;
        // Same call, nonce and gas limit: no new estimation, the call was checked when first signed
        let request = TransactionRequest::default()
            .with_from(self.from)
            .with_to(to)
            .with_input(tx.input().clone())
            .with_value(tx.value())
            .with_nonce(tx.nonce)
            .with_chain_id(tx.chain_id)
            .with_gas_limit(tx.gas_limit)
            .with_max_fee_per_gas(max_fee)
            .with_max_priority_fee_per_gas(tip);
        let envelope = request.build(&self.wallet).await.map_err(chain_err)?;
        Ok(Some(Bytes::from(envelope.encoded_2718())))
    }

    async fn broadcast(&self, raw: &Bytes) -> Result<(), DriverError> {
        match self.reader.send_raw_transaction(raw).await {
            Ok(_) => Ok(()),
            Err(e) => {
                let text = e.to_string().to_lowercase();
                // Rebroadcasting a transaction that is already pending or mined is fine
                if text.contains("already known") || text.contains("nonce too low") || text.contains("known transaction") {
                    Ok(())
                } else {
                    Err(chain_err(e))
                }
            }
        }
    }
}
