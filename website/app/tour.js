// Monzero Studio guided tour: walks through making a sample NFT on the real form.
// Starts from the "Take the tour" buttons or /app/?tour=1.
(() => {
  'use strict';

  const $ = (selector) => document.querySelector(selector);
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let studio;

  const STEPS = [
    {
      title: 'Make your first NFT in two minutes',
      body: 'We\'ll make a sample NFT together on the real form. Everything happens in your browser: nothing is uploaded, issued or paid for. You can leave the tour at any time.',
    },
    {
      target: '.st-kinds',
      title: '1. Choose what you\'re making',
      body: 'An NFT is one of one. An edition is numbered copies, a collection groups NFTs, and a token has a fixed supply. We\'ll pick NFT.',
      enter: () => { studio.setKind('nft'); studio.showStep(0); },
    },
    {
      target: '.st-step-nav',
      title: '2. Four short steps',
      body: 'Artwork, details, hosting, then review and export. You can jump between them at any time, and nothing is final until you export.',
    },
    {
      target: '#dropzone',
      title: '3. Add your artwork',
      body: 'Drop in an image and the Studio fingerprints it on your computer. We\'ve generated a sample piece for you. Your real artwork works the same way.',
      enter: async () => {
        studio.showStep(0);
        const traits = await studio.sampleArt();
        studio.clearTraits();
        traits.forEach((t) => studio.addTrait(t.trait_type, t.value));
        studio.update();
      },
    },
    {
      target: '#name-field',
      title: '4. Name it',
      body: 'Give it a clear name. If it\'s part of a series, number it so collectors can tell the pieces apart.',
      enter: () => { studio.showStep(1); studio.setField('name', 'Tide Glass #001'); studio.setField('creator', 'Studio tour'); },
    },
    {
      target: '.st-traits',
      title: '5. Describe it with traits',
      body: 'Traits are name and value pairs such as Palette: Tide. Galleries show them, and in a collection they show which pieces are rare. The sample came with two traits.',
      enter: () => studio.showStep(1),
    },
    {
      target: '#license-field',
      title: '6. Choose a licence',
      body: 'The licence tells people what they may do with the image. "All rights reserved" keeps every right with you. The Creator hub explains each option.',
      enter: () => studio.showStep(1),
    },
    {
      target: '#hosting',
      title: '7. Say where the files will live',
      body: 'Upload the image and metadata.json to IPFS, Arweave or your own site, then paste the addresses here. The check buttons test that a host really serves your file. We\'ve filled in example addresses: use your real ones.',
      enter: () => {
        studio.showStep(2);
        studio.setField('image', 'ipfs://example/tide-glass-001.png');
        studio.setField('metadata_uri', 'ipfs://example/tide-glass-001.json');
      },
    },
    {
      target: '#card',
      title: '8. See it as collectors will',
      body: 'This preview updates as you type. "Download card image" gives you a picture of it for announcing your piece.',
    },
    {
      target: '.st-hash',
      title: '9. The metadata hash is its fingerprint',
      body: 'This hash is computed from your exact metadata. Change one letter and it changes. Once assets are live, the hash is recorded on chain, so anyone can prove the files are genuine.',
      enter: () => studio.showStep(3),
    },
    {
      target: '#download-metadata',
      title: '10. Export your files',
      body: 'Download metadata.json and upload exactly that file to your host. "Download project backup" keeps everything, including the original artwork, outside this browser.',
      enter: () => studio.showStep(3),
    },
    {
      target: '.st-project-bar',
      title: '11. Your work is saved as a project',
      body: 'Projects are kept in this browser, and you\'ll find them under My projects. Start a new one here at any time.',
    },
    {
      target: '#batch',
      title: '12. Got a whole series?',
      body: 'The batch builder turns a folder of images and an optional traits spreadsheet into numbered metadata files with a rarity overview, ready to host.',
    },
    {
      target: '#verify',
      title: '13. Anyone can check an NFT',
      body: 'Collectors drop the metadata file and image in here to check that they match their hashes. Encourage your collectors to use it.',
    },
    {
      title: 'That\'s it: you made an NFT draft',
      body: 'You can keep editing the sample, or start a new project of your own. The Creator hub has guides on artwork, licences, hosting and planning a collection.',
      finish: true,
    },
  ];

  let index = -1;
  let spot;
  let pop;
  let lastFocus;

  function build() {
    spot = document.createElement('div');
    spot.className = 'st-tour-spot';
    spot.setAttribute('aria-hidden', 'true');
    pop = document.createElement('div');
    pop.className = 'st-tour-pop';
    pop.setAttribute('role', 'dialog');
    pop.setAttribute('aria-modal', 'false');
    pop.setAttribute('aria-labelledby', 'st-tour-title');
    pop.innerHTML = `
      <div class="st-tour-progress"><span id="st-tour-count"></span><div class="st-tour-bar"><i></i></div></div>
      <h2 id="st-tour-title" tabindex="-1"></h2>
      <p id="st-tour-body"></p>
      <div class="st-tour-actions">
        <button type="button" class="st-link" data-tour="close">Exit tour</button>
        <span>
          <button type="button" class="st-btn ghost" data-tour="back">Back</button>
          <button type="button" class="st-btn" data-tour="next">Next</button>
        </span>
      </div>`;
    pop.addEventListener('click', (event) => {
      const action = event.target.closest('[data-tour]')?.dataset.tour;
      if (action === 'close') close();
      if (action === 'back') go(index - 1);
      if (action === 'next') go(index + 1);
      if (action === 'new') { close(); $('#new-project').click(); $('#create').scrollIntoView({ behavior: reduced ? 'auto' : 'smooth' }); }
      if (action === 'hub') location.href = 'creators/';
    });
    document.body.append(spot, pop);
  }

  function place() {
    if (index < 0) return;
    const step = STEPS[index];
    const target = step.target && document.querySelector(step.target);
    if (!target || !target.getClientRects().length) {
      spot.hidden = true;
      pop.classList.add('center');
      return;
    }
    const r = target.getBoundingClientRect();
    const pad = 8;
    spot.hidden = false;
    Object.assign(spot.style, { top: `${r.top - pad}px`, left: `${r.left - pad}px`, width: `${r.width + pad * 2}px`, height: `${r.height + pad * 2}px` });
    pop.classList.remove('center');
    if (innerWidth <= 700) { pop.style.top = ''; pop.style.left = ''; return; }
    const w = pop.offsetWidth;
    const h = pop.offsetHeight;
    let top = r.bottom + 16;
    let left = Math.min(Math.max(12, r.left), innerWidth - w - 12);
    if (top + h > innerHeight - 12) top = r.top - h - 16;
    if (top < 12) {
      // A tall target: sit beside it when there is room, otherwise at the bottom of the screen.
      top = Math.min(Math.max(12, r.top), innerHeight - h - 12);
      if (r.right + 16 + w <= innerWidth - 12) left = r.right + 16;
      else if (r.left - 16 - w >= 12) left = r.left - 16 - w;
      else top = Math.max(12, innerHeight - h - 12);
    }
    pop.style.top = `${top}px`;
    pop.style.left = `${left}px`;
  }

  async function go(next) {
    if (next < 0 || next >= STEPS.length) { close(); return; }
    index = next;
    const step = STEPS[index];
    $('#st-tour-count').textContent = `Step ${index + 1} of ${STEPS.length}`;
    pop.querySelector('.st-tour-bar i').style.width = `${((index + 1) / STEPS.length) * 100}%`;
    $('#st-tour-title').textContent = step.title;
    $('#st-tour-body').textContent = step.body;
    pop.querySelector('[data-tour="back"]').hidden = index === 0;
    const actions = pop.querySelector('.st-tour-actions span');
    actions.querySelectorAll('[data-tour="new"], [data-tour="hub"]').forEach((b) => b.remove());
    pop.querySelector('[data-tour="next"]').textContent = index === 0 ? 'Start' : step.finish ? 'Keep the sample' : 'Next';
    if (step.finish) {
      const hub = Object.assign(document.createElement('button'), { type: 'button', className: 'st-btn ghost', textContent: 'Creator hub' });
      hub.dataset.tour = 'hub';
      const fresh = Object.assign(document.createElement('button'), { type: 'button', className: 'st-btn ghost', textContent: 'Start my own' });
      fresh.dataset.tour = 'new';
      actions.prepend(hub, fresh);
    }
    pop.hidden = false;
    try { if (step.enter) await step.enter(); } catch { /* keep the tour going even if a demo step fails */ }
    const target = step.target && document.querySelector(step.target);
    if (target) target.scrollIntoView({ behavior: reduced ? 'auto' : 'smooth', block: 'center' });
    place();
    setTimeout(place, reduced ? 0 : 450);
    $('#st-tour-title').focus({ preventScroll: true });
  }

  function onKey(event) {
    if (index < 0) return;
    if (event.key === 'Escape') close();
    else if (event.target.closest('input, textarea, select')) return;
    else if (event.key === 'ArrowRight') go(index + 1);
    else if (event.key === 'ArrowLeft') go(index - 1);
  }

  function start() {
    if (!studio) return;
    if (!studio.isEmpty() && !confirm('The tour fills the form with a sample. Save your current project first if you want to keep it. Start the tour?')) return;
    if (!spot) build();
    lastFocus = document.activeElement;
    go(0);
  }

  function close() {
    index = -1;
    if (spot) { spot.hidden = true; pop.hidden = true; }
    if (lastFocus?.focus) lastFocus.focus({ preventScroll: true });
  }

  addEventListener('resize', place);
  addEventListener('scroll', place, { passive: true });
  addEventListener('keydown', onKey);

  function ready() {
    studio = window.MonzeroStudio;
    document.querySelectorAll('[data-start-tour]').forEach((b) => b.addEventListener('click', start));
    if (new URLSearchParams(location.search).get('tour')) setTimeout(start, 300);
  }
  if (window.MonzeroStudio) ready();
  else document.addEventListener('monzero-studio-ready', ready, { once: true });
})();
