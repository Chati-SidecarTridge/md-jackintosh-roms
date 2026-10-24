// The build page: four steps (ROM, machine, build, download). The work is
// src/jackrom.js's; this is DOM glue, the way magic-c-builder and USM-web do it.

import { buildImage, MACHINE_FILES, JackRomError } from './jackrom.js';
import { HELP_CONTENT } from './help-content.js';

function initBuilder() {
  const state = { rom: null, romName: '', machine: 'st', url: null };
  const $ = (id) => document.getElementById(id);
  const ok = typeof File !== 'undefined' && typeof Blob !== 'undefined'
    && typeof URL !== 'undefined' && typeof URL.createObjectURL === 'function' && typeof fetch === 'function';
  if (!ok) {
    $('browserWarning').style.display = 'block';
    $('browserWarning').textContent = 'Your browser lacks the File, Blob or fetch APIs this page needs. Please use a recent Chrome, Firefox or Safari.';
    return;
  }

  function step(n, status) {
    const li = $(`step${n}`);
    li.classList.toggle('active', status === 'Ready');
    li.classList.toggle('complete', status === 'Done');
    li.querySelector('.step-status').textContent = status;
  }

  function log(text, kind = '') {
    const line = document.createElement('div');
    line.className = 'log-line' + (kind ? ` log-${kind}` : '');
    line.textContent = text;
    $('log').appendChild(line);
  }

  function invalidate() {
    if (state.url) URL.revokeObjectURL(state.url);
    state.url = null;
    $('downloads').innerHTML = '';
    step(4, 'Pending');
    step(3, state.rom ? 'Ready' : 'Pending');
    $('buildBtn').disabled = !state.rom;
  }

  async function takeFile(file) {
    if (!file) return;
    state.rom = new Uint8Array(await file.arrayBuffer());
    state.romName = file.name;
    $('romHint').textContent = `Loaded ${file.name} (${state.rom.length} bytes). It stays in your browser.`;
    step(1, 'Done');
    step(2, 'Done');
    invalidate();
  }

  // The release and the build the published data carries, as the Atari's
  // diagnostic screen shows the build.
  fetch('data/st.json', { cache: 'no-cache' })
    .then((response) => (response.ok ? response.json() : null))
    .then((data) => {
      if (data && data.build) $('releaseInfo').textContent = `Release ${data.release}, build ${data.build}`;
    })
    .catch(() => {});

  const drop = $('dropZone');
  drop.addEventListener('dragover', (e) => { e.preventDefault(); drop.classList.add('drag-active'); });
  drop.addEventListener('dragleave', () => drop.classList.remove('drag-active'));
  drop.addEventListener('drop', (e) => {
    e.preventDefault();
    drop.classList.remove('drag-active');
    takeFile(e.dataTransfer.files[0]);
  });
  $('fileInput').addEventListener('change', (e) => takeFile(e.target.files[0]));
  for (const radio of document.querySelectorAll('input[name="machine"]')) {
    radio.addEventListener('change', () => { state.machine = radio.value; invalidate(); });
  }

  $('buildBtn').addEventListener('click', async () => {
    invalidate();
    $('log').innerHTML = '';
    $('buildBtn').disabled = true;
    log(`Building the ${state.machine === 'st' ? 'ST family' : 'STE family'} image from ${state.romName}`, 'start');
    try {
      const response = await fetch(`data/${state.machine}.json`, { cache: 'no-cache' });
      if (!response.ok) throw new JackRomError(`the builder's data (data/${state.machine}.json) did not load: HTTP ${response.status}`);
      const data = await response.json();
      if (data.build) log(`Release ${data.release}, build ${data.build}`);
      const image = buildImage(state.rom, data, (line) => log(line));
      const name = MACHINE_FILES[state.machine](data.release);
      state.url = URL.createObjectURL(new Blob([image], { type: 'application/octet-stream' }));
      const link = document.createElement('a');
      link.href = state.url;
      link.download = name;
      link.className = 'download-btn';
      link.textContent = `Download ${name} (${image.length / 1024} KB)`;
      $('downloads').appendChild(link);
      log(`Done: ${name}`, 'done');
      step(3, 'Done');
      step(4, 'Ready');
    } catch (err) {
      log(`Build failed: ${err.message}`, 'error');
      if (!(err instanceof JackRomError)) console.error(err);
      step(3, 'Ready');
    } finally {
      $('buildBtn').disabled = !state.rom;
    }
  });

  $('resetBtn').addEventListener('click', () => {
    state.rom = null;
    $('fileInput').value = '';
    $('romHint').textContent = '';
    $('log').innerHTML = '';
    step(1, 'Ready');
    step(2, 'Ready');
    invalidate();
  });

  const dialog = $('helpModal');
  dialog.querySelector('.modal-close').addEventListener('click', () => dialog.close());
  dialog.addEventListener('click', (e) => { if (e.target === dialog) dialog.close(); });
  document.addEventListener('click', (e) => {
    const btn = e.target.closest && e.target.closest('.help-icon[data-help-key]');
    const entry = btn && HELP_CONTENT[btn.dataset.helpKey];
    if (!entry) return;
    $('helpModalTitle').textContent = entry.title;
    $('helpModalBody').innerHTML = entry.html;
    dialog.showModal();
  });
}

if (typeof document !== 'undefined') {
  initBuilder();
}
