// Tabs for /swaps/: the completed swap and the refund path (#refund).
(() => {
  const tabs = [...document.querySelectorAll('[role="tab"]')];
  function show(id, focus) {
    tabs.forEach(t => {
      const on = t.getAttribute('aria-controls') === id;
      t.setAttribute('aria-selected', String(on));
      t.tabIndex = on ? 0 : -1;
      document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
      if (on && focus) t.focus();
    });
  }
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => show(t.getAttribute('aria-controls')));
    t.addEventListener('keydown', e => {
      if (e.key !== 'ArrowRight' && e.key !== 'ArrowLeft') return;
      const next = tabs[(i + (e.key === 'ArrowRight' ? 1 : tabs.length - 1)) % tabs.length];
      show(next.getAttribute('aria-controls'), true);
    });
  });
  const start = location.hash.slice(1);
  show(start === 'refund' ? 'refund' : 'swap');
})();
