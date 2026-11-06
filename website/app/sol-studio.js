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
    'mainnet-beta': { url: 'https://api.mainnet-beta.solana.com', label: 'Mainnet', explorer: '' },
  };
  const MINT_SIZE = 82; // SPL mint account size
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
    await loadScript('/app/sol-mint.bundle.js');       // -> window.SolMintClient (uses the web3 global)
    if (!window.solanaWeb3 || !window.SolMintClient) throw new Error('Solana libraries did not load');
    loaded = true;
  }
  function wallet() {
    const w = (window.phantom && window.phantom.solana) || window.solana;
    if (!w || !w.isPhantom && !w.isSolflare && !w.connect) return null;
    return w;
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
      <p class="st-help">Optionally publish this token or NFT to Solana. Libraries are served
      from monzero.org and load only when you connect. Starts on Devnet; switch to Mainnet to
      spend real SOL. The mint happens in your wallet — Studio never sees your keys.</p></div>
      <div class="st-sol-row">
        <label>Network <select id="sol-net">
          <option value="devnet">Devnet (test)</option>
          <option value="mainnet-beta">Mainnet</option></select></label>
        <button type="button" class="st-btn" id="sol-connect">Connect wallet</button>
        <button type="button" class="st-btn" id="sol-create-btn" disabled>Create on Solana</button>
      </div>
      <p class="st-sol-status" id="sol-status" role="status"></p>`;
    return box;
  }

  function mount() {
    const anchor = document.getElementById('activation') || document.getElementById('create');
    if (!anchor || document.getElementById('sol-create')) return;
    const p = panel();
    anchor.parentNode.insertBefore(p, anchor);
    const status = p.querySelector('#sol-status');
    const createBtn = p.querySelector('#sol-create-btn');
    const say = (m, err) => { status.textContent = m; status.style.color = err ? '#c0392b' : ''; };
    let pub = null;

    p.querySelector('#sol-connect').addEventListener('click', async () => {
      try {
        const w = wallet();
        if (!w) return say('No Solana wallet found. Install Phantom or Solflare.', true);
        say('Loading Solana libraries…');
        await ensureLibs();
        const res = await w.connect();
        pub = (res && res.publicKey) || w.publicKey;
        createBtn.disabled = false;
        say('Connected ' + pub.toBase58().slice(0, 4) + '…' + pub.toBase58().slice(-4));
      } catch (e) { say('Connect failed: ' + (e.message || e), true); }
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
          built = SM.buildCreateToken({ payer: pub, decimals, supply, rentLamports: rent });
          what = 'token';
        } else {
          const uri = field('metadata_uri');
          if (!uri) return say('Set the metadata URI (export + host the metadata JSON first).', true);
          built = SM.buildCreateNft({ payer: pub, name, symbol: field('symbol'), uri, rentLamports: rent });
          what = 'NFT';
        }

        say('Building transaction…');
        const tx = new web3.Transaction().add(...built.instructions);
        tx.feePayer = pub;
        tx.recentBlockhash = (await conn.getLatestBlockhash()).blockhash;
        tx.partialSign(...built.signers);           // the new mint keypair co-signs
        const signed = await w.signTransaction(tx); // the wallet signs as payer
        say('Submitting…');
        const sig = await conn.sendRawTransaction(signed.serialize());
        await conn.confirmTransaction(sig, 'confirmed');
        const mint = built.mint.publicKey.toBase58();
        say('');
        status.innerHTML = `Created ${what}. Mint <code>${mint}</code> · ` +
          `<a href="https://explorer.solana.com/address/${mint}${cluster.explorer}" target="_blank" rel="noopener">view on explorer</a>`;
      } catch (e) { say('Create failed: ' + (e.message || e), true); }
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
  else mount();
})();
