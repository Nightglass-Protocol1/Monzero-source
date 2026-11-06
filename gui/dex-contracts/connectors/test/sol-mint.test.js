'use strict';
// SolMintClient: it must assemble the correct Solana instructions to create a fixed-supply
// token or a 1/1 NFT, so Monzero Studio can hand them to the user's wallet to sign. Mint
// addresses are random per call, so these assert structure, program ids, and the Metaplex
// metadata encoding rather than fixed addresses.
const assert = require('assert');
const { PublicKey, SystemProgram, Keypair } = require('@solana/web3.js');
const spl = require('@solana/spl-token');
const SolMint = require('../../../../website/app/sol-mint.js');

// a real on-curve wallet pubkey (ATAs require the owner to be on-curve)
const PAYER = Keypair.generate().publicKey;
const RENT = 1_461_600; // typical mint rent-exempt lamports (caller supplies it)

describe('SolMintClient — Studio asset creation', () => {
  it('buildCreateToken assembles create-mint -> init -> ATA -> mintTo -> lock', () => {
    const { mint, ata, instructions, signers } = SolMint.buildCreateToken({
      payer: PAYER, decimals: 6, supply: 1_000_000_000n, rentLamports: RENT });
    assert.strictEqual(instructions.length, 5, 'fixed supply adds the authority-drop ix');
    assert.deepStrictEqual(signers, [mint], 'the mint keypair co-signs');
    assert.ok(instructions[0].programId.equals(SystemProgram.programId), 'createAccount');
    assert.ok(instructions[1].programId.equals(spl.TOKEN_PROGRAM_ID), 'initializeMint2');
    assert.ok(instructions[3].programId.equals(spl.TOKEN_PROGRAM_ID), 'mintTo');
    assert.ok(instructions[4].programId.equals(spl.TOKEN_PROGRAM_ID), 'setAuthority (lock)');
    assert.ok(ata.equals(spl.getAssociatedTokenAddressSync(mint.publicKey, PAYER)), 'creator ATA');
  });

  it('buildCreateToken can leave the supply mintable (no lock ix)', () => {
    const { instructions } = SolMint.buildCreateToken({
      payer: PAYER, decimals: 0, supply: 100n, rentLamports: RENT, fixedSupply: false });
    assert.strictEqual(instructions.length, 4);
  });

  it('buildCreateToken attaches Metaplex metadata when a name is given', () => {
    const r = SolMint.buildCreateToken({ payer: PAYER, decimals: 9, supply: 1000n, rentLamports: RENT,
      name: 'Monzero Token', symbol: 'MZT', uri: 'https://x/t.json' });
    assert.strictEqual(r.instructions.length, 6, 'create+init+ATA+mintTo+metadata+lock');
    const meta = r.instructions[4];
    assert.ok(meta.programId.equals(SolMint.METADATA_PROGRAM_ID), 'metadata ix');
    assert.strictEqual(meta.data[0], 33, 'CreateMetadataAccountV3');
    assert.ok(Buffer.from(meta.data).includes(Buffer.from('Monzero Token', 'utf8')), 'name embedded');
    assert.ok(r.metadata.equals(SolMint.metadataPda(r.mint.publicKey)), 'metadata PDA returned');
  });

  it('buildCreateNft makes a 1/1 mint with on-chain Metaplex metadata', () => {
    const name = 'Monzero Genesis #1';
    const { mint, metadata, instructions, signers } = SolMint.buildCreateNft({
      payer: PAYER, name, symbol: 'MZN', uri: 'https://arweave.net/abc.json', rentLamports: RENT });
    assert.strictEqual(instructions.length, 6);
    assert.deepStrictEqual(signers, [mint]);
    // the metadata instruction (index 4)
    const meta = instructions[4];
    assert.ok(meta.programId.equals(SolMint.METADATA_PROGRAM_ID), 'Metaplex Token Metadata program');
    assert.strictEqual(meta.data[0], 33, 'CreateMetadataAccountV3 discriminator');
    assert.ok(Buffer.from(meta.data).includes(Buffer.from(name, 'utf8')), 'name embedded');
    assert.ok(Buffer.from(meta.data).includes(Buffer.from('MZN', 'utf8')), 'symbol embedded');
    assert.ok(Buffer.from(meta.data).includes(Buffer.from('https://arweave.net/abc.json', 'utf8')), 'uri embedded');
    // metadata account is the Metaplex PDA, and it's the first key of the ix
    assert.ok(metadata.equals(SolMint.metadataPda(mint.publicKey)));
    assert.ok(meta.keys[0].pubkey.equals(metadata) && meta.keys[0].isWritable);
    // last ix drops the mint authority so it stays a true 1/1
    assert.ok(instructions[5].programId.equals(spl.TOKEN_PROGRAM_ID));
  });

  it('metadataPda is deterministic', () => {
    const m = new PublicKey('CktRuQ2mttgRGkXJtyksdKHjUdc2C4TgDzyB98oEzy8');
    assert.ok(SolMint.metadataPda(m).equals(SolMint.metadataPda(m)));
  });

  it('rejects invalid inputs (guards before a user signs)', () => {
    assert.throws(() => SolMint.buildCreateToken({ payer: PAYER, decimals: 12, supply: 1n, rentLamports: RENT }), /decimals/);
    assert.throws(() => SolMint.buildCreateToken({ payer: PAYER, decimals: 0, supply: 0n, rentLamports: RENT }), /supply/);
    assert.throws(() => SolMint.buildCreateNft({ payer: PAYER, name: 'x'.repeat(33), uri: 'u', rentLamports: RENT }), /name exceeds/);
    assert.throws(() => SolMint.buildCreateNft({ payer: PAYER, name: 'ok', uri: '', rentLamports: RENT }), /required/);
  });
});
