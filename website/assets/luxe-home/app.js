/* MONZERO / ZERO EDITION — zero-dependency interactive enhancements */
(() => {
  'use strict';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const icon = name => `<svg class="icon"><use href="#i-${name}"></use></svg>`;

  // Menus work without route rewriting: existing live Monzero services retain their URLs.
  const menuButton = $('#menu-toggle');
  const mobileMenu = $('#mobile-menu');
  function closeMenu() {
    if (!menuButton || !mobileMenu) return;
    mobileMenu.hidden = true;
    menuButton.setAttribute('aria-expanded', 'false');
    menuButton.setAttribute('aria-label', 'Open menu');
    menuButton.innerHTML = icon('menu');
  }
  menuButton?.addEventListener('click', () => {
    const open = mobileMenu.hidden;
    mobileMenu.hidden = !open;
    menuButton.setAttribute('aria-expanded', String(open));
    menuButton.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
    menuButton.innerHTML = icon(open ? 'close' : 'menu');
  });
  $$('a', mobileMenu).forEach(a => a.addEventListener('click', closeMenu));
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape' && !mobileMenu.hidden) { closeMenu(); menuButton.focus(); }
  });
  document.addEventListener('click', event => {
    if (!mobileMenu.hidden && !event.composedPath().includes($('.site-header'))) closeMenu();
  });
  window.addEventListener('resize', () => {
    if (window.innerWidth > 1020 && !mobileMenu.hidden) closeMenu();
  }, { passive: true });
  $('#current-year').textContent = String(new Date().getFullYear());

  // A signature zero-field, not a template 'matrix rain': translucent, bidirectional 0 streams.
  // Canvas remains behind multiple opaque text gradients. Capped to ~30fps, paused offscreen.
  const canvas = $('#zero-rain');
  const ctx = canvas?.getContext('2d', { alpha: true });
  const hero = $('.hero');
  const motionToggle = $('#motion-toggle');
  const motionLabel = $('#motion-label');
  let width = 0, height = 0, dpr = 1, streams = [], raf = 0, lastFrame = 0;
  let inView = true;
  let motionAllowed = !reducedMotion.matches;
  try { if (localStorage.getItem('monzero-zero-motion') === 'off') motionAllowed = false; } catch (_) { /* Private browsing may deny storage */ }

  const rand = (min, max) => min + Math.random() * (max - min);
  function initStreams() {
    if (!canvas || !ctx) return;
    const box = hero.getBoundingClientRect();
    width = Math.max(1, Math.round(box.width));
    height = Math.max(1, Math.round(box.height));
    dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    canvas.style.width = `${width}px`;
    canvas.style.height = `${height}px`;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const gap = width < 700 ? 64 : 72;
    const count = Math.min(27, Math.ceil(width / gap));
    streams = Array.from({ length: count }, (_, i) => ({
      x: (i + rand(.2, .8)) * width / count,
      y: rand(-height, height),
      direction: i % 3 === 0 ? -1 : 1,
      speed: rand(7, 18),
      length: Math.floor(rand(4, 10)),
      spacing: rand(23, 34),
      opacity: rand(.26, .68),
      fontSize: rand(11, 16),
      sway: rand(-2, 2)
    }));
    drawFrame(0);
  }
  function drawFrame(delta) {
    if (!ctx) return;
    ctx.clearRect(0, 0, width, height);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    for (const col of streams) {
      col.y += col.direction * col.speed * delta;
      const span = col.length * col.spacing;
      if (col.direction > 0 && col.y - span > height) col.y = -span - rand(0, height * .15);
      if (col.direction < 0 && col.y + span < 0) col.y = height + span + rand(0, height * .15);
      ctx.font = `500 ${col.fontSize}px "IBM Plex Mono", monospace`;
      for (let j = 0; j < col.length; j++) {
        const y = col.y - col.direction * j * col.spacing;
        if (y < -25 || y > height + 25) continue;
        let alpha = (.055 + .26 * Math.pow(1 - j / col.length, 2)) * col.opacity;
        if (j === 0) alpha = .35 * col.opacity;
        const edge = Math.min(1, y / 80, (height - y) / 80);
        alpha *= Math.max(0, edge);
        if (alpha <= 0) continue;
        ctx.fillStyle = `rgba(${j === 0 ? '131,255,248' : '56,206,215'},${alpha})`;
        ctx.fillText('0', col.x + Math.sin(j * 1.5) * col.sway, y);
      }
    }
  }
  function shouldAnimate() { return motionAllowed && !reducedMotion.matches && inView && !document.hidden; }
  function stopRain(clear = false) {
    if (raf) cancelAnimationFrame(raf);
    raf = 0;
    lastFrame = 0;
    if (clear && ctx) ctx.clearRect(0, 0, width, height);
  }
  function frame(now) {
    raf = requestAnimationFrame(frame);
    if (now - lastFrame < 33) return;
    const delta = lastFrame ? Math.min((now - lastFrame) / 1000, .08) : 0;
    lastFrame = now;
    drawFrame(delta);
  }
  function syncMotion() {
    const playing = motionAllowed && !reducedMotion.matches;
    document.body.classList.toggle('motion-off', !playing);
    motionToggle?.setAttribute('aria-pressed', String(playing));
    motionToggle?.setAttribute('aria-label', playing ? 'Pause animated zeros' : 'Resume animated zeros');
    if (motionLabel) motionLabel.textContent = playing ? 'ZEROS / ON' : 'ZEROS / OFF';
    if (shouldAnimate()) { if (!raf) raf = requestAnimationFrame(frame); }
    else stopRain(!playing);
  }
  if (canvas && ctx) {
    initStreams();
    motionToggle?.addEventListener('click', () => {
      motionAllowed = !motionAllowed;
      try { localStorage.setItem('monzero-zero-motion', motionAllowed ? 'on' : 'off'); } catch (_) {}
      syncMotion();
    });
    window.addEventListener('resize', () => {
      stopRain();
      initStreams();
      syncMotion();
    }, { passive: true });
    document.addEventListener('visibilitychange', syncMotion);
    reducedMotion.addEventListener?.('change', syncMotion);
    if ('IntersectionObserver' in window) {
      const zeroObserver = new IntersectionObserver(([entry]) => {
        inView = entry.isIntersecting;
        syncMotion();
      }, { rootMargin: '80px 0px' });
      zeroObserver.observe(hero);
    }
    syncMotion();
  }

  // Progressive reveal animation: never hide content from JS-disabled users or reduced-motion users.
  if (!reducedMotion.matches && 'IntersectionObserver' in window) {
    document.body.classList.add('js-animations');
    const observer = new IntersectionObserver((entries, io) => {
      for (const entry of entries) if (entry.isIntersecting) {
        entry.target.classList.add('visible');
        io.unobserve(entry.target);
      }
    }, { threshold: .07, rootMargin: '0px 0px 40px 0px' });
    $$('.reveal').forEach(el => observer.observe(el));
  }

  const privacyInfo = [
    { tag: 'RING SIGNATURES', title: 'The sender gets lost in the ring.',
      description: 'Ring signatures make it difficult to determine which output in a group was actually spent. Monzero uses a ring size of 16.' },
    { tag: 'RING CONFIDENTIAL TRANSACTIONS', title: 'The amount stays private.',
      description: 'RingCT conceals the transferred amount from the public blockchain while allowing network rules to validate transactions.' },
    { tag: 'ONE-TIME ADDRESSES', title: 'A unique destination per payment.',
      description: 'One-time recipient addresses reduce direct on-chain linkage between payments and a public wallet address.' }
  ];
  const tabs = $$('.proto-tab');
  function selectPrivacy(idx, focus = false) {
    const info = privacyInfo[idx]; if (!info) return;
    tabs.forEach((tab, i) => {
      tab.classList.toggle('active', i === idx);
      tab.setAttribute('aria-selected', String(i === idx));
      tab.tabIndex = i === idx ? 0 : -1;
      if (focus && i === idx) tab.focus();
    });
    $('#protocol-explainer').setAttribute('aria-labelledby', `privacy-tab-${idx}`);
    $('#privacy-tag').textContent = info.tag;
    $('#privacy-title').textContent = info.title;
    $('#privacy-desc').textContent = info.description;
    $('#diagram-sender').textContent = idx === 0 ? '0x······' : 'F·····XMZ';
    $('#diagram-receiver').textContent = idx === 2 ? '0x······' : 'F·····XMZ';
  }
  tabs.forEach((tab, i) => {
    tab.addEventListener('click', () => selectPrivacy(i));
    tab.addEventListener('keydown', e => {
      if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
      e.preventDefault();
      const next = e.key === 'Home' ? 0 : e.key === 'End' ? tabs.length - 1 : (i + (e.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length;
      selectPrivacy(next, true);
    });
  });

  // Verified roadmap phases, not gamified or invented progress percentages.
  const phases = [
    { badge: '● RELEASED', title: ['The network is real.', 'And running.'],
      description: 'Monzero core v1.0.0, Windows and Linux command-line packages, the Windows GUI update, an explorer and mining tools have been published. Check release limitations before installing.',
      checks: ['Independent mainnet and RandomX PoW', 'Core software and wallet downloads', 'Explorer, mining and community tools'], url: 'https://monzero.org/download/', link: 'View release evidence' },
    { badge: '◌ IN DEVELOPMENT', title: ['Testing the edges.', 'Building the future.'],
      description: 'Native Windows validation and broader wallet and ecosystem work continue. The roadmap records public testnet swap evidence, mobile-wallet emulator results and DEX engineering—but not an enabled public trading service.',
      checks: ['Mobile wallet / miner: emulator checks reported', 'DEX: individual testnet and funded mainnet trials', 'Security review and release acceptance pending'], url: 'https://monzero.org/roadmap/', link: 'Read engineering updates' },
    { badge: '○ CONDITIONAL', title: ['No shortcuts.', 'No invented dates.'],
      description: 'Broader exchange access, public trading infrastructure and other proposed services depend on testing, security review, recovery checks and operator acceptance. There is no committed public launch date.',
      checks: ['Complete acceptance & recovery testing', 'External security evaluation', 'Public deployment only when ready'], url: 'https://monzero.org/roadmap/', link: 'Explore planned milestones' }
  ];
  const roadTabs = $$('.roadmap-stop');
  function selectPhase(idx, focus = false) {
    const phase = phases[idx]; if (!phase) return;
    roadTabs.forEach((tab, i) => {
      tab.classList.toggle('active', i === idx);
      tab.setAttribute('aria-selected', String(i === idx));
      tab.tabIndex = i === idx ? 0 : -1;
      if (focus && i === idx) tab.focus();
    });
    $('#road-panel').setAttribute('aria-labelledby', `road-tab-${idx}`);
    $('#road-status').textContent = phase.badge;
    $('#road-count').textContent = `0${idx + 1} / 03`;
    const title = $('#road-title');
    title.replaceChildren(document.createTextNode(phase.title[0]), document.createElement('br'));
    const span = document.createElement('span'); span.textContent = phase.title[1]; title.append(span);
    $('#road-desc').textContent = phase.description;
    const checks = $('#road-checks'); checks.replaceChildren();
    for (const item of phase.checks) {
      const div = document.createElement('div');
      div.innerHTML = '<svg class="icon"><use href="#i-check"></use></svg>';
      div.append(document.createTextNode(item));
      checks.append(div);
    }
    const link = $('#road-link');
    link.href = phase.url;
    link.replaceChildren(document.createTextNode(`${phase.link} `));
    link.insertAdjacentHTML('beforeend', '<svg class="icon"><use href="#i-arrow-up"></use></svg>');
  }
  roadTabs.forEach((tab, i) => {
    tab.addEventListener('click', () => selectPhase(i));
    tab.addEventListener('keydown', event => {
      if (!['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? roadTabs.length - 1 : (i + (['ArrowRight', 'ArrowDown'].includes(event.key) ? 1 : -1) + roadTabs.length) % roadTabs.length;
      selectPhase(next, true);
    });
  });

  // Real-world network reads. Never invent a hashrate, circulating supply, USD price or miner count.
  const base = ''; // same-origin on monzero.org (CSP connect-src 'self')
  const networkStatus = $('#network-status');
  const networkLabel = $('#network-label');
  const ageText = $('#network-age');
  const disclaimer = $('#network-disclaimer');
  const whole = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });
  const precise = new Intl.NumberFormat('en-US', { maximumFractionDigits: 2 });
  const numeric = input => {
    if (input === null || input === undefined || String(input).trim() === '') return null;
    const value = Number(input);
    return Number.isFinite(value) && value >= 0 ? value : null;
  };
  const formatRate = value => {
    if (value === null) return '—';
    if (value >= 1e9) return `${precise.format(value / 1e9)} GH/s`;
    if (value >= 1e6) return `${precise.format(value / 1e6)} MH/s`;
    if (value >= 1e3) return `${precise.format(value / 1e3)} kH/s`;
    return `${precise.format(value)} H/s`;
  };
  const observedAge = value => {
    const ts = numeric(value);
    if (ts === null) return null;
    return Math.max(0, Math.round(Date.now() / 1000 - ts));
  };
  const ageLabel = age => age < 90 ? 'under 2 minutes old' : age < 3600 ? `${Math.round(age / 60)} minutes old` : `${Math.round(age / 3600)} hours old`;
  async function getAPI(path, signal) {
    const res = await fetch(base + path, { method: 'GET', mode: 'cors', cache: 'no-store', credentials: 'omit', signal, headers: { 'Accept': 'application/json' } });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = await res.json();
    if (json?.schema !== 'monzero-api-v1' || !json?.data || json?.error) throw new Error('Unexpected response schema');
    return json;
  }
  let fetching = false;
  async function refreshNetwork() {
    if (fetching || document.hidden) return;
    fetching = true;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 9000);
    try {
      const [network, supply] = await Promise.allSettled([
        getAPI('/api/v1/network/stats/', controller.signal),
        getAPI('/api/v1/supply/', controller.signal)
      ]);
      let gotData = false, old = false, details = [];
      if (network.status === 'fulfilled') {
        const d = network.value.data;
        const height = numeric(d.top_block_height ?? (numeric(d.height) !== null ? numeric(d.height) - 1 : null));
        const rate = numeric(d.hashrate_estimate);
        $('#stat-height').textContent = height === null ? '—' : whole.format(height);
        $('#stat-hashrate').textContent = formatRate(rate);
        gotData = height !== null || rate !== null;
        const age = observedAge(network.value.observed_at);
        if (age !== null) { details.push(`Network observation ${ageLabel(age)}`); if (age > 600) old = true; }
        else details.push('Network observation time unavailable');
      } else {
        $('#stat-height').textContent = '—'; $('#stat-hashrate').textContent = '—';
        details.push('Network API unavailable');
      }
      if (supply.status === 'fulfilled') {
        const amount = numeric(supply.value.data.circulating_supply);
        $('#stat-supply').textContent = amount === null ? '—' : whole.format(amount);
        gotData ||= amount !== null;
        const age = observedAge(supply.value.observed_at);
        if (age !== null) { details.push(`Supply ${ageLabel(age)}`); if (age > 600) old = true; }
      } else {
        $('#stat-supply').textContent = '—'; details.push('Supply API unavailable');
      }
      networkStatus.classList.toggle('is-stale', old || !gotData);
      networkLabel.textContent = !gotData ? 'PUBLIC DATA UNAVAILABLE' : old ? 'PUBLIC DATA / MAY BE STALE' : 'PUBLIC NODE / REPORTING';
      ageText.textContent = details.join(' · ');
      disclaimer.textContent = old
        ? 'Public-node observations are not current. Figures shown are timestamped source data, not confirmed live readings.'
        : 'Figures are observations from Monzero’s read-only API, not an independent consensus proof. Supply excludes immature rewards.';
    } catch (_) {
      networkStatus.classList.add('is-stale');
      networkLabel.textContent = 'PUBLIC DATA UNAVAILABLE';
      ageText.textContent = 'Visit the explorer to check the network';
    } finally { clearTimeout(timeout); fetching = false; }
  }
  refreshNetwork();
  setInterval(refreshNetwork, 90_000);
})();
