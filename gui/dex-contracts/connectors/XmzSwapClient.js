// Wallet-agnostic client for the XmzSwap contract. Works with ANY ethers v6 Signer —
// MetaMask (injected), Ledger (@ledgerhq signer), or WalletConnect — because they all
// expose the same Signer interface. The GUI/web UI calls these methods; the wallet just
// signs. Non-custodial: keys stay in the user's wallet; this only builds/sends calls.
const { ethers } = require("ethers");
const ARTIFACT = require("../artifacts/contracts/XmzSwap.sol/XmzSwap.json");

const ZERO = "0x0000000000000000000000000000000000000000";

// Generate a swap secret: the secret is a secp256k1 private key, committed to as the
// address of secret*G (the contract verifies with ecrecover). Keep `secret` private;
// publish only `commitment`.
function makeSecret() {
  const w = ethers.Wallet.createRandom();
  return { secret: w.privateKey, commitment: w.address };
}

class XmzSwapClient {
  constructor(contractAddress, signerOrProvider) {
    this.address = ethers.getAddress(contractAddress);
    this.contract = new ethers.Contract(this.address, ARTIFACT.abi, signerOrProvider);
  }

  _tuple(s) {
    return [s.owner, s.claimer, s.claimCommitment, s.refundCommitment, s.timeout1, s.timeout2, s.asset, s.value, s.nonce];
  }

  // EVM-side trader locks funds. `asset` = ZERO for the native coin, else an ERC-20
  // address (approve it first for tokens). Returns the full swap struct + id to persist.
  async lock({ claimer, claimCommitment, refundCommitment, duration1, duration2, asset = ZERO, value, nonce }) {
    const overrides = asset === ZERO ? { value } : {};
    const tx = await this.contract.newSwap(claimer, claimCommitment, refundCommitment, duration1, duration2, asset, value, nonce, overrides);
    const receipt = await tx.wait();
    const ev = receipt.logs.map((l) => { try { return this.contract.interface.parseLog(l); } catch { return null; } }).find((e) => e?.name === "New");
    if (!ev) throw new Error("lock: New event not found");
    const swap = { owner: ev.args.owner, claimer, claimCommitment, refundCommitment, timeout1: ev.args.timeout1, timeout2: ev.args.timeout2, asset, value, nonce };
    return { swap, id: ev.args.swapId, txHash: receipt.hash };
  }

  // Owner confirms the XMZ lock (only before timeout1).
  async confirm(swap) { return (await (await this.contract.setReady(this._tuple(swap))).wait()).hash; }

  // Claimer takes the funds by revealing the claim secret.
  async claim(swap, claimSecret) { return (await (await this.contract.connect(this.contract.runner).claim(this._tuple(swap), claimSecret)).wait()).hash; }

  // Owner refunds (early while pending, or after timeout2) by revealing the refund secret.
  async refund(swap, refundSecret) { return (await (await this.contract.refund(this._tuple(swap), refundSecret)).wait()).hash; }

  // Stage: 0 INVALID, 1 PENDING, 2 READY, 3 COMPLETED.
  async stage(swap) { return Number(await this.contract.swaps(await this.contract.swapId(this._tuple(swap)))); }
}

module.exports = { XmzSwapClient, makeSecret, ZERO };
