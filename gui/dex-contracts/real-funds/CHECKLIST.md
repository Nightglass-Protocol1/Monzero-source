# Real-funds test: XMZ ↔ BNB on mainnet

The owner's own test of the swap with real coins, between two accounts of one dedicated test
phrase: the **buyer** (account 0) pays BNB and receives XMZ, the **seller** (account 1) sells XMZ
and receives BNB. Everything runs on one PC, in two terminals.

Keep it small: the runner refuses more than **0.1 BNB** per trade. Never use a main wallet's
recovery phrase; the test phrase is created fresh in step 1.

Commands run from `gui/dex-contracts`. Needed: Node.js (`npm ci` once), the synced mainnet node
(`monzerod`, RPC 127.0.0.1:6175), `monzero-wallet-rpc` v1.0.0.

## 1. Create the test phrase

```
node scripts/new-test-phrase.js
```
It saves `~/.config/monzero-dex/bsc-mainnet-accounts.json` (private) and prints the buyer and
seller addresses. Back the file up while it holds funds.

## 2. Deploy the swap contract (once)

From MetaMask, export the private key of the account that pays for the deployment
(Account details → Show private key). The deployment costs about **0.002 BNB**.
```
npx hardhat run --network bscMainnet scripts/deploy-mainnet.js
```
Paste the key at the hidden prompt, check the network (chain 56), balance and estimate, then type
`DEPLOY`. The script waits 15 confirmations, checks the code on chain is exactly the compiled
contract and saves `deployments/bsc-mainnet.json`; it should print `Same code as testnet: yes`.
The contract has no owner or admin: the deploying account gets no control over it.

Commit `deployments/bsc-mainnet.json` so every app uses the same contract.

## 3. Fund the two test accounts (BNB)

From MetaMask, on BNB Smart Chain:
- **Buyer** (account 0): the trade amount + **0.005 BNB** for gas (e.g. 0.015 BNB for a 0.01 BNB trade).
- **Seller** (account 1): **0.005 BNB** for the gas of collecting.

## 4. Run the trade

Pick the amounts, e.g. 0.01 BNB for 5 XMZ. Terminal 1, the buyer:
```
BNB_AMOUNT=0.01 XMZ_AMOUNT=5 real-funds/run-trader.sh buyer ~/dex-real/buyer-1
```
Terminal 2, the seller, once the buyer prints "answering key requests":
```
BNB_AMOUNT=0.01 XMZ_AMOUNT=5 real-funds/run-trader.sh seller ~/dex-real/seller-1 ~/dex-real/buyer-1
```
The seller prints its XMZ wallet address (also in `~/dex-real/seller-1/xmz-address.txt`) and waits.
**Send it the XMZ amount plus half again** (7.5 XMZ for a 5 XMZ trade) from your XMZ wallet, then
wait for it to unlock (10 blocks).

Then both run on their own. Expected states:
- buyer: `Start → LockSent → Locked → ReadySent → Ready → Sweeping → Done(Swapped)`
- seller: `WaitingForEvmLock → XmzLockSent → XmzLocked → ClaimSent → Done(Swapped)`

Each writes `summary.json` with the BNB transactions (check them on bscscan.com) when done.
The buyer's offer is valid for 2 hours; the whole trade takes about 30–60 minutes.

**If anything stops** (crash, reboot, Ctrl-C): start that side again with `resume`:
```
BNB_AMOUNT=0.01 XMZ_AMOUNT=5 real-funds/run-trader.sh buyer ~/dex-real/buyer-1 resume
```
Nothing is lost: the trade is saved before anything is locked, and refunds are automatic.

## 5. Refund test (recommended, second trade)

Start a new pair of run directories as in step 4. When the buyer reaches `Locked`, stop the seller
(Ctrl-C) **before** it reaches `XmzLockSent`, and leave it stopped. The buyer refunds itself after
the deadline (about 4 hours): `RefundSent → Done(Refunded)`. Check the BNB came back.

A second case, **the buyer disappears**: stop the buyer after the seller reaches `XmzLocked` and
leave it stopped. The seller cannot be blocked: after the first deadline (about 2 hours) it collects
the BNB anyway (`ClaimSent → Done(Swapped)`). Then restart the buyer with `resume`: it finds the
seller's claim and still sweeps its XMZ (`Sweeping → Done(Swapped)`).

## 6. Afterwards: take the coins home

- **XMZ the buyer received**: in the wallet `~/dex-real/buyer-1/main/owner` (empty password). Open
  it with `monzero-wallet-cli --wallet-file ~/dex-real/buyer-1/main/owner --daemon-address 127.0.0.1:6175`
  and `sweep_all <your address>`. Same for leftover XMZ in the seller's wallet (`seller-1/main/claimer`).
- **BNB**: import the test phrase into MetaMask (Import wallet), add accounts 1 and 2, and send the
  BNB back to your main account.
- Then the test phrase can be retired. Keep the run directories and `summary.json` files as evidence.

## Report

For each trade: amounts, run directories' `summary.json`, the BNB transaction links, the final
states, and anything unexpected in `trader.log`.
