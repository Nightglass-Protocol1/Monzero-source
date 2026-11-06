#!/usr/bin/env node
'use strict';
// Smoke test for Monzero Studio's Solana creation engine (website/app/sol-mint.js):
// create a fixed-supply SPL token and a 1/1 NFT on a real cluster and verify the on-chain
// result — including that the hand-built Metaplex CreateMetadataAccountV3 instruction is
// accepted by the real Metaplex Token Metadata program.
//
// Run it (from gui/dex-contracts):  npm run smoke:solana  [-- --rpc URL --keypair FILE]
//
//   Local validator (no funding/faucet needed — clone Metaplex first, in another shell):
//     solana-test-validator --reset --quiet --url https://api.mainnet-beta.solana.com \
//       --clone-upgradeable-program metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s
//     npm run smoke:solana
//
//   Devnet (fund the printed payer first — devnet's RPC airdrop is closed, use faucet.solana.com):
//     npm run smoke:solana -- --rpc https://api.devnet.solana.com --keypair payer.json
//
// Not part of CI: it needs a validator + the Metaplex program + network.
const path = require('path');
const fs = require('fs');

// Make @solana/* (and the engine's own requires) resolvable from gui/dex-contracts/node_modules,
// even though the engine lives under website/app with no node_modules of its own.
const Module = require('module');
const NM = path.resolve(__dirname, '../../node_modules');
process.env.NODE_PATH = NM + (process.env.NODE_PATH ? path.delimiter + process.env.NODE_PATH : '');
Module._initPaths();

const web3 = require('@solana/web3.js');
const spl = require('@solana/spl-token');
const SolMint = require(path.resolve(__dirname, '../../../../website/app/sol-mint.js'));

const arg = (name, def) => { const i = process.argv.indexOf(name); return i >= 0 ? process.argv[i + 1] : def; };
const assert = (cond, msg) => { if (!cond) { console.error('FAIL:', msg); process.exit(1); } };

async function ensureFunded(conn, payer) {
  let bal = await conn.getBalance(payer.publicKey);
  if (bal >= 50_000_000) return bal;
  try {
    const sig = await conn.requestAirdrop(payer.publicKey, 2_000_000_000);
    await conn.confirmTransaction(sig, 'confirmed');
    return conn.getBalance(payer.publicKey);
  } catch (e) {
    console.error(`\nPayer ${payer.publicKey.toBase58()} has ${bal / 1e9} SOL and airdrop failed (${e.message}).`);
    console.error('Fund it (faucet.solana.com for devnet) and re-run with --keypair.');
    process.exit(2);
  }
}

async function send(conn, payer, built, label) {
  const tx = new web3.Transaction().add(...built.instructions);
  tx.feePayer = payer.publicKey;
  tx.recentBlockhash = (await conn.getLatestBlockhash()).blockhash;
  tx.sign(payer, ...built.signers);
  const sig = await conn.sendRawTransaction(tx.serialize());
  await conn.confirmTransaction(sig, 'confirmed');
  console.log(`  ${label} tx: ${sig}`);
  return sig;
}

(async () => {
  const rpc = arg('--rpc', 'http://127.0.0.1:8899');
  const clusterQ = rpc.includes('devnet') ? '?cluster=devnet'
    : rpc.includes('testnet') ? '?cluster=testnet'
    : /127\.0\.0\.1|localhost/.test(rpc) ? '?cluster=custom' : '';
  const conn = new web3.Connection(rpc, 'confirmed');
  console.log('cluster:', rpc);

  const kpFile = arg('--keypair');
  const payer = kpFile
    ? web3.Keypair.fromSecretKey(Uint8Array.from(JSON.parse(fs.readFileSync(kpFile, 'utf8'))))
    : web3.Keypair.generate();
  const bal = await ensureFunded(conn, payer);
  console.log('payer: ', payer.publicKey.toBase58(), `(${bal / 1e9} SOL)`);
  const rent = await conn.getMinimumBalanceForRentExemption(spl.MINT_SIZE);

  // Both token + NFT now attach Metaplex metadata, so the program must be present.
  const mpl = await conn.getAccountInfo(SolMint.METADATA_PROGRAM_ID);
  assert(mpl && mpl.executable, 'Metaplex Token Metadata program is not on this cluster (clone it on a local validator)');

  console.log('\n[1/2] fixed-supply SPL token (+ metadata)');
  let b = SolMint.buildCreateToken({ payer: payer.publicKey, decimals: 6, supply: 1_000_000_000n, rentLamports: rent,
    name: 'Monzero Smoke Token', symbol: 'MST', uri: 'https://monzero.org/app/creators/materials/metadata-example.json' });
  await send(conn, payer, b, 'token');
  let m = await spl.getMint(conn, b.mint.publicKey);
  assert(m.decimals === 6 && m.supply === 1_000_000_000n && m.mintAuthority === null, 'token: 6dp, fixed supply, locked');
  let tmeta = await conn.getAccountInfo(b.metadata);
  assert(tmeta && tmeta.owner.equals(SolMint.METADATA_PROGRAM_ID), 'token has Metaplex metadata');
  console.log('  ✓ token', b.mint.publicKey.toBase58(), '— supply locked + on-chain metadata');

  console.log('\n[2/2] 1/1 NFT + Metaplex metadata');
  b = SolMint.buildCreateNft({ payer: payer.publicKey, name: 'Monzero Smoke NFT', symbol: 'MZS',
    uri: 'https://monzero.org/app/creators/materials/metadata-example.json', rentLamports: rent });
  await send(conn, payer, b, 'nft');
  m = await spl.getMint(conn, b.mint.publicKey);
  const acct = await spl.getAccount(conn, spl.getAssociatedTokenAddressSync(b.mint.publicKey, payer.publicKey));
  const meta = await conn.getAccountInfo(b.metadata);
  assert(m.decimals === 0 && m.supply === 1n && m.mintAuthority === null, 'NFT is a locked 1/1');
  assert(acct.amount === 1n, 'NFT held by creator');
  assert(meta && meta.owner.equals(SolMint.METADATA_PROGRAM_ID), 'metadata owned by Metaplex');
  console.log('  ✓ nft', b.mint.publicKey.toBase58(), '— 1/1 with on-chain metadata');
  console.log('    explorer: https://explorer.solana.com/address/' + b.mint.publicKey.toBase58() + clusterQ);

  console.log('\nSMOKE TEST PASSED ✓');
})().catch((e) => { console.error('ERROR:', e.message); if (e.logs) console.error(e.logs.join('\n')); process.exit(1); });
