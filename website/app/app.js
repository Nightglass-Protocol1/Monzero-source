const tabs = [...document.querySelectorAll('[role="tab"]')];
const panels = [...document.querySelectorAll('[role="tabpanel"]')];
const draftState = document.querySelector('#draft-state');
const draftType = document.querySelector('#draft-type');
const draftTitle = document.querySelector('#draft-title');
const draftDetail = document.querySelector('#draft-detail');
const draftJson = document.querySelector('#draft-json');

function formDraft(form) {
  if (!form) return {};
  return {
    schema: 'monzero-studio-draft-v1',
    type: form.dataset.type,
    network: 'mainnet-preview',
    submission_enabled: false,
    created_at: new Date().toISOString(),
    fields: Object.fromEntries(new FormData(form).entries()),
  };
}

function updateDraft(form) {
  if (!form) return;
  const draft = formDraft(form);
  const fields = draft.fields;
  draftType.textContent = String(draft.type).toUpperCase();
  draftTitle.textContent = fields.name || fields.title || fields.collection_name || fields.offer || 'Untitled draft';
  draftDetail.textContent = fields.symbol || fields.collection_id || fields.metadata_uri || fields.request || 'Local preview only';
  draftJson.textContent = JSON.stringify(draft, null, 2);
  draftState.textContent = 'Not submitted';
}

function activateTab(tab, focus = false) {
  tabs.forEach((item) => {
    const active = item === tab;
    item.setAttribute('aria-selected', String(active));
    item.tabIndex = active ? 0 : -1;
  });
  panels.forEach((panel) => {
    const active = panel.id === tab.dataset.panel;
    panel.hidden = !active;
    panel.classList.toggle('active', active);
  });
  if (focus) tab.focus();
  updateDraft(document.querySelector(`#${tab.dataset.panel} form`));
}

tabs.forEach((tab, index) => {
  tab.addEventListener('click', () => activateTab(tab));
  tab.addEventListener('keydown', (event) => {
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    let next = index;
    if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
    if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
    if (event.key === 'Home') next = 0;
    if (event.key === 'End') next = tabs.length - 1;
    activateTab(tabs[next], true);
  });
});

document.querySelectorAll('.studio-form').forEach((form) => {
  form.addEventListener('input', () => updateDraft(form));
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const draft = formDraft(form);
    const blob = new Blob([`${JSON.stringify(draft, null, 2)}\n`], {type: 'application/json'});
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = `monzero-${draft.type}-draft.json`;
    link.click();
    URL.revokeObjectURL(link.href);
    draftState.textContent = 'Draft downloaded';
  });
});

fetch('/api/node-info/', {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: '{}',
}).then((response) => {
  if (!response.ok) throw new Error('node unavailable');
  return response.json();
}).then((info) => {
  const ready = Boolean(info.network_ready);
  document.querySelector('#node-state').textContent = ready ? 'Network ready' : (info.synchronized ? 'Node synchronized' : 'Node unavailable');
  document.querySelector('#node-state').classList.toggle('ready', ready);
  document.querySelector('#node-dot').classList.toggle('ready', ready);
  document.querySelector('#height').textContent = Number(info.height).toLocaleString();
  document.querySelector('#peers').textContent = Number(info.incoming_connections_count || 0) + Number(info.outgoing_connections_count || 0);
  document.querySelector('#tip').textContent = info.tip_fresh ? 'Fresh' : 'Stale';
}).catch(() => {
  document.querySelector('#node-state').textContent = 'Unavailable';
  document.querySelector('#node-dot').classList.add('offline');
});

activateTab(tabs[0]);
