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

  // Approve an ERC-20 for a token swap (call before lock() with a token asset).
  async approveToken(tokenAddress, amount) {
    const erc20 = new ethers.Contract(tokenAddress, ["function approve(address,uint256) returns (bool)"], this.contract.runner);
    return (await (await erc20.approve(this.address, amount)).wait()).hash;
  }

  // Claimer signs a GASLESS claim: a relayer can then submit it and take `fee` for the
  // gas. Naming the relayer stops a front-runner from stealing the fee. (this client must
  // be bound to the CLAIMER's signer.)
  async signRelayClaim({ id, fee, relayer }) {
    const net = await this.contract.runner.provider.getNetwork();
    const inner = ethers.keccak256(ethers.AbiCoder.defaultAbiCoder().encode(
      ["string", "uint256", "address", "bytes32", "uint256", "address"],
      ["XMZ-SWAP-RELAY-V1", net.chainId, this.address, id, fee, relayer]));
    const sig = ethers.Signature.from(await this.contract.runner.signMessage(ethers.getBytes(inner)));
    return { fee, relayer, v: sig.v, r: sig.r, s: sig.s };
  }

  // Relayer submits the claimer's signed claim, revealing `secret`; gets `fee`, the
  // claimer gets the rest. (this client must be bound to the RELAYER's signer.)
  async claimRelayer(swap, secret, rc) {
    return (await (await this.contract.claimRelayer(this._tuple(swap), secret, rc.fee, rc.v, rc.r, rc.s)).wait()).hash;
  }

  // Read the secret revealed on-chain when a swap settled (Claimed or Refunded). This is
  // how the counterparty learns the secret needed to complete/settle their own side.
  async revealedSecret(swapId) {
    const claimed = await this.contract.queryFilter(this.contract.filters.Claimed(swapId));
    if (claimed.length) return { event: "Claimed", secret: claimed[claimed.length - 1].args.secret };
    const refunded = await this.contract.queryFilter(this.contract.filters.Refunded(swapId));
    if (refunded.length) return { event: "Refunded", secret: refunded[refunded.length - 1].args.secret };
    return null;
  }

  // Stage: 0 INVALID, 1 PENDING, 2 READY, 3 COMPLETED.
  async stage(swap) { return Number(await this.contract.swaps(await this.contract.swapId(this._tuple(swap)))); }
}

module.exports = { XmzSwapClient, makeSecret, ZERO };
