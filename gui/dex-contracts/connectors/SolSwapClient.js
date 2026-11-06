'use strict';
// SolSwapClient — wallet-agnostic client for the Monzero XMZ<->Solana swap program, the
// Solana counterpart of XmzSwapClient.js. Builds the on-chain instructions (new / set_ready
// / claim / refund), derives the escrow PDA, and computes ed25519 secret commitments in the
// SAME group as the XMZ side. It holds no keys and signs nothing: the caller signs with any
// Solana wallet (Phantom, Solflare, a Ledger Solana app, or a Keypair) and submits.
//
// Correctness is fund-critical: a mis-encoded instruction or wrong PDA would strand the
// escrow. The encoding + PDA derivation + commit() are cross-checked byte-for-byte against
// the on-chain Rust program's reference vectors (see test/sol-connector.test.js).
const { PublicKey, SystemProgram, TransactionInstruction, Ed25519Program,
  SYSVAR_INSTRUCTIONS_PUBKEY } = require('@solana/web3.js');
const { ed25519 } = require('@noble/curves/ed25519');

const SEED = Buffer.from('swap');
const VAULT_SEED = Buffer.from('vault');
const RELAY_PREFIX = Buffer.from('monzero-sol-relay-v1');
// classic SPL token program (USDC/USDT are classic mints)
const TOKEN_PROGRAM_ID = new PublicKey('TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA');

function u64le(n) { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(n)); return b; }
function buf32(x) {
  const b = Buffer.isBuffer(x) ? x : Buffer.from(x);
  if (b.length !== 32) throw new Error(`expected 32 bytes, got ${b.length}`);
  return b;
}
function bytesToNumberLE(bytes) {
  let n = 0n;
  for (let i = bytes.length - 1; i >= 0; i--) n = (n << 8n) | BigInt(bytes[i]);
  return n;
}

class SolSwapClient {
  /** @param {string|PublicKey} programId deployed swap program id (pinned in the app build) */
  constructor(programId) { this.programId = new PublicKey(programId); }

  /** Compressed ed25519 commitment A = s*B for a 32-byte secret (same group as XMZ). */
  static commit(secret32) {
    const s = bytesToNumberLE(buf32(secret32));
    if (s === 0n) throw new Error('secret must be non-zero');
    return Buffer.from(ed25519.ExtendedPoint.BASE.multiply(s).toRawBytes());
  }

  /** Deterministic escrow address; identical inputs => identical PDA (replay-safe). */
  swapPda(owner, claimer, claimCommit, refundCommit, amount, nonce) {
    return PublicKey.findProgramAddressSync([
      SEED, new PublicKey(owner).toBuffer(), new PublicKey(claimer).toBuffer(),
      buf32(claimCommit), buf32(refundCommit), u64le(amount), u64le(nonce),
    ], this.programId);
  }

  /** Lock: owner escrows `amount` lamports; timeouts are now+d1 and now+d1+d2 on-chain. */
  newSwap({ owner, claimer, claimCommit, refundCommit, duration1, duration2, amount, nonce }) {
    const [pda, bump] = this.swapPda(owner, claimer, claimCommit, refundCommit, amount, nonce);
    const data = Buffer.concat([
      Buffer.from([0]), buf32(claimCommit), buf32(refundCommit),
      u64le(duration1), u64le(duration2), u64le(amount), u64le(nonce)]);
    const ix = new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(owner), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(claimer), isSigner: false, isWritable: false },
      { pubkey: pda, isSigner: false, isWritable: true },
      { pubkey: SystemProgram.programId, isSigner: false, isWritable: false }]});
    return { ix, pda, bump };
  }

  /** Owner confirms the XMZ lock (only before timeout1). */
  setReady({ owner, pda }) {
    return new TransactionInstruction({ programId: this.programId, data: Buffer.from([1]), keys: [
      { pubkey: new PublicKey(owner), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(pda), isSigner: false, isWritable: true }]});
  }

  /** Claimer reveals the claim secret; the escrow pays out and closes (rent back to owner). */
  claim({ claimer, pda, owner, secret }) {
    const data = Buffer.concat([Buffer.from([2]), buf32(secret)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(claimer), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(pda), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(owner), isSigner: false, isWritable: true }]});
  }

  /** Owner reveals the refund secret (early while Pending, or after timeout2); escrow closes. */
  refund({ owner, pda, secret }) {
    const data = Buffer.concat([Buffer.from([3]), buf32(secret)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(owner), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(pda), isSigner: false, isWritable: true }]});
  }

  // --- SPL-token swaps (USDC/USDT) --------------------------------------------------------
  /** The PDA-owned token vault holding the escrowed SPL tokens for a given swap PDA. */
  vaultPda(swapPda) {
    return PublicKey.findProgramAddressSync([VAULT_SEED, new PublicKey(swapPda).toBuffer()], this.programId);
  }

  /** Lock `amount` base units of `mint` from `ownerToken` into the swap's PDA vault. */
  newSpl({ owner, claimer, claimCommit, refundCommit, duration1, duration2, amount, nonce, mint, ownerToken }) {
    const [pda, bump] = this.swapPda(owner, claimer, claimCommit, refundCommit, amount, nonce);
    const [vault, vaultBump] = this.vaultPda(pda);
    const data = Buffer.concat([
      Buffer.from([4]), buf32(claimCommit), buf32(refundCommit),
      u64le(duration1), u64le(duration2), u64le(amount), u64le(nonce)]);
    const ix = new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(owner), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(claimer), isSigner: false, isWritable: false },
      { pubkey: pda, isSigner: false, isWritable: true },
      { pubkey: new PublicKey(mint), isSigner: false, isWritable: false },
      { pubkey: new PublicKey(ownerToken), isSigner: false, isWritable: true },
      { pubkey: vault, isSigner: false, isWritable: true },
      { pubkey: TOKEN_PROGRAM_ID, isSigner: false, isWritable: false },
      { pubkey: SystemProgram.programId, isSigner: false, isWritable: false }]});
    return { ix, pda, bump, vault, vaultBump };
  }

  /** Claimer reveals the claim secret; the vault pays out tokens to `claimerToken` + closes. */
  claimSpl({ claimer, swap, claimerToken, vault, owner, mint, secret }) {
    const data = Buffer.concat([Buffer.from([5]), buf32(secret)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(claimer), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(swap), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(claimerToken), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(vault), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(owner), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(mint), isSigner: false, isWritable: false },
      { pubkey: TOKEN_PROGRAM_ID, isSigner: false, isWritable: false }]});
  }

  /** Owner reveals the refund secret; the vault returns tokens to `ownerToken` + closes. */
  refundSpl({ owner, swap, ownerToken, vault, mint, secret }) {
    const data = Buffer.concat([Buffer.from([6]), buf32(secret)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(owner), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(swap), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(ownerToken), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(vault), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(mint), isSigner: false, isWritable: false },
      { pubkey: TOKEN_PROGRAM_ID, isSigner: false, isWritable: false }]});
  }

  // --- gasless relayed claim (native SOL) -------------------------------------------------
  /** The exact bytes the claimer signs to authorize `(swap, fee, relayer)`. */
  relayMessage(swap, fee, relayer) {
    return Buffer.concat([RELAY_PREFIX, new PublicKey(swap).toBuffer(), u64le(fee), new PublicKey(relayer).toBuffer()]);
  }

  /** The Ed25519 program instruction proving the claimer signed `relayMessage(...)`. Place
   *  it immediately before claimRelayed in the transaction. `signature` is 64 bytes. */
  static ed25519AuthIx({ claimer, message, signature }) {
    return Ed25519Program.createInstructionWithPublicKey({
      publicKey: new PublicKey(claimer).toBytes(), message, signature });
  }

  /** The relayed claim itself: relayer is the fee payer/signer and takes `fee`; the claimer
   *  receives amount-fee without needing any SOL. Submit [ed25519AuthIx, claimRelayed]. */
  claimRelayed({ relayer, swap, claimer, owner, secret, fee }) {
    const data = Buffer.concat([Buffer.from([7]), buf32(secret), u64le(fee)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(relayer), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(swap), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(claimer), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(owner), isSigner: false, isWritable: true },
      { pubkey: SYSVAR_INSTRUCTIONS_PUBKEY, isSigner: false, isWritable: false }]});
  }

  /** Gasless SPL claim: relayer submits + takes `fee` in the token; claimer gets amount-fee.
   *  The claimer still signs `relayMessage(...)` (same auth); place ed25519AuthIx before this. */
  claimRelayedSpl({ relayer, swap, claimerToken, relayerToken, vault, owner, mint, secret, fee }) {
    const data = Buffer.concat([Buffer.from([8]), buf32(secret), u64le(fee)]);
    return new TransactionInstruction({ programId: this.programId, data, keys: [
      { pubkey: new PublicKey(relayer), isSigner: true, isWritable: true },
      { pubkey: new PublicKey(swap), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(claimerToken), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(relayerToken), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(vault), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(owner), isSigner: false, isWritable: true },
      { pubkey: new PublicKey(mint), isSigner: false, isWritable: false },
      { pubkey: TOKEN_PROGRAM_ID, isSigner: false, isWritable: false },
      { pubkey: SYSVAR_INSTRUCTIONS_PUBKEY, isSigner: false, isWritable: false }]});
  }
}

module.exports = { SolSwapClient };
