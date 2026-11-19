'use strict';
// sol-studio.js — wires Solana token/NFT creation into Monzero Studio.
//
// The Studio otherwise only *prepares* assets; this adds an opt-in "Create on Solana" panel
// that can actually mint. It stays faithful to the Studio's self-contained model: the heavy
// Solana libraries are SELF-HOSTED (served from monzero.org, never a third-party CDN) and
// loaded only when the creator opts in. Instruction building is the audited, unit-tested
// engine in sol-mint.js (window.SolMintClient); this file is the wallet + form glue.
//
// Flow: connect a Solana wallet (Phantom/Solflare) -> read the current kind + fields ->
// build the instructions -> the generated mint keypair co-signs -> the wallet signs as payer
// -> submit. Defaults to devnet so a misclick never spends real SOL.
(function () {
  const CLUSTERS = {
    devnet: { url: 'https://api.devnet.solana.com', label: 'Devnet (test)', explorer: '?cluster=devnet' },
    // Solana's own main-network address refuses requests from web pages (403); this public
    // node accepts them. The DEX page uses the same one.
    'mainnet-beta': { url: 'https://solana-rpc.publicnode.com', label: 'Mainnet', explorer: '' },
  };
  const MINT_SIZE = 82; // SPL mint account size
  // The Studio fee, in SOL, by what is created. Charged on Mainnet only; Devnet is free. It is a
  // plain transfer added to the same transaction as the creation, so it is paid only if the
  // creation succeeds, and the wallet shows it before the creator approves.
  const FEE_WALLET = '5LpU8TxNqWvs5ACyhxCXNVFXNMq6mnvvK1YvMGkBWXog'; // the project's Ledger
  const FEES = { fungible: '0.1', collection: '0.05', nft: '0.01', edition: '0.01' };
  const LAMPORTS = { '0.1': 100000000, '0.05': 50000000, '0.01': 10000000 };
  function feeFor(kind, net) {
    return net === 'mainnet-beta' ? (FEES[kind] || FEES.nft) : null;
  }
  let loaded = false;

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      if ([...document.scripts].some((s) => s.src.endsWith(src))) return resolve();
      const el = document.createElement('script');
      el.src = src; el.onload = () => resolve(); el.onerror = () => reject(new Error('failed to load ' + src));
      document.head.appendChild(el);
    });
  }
  async function ensureLibs() {
    if (loaded) return;
    await loadScript('/app/solana-web3.iife.min.js'); // -> window.solanaWeb3
    await loadScript('/app/sol-mint.bundle.js?v=4');   // -> window.SolMintClient (self-contained: web3 aliased to the global, Buffer polyfilled in)
    if (!window.solanaWeb3 || !window.SolMintClient) throw new Error('Solana libraries did not load');
    loaded = true;
  }
  // Wait until the network has the transaction. Asked over plain HTTPS: the library's own
  // confirmTransaction opens a WebSocket, which this page's security policy does not allow, so
  // it reported every mint as failed after 30 seconds, including the ones that had gone through.
  async function waitConfirmed(conn, sig, lastValidBlockHeight) {
    for (;;) {
      const st = (await conn.getSignatureStatuses([sig])).value[0];
      if (st && st.err) throw new Error('the network rejected the transaction (' + JSON.stringify(st.err) + '). Nothing was created.');
      if (st && (st.confirmationStatus === 'confirmed' || st.confirmationStatus === 'finalized')) return;
      if (!st && (await conn.getBlockHeight('confirmed')) > lastValidBlockHeight) {
        throw new Error('the transaction expired before the network took it. Nothing was created; try again.');
      }
      await new Promise((r) => setTimeout(r, 1500));
    }
  }
  // What went wrong, in words a creator can act on
  function reason(e) {
    const m = String((e && e.message) || e);
    if (/may not be used to pay transaction fees|insufficient (funds|lamports)|no record of a prior credit/i.test(m)) {
      return 'this wallet does not hold enough SOL on the chosen network to pay for it. Nothing was created.';
    }
    if (/reject|denied|cancel/i.test(m)) return 'you declined it in your wallet. Nothing was created.';
    return m.split('\n')[0];
  }
  function wallet() {
    // Phantom injects window.phantom.solana (and window.solana); Solflare injects
    // window.solflare (and sometimes window.solana). Accept any provider with connect().
    const w = (window.phantom && window.phantom.solana) || window.solflare || window.solana;
    return (w && typeof w.connect === 'function') ? w : null;
  }
  function field(name) {
    const el = document.querySelector('#create [name="' + name + '"]');
    return el ? String(el.value || '').trim() : '';
  }
  function currentKind() {
    const btn = document.querySelector('#create [data-kind][aria-checked="true"]');
    return (btn && btn.dataset.kind) || field('kind') || 'nft';
  }
  function atomic(supplyStr, decimals) {
    const clean = String(supplyStr).replace(/[,_\s]/g, '');
    const [whole, frac = ''] = clean.split('.');
    const digits = (whole || '0') + frac.padEnd(decimals, '0').slice(0, decimals);
    return BigInt(digits.replace(/^0+(?=\d)/, '') || '0');
  }

  function el(tag, attrs, html) {
    const n = document.createElement(tag);
    if (attrs) for (const k in attrs) n.setAttribute(k, attrs[k]);
    if (html != null) n.innerHTML = html;
    return n;
  }

  function panel() {
    const box = el('section', { class: 'st-create section', id: 'sol-create' });
    box.innerHTML = `
      <div class="st-create-head"><p class="eyebrow">Create on Solana</p>
      <h2>Mint to Solana</h2>
      <p class="st-help">Publish this token or NFT to Solana. Libraries are served
      from monzero.org and load only when you connect. Starts on Devnet, which is free; switch to Mainnet to
      create the real thing with real SOL. The mint happens in your wallet — Studio never sees your keys.</p></div>
      <div class="st-sol-row">
        <label>Network <select id="sol-net">
          <option value="devnet">Devnet (test)</option>
          <option value="mainnet-beta">Mainnet</option></select></label>
        <button type="button" class="st-btn" id="sol-connect">Connect wallet</button>
        <button type="button" class="st-btn" id="sol-create-btn" disabled>Create on Solana</button>
      </div>
      <p class="st-sol-fee" id="sol-fee"></p>
      <p class="st-sol-status" id="sol-status" role="status"></p>`;
    return box;
  }

  function mount() {
    // Directly under the creation form: minting is the step after designing
    const create = document.getElementById('create');
    if (!create || document.getElementById('sol-create')) return;
    const p = panel();
    create.parentNode.insertBefore(p, create.nextSibling);
    const status = p.querySelector('#sol-status');
    const createBtn = p.querySelector('#sol-create-btn');
    const say = (m, err) => { status.textContent = m; status.style.color = err ? '#c0392b' : ''; };
    let pub = null;
    const feeLine = p.querySelector('#sol-fee');
    const showFee = () => {
      const fee = feeFor(currentKind(), p.querySelector('#sol-net').value);
      feeLine.textContent = fee
        ? 'Studio fee: ' + fee + ' SOL, paid in the same transaction and only if the creation succeeds. Solana\'s own network costs are extra (about 0.002 to 0.02 SOL).'
        : 'Free on Devnet. On Mainnet the Studio fee is ' + FEES.fungible + ' SOL for a token, ' + FEES.collection + ' SOL for a collection and ' + FEES.nft + ' SOL for an NFT or edition.';
    };
    showFee();
    p.querySelector('#sol-net').addEventListener('change', showFee);
    // The kind buttons live in the form above; a click there changes the fee shown here
    document.addEventListener('click', () => setTimeout(showFee, 0));

    p.querySelector('#sol-connect').addEventListener('click', async () => {
      try {
        const w = wallet();
        if (!w) return say('No Solana wallet found. Install Phantom or Solflare.', true);
        say('Loading Solana libraries…');
        await ensureLibs();
        const res = await w.connect();
        pub = (res && res.publicKey) || w.publicKey;
        createBtn.disabled = false;
        // A wallet that already trusts monzero.org (from the DEX page, say) connects without asking
        say('Connected ' + pub.toBase58().slice(0, 4) + '…' + pub.toBase58().slice(-4) +
          '. Nothing is sent until you approve it in your wallet.');
      } catch (e) { say('Connect failed: ' + reason(e), true); }
    });

    createBtn.addEventListener('click', async () => {
      try {
        const w = wallet(); if (!w || !pub) return say('Connect a wallet first.', true);
        await ensureLibs();
        const web3 = window.solanaWeb3, SM = window.SolMintClient;
        const net = p.querySelector('#sol-net').value;
        const cluster = CLUSTERS[net];
        const conn = new web3.Connection(cluster.url, 'confirmed');
        const rent = await conn.getMinimumBalanceForRentExemption(MINT_SIZE);
        const kind = currentKind();
        const name = field('name');
        if (!name) return say('Give your asset a name first.', true);

        let built, what;
        if (kind === 'fungible') {
          const decimals = Math.max(0, Math.min(9, parseInt(field('decimals') || '0', 10)));
          const supply = atomic(field('supply') || '0', decimals);
          if (supply <= 0n) return say('Enter a positive supply.', true);
          built = SM.buildCreateToken({ payer: pub, decimals, supply, rentLamports: rent,
            name, symbol: field('symbol'), uri: field('metadata_uri') });
          what = 'token';
        } else {
          const uri = field('metadata_uri');
          if (!uri) return say('Set the metadata URI (export + host the metadata JSON first).', true);
          const common = { payer: pub, name, symbol: field('symbol'), uri, rentLamports: rent };
          if (kind === 'collection') { built = SM.buildCreateCollection(common); what = 'collection (use this mint address as the collection address of its members)'; }
          else if (kind === 'edition') {
            // A master edition whose maxSupply caps the numbered prints; the prints themselves
            // are minted later with another tool. Never fall back to an unlimited master.
            const n = Number(field('edition_size'));
            if (!Number.isInteger(n) || n < 2 || n > 1000000) return say('Set the number of copies (2 to 1,000,000) first.', true);
            built = SM.buildCreateEdition({ ...common, maxSupply: n });
            what = 'master edition (up to ' + n.toLocaleString() + ' prints; mint the prints later with another Solana tool)';
          } else { built = SM.buildCreateNft(common); what = 'NFT'; }
        }

        say('Building transaction…');
        // collections/editions add a master-edition CPI — give the tx headroom.
        const tx = new web3.Transaction()
          .add(web3.ComputeBudgetProgram.setComputeUnitLimit({ units: 400000 }))
          .add(...built.instructions);
        const fee = feeFor(kind, net);
        if (fee) {
          tx.add(web3.SystemProgram.transfer({ fromPubkey: pub, toPubkey: new web3.PublicKey(FEE_WALLET), lamports: LAMPORTS[fee] }));
        }
        tx.feePayer = pub;
        const latest = await conn.getLatestBlockhash('confirmed');
        tx.recentBlockhash = latest.blockhash;
        tx.partialSign(...built.signers);           // the new mint keypair co-signs
        const signed = await w.signTransaction(tx); // the wallet signs as payer
        say('Submitting…');
        const sig = await conn.sendRawTransaction(signed.serialize());
        say('Sent. Waiting for the network to confirm…');
        await waitConfirmed(conn, sig, latest.lastValidBlockHeight);
        const mint = built.mint.publicKey.toBase58();
        say('');
        status.innerHTML = `Created ${what}${fee ? ' (Studio fee ' + fee + ' SOL paid)' : ''}. Mint <code>${mint}</code> · ` +
          `<a href="https://explorer.solana.com/address/${mint}${cluster.explorer}" target="_blank" rel="noopener">view on explorer</a>`;
      } catch (e) { say('Create failed: ' + reason(e), true); }
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
  else mount();
})();
