# EVM wallet connectors

`XmzSwapClient` drives the swap contract with **any ethers v6 Signer**, so the same code
works with MetaMask, a Ledger, or WalletConnect — the wallet only signs; keys never
leave it (non-custodial). Tested end-to-end in `test/connector.test.js`.

## Get a signer (pick the wallet)

```js
const { ethers } = require("ethers");
const { XmzSwapClient, makeSecret } = require("./XmzSwapClient");

// MetaMask / any injected wallet (also how a Ledger connected *through* MetaMask works):
const provider = new ethers.BrowserProvider(window.ethereum);
await provider.send("eth_requestAccounts", []);
const signer = await provider.getSigner();

// Ledger directly (desktop), via @ledgerhq + a small adapter that returns an ethers Signer.
// WalletConnect v2: build an EthersProvider from the WC provider, then getSigner().
// In all three cases you end up with an ethers `signer`.

const client = new XmzSwapClient(CONTRACT_ADDRESS, signer); // address pinned by the app, not the website
```

## Do a swap (EVM-side trader locks, counterparty claims)

```js
const c = makeSecret(), r = makeSecret();          // keep .secret private, publish .commitment
const { swap, id } = await client.lock({
  claimer, claimCommitment: c.commitment, refundCommitment: r.commitment,
  duration1: 3*3600, duration2: 6*3600,            // cancel / punish windows (seconds)
  asset: ethers.ZeroAddress, value: amount, nonce: 1n,   // ZeroAddress = native coin; else ERC-20 (approve first)
});                                                 // wallet signs the lock
await client.confirm(swap);                         // once the XMZ side is locked
// counterparty: await client.claim(swap, c.secret);
// to back out:  await client.refund(swap, r.secret);
```

`client.stage(swap)` → 1 PENDING · 2 READY · 3 COMPLETED.

## Security
- The **contract address is pinned by the app**, never fetched from the website.
- Secrets are secp256k1 keys committed as `secret·G`'s address; never send `.secret`
  until it's time to claim/refund.
- For ERC-20s, `approve` the contract for `value` first; the contract rejects
  fee-on-transfer tokens.
