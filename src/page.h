// The editor page HandPoser serves at http://127.0.0.1:<port>/ (no outside resources).
#pragma once

static const char* kPageParts[] = {
R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Hand Poser</title>
<style>
:root { --bg:#16181d; --panel:#1f232b; --line:#2e3440; --text:#e6e9ef; --dim:#8b93a3; --acc:#5fb3ff; --ok:#6cd38b; --warn:#ffb454; }
* { box-sizing:border-box; }
body { margin:0; background:var(--bg); color:var(--text); font:14px/1.4 system-ui,Segoe UI,sans-serif; }
header { padding:12px 16px; border-bottom:1px solid var(--line); display:flex; gap:12px; align-items:baseline; flex-wrap:wrap; }
header h1 { font-size:18px; margin:0; }
header .hint { color:var(--dim); font-size:13px; }
#hands { display:grid; grid-template-columns:repeat(auto-fit,minmax(380px,1fr)); gap:16px; padding:16px; }
.hand { background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:14px; }
.hand h2 { margin:0 0 4px; font-size:16px; }
.item { color:var(--dim); font-size:13px; margin-bottom:10px; word-break:break-all; }
.empty { color:var(--dim); padding:18px 0; }
.tag { display:inline-block; font-size:12px; padding:1px 7px; border-radius:9px; margin-left:6px; }
.tag.saved { background:#1f3d2a; color:var(--ok); } .tag.unsaved { background:#3d311f; color:var(--warn); } .tag.default { background:#2a2f3a; color:var(--dim); }
fieldset { border:1px solid var(--line); border-radius:8px; margin:10px 0; padding:8px 10px; }
legend { color:var(--acc); font-size:13px; padding:0 4px; }
.row { display:grid; grid-template-columns:96px 1fr 28px 64px 28px; gap:6px; align-items:center; margin:3px 0; }
.row label { color:var(--dim); font-size:12px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.row input[type=range] { width:100%; }
.row input[type=number] { width:64px; background:#11141a; color:var(--text); border:1px solid var(--line); border-radius:5px; padding:3px 4px; font-size:12px; }
button { background:#2a3140; color:var(--text); border:1px solid var(--line); border-radius:6px; padding:5px 10px; cursor:pointer; font-size:13px; }
button:hover { border-color:var(--acc); }
.row button { padding:2px 0; font-size:12px; }
.bar { display:flex; gap:8px; flex-wrap:wrap; margin-top:10px; }
.bar .save { background:#1f4a31; } .bar .reset { background:#4a2a24; }
.bone h3 { font-size:12px; margin:8px 0 2px; color:var(--text); font-weight:600; }
.status { color:var(--dim); font-size:12px; margin-top:8px; min-height:16px; }
details summary { cursor:pointer; color:var(--acc); font-size:13px; margin:6px 0; }
.letgo { background:#3d311f; color:var(--warn); border-radius:8px; padding:8px 10px; margin:6px 0; font-size:13px; }
.letgo button { margin-left:6px; }
</style></head><body>
<header><h1>Hand Poser</h1><span class="hint">Hold a magazine in the game. Changes show on your hand in about a third of a second. Save keeps the pose for every magazine of that kind.</span><span id="conn" class="hint"></span></header>
<div id="hands"></div>
<script>
)HTML",
R"HTML(
const HANDS = ['left', 'right'];
let state = { hands: [] }, dragging = null, lastSend = {}, built = {};
const fmt = (v, d) => (Math.round(v * Math.pow(10, d)) / Math.pow(10, d)).toFixed(d);
function api(q) { return fetch('/api/cmd?' + q).catch(() => {}); }
function send(hand, f, v, extra) {
  const key = hand + f + (extra || '');
  const now = performance.now();
  lastSend[key] = { v, t: now };
  api('hand=' + hand + '&op=set&f=' + f + (extra || '') + '&v=' + v);
}
function row(hand, label, f, extra, val, min, max, step, digits) {
  const id = hand + '_' + f + (extra || '').replace(/[^a-z0-9]/gi, '_');
  return '<div class="row" data-id="' + id + '"><label title="' + label + '">' + label + '</label>' +
    '<input type="range" id="r_' + id + '" min="' + min + '" max="' + max + '" step="' + step + '" value="' + val + '">' +
    '<button data-step="-">&minus;</button><input type="number" id="n_' + id + '" step="' + step + '" value="' + fmt(val, digits) + '">' +
    '<button data-step="+">+</button></div>';
}
function wire(el, hand, f, extra, step, digits) {
  const id = hand + '_' + f + (extra || '').replace(/[^a-z0-9]/gi, '_');
  const r = el.querySelector('#r_' + id), n = el.querySelector('#n_' + id);
  const set = v => { r.value = v; n.value = fmt(+v, digits); send(hand, f, v, extra); };
  r.addEventListener('input', () => { dragging = id; set(r.value); });
  r.addEventListener('change', () => { dragging = null; });
  n.addEventListener('change', () => set(n.value));
  r.parentElement.querySelectorAll('button').forEach(b => b.addEventListener('click', () => {
    const v = (+n.value) + (b.dataset.step === '+' ? 1 : -1) * step; set(fmt(v, digits));
  }));
}
)HTML",
R"HTML(
function build(h) {
  const el = document.getElementById('hand_' + h.hand);
  h.open = {}; el.querySelectorAll('details[open]').forEach(d => h.open[d.dataset.finger] = true);
  const tag = h.saved ? (h.dirty ? '<span class="tag unsaved">changed</span>' : '<span class="tag saved">your pose</span>')
                      : (h.dirty ? '<span class="tag unsaved">changed</span>' : '<span class="tag default">default</span>');
  let html = '<h2>' + (h.hand === 'left' ? 'Left hand' : 'Right hand') + tag + '</h2>' +
    '<div class="item">' + h.label + ' &middot; ' + h.key + '</div>' +
    '<div class="letgo" id="held_' + h.hand + '"' + (h.held ? ' hidden' : '') + '>You let go of it. Any change puts it back in your hand. ' +
    '<button data-op="take">Put it back in my hand</button></div>' +
    '<fieldset><legend>Where the hand sits on it (cm along the magazine, degrees)</legend>' +
    row(h.hand, 'front / back', 'loc', '&i=0', h.loc[0], h.loc[0] - 10, h.loc[0] + 10, 0.05, 2) +
    row(h.hand, 'side to side', 'loc', '&i=1', h.loc[1], h.loc[1] - 10, h.loc[1] + 10, 0.05, 2) +
    row(h.hand, 'up / down', 'loc', '&i=2', h.loc[2], h.loc[2] - 10, h.loc[2] + 10, 0.05, 2) +
    row(h.hand, 'tilt (pitch)', 'rot', '&i=0', h.rot[0], -180, 180, 0.5, 1) +
    row(h.hand, 'turn (yaw)', 'rot', '&i=1', h.rot[1], -180, 180, 0.5, 1) +
    row(h.hand, 'roll', 'rot', '&i=2', h.rot[2], -180, 180, 0.5, 1) + '</fieldset>' +
    '<fieldset><legend>Fingers</legend><div class="bar"><button data-curl="0.9">Open all 10%</button><button data-curl="1.1">Close all 10%</button></div>';
  const fingers = {}, order = ['thumb', 'index', 'middle', 'ring', 'pinky'];
  const joint = { '01': 'base knuckle', '02': 'middle joint', '03': 'tip joint' };
  h.bones.forEach(b => { const f = b.name.split('_')[0]; (fingers[f] = fingers[f] || []).push(b); });
  Object.keys(fingers).sort((a, b) => order.indexOf(a) - order.indexOf(b)).forEach(f => {
    fingers[f].sort((a, b) => a.name.localeCompare(b.name));
    html += '<details' + (h.open && h.open[f] ? ' open' : '') + ' data-finger="' + f + '"><summary>' + f[0].toUpperCase() + f.slice(1) + '</summary>';
    fingers[f].forEach(b => {
      const j = joint[b.name.split('_')[1]] || b.name;
      html += '<div class="bone"><h3>' + f[0].toUpperCase() + f.slice(1) + ' &middot; ' + j + ' <span style="color:var(--dim);font-weight:400">(' + b.name + ')</span></h3>' +
        row(h.hand, 'bend', 'bone', '&b=' + b.name + '&i=1', b.rot[1], -120, 120, 0.5, 1) +
        row(h.hand, 'side to side', 'bone', '&b=' + b.name + '&i=0', b.rot[0], -120, 120, 0.5, 1) +
        row(h.hand, 'twist', 'bone', '&b=' + b.name + '&i=2', b.rot[2], -120, 120, 0.5, 1) + '</div>';
    });
    html += '</details>';
  });
  html += '</fieldset><div class="bar"><button class="save" data-op="save">Save for every ' + h.label + '</button>' +
    '<button data-op="revert">Undo changes</button><button class="reset" data-op="reset">Back to the default</button></div>' +
    '<div class="status" id="st_' + h.hand + '"></div>';
  el.innerHTML = html;
  [['loc', 0.05, 2], ['rot', 0.5, 1]].forEach(([f, s, d]) => [0, 1, 2].forEach(i => wire(el, h.hand, f, '&i=' + i, s, d)));
  h.bones.forEach(b => [1, 0, 2].forEach(i => wire(el, h.hand, 'bone', '&b=' + b.name + '&i=' + i, 0.5, 1)));
  el.querySelectorAll('[data-op]').forEach(b => b.addEventListener('click', () => {
    if (b.dataset.op === 'reset' && !confirm('Forget your pose for ' + h.label + ' and go back to the default?')) return;
    api('hand=' + h.hand + '&op=' + b.dataset.op).then(() => { document.getElementById('st_' + h.hand).textContent = b.textContent + ': done'; });
  }));
  el.querySelectorAll('[data-curl]').forEach(b => b.addEventListener('click', () => api('hand=' + h.hand + '&op=curl&v=' + b.dataset.curl)));
  el.querySelectorAll('details').forEach(d => d.addEventListener('toggle', () => { (h.open = h.open || {})[d.dataset.finger] = d.open; }));
  built[h.hand] = h.sig;
}
)HTML",
R"HTML(
function refresh(h) {  // values only, keeping what the user is dragging or typing
  const el = document.getElementById('hand_' + h.hand);
  const put = (f, extra, v, digits) => {
    const id = h.hand + '_' + f + extra.replace(/[^a-z0-9]/gi, '_');
    const r = el.querySelector('#r_' + id), n = el.querySelector('#n_' + id);
    if (!r || dragging === id || document.activeElement === n) return;
    const sent = lastSend[h.hand + f + extra]; if (sent && performance.now() - sent.t < 800) return;
    r.value = v; n.value = fmt(v, digits);
  };
  [0, 1, 2].forEach(i => { put('loc', '&i=' + i, h.loc[i], 2); put('rot', '&i=' + i, h.rot[i], 1); });
  h.bones.forEach(b => [0, 1, 2].forEach(i => put('bone', '&b=' + b.name + '&i=' + i, b.rot[i], 1)));
  const letgo = el.querySelector('#held_' + h.hand);
  if (letgo) letgo.hidden = !!h.held;
  const tag = el.querySelector('.tag');
  if (tag) { tag.className = 'tag ' + (h.dirty ? 'unsaved' : h.saved ? 'saved' : 'default'); tag.textContent = h.dirty ? 'changed' : h.saved ? 'your pose' : 'default'; }
}
function render() {
  const root = document.getElementById('hands');
  HANDS.forEach(hand => {
    let el = document.getElementById('hand_' + hand);
    if (!el) { el = document.createElement('div'); el.className = 'hand'; el.id = 'hand_' + hand; root.appendChild(el); }
    const h = state.hands.find(x => x.hand === hand);
    if (!h) { if (built[hand] !== 'none') { el.innerHTML = '<h2>' + (hand === 'left' ? 'Left hand' : 'Right hand') + '</h2><div class="empty">Not holding a magazine.</div>'; built[hand] = 'none'; } return; }
    if (built[hand] !== h.sig) build(h); else refresh(h);
  });
}
async function poll() {
  try {
    const r = await fetch('/api/state'); state = await r.json();
    document.getElementById('conn').textContent = '';
    render();
  } catch (e) { document.getElementById('conn').textContent = 'The game is not answering (is it running with HandPoser?)'; }
  setTimeout(poll, 400);
}
poll();
</script></body></html>
)HTML",
};
