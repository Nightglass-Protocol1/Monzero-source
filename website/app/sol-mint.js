'use strict';
// SolMintClient — build the Solana instructions to CREATE assets from Monzero Studio:
// a fixed-supply SPL token, or a 1/1 NFT with on-chain Metaplex metadata. It constructs
// instructions only and holds no keys: the caller assembles a transaction, the user's
// Solana wallet (Phantom/Solflare/Ledger) signs as payer, the generated mint keypair
// co-signs, and the wallet submits. Works in Node (tests) and in the browser (the Studio
// loads @solana/web3.js + @solana/spl-token from a CDN as globals).
(function (root, factory) {
  const web3 = (typeof require !== 'undefined') ? require('@solana/web3.js') : root.solanaWeb3;
  const spl = (typeof require !== 'undefined') ? require('@solana/spl-token') : root.splToken;
  const mod = factory(web3, spl);
  if (typeof module !== 'undefined' && module.exports) module.exports = mod;
  else root.SolMintClient = mod;
})(typeof globalThis !== 'undefined' ? globalThis : this, function (web3, spl) {
  const { PublicKey, SystemProgram, Keypair, TransactionInstruction } = web3;

  // Metaplex Token Metadata program (mainnet + devnet).
  const METADATA_PROGRAM_ID = new PublicKey('metaqbxxUerdq28cj1RbAWkYQm3ybzjb6a8bt518x1s');

  // --- minimal borsh encoders for CreateMetadataAccountV3's DataV2 ---
  const u16le = (n) => { const b = Buffer.alloc(2); b.writeUInt16LE(n); return b; };
  const u32le = (n) => { const b = Buffer.alloc(4); b.writeUInt32LE(n); return b; };
  const u64le = (n) => { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(n)); return b; };
  const borshStr = (s) => { const b = Buffer.from(String(s), 'utf8'); return Buffer.concat([u32le(b.length), b]); };
  const NONE = Buffer.from([0]); // Option::None

  /** Master Edition PDA — makes an NFT a "printable" master (or, with maxSupply 0, locks it). */
  function masterEditionPda(mint) {
    return PublicKey.findProgramAddressSync(
      [Buffer.from('metadata'), METADATA_PROGRAM_ID.toBuffer(), new PublicKey(mint).toBuffer(), Buffer.from('edition')],
      METADATA_PROGRAM_ID)[0];
  }

  // Metaplex CreateMasterEditionV3 (discriminator 17): turns the 1/1 into a master edition and
  // moves the mint authority to the edition PDA. maxSupply: null = unlimited prints, N = N prints,
  // 0 = none (used for a collection parent).
  function createMasterEditionV3Ix({ mint, updateAuthority, mintAuthority, payer, maxSupply }) {
    const ms = (maxSupply == null) ? NONE : Buffer.concat([Buffer.from([1]), u64le(maxSupply)]);
    return new TransactionInstruction({
      programId: METADATA_PROGRAM_ID, data: Buffer.concat([Buffer.from([17]), ms]),
      keys: [
        { pubkey: masterEditionPda(mint), isSigner: false, isWritable: true },
        { pubkey: new PublicKey(mint), isSigner: false, isWritable: true },
        { pubkey: new PublicKey(updateAuthority), isSigner: true, isWritable: false },
        { pubkey: new PublicKey(mintAuthority), isSigner: true, isWritable: false },
        { pubkey: new PublicKey(payer), isSigner: true, isWritable: true },
        { pubkey: metadataPda(mint), isSigner: false, isWritable: true },
        { pubkey: spl.TOKEN_PROGRAM_ID, isSigner: false, isWritable: false },
        { pubkey: SystemProgram.programId, isSigner: false, isWritable: false },
      ],
    });
  }

  /** PDA of an NFT's Metaplex metadata account. */
  function metadataPda(mint) {
    return PublicKey.findProgramAddressSync(
      [Buffer.from('metadata'), METADATA_PROGRAM_ID.toBuffer(), new PublicKey(mint).toBuffer()],
      METADATA_PROGRAM_ID)[0];
  }

  function createMetadataV3Ix({ mint, mintAuthority, payer, updateAuthority, name, symbol, uri, sellerFeeBasisPoints, collectionSize }) {
    if (Buffer.from(name, 'utf8').length > 32) throw new Error('name exceeds 32 bytes');
    if (Buffer.from(symbol, 'utf8').length > 10) throw new Error('symbol exceeds 10 bytes');
    if (Buffer.from(uri, 'utf8').length > 200) throw new Error('uri exceeds 200 bytes');
    const data = Buffer.concat([
      Buffer.from([33]),                      // CreateMetadataAccountV3 discriminator
      borshStr(name), borshStr(symbol), borshStr(uri),
      u16le(sellerFeeBasisPoints || 0),
      NONE,                                   // creators: Option<Vec<Creator>>
      NONE,                                   // collection: Option<Collection>
      NONE,                                   // uses: Option<Uses>
      Buffer.from([1]),                       // isMutable = true
      // collectionDetails: None, or Some(V1 { size }) to mark this mint a collection parent
      (collectionSize == null ? NONE : Buffer.concat([Buffer.from([1, 0]), u64le(collectionSize)])),
    ]);
    return new TransactionInstruction({
      programId: METADATA_PROGRAM_ID, data,
      keys: [
        { pubkey: metadataPda(mint), isSigner: false, isWritable: true },
        { pubkey: new PublicKey(mint), isSigner: false, isWritable: false },
        { pubkey: new PublicKey(mintAuthority), isSigner: true, isWritable: false },
        { pubkey: new PublicKey(payer), isSigner: true, isWritable: true },
        { pubkey: new PublicKey(updateAuthority), isSigner: false, isWritable: false },
        { pubkey: SystemProgram.programId, isSigner: false, isWritable: false },
      ],
    });
  }

  function createMintAccountIx(payer, mint, rentLamports) {
    return SystemProgram.createAccount({
      fromPubkey: payer, newAccountPubkey: mint, space: spl.MINT_SIZE,
      lamports: rentLamports, programId: spl.TOKEN_PROGRAM_ID,
    });
  }

  /** Fixed-supply fungible token: create the mint, mint the full supply to the creator,
   *  attach on-chain Metaplex metadata (name/symbol/logo, so wallets show it), then (by
   *  default) drop the mint authority so the supply can never grow. */
  function buildCreateToken({ payer, decimals, supply, rentLamports, fixedSupply = true,
    freezeAuthority = null, name = '', symbol = '', uri = '' }) {
    const payerPk = new PublicKey(payer);
    const mint = Keypair.generate();
    if (!Number.isInteger(decimals) || decimals < 0 || decimals > 9) throw new Error('decimals must be 0..9');
    const amount = BigInt(supply);
    if (amount <= 0n) throw new Error('supply must be positive');
    const ata = spl.getAssociatedTokenAddressSync(mint.publicKey, payerPk);
    const freeze = freezeAuthority ? new PublicKey(freezeAuthority) : null;
    const instructions = [
      createMintAccountIx(payerPk, mint.publicKey, rentLamports),
      spl.createInitializeMint2Instruction(mint.publicKey, decimals, payerPk, freeze),
      spl.createAssociatedTokenAccountInstruction(payerPk, ata, payerPk, mint.publicKey),
      spl.createMintToInstruction(mint.publicKey, ata, payerPk, amount),
    ];
    // Metaplex metadata (needs the mint authority as signer, so add it before any drop).
    if (name) {
      instructions.push(createMetadataV3Ix({ mint: mint.publicKey, mintAuthority: payerPk,
        payer: payerPk, updateAuthority: payerPk, name, symbol, uri, sellerFeeBasisPoints: 0 }));
    }
    if (fixedSupply) {
      instructions.push(spl.createSetAuthorityInstruction(mint.publicKey, payerPk, spl.AuthorityType.MintTokens, null));
    }
    return { mint, ata, metadata: name ? metadataPda(mint.publicKey) : null, instructions, signers: [mint] };
  }

  /** A 1/1 NFT: a 0-decimal mint with exactly one unit, on-chain Metaplex metadata, and the
   *  mint authority dropped so it stays unique. */
  function buildCreateNft({ payer, name, symbol = '', uri, rentLamports, sellerFeeBasisPoints = 0 }) {
    const payerPk = new PublicKey(payer);
    const mint = Keypair.generate();
    if (!name || !uri) throw new Error('name and uri are required');
    const ata = spl.getAssociatedTokenAddressSync(mint.publicKey, payerPk);
    const metadata = metadataPda(mint.publicKey);
    const instructions = [
      createMintAccountIx(payerPk, mint.publicKey, rentLamports),
      spl.createInitializeMint2Instruction(mint.publicKey, 0, payerPk, payerPk),
      spl.createAssociatedTokenAccountInstruction(payerPk, ata, payerPk, mint.publicKey),
      spl.createMintToInstruction(mint.publicKey, ata, payerPk, 1n),
      createMetadataV3Ix({ mint: mint.publicKey, mintAuthority: payerPk, payer: payerPk,
        updateAuthority: payerPk, name, symbol, uri, sellerFeeBasisPoints }),
      spl.createSetAuthorityInstruction(mint.publicKey, payerPk, spl.AuthorityType.MintTokens, null),
    ];
    return { mint, ata, metadata, instructions, signers: [mint] };
  }

  /** A collection "parent" NFT: a 1/1 flagged as a sized collection, with a master edition
   *  (maxSupply 0 — not printable). NFTs can later be verified into it. */
  function buildCreateCollection({ payer, name, symbol = '', uri, rentLamports, sellerFeeBasisPoints = 0 }) {
    const payerPk = new PublicKey(payer);
    const mint = Keypair.generate();
    if (!name || !uri) throw new Error('name and uri are required');
    const ata = spl.getAssociatedTokenAddressSync(mint.publicKey, payerPk);
    const instructions = [
      createMintAccountIx(payerPk, mint.publicKey, rentLamports),
      spl.createInitializeMint2Instruction(mint.publicKey, 0, payerPk, payerPk),
      spl.createAssociatedTokenAccountInstruction(payerPk, ata, payerPk, mint.publicKey),
      spl.createMintToInstruction(mint.publicKey, ata, payerPk, 1n),
      createMetadataV3Ix({ mint: mint.publicKey, mintAuthority: payerPk, payer: payerPk,
        updateAuthority: payerPk, name, symbol, uri, sellerFeeBasisPoints, collectionSize: 0 }),
      createMasterEditionV3Ix({ mint: mint.publicKey, updateAuthority: payerPk, mintAuthority: payerPk, payer: payerPk, maxSupply: 0 }),
    ];
    return { mint, ata, metadata: metadataPda(mint.publicKey), masterEdition: masterEditionPda(mint.publicKey), instructions, signers: [mint] };
  }

  /** A master-edition NFT from which up to `maxSupply` numbered prints can be minted
   *  (null = unlimited). */
  function buildCreateEdition({ payer, name, symbol = '', uri, rentLamports, maxSupply = null, sellerFeeBasisPoints = 0 }) {
    const payerPk = new PublicKey(payer);
    const mint = Keypair.generate();
    if (!name || !uri) throw new Error('name and uri are required');
    if (maxSupply != null && (!Number.isInteger(maxSupply) || maxSupply < 0)) throw new Error('maxSupply must be a non-negative integer or null');
    const ata = spl.getAssociatedTokenAddressSync(mint.publicKey, payerPk);
    const instructions = [
      createMintAccountIx(payerPk, mint.publicKey, rentLamports),
      spl.createInitializeMint2Instruction(mint.publicKey, 0, payerPk, payerPk),
      spl.createAssociatedTokenAccountInstruction(payerPk, ata, payerPk, mint.publicKey),
      spl.createMintToInstruction(mint.publicKey, ata, payerPk, 1n),
      createMetadataV3Ix({ mint: mint.publicKey, mintAuthority: payerPk, payer: payerPk,
        updateAuthority: payerPk, name, symbol, uri, sellerFeeBasisPoints }),
      createMasterEditionV3Ix({ mint: mint.publicKey, updateAuthority: payerPk, mintAuthority: payerPk, payer: payerPk, maxSupply }),
    ];
    return { mint, ata, metadata: metadataPda(mint.publicKey), masterEdition: masterEditionPda(mint.publicKey), instructions, signers: [mint] };
  }

  return { buildCreateToken, buildCreateNft, buildCreateCollection, buildCreateEdition,
    metadataPda, masterEditionPda, METADATA_PROGRAM_ID };
});
