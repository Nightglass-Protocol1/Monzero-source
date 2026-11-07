# XMZ ↔ ETH: from Sepolia to a first real-funds swap

The same swap as BNB (see `CHECKLIST.md` for how each step behaves) on Ethereum. Commands run from
`gui/dex-contracts`. Sepolia comes first: a pair moves to real funds only after its test-network
gates pass.

Ethereum differs from BNB in three ways:
- **A trade waits 96 blocks (about 19 minutes)** each time one side acts on the other's Ethereum
  transaction, so that it is final. A whole trade takes about 60–90 minutes.
- **Use plain accounts.** The contract pays out with a plain transfer, so the runner refuses an
  account with code on chain, including a wallet upgraded with EIP-7702. Accounts from a fresh test
  phrase are plain. Do not upgrade them to a smart account in MetaMask while a trade runs.
- **Gas can spike.** Replacement fees stop at 200 gwei. Check the cost in step 1 before starting.

## 1. Check the connection (no key, no funds)

```
cd engine && cargo run --example evm_preflight -- eth-sepolia   # or: eth
```
Every line must say `ok`. It also prints what each step costs at today's fees.

## 2. Sepolia

```
node scripts/new-test-phrase.js ~/.config/monzero-dex/eth-sepolia-accounts.json
```
Send SepoliaETH from a faucet: account 0 about 0.03 (the deployment, the 0.02 trade and gas), account
1 about 0.005. Then deploy, wait for 96 blocks, and commit `deployments/eth-sepolia.json`:
```
DEPLOYER=test-phrase npx hardhat run --network ethSepolia scripts/deploy-mainnet.js
```
Run the trade (20 tXMZ for 0.02 SepoliaETH) against the XMZ testnet node on 127.0.0.1:16175:
```
CHAIN=eth NETWORK=testnet real-funds/run-trader.sh buyer ~/dex-eth/sepolia-buyer-1
CHAIN=eth NETWORK=testnet real-funds/run-trader.sh seller ~/dex-eth/sepolia-seller-1 ~/dex-eth/sepolia-buyer-1
```
Send the seller's address (`xmz-address.txt`) 30 tXMZ. Then run the refund cases from step 5 of
`CHECKLIST.md`, and a stop-and-`resume`.

## 3. Real funds

Start the mainnet XMZ node (RPC 127.0.0.1:6175, synced) first. Then:
```
node scripts/new-test-phrase.js ~/.config/monzero-dex/eth-mainnet-accounts.json
```
Never use a main wallet's phrase. Fund account 0 with the trade amount plus gas and the deployment,
and account 1 with gas, using the costs from step 1 with a generous margin. Then:
```
DEPLOYER=test-phrase npx hardhat run --network ethMainnet scripts/deploy-mainnet.js
```
It shows the balance and estimate and deploys only after you type `DEPLOY`. The contract has no owner
or admin, so the deploying account gets no control over it. Commit `deployments/eth-mainnet.json`.

The runner refuses more than **0.02 ETH** per trade. XMZ_AMOUNT = ETH_AMOUNT × (ETH price in $) ÷ 0.001,
the starting point chosen on 2026-10-31.
```
CHAIN=eth ETH_AMOUNT=0.005 XMZ_AMOUNT=<amount> real-funds/run-trader.sh buyer ~/dex-eth/buyer-1
CHAIN=eth ETH_AMOUNT=0.005 XMZ_AMOUNT=<amount> real-funds/run-trader.sh seller ~/dex-eth/seller-1 ~/dex-eth/buyer-1
```
Send the seller's XMZ address the XMZ amount plus half again, and wait for it to unlock. To continue
after any stop, add `resume` in place of the buyer's run directory argument, as in `CHECKLIST.md`.

**This is a test between your own two accounts, not a market trade.** Don't publish its price or add it
to any public price history.

## Rehearsal without funds

A whole swap and refund on a local copy of mainnet (chain 1, real state), which never reaches the real
chain:
```
ETH_FORK_RPC=https://ethereum-rpc.publicnode.com npx hardhat node --port 18545
cd engine && MONZERO_ETH_FORK=http://127.0.0.1:18545 cargo test --test eth_fork -- --test-threads=1
```
