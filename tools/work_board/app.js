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
// Checks run_all makes after the games (widescreen frame comparisons): their FAIL lines in the summary.
const checkFailures = b => String(b.summary || '').split(/\r?\n/).filter(l => /: FAIL$/.test(l.trim()));
// Failed games and failed checks of a batch, as a list.
function failureList(b) {
  return `<ul class="failures">${b.failures.map(g => `<li>${esc(g.scenario)} (${esc(g.install)}): ${esc(g.result || '')}${g.failures[0] ? ' - ' + esc(g.failures.slice(0, 3).join(' | ')) : ''}</li>`).join('')}` +
    checkFailures(b).map(l => `<li>${esc(l.trim())}</li>`).join('') + '</ul>';
}
const failCount = b => b.failed + checkFailures(b).length;
function batchSummary(b, withId = true) {
  const parts = withId ? [`#${b.id}`, esc(b.state)] : [esc(b.state)];
  if (b.total) parts.push(`${b.done}/${b.total} games`);
  if (b.running) parts.push(`${b.running} running`);
  if (b.queued && b.state !== 'queued') parts.push(`${b.queued} queued`);
  const time = b.state === 'running' ? 'for ' + fromFetch(b.elapsed || 0)
    : b.state === 'queued' ? 'waiting ' + fromFetch(b.queued_for || 0) : b.finished ? dur(b.elapsed) + ', ' + ageOf(b.finished) + ' ago' : '';
  if (time) parts.push(time);
  const checks = checkFailures(b);
  const bad = b.failed ? ` <span class="bad">${b.failed} failed</span>`
    : checks.length ? ` <span class="bad">${checks.length} check${checks.length > 1 ? 's' : ''} failed after the games</span>`
    : b.exit_code ? ` <span class="bad">exit ${b.exit_code}</span>`
    : b.state === 'finished' && b.total ? ' <span class="ok">all passed</span>' : '';
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
// A note or commit line is cut to fit; clicking it shows all of it (kept open across redraws).
function actLine(cls, key, what, when, title) {
  return `<div class="act ${cls}${open.has(key) ? ' expanded' : ''}" data-act="${esc(key)}" title="${esc(title)}"><span class="what">${what}</span><span class="when">${when}</span></div>`;
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
  if (n) act.push(actLine('note', `n-${t.id}-${n.time}`, esc(n.text), ageOf(n.time), 'Click to show it all'));
  if (a && c) {
    const merge = /^Merge /.test(c.subject || '');
    act.push(actLine('commit' + (merge ? ' merge' : ''), `c-${t.id}-${c.hash}`, `<span class="hash">${esc(c.hash)}</span>${esc(c.subject)}`, ageOf(c.time), c.subject));
  }
  if (b) act.push(`<div class="act tests">${progress(b)}<span class="txt">${batchSummary(b)}</span></div>`);
  // While a new batch waits or runs, the previous result stays in view.
  const prev = t.previous;
  if (prev) act.push(`<div class="act tests prev"><span class="txt">previous ${batchSummary(prev)}</span></div>`);
  const more = t.notes.slice(1);
  const moreNotes = more.length ? details('n-' + t.id, `${more.length} earlier note${more.length > 1 ? 's' : ''}`,
    more.map(m => actLine('note old', `n-${t.id}-${m.time}`, esc(m.text), ageOf(m.time), 'Click to show it all')).join('')) : '';
  const shown = [b, prev].find(x => x && failCount(x));
  const fails = shown ? details(`f-${t.id}-${shown.id}`, `<span class="bad">what failed in #${shown.id}</span>`, failureList(shown)) : '';
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

// Queued games show only what they are expected to take; running ones count up against it.
function gameRow(g) {
  const muted = t => `<span class="muted">${t}</span>`;
  const exp = g.expected ? dur(g.expected) : '';
  let slot = g.slot ?? '', time = '';
  if (g.state === 'queued') {
    slot = muted('–');
    time = exp ? muted('about ' + exp) : '';
  } else if (g.state === 'running') {
    const live = g.expected ? ` data-t="${Math.round(fetchedAt - (g.elapsed || 0) * 1000)}" data-exp="${g.expected}"` : '';
    const pct = g.expected ? Math.min(100, 100 * (g.elapsed || 0) / g.expected) : 0;
    time = (g.expected ? `<span class="gamebar"><i${live} style="width:${pct.toFixed(0)}%"></i></span>` : '') +
      `<span class="mono">${fromFetch(g.elapsed || 0)}</span>` + (exp ? muted(' of ' + exp) : '');
  } else if (g.elapsed != null) {
    const over = g.expected && g.elapsed > 1.5 * g.expected;
    time = `<span class="mono${over ? ' bad' : ''}">${dur(g.elapsed)}</span>` + (over ? muted(' expected ' + exp) : '');
  }
  const result = g.state === 'finished' ? `<span class="${/^PASS/.test(g.result || '') ? 'ok' : 'bad'}">${esc(g.result || '')}</span>`
    : g.state === 'running' ? `<span class="run">running</span>` : muted(esc(g.state));
  const alone = g.alone ? ' <span class="pill" title="Sets the display mode, so it runs with no other game">alone</span>' : '';
  return `<tr class="g-${g.state}"><td>${esc(g.scenario)}${alone}${g.attempt ? ` <span class="pill">retry ${g.attempt}</span>` : ''}</td><td>${esc(g.install)}</td>` +
    `<td class="num">${slot}</td><td>${time}</td><td>${result}</td></tr>` +
    (g.failures.length ? `<tr><td colspan="5"><ul class="failures">${g.failures.slice(0, 6).map(f => `<li>${esc(f)}</li>`).join('')}</ul></td></tr>` : '');
}
function gamesTable(b, now) {
  const order = { running: 0, queued: 1, finished: 2, cancelled: 3 };
  const games = [...b.games].sort((x, y) => order[x.state] - order[y.state]);
  return `<div class="scroll"><table class="games"><colgroup><col style="width:34%"><col style="width:16%"><col style="width:7%"><col style="width:28%"><col style="width:15%"></colgroup><thead><tr><th>Scenario</th><th>Install</th><th class="num">Slot</th><th>Time</th><th>State</th></tr></thead><tbody>` +
    games.map(g => gameRow(g)).join('') + `</tbody></table></div>`;
}
// What a batch is for: its ticket's title, or the worktree's folder for a batch with no ticket (main).
function batchWhat(b) {
  const t = data.tickets.find(t => t.id === b.ticket);
  return `<span class="bwhat" title="${esc(b.worktree)}">${esc(t ? t.title : String(b.worktree || '').split(/[\\/]/).pop())}</span>`;
}
function batchPanel(b, pos) {
  // Collapsed by default: the head and progress bar show; a click opens the arguments and the games.
  const now = sinceFetch(), key = 'b-' + b.id;
  return `<details class="panel batch" data-key="${key}"${open.has(key) ? ' open' : ''}><summary><div class="head"><span class="id">#${b.id}</span><span>${esc(b.ticket || '-')}</span>` +
    `${batchWhat(b)}${pos ? `<span class="pill">position ${pos}</span>` : ''}` +
    `<span class="sub" style="margin-left:auto">${batchSummary(b, false)}</span></div>${progress(b)}</summary><div class="body">` +
    (b.args.length ? `<div class="mono" style="font-size:11.5px;color:var(--muted)">${esc(b.args.join(' '))}</div>` : '') +
    // The submitter's EI_* variables the games ran with (EI_PROFILE=1, EI_PLAIN=1, ...).
    (Object.keys(b.env || {}).length ? `<div class="mono" style="font-size:11.5px;color:var(--muted)" title="Environment of this batch's games">env: ${esc(Object.entries(b.env).map(([k, v]) => k + '=' + v).join(' '))}</div>` : '') +
    (b.games.length ? gamesTable(b, now) : '<p class="empty">No games yet.</p>') + `</div></details>`;
}
function runsView() {
  const r = data.runs, p = data.processes;
  const out = [];
  // Not alive means no heartbeat for a while: either it exited (the next work.py test starts one) or it is stuck.
  const hb = p.scheduler.heartbeat_age;
  if (!p.scheduler.alive && (r.queued.length || r.running.length)) out.push(hb != null && hb < 600
    ? `<p class="empty bad">The scheduler has not checked in for ${fromFetch(hb)}; queued batches wait until it does (<code>build/live/scheduler.log</code>).</p>`
    : `<p class="empty bad">The scheduler is not running, so queued batches wait; it starts with the next <code>work.py test</code>.</p>`);
  out.push(`<section class="lane"><h2>Running<span class="n">${r.running.length}</span></h2>` +
    (r.running.length ? r.running.map(b => batchPanel(b, 0)).join('') : '<p class="empty">No batch is running.</p>') + '</section>');
  out.push(`<section class="lane"><h2>Queue<span class="n">${r.queued.length}</span></h2>` +
    (r.queued.length ? r.queued.map((b, i) => batchPanel(b, i + 1)).join('') : '<p class="empty">Queue empty.</p>') + '</section>');
  const rows = r.history.map(b => {
    const key = 'h-' + b.id;
    const n = failCount(b), checks = checkFailures(b).length;
    const fails = n ? details(key, `<span class="bad">what failed</span>`, failureList(b)) : '';
    const games = `<span class="${b.failed ? 'bad' : 'ok'}">${b.passed}/${b.total} games passed</span>`;
    const result = b.state === 'cancelled' ? 'cancelled'
      : games + (checks ? `, <span class="bad">${checks} check${checks > 1 ? 's' : ''} failed</span>` : '');
    return `<tr><td class="num">#${b.id}</td><td>${esc(b.ticket || '-')}</td><td>${batchWhat(b)}</td><td class="num">${ageOf(b.finished)}</td>` +
      `<td class="num">${dur(b.elapsed)}</td><td>${result}${b.exit_code && !n ? ` <span class="pill down">exit ${b.exit_code}</span>` : ''}` +
      (Object.keys(b.env || {}).length ? `<div class="mono" style="font-size:11.5px;color:var(--muted)">env: ${esc(Object.entries(b.env).map(([k, v]) => k + '=' + v).join(' '))}</div>` : '') +
      `</td><td>${fails}</td></tr>`;
  }).join('');
  const bad = r.history.filter(b => b.state !== 'cancelled' && (failCount(b) || b.exit_code)).length;
  out.push(`<section class="lane"><h2>History<span class="n">${r.history.length}</span></h2>` + (rows ?
    `<details class="panel batch" data-key="history"${open.has('history') ? ' open' : ''}><summary><div class="head">` +
    `<span>${r.history.length} finished batch${r.history.length > 1 ? 'es' : ''}</span>${bad ? `<span class="bad">${bad} with failures</span>` : ''}</div></summary>` +
    `<div class="body scroll"><table><thead><tr><th class="num">Batch</th><th>Ticket</th><th>What</th><th class="num">Ago</th><th class="num">Took</th><th>Result</th><th></th></tr></thead><tbody>${rows}</tbody></table></div></details>` :
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
  const act = e.target.closest('.act[data-act]');
  if (act && !getSelection().toString()) {
    const k = act.dataset.act;
    open.has(k) ? open.delete(k) : open.add(k);
    act.classList.toggle('expanded', open.has(k));
  }
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
