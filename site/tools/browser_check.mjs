#!/usr/bin/env node
// End-to-end check of the build page in headless Chrome (magic-c-builder's
// tools/browser_check.mjs, adapted): serve site/ over HTTP from the LAN
// address (an insecure origin, as a tablet sees it), load the ROM into the
// file input, build each machine, and compare the download's SHA-256 with
// tools/parity.sh's jackrom.py merge output in OUTDIR; then check that a
// wrong file is refused. Locally only: it reads Apple's ROM (D-02).
//
//   tools/parity.sh && node tools/browser_check.mjs ../build/site-parity
//
// Needs Google Chrome, Node 22+ (built-in WebSocket) and site/data/*.json
// (tools/jackrom.py export) from the same glue as OUTDIR's images.
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir, networkInterfaces } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const HTTP_PORT = 8766;
const CDP_PORT = 9334;
const outDir = process.argv[2] && resolve(process.argv[2]);
if (!outDir) {
  console.error('usage: tools/browser_check.mjs OUTDIR');
  process.exit(2);
}
const LAN_IP = Object.values(networkInterfaces()).flat()
  .find((a) => a && a.family === 'IPv4' && !a.internal)?.address || '127.0.0.1';
const PAGE = `http://${LAN_IP}:${HTTP_PORT}/index.html#/build`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function waitFor(fn, what, ms = 15000) {
  const end = Date.now() + ms;
  for (;;) {
    try {
      const v = await fn();
      if (v) return v;
    } catch {
      // not up yet
    }
    if (Date.now() > end) throw new Error(`timed out waiting for ${what}`);
    await sleep(200);
  }
}

const server = spawn('python3', ['-m', 'http.server', String(HTTP_PORT), '--bind', LAN_IP], { cwd: ROOT, stdio: 'ignore' });
const profile = mkdtempSync(join(tmpdir(), 'jk-chrome-'));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${profile}`,
  '--no-first-run', '--no-default-browser-check', '--window-size=1280,1600', 'about:blank'], { stdio: 'ignore' });
const wrongFile = join(profile, 'not-a-rom.bin');
writeFileSync(wrongFile, Buffer.alloc(1000, 0x55));

let failed = false;
const check = (ok, text) => { if (!ok) failed = true; console.log(`${ok ? 'PASS' : 'FAIL'} ${text}`); };
try {
  await waitFor(() => fetch(PAGE).then((r) => r.ok), 'the HTTP server');
  console.log(`page: ${PAGE}`);
  const target = await waitFor(() => fetch(`http://127.0.0.1:${CDP_PORT}/json/new?about:blank`, { method: 'PUT' }).then((r) => r.json()), 'Chrome');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let nextId = 1;
  const pending = new Map();
  const consoleLines = [];
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.id && pending.has(msg.id)) {
      pending.get(msg.id)(msg);
      pending.delete(msg.id);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      consoleLines.push(`exception: ${msg.params.exceptionDetails.text} ${msg.params.exceptionDetails.exception?.description || ''}`);
    } else if (msg.method === 'Runtime.consoleAPICalled') {
      consoleLines.push(`console.${msg.params.type}: ${msg.params.args.map((a) => a.value ?? a.description).join(' ')}`);
    }
  };
  const send = (method, params = {}) => new Promise((res) => {
    const id = nextId++;
    pending.set(id, res);
    ws.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.result.exceptionDetails) throw new Error(r.result.exceptionDetails.exception?.description || r.result.exceptionDetails.text);
    return r.result.result.value;
  };
  const setFile = async (path) => {
    const { root } = (await send('DOM.getDocument')).result;
    const { nodeId } = (await send('DOM.querySelector', { nodeId: root.nodeId, selector: '#fileInput' })).result;
    await send('DOM.setFileInputFiles', { nodeId, files: [path] });
    await waitFor(() => evaluate("document.getElementById('romHint').textContent.startsWith('Loaded')"), 'the file to load');
  };

  await send('Runtime.enable');
  await send('Page.enable');
  await send('DOM.enable');
  await send('Page.navigate', { url: PAGE });
  await waitFor(() => evaluate("document.readyState === 'complete' && !!document.getElementById('buildBtn')"), 'the page');
  console.log(`secure context: ${await evaluate('window.isSecureContext')}`);

  for (const machine of ['st', 'ste']) {
    await evaluate("document.getElementById('resetBtn').click()");
    await setFile(join(outDir, 'rom.bin'));
    const result = await evaluate(`(async () => {
      document.querySelector('input[name=machine][value=${machine}]').click();
      document.getElementById('buildBtn').click();
      const end = Date.now() + 60000;
      while (!document.querySelector('#downloads a')) {
        if (document.getElementById('log').textContent.includes('Build failed')) throw new Error(document.getElementById('log').textContent);
        if (Date.now() > end) throw new Error('no download after 60 s');
        await new Promise((r) => setTimeout(r, 100));
      }
      const a = document.querySelector('#downloads a');
      const buf = new Uint8Array(await (await fetch(a.href)).arrayBuffer());
      let bin = '';
      for (let i = 0; i < buf.length; i += 0x8000) bin += String.fromCharCode(...buf.subarray(i, i + 0x8000));
      return { name: a.download, b64: btoa(bin) };
    })()`);
    const sha = createHash('sha256').update(Buffer.from(result.b64, 'base64')).digest('hex');
    const expected = createHash('sha256').update(readFileSync(join(outDir, `merge-${machine}.img`))).digest('hex');
    check(sha === expected, `${machine}: ${result.name} ${sha.slice(0, 16)}${sha === expected ? ' = jackrom.py merge' : ` (merge ${expected.slice(0, 16)})`}`);
  }

  await evaluate("document.getElementById('resetBtn').click()");
  await setFile(wrongFile);
  const refusal = await evaluate(`(async () => {
    document.getElementById('buildBtn').click();
    const end = Date.now() + 10000;
    while (!document.getElementById('log').textContent.includes('Build failed')) {
      if (Date.now() > end) return '';
      await new Promise((r) => setTimeout(r, 100));
    }
    return document.querySelector('.log-error').textContent;
  })()`);
  check(/size is 1000 bytes/.test(refusal) && !(await evaluate("!!document.querySelector('#downloads a')")), `a wrong file is refused: ${refusal}`);

  if (process.env.SHOT) {
    const { layoutViewport, contentSize } = (await send('Page.getLayoutMetrics')).result;
    const shot = await send('Page.captureScreenshot', {
      format: 'png', captureBeyondViewport: true,
      clip: { x: 0, y: 0, width: layoutViewport.clientWidth, height: contentSize.height, scale: 1 },
    });
    writeFileSync(process.env.SHOT, Buffer.from(shot.result.data, 'base64'));
    console.log(`screenshot: ${process.env.SHOT}`);
  }
  for (const line of consoleLines) console.log(line);
  ws.close();
} catch (err) {
  failed = true;
  console.error(`browser check failed: ${err.message}`);
} finally {
  chrome.kill();
  server.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
process.exit(failed ? 1 : 0);
