// Monzero Studio extras: generated sample artwork and a shareable card image.
// Works through window.MonzeroStudio, which studio-20261025.js exposes.
(() => {
  'use strict';

  const core = window.MonzeroStudioCore;
  const $ = (selector) => document.querySelector(selector);

  function ready() {
    const studio = window.MonzeroStudio;

    // A generated piece for trying the Studio without an image of your own. Returns its traits.
    studio.sampleArt = async (seed = Math.floor(Math.random() * 2 ** 31)) => {
      const { canvas, traits } = core.sampleArtwork(seed);
      await studio.loadArt(await core.canvasFile(canvas, `sample-${seed}.png`));
      return traits;
    };

    $('#sample-art').addEventListener('click', async () => {
      const traits = await studio.sampleArt();
      if (!studio.traits().length) {
        studio.clearTraits();
        traits.forEach((t) => studio.addTrait(t.trait_type, t.value));
        studio.update();
      }
      studio.toast('Sample artwork made in your browser. Generate again for a new one.');
    });

    // A 1080×1350 picture of the preview card, for announcing a piece.
    $('#share-card').addEventListener('click', async () => {
      const meta = studio.buildMetadata();
      const { state } = studio;
      if (!meta.name) { studio.toast('Give it a name first'); return; }
      const W = 1080;
      const H = 1350;
      const canvas = document.createElement('canvas');
      canvas.width = W;
      canvas.height = H;
      const g = canvas.getContext('2d');
      const css = getComputedStyle(document.body);
      const color = (name, fallback) => css.getPropertyValue(name).trim() || fallback;
      g.fillStyle = color('--surface', '#10242d');
      g.fillRect(0, 0, W, H);
      g.fillStyle = color('--surface-raised', '#142d35');
      for (let y = 0; y < W; y += 40) for (let x = (y / 40) % 2 ? 40 : 0; x < W; x += 80) g.fillRect(x, y, 40, 40);
      const src = state.art?.url || state.art?.thumb;
      if (src) {
        try {
          const img = await core.loadImage(src);
          const round = state.kind === 'fungible';
          const box = round ? W * 0.62 : W;
          const scale = Math.min(box / img.naturalWidth, box / img.naturalHeight);
          const w = img.naturalWidth * scale;
          const h = img.naturalHeight * scale;
          g.save();
          if (round) { g.beginPath(); g.arc(W / 2, W / 2, box / 2, 0, Math.PI * 2); g.clip(); }
          g.drawImage(img, (W - w) / 2, (W - h) / 2, w, h);
          g.restore();
        } catch { /* the card still shows its text */ }
      }
      g.fillStyle = color('--bg', '#0b1720');
      g.fillRect(0, W, W, H - W);
      const sans = color('--sans', 'sans-serif');
      const mono = color('--mono', 'monospace');
      g.textBaseline = 'top';
      g.fillStyle = color('--accent', '#69d5c2');
      g.font = `600 26px ${mono}`;
      const kicker = $('#card-edition').textContent;
      g.fillText(`${studio.KIND_LABEL[state.kind].toUpperCase()}${kicker ? `  ·  ${kicker.toUpperCase()}` : ''}`, 56, W + 40);
      g.fillStyle = color('--ink', '#edf3f7');
      g.font = `650 60px ${sans}`;
      let name = meta.name;
      while (g.measureText(name).width > W - 112 && name.length > 1) name = `${name.slice(0, -2)}…`;
      g.fillText(name, 56, W + 82);
      g.fillStyle = color('--muted', '#a9c0c5');
      g.font = `400 30px ${sans}`;
      if (meta.creator) g.fillText(`by ${meta.creator}`.slice(0, 60), 56, W + 160);
      g.font = `400 22px ${mono}`;
      const hash = state.metadataHash || '';
      g.fillText(`Metadata ${hash.slice(0, 16)}…${hash.slice(-16)}`, 56, H - 64);
      g.textAlign = 'right';
      g.fillStyle = color('--accent', '#69d5c2');
      g.fillText('Made with Monzero Studio', W - 56, H - 100);
      core.download(`${core.slug(meta.name)}-card.png`, await new Promise((resolve) => canvas.toBlob(resolve, 'image/png')));
      studio.toast('Card image downloaded');
    });
  }

  if (window.MonzeroStudio) ready();
  else document.addEventListener('monzero-studio-ready', ready, { once: true });
})();
