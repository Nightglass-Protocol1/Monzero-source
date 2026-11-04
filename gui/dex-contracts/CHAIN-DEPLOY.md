# Adding EVM chains to the Monzero DEX

The swap contract (`XmzSwap.sol`) is **chain-agnostic** — it binds every signature to
`block.chainid`, has **no owner/admin/upgrade/pause**, and is immutable (a new version
is a new deployment). So "adding a chain" = deploy the same, already-tested contract
(27/27 tests passing) on that chain and record its address. Nothing else changes.

## Recommended chains (priority order)
1. **BNB Smart Chain** (56) — already in progress; keep.
2. **Base** (8453) — Coinbase L2, deep + growing liquidity, cheap gas, native USDC.
3. **Arbitrum One** (42161) — largest L2 by TVL, deep USDC/USDT liquidity, cheap gas.
4. **Polygon PoS** (137) — very cheap, broad stablecoin coverage.
5. **Ethereum** (1) — deepest liquidity and credibility, but high gas → **larger trades
   only** (set a minimum size so the spread covers gas, same logic as the BTC fee curve).

Low-gas L2s (Base/Arbitrum/Polygon) come before Ethereum because cheap gas keeps small
swaps viable. Each is configured in `hardhat.config.js`; testnets are wired too.

## Do fees still work? Yes.
Two separate things, both chain-independent:
- **Monzero's revenue = the maker spread** baked into the quoted XMZ↔asset rate (same as
  BTC: dynamic, size-aware). It lives in the app's quoting, not the contract, so it works
  identically on every chain.
- **The in-contract `fee`** is a *relayer* reimbursement (gas) — the claimer signs
  `(chainid, contract, swapId, fee, relayer)` so a relayer can submit the claim and be
  paid. Verified by the test suite ("pays the named relayer its fee", "refuses a changed
  fee / a fee above the value / a front-runner / a high-s signature").

Deploying on more chains does **not** weaken either. On cheap L2s the economics are best;
on Ethereum, use a minimum trade size so the spread clears gas.

## Deploy a chain (owner action — real funds, gated)
Deploys are **not** run by tooling or CI automatically; they spend real gas and are gated
behind an explicit opt-in, and mainnet should wait for the contract audit/pentest.

1. Put a funded deployer mnemonic **outside the repo**, mode 600:
   `~/.config/monzero-dex/<network>-accounts.json` → `{ "mnemonic": "..." }`
   (network names: `base`, `arbitrum`, `polygon`, `ethereum`, `bsc`, or the `*Sepolia` /
   `bscTestnet` / `polygonAmoy` testnets). Optionally set a private RPC via the env var in
   `hardhat.config.js` (e.g. `BASE_RPC`).
2. **Testnet first:**
   `npx hardhat run --network baseSepolia scripts/deploy.js`
3. **Mainnet** (after testnet + audit), with the opt-in:
   `MONZERO_DEX_EVM_MAINNET=I_ACCEPT_REAL_FUNDS_RISK npx hardhat run --network base scripts/deploy.js`

`scripts/deploy.js` verifies the on-chain runtime bytecode equals the compiled artifact,
refuses to overwrite an existing record, and writes `deployments/<network>.json`
(`chainId`, `contract`, `deployer`, `deployTx`). That record is how the app learns the
contract address for the chain.

## Still gated before public mainnet use
- **The contract is not audited yet** (see `README.md`). Audit / community pentest first.
- App-side: add each new chain's asset list (native coin + the stablecoins to quote) and
  surface it in the trade UI + the website pairs.
- Keep mainnet trading behind the signed operator control document / kill switch, as on
  the Bitcoin side.

## Gas & minimum trade size (measured)
From `test/gas.test.js` (native-coin ops, gas units):

| op | gas |
|----|-----|
| deploy (one-time per chain) | ~1,141,500 |
| newSwap (lock) | ~51,400 |
| setReady | ~32,000 |
| claim | ~44,000 |
| refund | ~43,900 |
| **maker per-swap** (newSwap+setReady) | **~83,400** |
| full happy path (newSwap+setReady+claim) | ~127,400 |

Rough per-swap maker gas **cost** (83,400 gas) at representative prices:

| chain | gas price | native $ | per-swap cost | viable trade size |
|-------|-----------|----------|---------------|-------------------|
| Base / Arbitrum | ~0.03 gwei | ETH ~$3,500 | **< $0.02** | any |
| Polygon | ~40 gwei | POL ~$0.40 | **< $0.01** | any |
| BNB | ~1 gwei | BNB ~$600 | **~$0.05** | any |
| Ethereum | ~15 gwei | ETH ~$3,500 | **~$4–5** | **≥ ~$1,000** to keep gas < 0.5% |

Takeaway: on **L2s, Polygon and BNB** gas is negligible — no practical minimum. On
**Ethereum** set a minimum trade size (~$1,000) so the maker spread clears gas, the
same size-aware logic as the BTC fee curve. (Prices move; re-run the table from the
measured gas when deploying.)
