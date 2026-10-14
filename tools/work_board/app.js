// The work board page: polls /api/status and draws the tickets view and the test-runs view.
const POLL_MS = 3000;
const ACTIVE_STATES = ['running', 'review'];
const state = { view: 'tickets', q: '' };
try { const v = location.hash.slice(1); if (v === 'runs' || v === 'tickets') state.view = v; } catch (e) {}
const open = new Set();            // keys of <details> the viewer opened, kept across redraws
let data = null, fetchedAt = Date.now();

const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const base = p => String(p || '').split('/').filter(Boolean).pop() || '';
// Elapsed times are spans the page counts up every second between polls (data-t: start, ms since epoch).
const tickText = t => dur(Math.max(0, (Date.now() - t) / 1000));
const tick = t => `<span class="tick" data-t="${Math.round(t)}">${tickText(t)}</span>`;
const fromFetch = secs => tick(fetchedAt - secs * 1000);     // an age the server measured at fetch time
function ageOf(stamp) {
  const t = Date.parse(stamp); if (!stamp || isNaN(t)) return '';
  return tick(t);
}
function dur(s) {
  if (s == null) return '';
  s = Math.round(s);
  if (s >= 86400) return Math.round(s / 86400) + 'd';
  if (s >= 3600) return Math.floor(s / 3600) + 'h' + String(Math.floor(s % 3600 / 60)).padStart(2, '0') + 'm';
  if (s >= 60) return Math.floor(s / 60) + 'm' + String(s % 60).padStart(2, '0') + 's';
  return s + 's';
}
const sinceFetch = () => (Date.now() - fetchedAt) / 1000;
const details = (key, summary, body) => `<details data-key="${esc(key)}"${open.has(key) ? ' open' : ''}><summary>${summary}</summary>${body}</details>`;

function progress(b) {
  if (!b || !b.total) return '';
  const w = n => (100 * n / b.total).toFixed(1) + '%';
  return `<div class="progress" title="${b.passed} passed, ${b.failed} failed, ${b.running} running, ${b.queued} queued">` +
    `<i class="p" style="width:${w(b.passed)}"></i><i class="f" style="width:${w(b.failed)}"></i><i class="r" style="width:${w(b.running)}"></i></div>`;
}
function batchSummary(b) {
  const parts = [`#${b.id}`, esc(b.state)];
  if (b.total) parts.push(`${b.done}/${b.total} games`);
  if (b.running) parts.push(`${b.running} running`);
  if (b.queued && b.state !== 'queued') parts.push(`${b.queued} queued`);
  const time = b.state === 'running' ? 'for ' + fromFetch(b.elapsed || 0)
    : b.state === 'queued' ? 'waiting ' + fromFetch(b.queued_for || 0) : b.finished ? dur(b.elapsed) + ', ' + ageOf(b.finished) + ' ago' : '';
  if (time) parts.push(time);
  const bad = b.failed ? ` <span class="bad">${b.failed} failed</span>` : b.state === 'finished' && b.total ? ' <span class="ok">all passed</span>' : '';
  return parts.join(' · ') + bad;
}

// ---- tickets view

const secsSince = stamp => { const t = Date.parse(stamp); return !stamp || isNaN(t) ? null : Math.max(0, (Date.now() - t) / 1000); };
// agent-a58ce77c2281ac40c -> a58ce77; branches named after the worktree are not worth showing.
const shortAgent = a => String(a || '?').replace(/^agent-/, '').slice(0, 7);
const defaultBranch = a => a.branch === 'worktree-' + a.agent || a.branch === 'worktree-' + base(a.worktree);
function freshness(t) {
  const a = t.assignment, n = t.notes[0], c = t.commit, b = t.batch;
  if (b && b.state === 'running') return { cls: 'live', html: 'testing now' };
  // Activity since the assignment began: notes, commits on the branch, test batches.
  const since = a ? secsSince(a.started) : null;
  const stamps = [n && n.time, c && c.time, b && (b.finished || b.started)].map(secsSince)
    .filter(x => x != null && (since == null || x <= since + 60));
  if (!stamps.length) {
    if (since == null) return { cls: 'quiet', html: '' };
    return { cls: since < 3600 ? 'slow' : 'quiet', html: 'nothing yet' };
  }
  const quiet = Math.min(...stamps);
  return { cls: quiet < 1200 ? 'live' : quiet < 3600 ? 'slow' : 'quiet', html: 'active ' + tick(Date.now() - quiet * 1000) + ' ago' };
}
function ticketCard(t) {
  const a = t.assignment, c = t.commit, n = t.notes[0], b = t.batch;
  const f = freshness(t);
  const sub = [];
  if (a) {
    sub.push(`<span class="who" title="${esc(a.agent)}\n${esc(a.worktree)}\n${esc(a.branch || '')}">${esc(shortAgent(a.agent))}${a.open ? '' : ' (ended)'}</span>`);
    sub.push(`on it ${ageOf(a.started)}`);
    if (a.branch && !defaultBranch(a)) sub.push(`<span class="mono">${esc(a.branch)}</span>`);
  }
  if (t.group_name) sub.push(esc(t.group_name));
  const act = [];
  if (n) act.push(`<div class="act note"><span class="what">${esc(n.text)}</span><span class="when">${ageOf(n.time)}</span></div>`);
  if (a && c) {
    const merge = /^Merge /.test(c.subject || '');
    act.push(`<div class="act commit${merge ? ' merge' : ''}" title="${esc(c.subject)}"><span class="what"><span class="hash">${esc(c.hash)}</span>${esc(c.subject)}</span><span class="when">${ageOf(c.time)}</span></div>`);
  }
  if (b) act.push(`<div class="act tests">${progress(b)}<span class="txt">${batchSummary(b)}</span></div>`);
  const more = t.notes.slice(1);
  const moreNotes = more.length ? details('n-' + t.id, `${more.length} earlier note${more.length > 1 ? 's' : ''}`,
    more.map(m => `<div class="act note old"><span class="what">${esc(m.text)}</span><span class="when">${ageOf(m.time)}</span></div>`).join('')) : '';
  const fails = b && b.failed ? details('f-' + t.id, `<span class="bad">${b.failed} failed game${b.failed > 1 ? 's' : ''}</span>`,
    `<ul class="failures">${b.failures.map(g => `<li>${esc(g.scenario)} (${esc(g.install)}): ${esc(g.result || '')}${g.failures[0] ? ' - ' + esc(g.failures[0]) : ''}</li>`).join('')}</ul>`) : '';
  return `<article class="card s-${t.state} f-${f.cls}">
    <div class="top"><span class="id">${esc(t.id)}</span><span class="title">${esc(t.title)}</span></div>
    <div class="tags"><span class="pill st">${esc(t.state)}</span>${t.model ? `<span class="pill">${esc(t.model)}</span>` : ''}<span class="fresh">${f.html}</span></div>
    ${sub.length ? `<div class="subline">${sub.join('<span class="dot">·</span>')}</div>` : ''}
    ${act.length ? `<div class="acts">${act.join('')}</div>` : ''}
    ${moreNotes}${fails}</article>`;
}

function ticketsView() {
  const q = state.q.trim().toLowerCase();
  const match = t => !q || [t.id, t.title, t.group_name, t.assignment && t.assignment.agent, t.assignment && t.assignment.branch].join(' ').toLowerCase().includes(q);
  const all = data.tickets.filter(match);
  const working = all.filter(t => t.assignment && t.assignment.open);
  const lost = all.filter(t => !(t.assignment && t.assignment.open) && ACTIVE_STATES.includes(t.state));
  const sections = [];
  const lane = (title, hint, list, cardFn) => `<section class="lane"><h2>${title}<span class="n">${list.length}</span></h2>` +
    (hint ? `<p class="hint">${hint}</p>` : '') + (list.length ? `<div class="cards">${list.map(cardFn).join('')}</div>` : '<p class="empty">Nothing here.</p>') + '</section>';
  working.sort((a, b) => (b.assignment.started || '').localeCompare(a.assignment.started || ''));
  sections.push(lane('Active work', 'Tickets with an open assignment (work.py start), newest first.', working, ticketCard));
  if (lost.length) sections.push(lane('Running or in review, nobody assigned', 'The toml says a worker has it, work.py has no open assignment.', lost, ticketCard));
  const others = all.filter(t => !working.includes(t) && !lost.includes(t) && t.state !== 'done');
  const chip = t => `<button class="chip s-${t.state}" title="${esc(t.title)} (${t.state})">${esc(t.id)}</button>`;
  const order = ['ready', 'waiting', 'watch', 'later'];
  for (const s of order) {
    const list = others.filter(t => t.state === s);
    if (list.length) sections.push(`<section class="lane"><h2>${s[0].toUpperCase() + s.slice(1)}<span class="n">${list.length}</span></h2><div class="chips">${list.map(chip).join('')}</div></section>`);
  }
  return `<div class="filters"><input id="q" type="search" value="${esc(state.q)}" placeholder="Filter by id, title, agent or branch" aria-label="Filter tickets"></div>` + sections.join('');
}

// ---- test runs view

function gameRow(g, now) {
  const elapsed = g.state === 'running' ? (g.elapsed || 0) + now : g.elapsed;
  let bar = '';
  if (g.expected && elapsed != null) {
    const pct = Math.min(100, 100 * elapsed / g.expected);
    const live = g.state === 'running' ? ` data-t="${Math.round(fetchedAt - (g.elapsed || 0) * 1000)}" data-exp="${g.expected}"` : '';
    bar = `<span class="gamebar"><i class="${elapsed > g.expected ? 'over' : ''}"${live} style="width:${pct.toFixed(0)}%"></i></span>`;
  }
  const result = g.state === 'finished' ? `<span class="${/^PASS/.test(g.result || '') ? 'ok' : 'bad'}">${esc(g.result || '')}</span>` : esc(g.state);
  return `<tr><td>${esc(g.scenario)}${g.alone ? ' <span class="pill">alone</span>' : ''}${g.attempt ? ` <span class="pill">retry ${g.attempt}</span>` : ''}</td><td>${esc(g.install)}</td>` +
    `<td class="num">${g.slot ?? ''}</td><td>${bar}<span class="mono">${g.state === 'running' ? fromFetch(g.elapsed || 0) : elapsed != null ? dur(elapsed) : ''}${g.expected ? ' / ' + dur(g.expected) : ''}</span></td><td>${result}</td></tr>` +
    (g.failures.length ? `<tr><td colspan="5"><ul class="failures">${g.failures.slice(0, 6).map(f => `<li>${esc(f)}</li>`).join('')}</ul></td></tr>` : '');
}
function gamesTable(b, now) {
  const order = { running: 0, queued: 1, finished: 2, cancelled: 3 };
  const games = [...b.games].sort((x, y) => order[x.state] - order[y.state]);
  return `<div class="scroll"><table><thead><tr><th>Scenario</th><th>Install</th><th class="num">Slot</th><th>Elapsed / expected</th><th>State</th></tr></thead><tbody>` +
    games.map(g => gameRow(g, now)).join('') + `</tbody></table></div>`;
}
function batchPanel(b, pos) {
  const now = sinceFetch();
  return `<section class="panel"><div class="head"><span class="id">#${b.id}</span><span>${esc(b.ticket || '-')}</span>` +
    `<span class="mono" style="color:var(--muted)">${esc(b.worktree)}</span>${pos ? `<span class="pill">position ${pos}</span>` : ''}` +
    `<span class="sub" style="margin-left:auto">${batchSummary(b)}</span></div>${progress(b)}` +
    (b.args.length ? `<div class="mono" style="font-size:11.5px;color:var(--muted)">${esc(b.args.join(' '))}</div>` : '') +
    (b.games.length ? gamesTable(b, now) : '') + `</section>`;
}
function runsView() {
  const r = data.runs, p = data.processes;
  const out = [];
  if (!p.scheduler.alive) out.push(`<p class="empty bad">The scheduler is not running${r.queued.length ? ', so queued batches wait' : ''}; it starts with the next <code>work.py test</code>.</p>`);
  out.push(`<section class="lane"><h2>Running<span class="n">${r.running.length}</span></h2>` +
    (r.running.length ? r.running.map(b => batchPanel(b, 0)).join('') : '<p class="empty">No batch is running.</p>') + '</section>');
  out.push(`<section class="lane"><h2>Queue<span class="n">${r.queued.length}</span></h2>` +
    (r.queued.length ? r.queued.map((b, i) => batchPanel(b, i + 1)).join('') : '<p class="empty">Queue empty.</p>') + '</section>');
  const rows = r.history.map(b => {
    const key = 'h-' + b.id;
    const fails = b.failures.length ? details(key, `<span class="bad">${b.failed} failed</span>`,
      `<ul class="failures">${b.failures.map(g => `<li>${esc(g.scenario)} (${esc(g.install)}) ${esc(g.result || '')}${g.failures.length ? ': ' + esc(g.failures.slice(0, 3).join(' | ')) : ''}</li>`).join('')}</ul>`) : '';
    const result = b.state === 'cancelled' ? 'cancelled' : `<span class="${b.failed || (b.exit_code ?? 0) ? 'bad' : 'ok'}">${b.passed}/${b.total} passed</span>`;
    return `<tr><td class="num">#${b.id}</td><td>${esc(b.ticket || '-')}</td><td class="mono">${esc(b.worktree)}</td><td class="num">${ageOf(b.finished)}</td>` +
      `<td class="num">${dur(b.elapsed)}</td><td>${result}${b.exit_code ? ` <span class="pill down">exit ${b.exit_code}</span>` : ''}</td><td>${fails}</td></tr>`;
  }).join('');
  out.push(`<section class="lane"><h2>History<span class="n">${r.history.length}</span></h2>` + (rows ?
    `<div class="panel scroll"><table><thead><tr><th class="num">Batch</th><th>Ticket</th><th>Worktree</th><th class="num">Ago</th><th class="num">Took</th><th>Result</th><th></th></tr></thead><tbody>${rows}</tbody></table></div>` :
    '<p class="empty">No finished batches yet.</p>') + '</section>');
  return out.join('');
}

// ---- page

function procs() {
  // The scheduler starts with the first work.py test and exits when idle, so not running is normal.
  return Object.entries(data.processes).map(([name, p]) => p.alive
    ? `<span class="pill up">${name} up</span>`
    : name === 'scheduler' ? `<span class="pill">scheduler idle</span>`
    : `<span class="pill down">${name} ${p.heartbeat_age == null ? 'never seen' : 'down ' + fromFetch(p.heartbeat_age)}</span>`).join('');
}
function render() {
  if (!data) return;
  document.querySelectorAll('.tab').forEach(b => b.setAttribute('aria-pressed', b.dataset.view === state.view));
  document.getElementById('procs').innerHTML = procs();
  document.getElementById('asof').textContent = 'Updated ' + (data.time || '').slice(11, 19) + '.';
  const el = document.getElementById('view');
  const focus = document.activeElement && document.activeElement.id === 'q';
  const scroll = window.scrollY;
  el.innerHTML = state.view === 'tickets' ? ticketsView() : runsView();
  if (focus) { const q = document.getElementById('q'); q.focus(); q.setSelectionRange(q.value.length, q.value.length); }
  window.scrollTo(0, scroll);
}
async function poll() {
  try {
    const r = await fetch('/api/status', { cache: 'no-store' });
    if (!r.ok) throw new Error(r.status + ' ' + (await r.text()).slice(0, 200));
    data = await r.json(); fetchedAt = Date.now();
    document.getElementById('error').textContent = '';
    render();
  } catch (e) { document.getElementById('error').textContent = 'Could not load /api/status: ' + e; }
}
document.addEventListener('click', e => {
  const tab = e.target.closest('.tab');
  if (tab) { state.view = tab.dataset.view; location.hash = state.view; render(); }
});
document.addEventListener('input', e => { if (e.target.id === 'q') { state.q = e.target.value; render(); } });
document.addEventListener('toggle', e => {
  const k = e.target.dataset && e.target.dataset.key;
  if (k) e.target.open ? open.add(k) : open.delete(k);
}, true);
window.addEventListener('hashchange', () => { const v = location.hash.slice(1); if (v === 'runs' || v === 'tickets') { state.view = v; render(); } });
function tickAll() {
  const now = Date.now();
  for (const el of document.querySelectorAll('.tick')) el.textContent = tickText(+el.dataset.t);
  for (const el of document.querySelectorAll('.gamebar i[data-exp]')) {
    const secs = (now - +el.dataset.t) / 1000, exp = +el.dataset.exp;
    el.style.width = Math.min(100, 100 * secs / exp).toFixed(0) + '%';
    el.classList.toggle('over', secs > exp);
  }
}
poll(); setInterval(poll, POLL_MS); setInterval(tickAll, 1000);
