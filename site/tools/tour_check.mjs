#!/usr/bin/env node
// How it works: the chapter selector against the chapters, in headless
// Chrome at several window sizes. Exactly one entry lit at all times; it
// follows scrolling in reading order, clicks and links, and survives the ST/STE
// switch.  node tools/tour_check.mjs  (serves site/ itself; needs Chrome)
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = process.env.SITE_ROOT || join(dirname(fileURLToPath(import.meta.url)), '..');
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PORT = 8791, CDP = 9352;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '127.0.0.1'], { cwd: ROOT, stdio: 'ignore' });
const profile = mkdtempSync(join(tmpdir(), 'tour-'));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP}`, `--user-data-dir=${profile}`, 'about:blank'], { stdio: 'ignore' });
let failures = 0;
const check = (ok, text) => { if (!ok) failures++; if (!ok || process.env.VERBOSE) console.log(`${ok ? 'PASS' : 'FAIL'} ${text}`); };
try {
  let target;
  for (let i = 0; i < 50 && !target; i++) { try { target = await (await fetch(`http://127.0.0.1:${CDP}/json/new?about:blank`, { method: 'PUT' })).json(); } catch { await sleep(200); } }
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 1; const pend = new Map(); const errors = [];
  ws.onmessage = (e) => { const m = JSON.parse(e.data); if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); } else if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails.exception?.description); };
  const send = (method, params = {}) => new Promise((r) => { const i = id++; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  const ev = async (x) => (await send('Runtime.evaluate', { expression: x, returnByValue: true, awaitPromise: true })).result.result.value;
  await send('Runtime.enable'); await send('Page.enable');
  const state = () => ev(`(() => { const on = [...document.querySelectorAll('#tourMap .map-seg.on')].map(l => l.dataset.chapter); const ids = [...document.querySelectorAll('.chapter')].map(c => c.id); return { on, ids, cur: [...document.querySelectorAll('.chapter.current')].map(c => c.id) }; })()`);

  for (const [w, h] of [[1280, 900], [1280, 650], [390, 800]]) {
    await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: w < 500 });
    await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/index.html#/how` });
    await sleep(1500);
    const { ids } = await state();
    check(ids.length === 14, `${w}x${h}: 14 chapters (${ids.length})`);
    // Scrolling down as a reader: one lit, in order, the chapter at the line.
    let last = -1, steps = 0, order = true, single = true, right = true;
    const maxY = await ev('document.documentElement.scrollHeight - innerHeight');
    for (let y = 0; y <= maxY + 200; y += Math.round(h / 4)) {
      await ev(`window.dispatchEvent(new WheelEvent('wheel')); window.scrollTo(0, ${y});`);
      await sleep(120);
      const s = await state();
      const expect = await ev(`(() => { const all = [...document.querySelectorAll('.chapter')]; if (innerHeight + scrollY >= document.documentElement.scrollHeight - 4) return all[all.length-1].id; let c = all[0]; for (const a of all) if (a.getBoundingClientRect().top <= innerHeight/3) c = a; return c.id; })()`);
      if (s.on.length !== 1 || s.cur.length !== 1) single = false;
      const idx = ids.indexOf(s.on[0]);
      if (idx < last) order = false;
      if (s.on[0] !== expect) right = false;
      last = idx; steps++;
    }
    check(single, `${w}x${h}: scrolling, exactly one lit at each of ${steps} positions`);
    check(order, `${w}x${h}: scrolling down, the lit chapter only moves forward`);
    check(right, `${w}x${h}: scrolling, the lit chapter is the one at the reading line`);
    check(last === ids.length - 1, `${w}x${h}: at the bottom, the last chapter is lit`);
    // Clicking each entry: that entry, and only it.
    for (const cid of ids) {
      await ev(`document.querySelector('#tourMap [data-chapter="${cid}"] button').click()`);
      await sleep(900);
      const s = await state();
      check(s.on.length === 1 && s.on[0] === cid && s.cur[0] === cid, `${w}x${h}: click ${cid} -> lit ${s.on.join(',')}`);
    }
  }
  // A link to a chapter, then the ST/STE switch.
  await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
  await send('Page.navigate', { url: `http://127.0.0.1:${PORT}/index.html#/how/cart` });
  await sleep(1800);
  let s = await state();
  check(s.on.length === 1 && s.on[0] === 'topic-cart', `link #/how/cart -> lit ${s.on.join(',')}`);
  await ev(`document.querySelector('.map-toggle [data-machine="ste"]').click()`);
  await sleep(300);
  s = await state();
  const addr = await ev(`document.querySelector('#tourMap [data-chapter="topic-boot"] .seg-addr').textContent`);
  check(s.on.length === 1 && s.on[0] === 'topic-cart' && addr === '$E00000', `STE switch: one lit (${s.on.join(',')}), power-on at ${addr}`);
  check(errors.length === 0, `no exceptions${errors.length ? ': ' + errors.join(' | ') : ''}`);
  ws.close();
} finally {
  chrome.kill(); server.kill(); await sleep(300); rmSync(profile, { recursive: true, force: true });
}
console.log(failures ? `FAIL: ${failures} checks` : 'PASS: the tour selector');
process.exit(failures ? 1 : 0);
