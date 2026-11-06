// Shows the Telegram alerts card only when the bot is configured on this site.
const card = document.getElementById('telegram-alerts');
if (card) {
  fetch('/api/telegram/', { headers: { Accept: 'application/json' } })
    .then((r) => (r.ok ? r.json() : null))
    .then((info) => {
      if (!info || info.enabled !== true || typeof info.username !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{4,31}$/.test(info.username)) return;
      const link = document.getElementById('telegram-link');
      link.href = `https://t.me/${info.username}`;
      link.textContent = `@${info.username}`;
      card.hidden = false;
    })
    .catch(() => {});
}
