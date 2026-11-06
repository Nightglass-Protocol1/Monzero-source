(function () {
  'use strict';

  // Links and all five panels remain readable until the stepper is enhanced.
  const stepList = document.getElementById('step-tabs');
  const tabs = Array.from(stepList.querySelectorAll('a'));
  const panels = tabs.map((tab) => document.getElementById(tab.hash.slice(1)));
  stepList.setAttribute('role', 'tablist');
  stepList.setAttribute('aria-orientation', 'horizontal');

  function selectStep(index, focus) {
    tabs.forEach((tab, i) => {
      tab.setAttribute('aria-selected', String(i === index));
      tab.tabIndex = i === index ? 0 : -1;
      panels[i].hidden = i !== index;
    });
    if (focus) tabs[index].focus();
  }

  tabs.forEach((tab, index) => {
    tab.setAttribute('role', 'tab');
    tab.setAttribute('aria-controls', panels[index].id);
    panels[index].setAttribute('role', 'tabpanel');
    panels[index].setAttribute('aria-labelledby', tab.id);
    panels[index].tabIndex = 0;
    tab.addEventListener('click', (event) => {
      event.preventDefault();
      selectStep(index, false);
    });
    tab.addEventListener('keydown', (event) => {
      let next;
      if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
      if (event.key === 'ArrowLeft') next = (index + tabs.length - 1) % tabs.length;
      if (event.key === 'Home') next = 0;
      if (event.key === 'End') next = tabs.length - 1;
      if (event.key === ' ') next = index;
      if (next !== undefined) {
        event.preventDefault();
        selectStep(next, true);
      }
    });
  });
  function followStepHash() {
    const index = panels.findIndex((panel) => '#' + panel.id === window.location.hash);
    if (index !== -1) selectStep(index, false);
    return index;
  }
  if (followStepHash() === -1) selectStep(0, false);
  window.addEventListener('hashchange', followStepHash);

  const quiz = document.getElementById('kind-quiz');
  const quizResult = document.getElementById('quiz-result');
  const recommendations = {
    nft: ['NFT', 'You want one standalone artwork with one copy. An NFT is a one-of-one work.', 'Start an NFT draft →'],
    edition: ['Edition', 'You want numbered copies of one artwork. An Edition has 2 to 1,000,000 copies. Decide the count before issuing.', 'Start an Edition draft →'],
    collection: ['Collection', 'You want different artworks to belong to a named group. Start with a Collection; each member NFT references its 64-hex-character collection ID.', 'Start a Collection draft →'],
    fungible: ['Token', 'You want interchangeable units. A Token has a fixed supply and 0–11 decimals. Nobody can mint more. The artwork answers do not change this choice.', 'Start a Token draft →']
  };
  function recommend() {
    const units = document.getElementById('quiz-purpose').value === 'units';
    const copies = document.getElementById('quiz-copies').value === 'many';
    const group = document.getElementById('quiz-group').value === 'series';
    const kind = units ? 'fungible' : group ? 'collection' : copies ? 'edition' : 'nft';
    const result = recommendations[kind];
    document.getElementById('quiz-kind').textContent = result[0];
    document.getElementById('quiz-reason').textContent = result[1] + (kind === 'collection' && copies ? ' For numbered copies of an individual work, also use the Edition option.' : '');
    const link = document.getElementById('quiz-link');
    link.textContent = result[2];
    link.href = '/app/?kind=' + kind + '#create';
    quizResult.hidden = false;
  }
  document.getElementById('quiz-submit').hidden = false;
  quiz.addEventListener('submit', (event) => { event.preventDefault(); recommend(); });
  quiz.addEventListener('change', recommend);

  const calculator = document.getElementById('supply-calculator');
  const supplyInput = document.getElementById('token-supply');
  const decimalsInput = document.getElementById('token-decimals');
  const atomicResult = document.getElementById('atomic-result');
  const unitResult = document.getElementById('unit-result');
  const supplyStatus = document.getElementById('supply-status');
  const maxSupply = (1n << 64n) - 1n;

  function calculate() {
    const supply = supplyInput.value.trim();
    const decimalsText = decimalsInput.value;
    supplyInput.removeAttribute('aria-invalid');
    decimalsInput.removeAttribute('aria-invalid');
    atomicResult.textContent = '—';
    unitResult.textContent = '—';
    function invalid(message, input) {
      supplyStatus.textContent = message;
      supplyStatus.dataset.valid = 'false';
      input.setAttribute('aria-invalid', 'true');
    }
    if (!/^\d{1,2}$/.test(decimalsText) || Number(decimalsText) > 11) {
      invalid('Choose a whole number of decimals from 0 to 11.', decimalsInput);
      return;
    }
    const decimals = Number(decimalsText);
    unitResult.textContent = decimals === 0 ? '1' : '0.' + '0'.repeat(decimals - 1) + '1';
    if (supply.length > 80 || !/^\d+(?:\.\d+)?$/.test(supply)) {
      invalid('Enter a positive supply using digits and an optional decimal point. No commas, signs or exponent notation.', supplyInput);
      return;
    }
    const parts = supply.split('.');
    // Insignificant trailing zeros do not require additional decimal places.
    const fraction = (parts[1] || '').replace(/0+$/, '');
    if (fraction.length > decimals) {
      invalid('This supply needs more than ' + decimals + ' decimal places. It cannot be expressed as whole smallest units.', supplyInput);
      return;
    }
    // Never convert supply through Number: 64-bit boundaries require exact integers.
    const atomic = BigInt(parts[0]) * (10n ** BigInt(decimals)) + BigInt(fraction.padEnd(decimals, '0') || '0');
    atomicResult.textContent = atomic.toString();
    if (atomic === 0n) {
      invalid('Supply must be greater than zero.', supplyInput);
    } else if (atomic > maxSupply) {
      invalid('Too large: smallest-unit supply must be at most 18446744073709551615 (2^64−1).', supplyInput);
    } else {
      supplyStatus.textContent = 'Valid — ' + atomic.toString() + ' smallest units fits within 2^64−1. The supply is fixed.';
      supplyStatus.dataset.valid = 'true';
    }
  }
  document.getElementById('calculate-button').hidden = false;
  calculator.addEventListener('submit', (event) => { event.preventDefault(); calculate(); });
  calculator.addEventListener('input', calculate);
  calculate();

  const storageKey = 'monzero-creators-checklist-v1';
  const checks = Array.from(document.querySelectorAll('.ch-check-items input'));
  const storageNote = document.getElementById('checklist-storage');
  const progress = document.getElementById('checklist-progress');
  function reportProgress() {
    progress.textContent = checks.filter((check) => check.checked).length + ' of ' + checks.length + ' checked';
  }
  function storageUnavailable() {
    storageNote.textContent = 'Browser storage is unavailable. Your checks work for this page visit only. Print or download a copy to keep them.';
  }
  try {
    const saved = JSON.parse(window.localStorage.getItem(storageKey) || '{}');
    if (saved && typeof saved === 'object' && !Array.isArray(saved)) {
      checks.forEach((check) => { check.checked = saved[check.name] === true; });
    }
    // Probe writes too: some private modes permit reads but reject writes.
    window.localStorage.setItem(storageKey, JSON.stringify(Object.fromEntries(checks.map((check) => [check.name, check.checked]))));
    storageNote.textContent = 'Your checks are saved only in this browser. Clearing browser data removes them. Keep a printed or downloaded copy too.';
  } catch (_) {
    storageUnavailable();
  }
  checks.forEach((check) => check.addEventListener('change', () => {
    reportProgress();
    try {
      window.localStorage.setItem(storageKey, JSON.stringify(Object.fromEntries(checks.map((item) => [item.name, item.checked]))));
    } catch (_) {
      storageUnavailable();
    }
  }));
  reportProgress();
  const printButton = document.getElementById('print-checklist');
  printButton.hidden = false;
  printButton.addEventListener('click', () => window.print());

  const copyButton = document.getElementById('copy-announcement');
  const announcement = document.getElementById('announcement-text');
  const copyStatus = document.getElementById('copy-status');
  copyButton.hidden = false;
  copyButton.addEventListener('click', async () => {
    try {
      if (!navigator.clipboard || !navigator.clipboard.writeText) throw new Error('Clipboard unavailable');
      await navigator.clipboard.writeText(announcement.value);
      copyStatus.textContent = 'Announcement copied. Replace all placeholders before sharing.';
    } catch (_) {
      announcement.focus();
      announcement.select();
      copyStatus.textContent = 'Text selected. Use your device’s Copy action to copy it.';
    }
  });
})();
