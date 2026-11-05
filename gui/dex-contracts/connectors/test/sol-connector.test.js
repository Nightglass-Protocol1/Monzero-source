'use strict';
// Cross-language correctness for SolSwapClient: it must reproduce, byte-for-byte, the
// on-chain Rust program's reference vectors (solana/program-tests/tests/vectors.rs). If any
// of these drift, the client would build an instruction the program rejects — or worse, a
// valid-looking one against the wrong PDA — so these are fund-safety tests.
const assert = require('assert');
const { PublicKey } = require('@solana/web3.js');
const { SolSwapClient } = require('../SolSwapClient');

const bytes = (v) => Buffer.from(new Array(32).fill(v));
const PROGRAM = new PublicKey(bytes(1));   // [1;32]
const OWNER = new PublicKey(bytes(2));     // [2;32]
const CLAIMER = new PublicKey(bytes(3));   // [3;32]
const CC = bytes(0x11);
const RC = bytes(0x22);
const AMOUNT = 2_000_000_000n;
const NONCE = 1n;

describe('SolSwapClient cross-language vectors', () => {
  const c = new SolSwapClient(PROGRAM);

  it('fixed pubkeys match the Rust base58', () => {
    assert.strictEqual(PROGRAM.toBase58(), '4vJ9JU1bJJE96FWSJKvHsmmFADCg4gpZQff4P3bkLKi');
    assert.strictEqual(OWNER.toBase58(), '8qbHbw2BbbTHBW1sbeqakYXVKRQM8Ne7pLK7m6CVfeR');
    assert.strictEqual(CLAIMER.toBase58(), 'CktRuQ2mttgRGkXJtyksdKHjUdc2C4TgDzyB98oEzy8');
  });

  it('derives the same escrow PDA + bump as the program', () => {
    const [pda, bump] = c.swapPda(OWNER, CLAIMER, CC, RC, AMOUNT, NONCE);
    assert.strictEqual(pda.toBase58(), 'HdCYbuvmah5qx9mwxJYLhwyQaCAJ52aYwV292Q4Suor9');
    assert.strictEqual(bump, 255);
  });

  it('encodes the New instruction exactly like the program expects', () => {
    const { ix, pda } = c.newSwap({
      owner: OWNER, claimer: CLAIMER, claimCommit: CC, refundCommit: RC,
      duration1: 100, duration2: 100, amount: AMOUNT, nonce: NONCE });
    const expected =
      '00' + '11'.repeat(32) + '22'.repeat(32) +
      '6400000000000000' + '6400000000000000' +
      '0094357700000000' + '0100000000000000';
    assert.strictEqual(ix.data.toString('hex'), expected);
    assert.strictEqual(pda.toBase58(), 'HdCYbuvmah5qx9mwxJYLhwyQaCAJ52aYwV292Q4Suor9');
    // account order/flags the program reads
    assert.deepStrictEqual(ix.keys.map(k => [k.isSigner, k.isWritable]),
      [[true, true], [false, false], [false, true], [false, false]]);
  });

  it('commit() matches the on-chain curve25519 commitment', () => {
    const secret = Buffer.from('0707070707070707070707070707070707070707070707070707070707070700', 'hex');
    assert.strictEqual(SolSwapClient.commit(secret).toString('hex'),
      'b5fc800b65e9d2545181aa64bf03bb393d9958d72c4ba5e060585bf1474ff558');
  });

  it('claim / refund / setReady opcodes + account flags', () => {
    const [pda] = c.swapPda(OWNER, CLAIMER, CC, RC, AMOUNT, NONCE);
    const secret = bytes(7);
    const claim = c.claim({ claimer: CLAIMER, pda, owner: OWNER, secret });
    assert.strictEqual(claim.data[0], 2);
    assert.strictEqual(claim.data.length, 33);
    assert.deepStrictEqual(claim.keys.map(k => [k.isSigner, k.isWritable]),
      [[true, true], [false, true], [false, true]]);
    const refund = c.refund({ owner: OWNER, pda, secret });
    assert.strictEqual(refund.data[0], 3);
    const ready = c.setReady({ owner: OWNER, pda });
    assert.strictEqual(ready.data[0], 1);
    assert.strictEqual(ready.data.length, 1);
  });

  it('rejects malformed inputs (fund-safety guards)', () => {
    assert.throws(() => SolSwapClient.commit(Buffer.alloc(31)), /32 bytes/);
    assert.throws(() => SolSwapClient.commit(Buffer.alloc(32)), /non-zero/);
    assert.throws(() => c.swapPda(OWNER, CLAIMER, Buffer.alloc(31), RC, AMOUNT, NONCE), /32 bytes/);
  });

  it('derives the SPL vault PDA like the program', () => {
    const [pda] = c.swapPda(OWNER, CLAIMER, CC, RC, AMOUNT, NONCE);
    const [vault, vbump] = c.vaultPda(pda);
    assert.strictEqual(vault.toBase58(), '8Eb7FLSiT39Q4S6qdMa3bbszgqzi4PSqn1mLmk9DW5Lr');
    assert.strictEqual(vbump, 254);
  });

  it('encodes NewSpl (opcode 4) + account order', () => {
    const MINT = new PublicKey(bytes(5));
    const OWNER_TOK = new PublicKey(bytes(6));
    const { ix, pda, vault } = c.newSpl({
      owner: OWNER, claimer: CLAIMER, claimCommit: CC, refundCommit: RC,
      duration1: 100, duration2: 100, amount: AMOUNT, nonce: NONCE, mint: MINT, ownerToken: OWNER_TOK });
    const expected =
      '04' + '11'.repeat(32) + '22'.repeat(32) +
      '6400000000000000' + '6400000000000000' + '0094357700000000' + '0100000000000000';
    assert.strictEqual(ix.data.toString('hex'), expected);
    assert.strictEqual(pda.toBase58(), 'HdCYbuvmah5qx9mwxJYLhwyQaCAJ52aYwV292Q4Suor9');
    assert.strictEqual(vault.toBase58(), '8Eb7FLSiT39Q4S6qdMa3bbszgqzi4PSqn1mLmk9DW5Lr');
    assert.strictEqual(ix.keys.length, 8);
    assert.deepStrictEqual(ix.keys.map(k => [k.isSigner, k.isWritable]),
      [[true, true], [false, false], [false, true], [false, false], [false, true], [false, true], [false, false], [false, false]]);
  });

  it('claimSpl / refundSpl opcodes + account counts', () => {
    const [pda] = c.swapPda(OWNER, CLAIMER, CC, RC, AMOUNT, NONCE);
    const [vault] = c.vaultPda(pda);
    const MINT = new PublicKey(bytes(5));
    const TOK = new PublicKey(bytes(6));
    const claim = c.claimSpl({ claimer: CLAIMER, swap: pda, claimerToken: TOK, vault, owner: OWNER, mint: MINT, secret: bytes(7) });
    assert.strictEqual(claim.data[0], 5);
    assert.strictEqual(claim.keys.length, 7);
    const refund = c.refundSpl({ owner: OWNER, swap: pda, ownerToken: TOK, vault, mint: MINT, secret: bytes(7) });
    assert.strictEqual(refund.data[0], 6);
    assert.strictEqual(refund.keys.length, 6);
  });
});
