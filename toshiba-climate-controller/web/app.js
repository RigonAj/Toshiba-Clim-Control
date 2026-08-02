'use strict';

/* ---------------------------------------------------------------------------
   Interface des climatiseurs Toshiba.

   Les boitiers sont symetriques (cahier v2 §4.1) : cette page n'a pas de
   boitier « principal ». Elle interroge chaque adresse declaree, directement,
   en JavaScript. Un boitier injoignable ne desactive que sa propre carte.

   Contrat d'API : docs/api-v2.md.
   --------------------------------------------------------------------------- */

const CFG_KEY   = 'clim.cfg.v1';
const POLL_MS   = 5000;   // interrogation de chaque boitier
const TIMEOUT_MS= 3000;   // au-dela, le boitier est declare hors ligne
const SEND_MS   = 450;    // regroupement des appuis rapides sur +/-

const MODE_LABELS = { auto:'Auto', cool:'Froid', heat:'Chaud', dry:'Déshu' };
const FAN_LABELS  = { auto:'Auto', quiet:'Silence', low:'Min', medium:'Moy', high:'Max', turbo:'Turbo' };
const SRC_LABELS  = { web:'interface', remote:'télécommande', restored:'restauré',
                      schedule:'planning', lock:'maintien de l\'arrêt' };
const CONF_LABELS = { fresh:'état récent', stale:'état ancien', unknown:'état inconnu' };

const DEFAULT_CFG = {
  devices: [
    { id:'salon',   name:'Salon',   base:'' },
    { id:'chambre', name:'Chambre', base:'http://192.168.1.41' }
  ],
  token: ''
};

let cfg = null;
const devices = [];

/* ============================ utilitaires ============================== */

const $  = (sel, root=document) => root.querySelector(sel);
const $$ = (sel, root=document) => Array.from(root.querySelectorAll(sel));

function uid() {
  const a = new Uint8Array(16);
  crypto.getRandomValues(a);
  return Array.from(a, b => b.toString(16).padStart(2, '0')).join('');
}

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

function hhmm(min) {
  return String(Math.floor(min / 60)).padStart(2, '0') + ':' + String(min % 60).padStart(2, '0');
}
function toMinutes(s) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(String(s || '')) ;
  return m ? clamp(+m[1], 0, 23) * 60 + clamp(+m[2], 0, 59) : 0;
}

function ago(iso) {
  if (!iso) return '';
  const s = Math.round((Date.now() - Date.parse(iso)) / 1000);
  if (!isFinite(s)) return '';
  if (s < 60)    return "à l'instant";
  if (s < 3600)  return 'il y a ' + Math.round(s / 60) + ' min';
  if (s < 86400) return 'il y a ' + Math.round(s / 3600) + ' h';
  return 'il y a ' + Math.round(s / 86400) + ' j';
}

function toast(msg, isError) {
  const el = document.createElement('div');
  el.className = 'toast' + (isError ? ' err' : '');
  el.textContent = msg;
  $('#toasts').appendChild(el);
  setTimeout(() => el.remove(), 4000);
}

function hex(bytes) {
  if (typeof bytes === 'string') return bytes;
  if (!Array.isArray(bytes)) return '';
  return bytes.map(b => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
}

/* ========================== configuration ============================== */

async function loadConfig() {
  const stored = localStorage.getItem(CFG_KEY);
  if (stored) {
    try { return normalizeCfg(JSON.parse(stored)); } catch (e) { /* config illisible */ }
  }
  // config.json est optionnel : il permet de livrer les adresses par defaut
  // avec les fichiers statiques, sans recompiler ni toucher au navigateur.
  try {
    const r = await fetch('config.json', { cache: 'no-store' });
    if (r.ok) return normalizeCfg(await r.json());
  } catch (e) { /* absent : on prend les valeurs par defaut */ }
  return normalizeCfg(DEFAULT_CFG);
}

function normalizeCfg(c) {
  const out = { devices: [], token: c.token || '' };
  (c.devices || []).forEach((d, i) => {
    out.devices.push({
      id:   d.id   || ('dev' + i),
      // Nom laisse vide : c'est alors celui declare par le boitier qui sert.
      name: d.name || '',
      base: String(d.base || '').replace(/\/+$/, '')
    });
  });
  if (!out.devices.length) out.devices = DEFAULT_CFG.devices.slice();
  return out;
}

function saveConfig() { localStorage.setItem(CFG_KEY, JSON.stringify(cfg)); }

/* ============================== reseau ================================= */

async function api(dev, path, opts) {
  opts = opts || {};
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  const headers = { 'Accept': 'application/json' };
  if (opts.body) headers['Content-Type'] = 'application/json';
  if (cfg.token) headers['Authorization'] = 'Bearer ' + cfg.token;
  try {
    const r = await fetch(dev.cfg.base + path, {
      method:  opts.method || 'GET',
      headers: headers,
      body:    opts.body ? JSON.stringify(opts.body) : undefined,
      signal:  ctrl.signal,
      cache:   'no-store'
    });
    let data = null;
    try { data = await r.json(); } catch (e) { /* corps vide ou non JSON */ }
    return { ok: r.ok, status: r.status, data: data };
  } catch (e) {
    return { ok: false, status: 0, data: null, error: e };
  } finally {
    clearTimeout(timer);
  }
}

/* ============================ construction ============================= */

function buildDevice(dcfg) {
  const node = $('#tpl-device').content.firstElementChild.cloneNode(true);
  const dev = {
    cfg: dcfg,
    el: node,
    status: null,
    online: null,
    draft: null,        // etat modifie localement, pas encore accepte par le boitier
    draftSeq: 0,
    sendTimer: null,
    caps: null,
    sched: null,        // planning tel que renvoye par le boitier
    schedDraft: null,   // copie en cours d'edition
    curve: 'week',
    sel: -1,            // index du point selectionne
    schedLoaded: false
  };
  dev.r = {
    name:     $('[data-name]', node),
    conn:     $('[data-conn]', node),
    conf:     $('[data-conf]', node),
    lock:     $('[data-lock]', node),
    power:    $('[data-power]', node),
    tempVal:  $('[data-temp-val]', node),
    modes:    $('[data-modes]', node),
    fans:     $('[data-fans]', node),
    swing:    $('[data-swing]', node),
    updated:  $('[data-updated]', node),
    schedPanel:  $('[data-sched-panel]', node),
    schedNote:   $('[data-sched-note]', node),
    schedEnabled:$('[data-sched-enabled]', node),
    curves:   $('[data-curves]', node),
    graph:    $('[data-graph]', node),
    editor:   $('[data-point-editor]', node),
    peTime:   $('[data-pe-time]', node),
    peTemp:   $('[data-pe-temp]', node),
    peMode:   $('[data-pe-mode]', node),
    peFan:    $('[data-pe-fan]', node),
    pePower:  $('[data-pe-power]', node),
    peSwing:  $('[data-pe-swing]', node),
    peSwingRow: $('[data-pe-swing-row]', node),
    peLock:   $('[data-pe-lock]', node),
    peLockRow: $('[data-pe-lock-row]', node),
    peLockHint: $('[data-pe-lock-hint]', node),
    peDelete: $('[data-pe-delete]', node),
    schedSave:   $('[data-sched-save]', node),
    schedRevert: $('[data-sched-revert]', node),
    schedStatus: $('[data-sched-status]', node),
    diagPanel:   $('[data-diag-panel]', node),
    diag:        $('[data-diag]', node),
    diagRefresh: $('[data-diag-refresh]', node)
  };
  dev.r.name.textContent = dcfg.name;
  dev.r.conn.textContent = 'connexion...';

  wireDevice(dev);
  $('#devices').appendChild(node);
  devices.push(dev);
  return dev;
}

function wireDevice(dev) {
  const r = dev.r;

  r.power.addEventListener('click', () => {
    const s = current(dev); if (!s) return;
    edit(dev, { power: !s.power });
  });

  $$('[data-temp]', dev.el).forEach(btn => {
    btn.addEventListener('click', () => {
      const s = current(dev); if (!s) return;
      const c = dev.caps || {};
      const lo = c.temperature_min_c != null ? c.temperature_min_c : 17;
      const hi = c.temperature_max_c != null ? c.temperature_max_c : 30;
      edit(dev, { target_temperature_c: clamp(s.target_temperature_c + (+btn.dataset.temp), lo, hi) });
    });
  });

  r.swing.addEventListener('change', () => edit(dev, { swing: r.swing.checked }));

  /* --- planning --- */
  r.schedPanel.addEventListener('toggle', () => {
    if (r.schedPanel.open && !dev.schedLoaded) loadSchedule(dev);
  });
  r.curves.addEventListener('click', e => {
    const b = e.target.closest('button[data-curve]'); if (!b) return;
    dev.curve = b.dataset.curve;
    dev.sel = -1;
    renderSchedule(dev);
  });
  r.schedEnabled.addEventListener('change', () => {
    if (!dev.schedDraft) return;
    dev.schedDraft.enabled = r.schedEnabled.checked;
    markSchedDirty(dev);
  });
  r.schedSave.addEventListener('click', () => saveSchedule(dev));
  r.schedRevert.addEventListener('click', () => {
    dev.schedDraft = JSON.parse(JSON.stringify(dev.sched));
    dev.sel = -1;
    renderSchedule(dev);
  });
  r.peDelete.addEventListener('click', () => {
    const pts = points(dev); if (dev.sel < 0) return;
    pts.splice(dev.sel, 1);
    dev.sel = -1;
    markSchedDirty(dev);
    renderSchedule(dev);
  });
  [['peTime','at'], ['peTemp','temperature_c'], ['peMode','mode'], ['peFan','fan'],
   ['pePower','power'], ['peSwing','swing'], ['peLock','lock']].forEach(([ref, field]) => {
    r[ref].addEventListener('change', () => {
      const p = points(dev)[dev.sel]; if (!p) return;
      const el = r[ref];
      if (field === 'at')                 p.at = hhmm(Math.round(toMinutes(el.value) / 30) * 30);
      else if (field === 'temperature_c') p.temperature_c = clamp(Math.round(+el.value || 0), +el.min, +el.max);
      else if (el.type === 'checkbox')    p[field] = el.checked;
      else                                p[field] = el.value;
      sortPoints(dev, p);
      markSchedDirty(dev);
      renderSchedule(dev);
    });
  });
  wireGraph(dev);

  /* --- diagnostic --- */
  r.diagPanel.addEventListener('toggle', () => { if (r.diagPanel.open) loadDiag(dev); });
  r.diagRefresh.addEventListener('click', () => loadDiag(dev));
}

/* ======================= etat courant et envoi ========================= */

function current(dev) {
  if (dev.draft) return dev.draft;
  return dev.status && dev.status.state ? dev.status.state : null;
}

function edit(dev, patch) {
  const s = current(dev); if (!s) return;
  dev.draft = Object.assign({}, s, patch);
  dev.draftSeq++;
  render(dev);
  clearTimeout(dev.sendTimer);
  dev.sendTimer = setTimeout(() => flush(dev), SEND_MS);
}

async function flush(dev) {
  if (!dev.draft) return;
  const seq = dev.draftSeq;
  const d = dev.draft;
  const body = {
    command_id: uid(),
    power: d.power,
    mode: d.mode,
    target_temperature_c: d.target_temperature_c,
    fan: d.fan,
    swing: d.swing,
    expected_revision: dev.status && dev.status.state ? dev.status.state.revision : undefined
  };
  const res = await api(dev, '/api/v1/climate', { method: 'PUT', body: body });

  if (res.status === 409 && res.data && res.data.state) {
    // Une commande plus recente (telecommande, planning, autre navigateur) a
    // pris la main : le boitier fait autorite, plan §10.2.
    dev.status.state = res.data.state;
    dev.draft = null;
    toast(dev.cfg.name + ' : état repris du boîtier');
    render(dev);
    return;
  }
  if (!res.ok) {
    toast(dev.cfg.name + ' : commande refusée' + (res.status ? ' (' + res.status + ')' : ''), true);
    if (seq === dev.draftSeq) { dev.draft = null; render(dev); }
    return;
  }
  if (res.data && res.data.state) {
    dev.status = dev.status || {};
    dev.status.state = res.data.state;
  }
  if (seq === dev.draftSeq) { dev.draft = null; render(dev); }
  else { flush(dev); }   // l'utilisateur a rejoue pendant l'envoi
}

/* ============================ interrogation ============================ */

async function pollDevice(dev) {
  const res = await api(dev, '/api/v1/status');
  if (!res.ok || !res.data) {
    dev.online = false;
    render(dev);
    return;
  }
  dev.online = res.data.online !== false;
  dev.caps = res.data.capabilities || dev.caps;
  // Ne pas ecraser une modification locale en attente d'envoi.
  if (dev.draft) {
    const keep = dev.status ? dev.status.state : null;
    dev.status = res.data;
    if (keep) dev.status.state = res.data.state || keep;
  } else {
    dev.status = res.data;
  }
  render(dev);
}

function pollAll() { devices.forEach(pollDevice); }

/* ============================== rendu ================================== */

function render(dev) {
  const r = dev.r, st = dev.status, s = current(dev);
  const caps = dev.caps || {};
  const offline = dev.online === false;

  dev.el.classList.toggle('is-offline', offline);
  // Le nom saisi dans les reglages prime : sinon un renommage semblerait echouer.
  r.name.textContent = dev.cfg.name || (st && st.display_name) || 'Boîtier';

  r.conn.textContent = offline ? 'hors ligne' : (dev.online === null ? 'connexion...' : 'en ligne');
  r.conn.className = 'chip' + (offline ? ' bad' : dev.online ? ' ok' : '');

  if (s && s.confidence) {
    r.conf.hidden = false;
    r.conf.textContent = CONF_LABELS[s.confidence] || s.confidence;
    r.conf.className = 'chip chip-ghost' + (s.confidence === 'stale' ? ' stale' : s.confidence === 'unknown' ? ' bad' : '');
  } else {
    r.conf.hidden = true;
  }

  const schedInfo = st && st.schedule;
  r.lock.hidden = !(schedInfo && schedInfo.lock_active);

  if (!s) {
    r.updated.textContent = offline ? 'Boîtier injoignable.' : '';
    return;
  }

  dev.el.classList.toggle('is-standby', !s.power);
  r.power.setAttribute('aria-pressed', String(!!s.power));
  r.tempVal.textContent = s.target_temperature_c != null ? s.target_temperature_c : '--';
  r.swing.checked = !!s.swing;

  renderSeg(r.modes, caps.modes || ['auto','cool','heat','dry'], MODE_LABELS, s.mode,
            v => edit(dev, { mode: v }));
  renderSeg(r.fans, caps.fans || ['auto'], FAN_LABELS, s.fan,
            v => edit(dev, { fan: v }));

  const unverified = caps.unverified_fields || [];
  $$('[data-warn]', dev.el).forEach(el => { el.hidden = unverified.indexOf(el.dataset.warn) < 0; });

  const lo = caps.temperature_min_c != null ? caps.temperature_min_c : 17;
  const hi = caps.temperature_max_c != null ? caps.temperature_max_c : 30;
  $$('[data-temp]', dev.el).forEach(b => {
    b.disabled = (+b.dataset.temp < 0) ? s.target_temperature_c <= lo : s.target_temperature_c >= hi;
  });
  r.peTemp.min = lo; r.peTemp.max = hi;

  const bits = [];
  if (dev.draft) bits.push('envoi en cours...');
  else if (s.updated_at) bits.push('modifié ' + ago(s.updated_at));
  if (s.source) bits.push(SRC_LABELS[s.source] || s.source);
  r.updated.textContent = bits.join(' · ');

  renderSchedNote(dev);
  renderGlobals();
}

function renderSeg(container, values, labels, active, onPick) {
  const wanted = values.join('|');
  if (container.dataset.values !== wanted) {
    container.dataset.values = wanted;
    container.innerHTML = '';
    values.forEach(v => {
      const b = document.createElement('button');
      b.type = 'button';
      b.dataset.value = v;
      b.textContent = labels[v] || v;
      b.addEventListener('click', () => onPick(v));
      container.appendChild(b);
    });
  }
  $$('button', container).forEach(b => b.classList.toggle('on', b.dataset.value === active));
}

function renderGlobals() {
  const known = devices.filter(d => d.online !== null);
  const anyOn = devices.some(d => d.online);
  const banner = $('#banner');
  if (known.length && !anyOn) {
    banner.hidden = false;
    banner.textContent = "Aucun boîtier joignable. Vérifier le réseau, ou les adresses dans les réglages.";
  } else {
    banner.hidden = true;
  }

  const versions = devices.filter(d => d.status && d.status.firmware)
                          .map(d => d.cfg.name + ' ' + d.status.firmware);
  $('#foot-versions').textContent = versions.join(' · ');

  const unsynced = devices.some(d => d.status && d.status.time && d.status.time.synced === false);
  $('#clock-warn').hidden = !unsynced;
}

/* ============================== planning =============================== */

function points(dev) {
  if (!dev.schedDraft) return [];
  const c = dev.schedDraft.curves || {};
  if (!c[dev.curve]) c[dev.curve] = [];
  return c[dev.curve];
}

function sortPoints(dev, keep) {
  const arr = points(dev);
  arr.sort((a, b) => toMinutes(a.at) - toMinutes(b.at));
  if (keep) dev.sel = arr.indexOf(keep);
}

function markSchedDirty(dev) {
  dev.schedDirty = true;
  dev.r.schedSave.disabled = false;
  dev.r.schedRevert.disabled = false;
  dev.r.schedStatus.textContent = 'modifications non enregistrées';
}

async function loadSchedule(dev) {
  const res = await api(dev, '/api/v1/schedule');
  if (!res.ok || !res.data) {
    dev.r.schedStatus.textContent = 'planning indisponible';
    return;
  }
  dev.schedLoaded = true;
  dev.sched = res.data;
  dev.schedDraft = JSON.parse(JSON.stringify(res.data));
  dev.schedDirty = false;
  dev.r.schedSave.disabled = true;
  dev.r.schedRevert.disabled = true;
  dev.r.schedStatus.textContent = '';
  renderSchedule(dev);
}

async function saveSchedule(dev) {
  const body = {
    expected_revision: dev.sched ? dev.sched.revision : undefined,
    enabled: !!dev.schedDraft.enabled,
    curves: dev.schedDraft.curves
  };
  dev.r.schedSave.disabled = true;
  dev.r.schedStatus.textContent = 'enregistrement...';
  const res = await api(dev, '/api/v1/schedule', { method: 'PUT', body: body });
  if (res.status === 409 && res.data) {
    dev.sched = res.data;
    dev.schedDraft = JSON.parse(JSON.stringify(res.data));
    dev.schedDirty = false;
    dev.r.schedStatus.textContent = 'planning repris du boîtier';
    renderSchedule(dev);
    return;
  }
  if (!res.ok) {
    dev.r.schedSave.disabled = false;
    dev.r.schedStatus.textContent = 'échec de l\'enregistrement';
    toast(dev.cfg.name + ' : planning non enregistré', true);
    return;
  }
  dev.sched = res.data || JSON.parse(JSON.stringify(dev.schedDraft));
  dev.schedDraft = JSON.parse(JSON.stringify(dev.sched));
  dev.schedDirty = false;
  dev.r.schedRevert.disabled = true;
  dev.r.schedStatus.textContent = 'enregistré';
  renderSchedule(dev);
}

function activeCurveName(dev) {
  if (dev.status && dev.status.schedule && dev.status.schedule.active_curve) {
    return dev.status.schedule.active_curve;
  }
  const d = new Date().getDay();
  return (d === 0 || d === 6) ? 'weekend' : 'week';
}

function renderSchedNote(dev) {
  const note = dev.r.schedNote;
  const info = dev.status && dev.status.schedule;
  if (!info) { note.textContent = ''; return; }
  if (!info.enabled) { note.textContent = 'inactif'; return; }
  const bits = [];
  if (info.next_point_at) bits.push('prochain point ' + info.next_point_at);
  if (info.lock_active) bits.push('arrêt maintenu');
  if (info.override_active) bits.push('réglage manuel en cours');
  note.textContent = bits.join(' · ');
}

/* --------------------------- graphe 24 heures -------------------------- */

// Repere du graphe, en unites de viewBox. Volontairement etroit : le SVG est
// mis a l'echelle de la carte, donc plus la boite est etroite, plus les
// libelles et les cibles tactiles restent lisibles une fois reduits.
const G = { W:480, H:300, L:40, R:12, TOP:16, BOT:222, OFF:252, AXIS:288 };

function tempBounds(dev) {
  const c = dev.caps || {};
  return [c.temperature_min_c != null ? c.temperature_min_c : 17,
          c.temperature_max_c != null ? c.temperature_max_c : 30];
}
function xOf(min)  { return G.L + (G.W - G.L - G.R) * (min / 1440); }
function minOf(x)  { return (x - G.L) / (G.W - G.L - G.R) * 1440; }
function yOf(dev, t) {
  const [lo, hi] = tempBounds(dev);
  return G.BOT - (clamp(t, lo, hi) - lo) / (hi - lo) * (G.BOT - G.TOP);
}
function tempOf(dev, y) {
  const [lo, hi] = tempBounds(dev);
  return lo + (G.BOT - y) / (G.BOT - G.TOP) * (hi - lo);
}

function renderSchedule(dev) {
  const r = dev.r;
  if (!dev.schedDraft) return;
  r.schedEnabled.checked = !!dev.schedDraft.enabled;
  $$('button[data-curve]', r.curves).forEach(b => b.classList.toggle('on', b.dataset.curve === dev.curve));
  drawGraph(dev);
  renderPointEditor(dev);
}

function drawGraph(dev) {
  const svg = dev.r.graph;
  const pts = points(dev).slice().sort((a, b) => toMinutes(a.at) - toMinutes(b.at));
  const [lo, hi] = tempBounds(dev);
  const parts = [];

  parts.push(`<rect class="g-band" x="${G.L}" y="${G.OFF - 16}" width="${G.W - G.L - G.R}" height="32" rx="6"/>`);

  for (let h = 0; h <= 24; h += 3) {
    const x = xOf(h * 60);
    parts.push(`<line class="g-grid" x1="${x}" y1="${G.TOP}" x2="${x}" y2="${G.OFF + 16}"/>`);
    parts.push(`<text class="g-axis" x="${x}" y="${G.AXIS}" text-anchor="middle">${h}h</text>`);
  }
  for (let t = Math.ceil(lo / 2) * 2; t <= hi; t += 2) {
    const y = yOf(dev, t);
    parts.push(`<line class="g-grid" x1="${G.L}" y1="${y}" x2="${G.W - G.R}" y2="${y}"/>`);
    parts.push(`<text class="g-axis" x="${G.L - 6}" y="${y + 6}" text-anchor="end">${t}</text>`);
  }
  parts.push(`<text class="g-axis" x="${G.L - 6}" y="${G.OFF + 6}" text-anchor="end">off</text>`);

  const yFor = p => p.power ? yOf(dev, p.temperature_c) : G.OFF;

  if (pts.length) {
    // Fonction en escalier : un point vaut jusqu'au suivant. Le dernier point
    // de la journee deborde sur le debut de la suivante (cahier v2 §3).
    const segs = [];
    for (let i = 0; i < pts.length; i++) {
      const from = toMinutes(pts[i].at);
      const to   = (i + 1 < pts.length) ? toMinutes(pts[i + 1].at) : 1440;
      segs.push([from, to, pts[i]]);
    }
    const first = toMinutes(pts[0].at);
    if (first > 0) segs.unshift([0, first, pts[pts.length - 1]]);

    segs.forEach(([a, b, p]) => {
      const y = yFor(p);
      const cls = p.power ? 'g-on' : (p.lock ? 'g-off g-lock' : 'g-off');
      parts.push(`<line class="${cls}" x1="${xOf(a)}" y1="${y}" x2="${xOf(b)}" y2="${y}"/>`);
    });
    for (let i = 0; i < segs.length - 1; i++) {
      const x = xOf(segs[i][1]);
      parts.push(`<line class="g-link" x1="${x}" y1="${yFor(segs[i][2])}" x2="${x}" y2="${yFor(segs[i + 1][2])}"/>`);
    }
  }

  // Heure courante, seulement sur la courbe qui s'applique aujourd'hui.
  if (dev.curve === activeCurveName(dev)) {
    const now = new Date();
    const x = xOf(now.getHours() * 60 + now.getMinutes());
    parts.push(`<line class="g-now" x1="${x}" y1="${G.TOP}" x2="${x}" y2="${G.OFF + 16}"/>`);
  }

  pts.forEach((p, i) => {
    const x = xOf(toMinutes(p.at)), y = yFor(p);
    const cls = 'g-pt' + (p.power ? '' : (p.lock ? ' off lock' : ' off'))
                       + (i === dev.sel ? ' sel' : '');
    parts.push(`<circle class="${cls}" cx="${x}" cy="${y}" r="9"/>`);
    parts.push(`<circle class="g-hit" data-i="${i}" cx="${x}" cy="${y}" r="24"/>`);
  });

  svg.innerHTML = parts.join('');
  // Les points ont pu etre reordonnes par le tri d'affichage.
  dev.schedDraft.curves[dev.curve] = pts;
}

function svgPoint(svg, evt) {
  const ctm = svg.getScreenCTM();
  if (!ctm) return { x: 0, y: 0 };
  const inv = ctm.inverse();
  const pt = svg.createSVGPoint();
  pt.x = evt.clientX; pt.y = evt.clientY;
  const p = pt.matrixTransform(inv);
  return { x: p.x, y: p.y };
}

function wireGraph(dev) {
  const svg = dev.r.graph;
  let dragging = -1;

  function place(i, x, y) {
    const arr = points(dev);
    const p = arr[i]; if (!p) return;
    const min = clamp(Math.round(minOf(x) / 30) * 30, 0, 1410);
    if (!arr.some((o, j) => j !== i && toMinutes(o.at) === min)) p.at = hhmm(min);
    if (y > G.BOT + 10) {
      p.power = false;
    } else {
      p.power = true;
      p.temperature_c = Math.round(tempOf(dev, y));
      const [lo, hi] = tempBounds(dev);
      p.temperature_c = clamp(p.temperature_c, lo, hi);
    }
  }

  svg.addEventListener('pointerdown', e => {
    if (!dev.schedDraft) return;
    const arr = points(dev);
    const hit = e.target.closest('.g-hit');
    const { x, y } = svgPoint(svg, e);
    if (hit) {
      dragging = +hit.dataset.i;
    } else {
      if (x < G.L - 10 || x > G.W - G.R + 10 || y < G.TOP - 10 || y > G.OFF + 20) return;
      // Nouveau point : il herite du reglage qui s'applique a cet instant.
      const min = clamp(Math.round(minOf(x) / 30) * 30, 0, 1410);
      if (arr.some(o => toMinutes(o.at) === min)) return;
      const ref = arr.length ? arr[arr.length - 1] : null;
      const [lo, hi] = tempBounds(dev);
      const s = current(dev) || {};
      arr.push({
        at: hhmm(min),
        power: y <= G.BOT + 10,
        temperature_c: y <= G.BOT + 10 ? clamp(Math.round(tempOf(dev, y)), lo, hi)
                                       : (ref ? ref.temperature_c : (s.target_temperature_c || 22)),
        mode:  ref ? ref.mode  : (s.mode || 'auto'),
        fan:   ref ? ref.fan   : (s.fan  || 'auto'),
        swing: ref ? !!ref.swing : !!s.swing,
        lock:  false
      });
      arr.sort((a, b) => toMinutes(a.at) - toMinutes(b.at));
      dragging = arr.findIndex(p => toMinutes(p.at) === min);
    }
    dev.sel = dragging;
    markSchedDirty(dev);
    // La capture est prise sur le SVG lui-meme, jamais sur le point : le
    // contenu est redessine a chaque deplacement et emporterait la cible.
    try { svg.setPointerCapture(e.pointerId); } catch (err) { /* pointeur deja relache */ }
    renderSchedule(dev);
    e.preventDefault();
  });

  svg.addEventListener('pointermove', e => {
    if (dragging < 0) return;
    const { x, y } = svgPoint(svg, e);
    const p = points(dev)[dragging];
    place(dragging, x, y);
    const arr = points(dev);
    arr.sort((a, b) => toMinutes(a.at) - toMinutes(b.at));
    dragging = arr.indexOf(p);
    dev.sel = dragging;
    renderSchedule(dev);
    e.preventDefault();
  });

  const end = e => {
    if (dragging < 0) return;
    dragging = -1;
    try { svg.releasePointerCapture(e.pointerId); } catch (err) { /* deja relache */ }
  };
  svg.addEventListener('pointerup', end);
  svg.addEventListener('pointercancel', end);
}

function renderPointEditor(dev) {
  const r = dev.r;
  const p = points(dev)[dev.sel];
  r.editor.hidden = !p;
  if (!p) return;
  const caps = dev.caps || {};
  fillSelect(r.peMode, caps.modes || ['auto','cool','heat','dry'], MODE_LABELS, p.mode);
  fillSelect(r.peFan,  caps.fans  || ['auto'], FAN_LABELS, p.fan);
  r.peTime.value  = p.at;
  r.peTemp.value  = p.temperature_c;
  r.pePower.checked = !!p.power;
  r.peSwing.checked = !!p.swing;
  r.peTemp.disabled = !p.power;
  r.peMode.disabled = !p.power;
  r.peFan.disabled  = !p.power;
  // Un point d'arret n'a ni consigne ni oscillation, mais il peut etre
  // « maintenu » : les deux commandes s'excluent, on montre celle qui a un sens.
  r.peSwingRow.hidden = !p.power;
  r.peLockRow.hidden  = !!p.power;
  r.peLock.checked    = !!p.lock;
  r.peLockHint.hidden = !(!p.power && p.lock);
}

function fillSelect(sel, values, labels, active) {
  const wanted = values.join('|');
  if (sel.dataset.values !== wanted) {
    sel.dataset.values = wanted;
    sel.innerHTML = values.map(v => `<option value="${v}">${labels[v] || v}</option>`).join('');
  }
  sel.value = active;
}

/* ============================= diagnostic ============================== */

async function loadDiag(dev) {
  dev.r.diag.textContent = 'lecture...';
  const res = await api(dev, '/api/v1/diagnostics/ir');
  if (!res.ok || !res.data) {
    dev.r.diag.textContent = res.status === 401 || res.status === 403
      ? 'Réservé à l\'administrateur : renseigner le jeton dans les réglages.'
      : 'Diagnostic indisponible.';
    return;
  }
  const d = res.data;
  const tx = d.last_tx || {}, rx = d.last_rx || {}, chk = d.self_check || {};
  const c  = d.counters || {};
  const verdict = chk.match === true
    ? '<span class="ok">les octets reçus par le TSOP sont identiques à ceux émis</span>'
    : chk.match === false
      ? '<span class="bad">écart entre la trame émise et la trame relue</span>'
      : 'aucune vérification depuis le démarrage';

  dev.r.diag.innerHTML = `
    <table>
      <tr><th>Dernière émission</th><td><code>${hex(tx.bytes) || '&mdash;'}</code><br><span class="hint">${ago(tx.at) || ''}</span></td></tr>
      <tr><th>Dernière réception</th><td><code>${hex(rx.bytes) || '&mdash;'}</code><br><span class="hint">${[rx.protocol, rx.bits ? rx.bits + ' bits' : '', rx.source === 'self' ? 'propre émission' : rx.source === 'remote' ? 'télécommande' : '', ago(rx.at)].filter(Boolean).join(' · ')}</span></td></tr>
      <tr><th>Checksum</th><td>${rx.checksum_valid === true ? '<span class="ok">valide</span>' : rx.checksum_valid === false ? '<span class="bad">invalide</span>' : '&mdash;'}</td></tr>
      <tr><th>Contrôle</th><td>${verdict}</td></tr>
      <tr><th>Compteurs</th><td>${c.tx || 0} émises · ${c.rx_valid || 0} valides · ${c.rx_rejected || 0} rejetées${c.lock_asserts ? ' · ' + c.lock_asserts + ' rappel(s) d\'arrêt' : ''}</td></tr>
    </table>`;
}

/* ============================== reglages =============================== */

function openSettings() {
  const box = $('#set-devices');
  box.innerHTML = '';
  cfg.devices.forEach((d, i) => {
    // Si le nom vient du boitier et non des reglages, le montrer quand meme :
    // sinon vider ce champ reviendrait a supprimer la ligne sans le vouloir.
    const dev = devices[i];
    const known = dev && dev.status && dev.status.display_name;
    addSettingRow(d.name || known || '', d.base);
  });
  $('#set-token').value = cfg.token || '';
  $('#settings').showModal();
}

function addSettingRow(name, base) {
  const row = document.createElement('div');
  row.className = 'set-row';
  row.innerHTML = `<input class="s-name" placeholder="Nom"><input class="s-base" placeholder="http://192.168.1.40 (vide = ce boîtier)"><button type="button" title="Retirer">&times;</button>`;
  $('.s-name', row).value = name || '';
  $('.s-base', row).value = base || '';
  $('button', row).addEventListener('click', () => row.remove());
  $('#set-devices').appendChild(row);
}

function applySettings() {
  const list = $$('.set-row', $('#set-devices')).map((row, i) => ({
    id:   'dev' + i,
    name: $('.s-name', row).value.trim(),
    base: $('.s-base', row).value.trim().replace(/\/+$/, '')
  })).filter(d => d.name);   // une ligne videe de son nom est une suppression
  cfg = normalizeCfg({ devices: list, token: $('#set-token').value });
  saveConfig();
  $('#settings').close();
  rebuild();
}

/* =============================== amorce ================================ */

function rebuild() {
  devices.forEach(d => clearTimeout(d.sendTimer));
  devices.length = 0;
  $('#devices').innerHTML = '';
  cfg.devices.forEach(buildDevice);
  pollAll();
}

function tickClock() {
  const d = new Date();
  $('#clock').textContent = String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0');
}

async function main() {
  cfg = await loadConfig();
  rebuild();

  $('#btn-settings').addEventListener('click', openSettings);
  $('#set-add').addEventListener('click', () => addSettingRow('', ''));
  $('#set-cancel').addEventListener('click', () => $('#settings').close());
  $('#set-save').addEventListener('click', applySettings);
  $('#set-reset').addEventListener('click', () => {
    localStorage.removeItem(CFG_KEY);
    location.reload();
  });

  tickClock();
  setInterval(tickClock, 10000);
  setInterval(pollAll, POLL_MS);
  document.addEventListener('visibilitychange', () => { if (!document.hidden) pollAll(); });
}

main();
