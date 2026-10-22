// "How it works": the topics' interactive diagrams. Every part a visitor can
// point at carries data-info; hovering, focusing or tapping it fills the
// topic's info panel, so it works with a mouse, a keyboard and a finger.

export const TOPICS = [
  ['picture', 'The big picture'],
  ['memory', 'Memory maps'],
  ['image', 'Building an image'],
  ['boot', 'The boot'],
];

function showInfo(target) {
  const topic = target.closest('.topic');
  const panel = topic && topic.querySelector('.info-panel');
  if (!panel) return;
  for (const el of topic.querySelectorAll('[data-info].lit')) el.classList.remove('lit');
  target.classList.add('lit');
  panel.innerHTML = `<strong>${target.dataset.title || ''}</strong> ${target.dataset.info}`;
}

function wireInfo(root) {
  for (const el of root.querySelectorAll('[data-info]')) {
    el.setAttribute('tabindex', '0');
    el.addEventListener('mouseenter', () => showInfo(el));
    el.addEventListener('focus', () => showInfo(el));
    el.addEventListener('click', () => showInfo(el));
  }
}

// Topic 2: the memory maps, one machine at a time.
function wireMemory() {
  const buttons = document.querySelectorAll('[data-memmap]');
  for (const button of buttons) {
    button.addEventListener('click', () => {
      for (const b of buttons) b.classList.toggle('current', b === button);
      for (const map of document.querySelectorAll('.memmap-atari')) {
        map.hidden = map.dataset.machine !== button.dataset.memmap;
      }
    });
  }
}

// Topic 3: the image, drawn from the published data (data/<machine>.json):
// the glue, its free space, and each patch site in the Mac ROM.
async function drawImage(machine) {
  const svg = document.getElementById('imageMap');
  const panel = document.getElementById('imageInfo');
  let data;
  try {
    const response = await fetch(`data/${machine}.json`, { cache: 'no-cache' });
    data = await response.json();
  } catch {
    panel.textContent = 'The image map needs the builder\'s data, which loads on the published site.';
    return;
  }
  const glue = atob(data.glue).length;
  const total = data.glueSize + data.rom.size;
  const W = 1000;
  const x = (offset) => (offset / total) * W;
  const ns = 'http://www.w3.org/2000/svg';
  svg.innerHTML = '';
  const rect = (x0, w, cls, title, info) => {
    const r = document.createElementNS(ns, 'rect');
    r.setAttribute('x', x0); r.setAttribute('y', 30); r.setAttribute('width', Math.max(w, 1)); r.setAttribute('height', 60);
    r.setAttribute('class', cls);
    r.dataset.title = title; r.dataset.info = info;
    svg.appendChild(r);
    return r;
  };
  const hex = (v) => '$' + v.toString(16).toUpperCase();
  rect(0, x(glue), 'seg-glue', 'The glue',
    `${glue.toLocaleString()} bytes of our own 68000 code at ${hex(data.base)}: the Atari reset header, RAM sizing, the menu, the disk driver, interrupts, keyboard and mouse.`);
  rect(x(glue), x(data.glueSize - data.checksumSize) - x(glue), 'seg-free', 'Free space',
    `${(data.glueSize - data.checksumSize - glue).toLocaleString()} bytes, filled with a pattern mixed from each address, so a stuck address line cannot pass for good data. The last long is the image's checksum.`);
  rect(x(data.glueSize), x(total) - x(data.glueSize), 'seg-rom', 'The Mac Plus ROM',
    `Your own 128 KB Mac Plus v3 ROM at ${hex(data.base + data.glueSize)}, every byte at its original offset, so System 6's own patches still find the code they expect.`);
  for (const patch of data.patches) {
    const t = document.createElementNS(ns, 'rect');
    t.setAttribute('x', x(data.glueSize + patch.offset)); t.setAttribute('y', 22);
    t.setAttribute('width', 2.2); t.setAttribute('height', 76); t.setAttribute('class', 'seg-patch');
    t.dataset.title = `Patch ${patch.name}`;
    t.dataset.info = `${patch.length} bytes at ROM offset ${hex(patch.offset)}, applied only after the SHA-256 of the bytes it replaces matches.`;
    svg.appendChild(t);
  }
  wireInfo(svg.closest('.topic'));
  panel.innerHTML = `<strong>${data.patches.length} patches</strong> in a ${total / 1024} KB image for the ${machine === 'st' ? 'ST family' : 'STE family'}: point at a part, or at an orange tick.`;
}

function wireImage() {
  const buttons = document.querySelectorAll('[data-imagemap]');
  for (const button of buttons) {
    button.addEventListener('click', () => {
      for (const b of buttons) b.classList.toggle('current', b === button);
      drawImage(button.dataset.imagemap);
    });
  }
  drawImage('st');
}

// Topic 4: the boot, one step at a time or played.
function wireBoot() {
  const steps = [...document.querySelectorAll('.boot-step')];
  const shot = document.getElementById('bootShot');
  let current = 0;
  let timer = null;
  const show = (n) => {
    current = (n + steps.length) % steps.length;
    steps.forEach((s, i) => s.classList.toggle('lit', i === current));
    const img = steps[current].dataset.shot;
    shot.hidden = !img;
    if (img) shot.src = img;
    document.getElementById('bootInfo').innerHTML = `<strong>${steps[current].dataset.title}</strong> ${steps[current].dataset.info}`;
  };
  steps.forEach((s, i) => s.addEventListener('click', () => { stop(); show(i); }));
  const stop = () => { clearInterval(timer); timer = null; document.getElementById('bootPlay').textContent = 'Play'; };
  document.getElementById('bootPrev').addEventListener('click', () => { stop(); show(current - 1); });
  document.getElementById('bootNext').addEventListener('click', () => { stop(); show(current + 1); });
  document.getElementById('bootPlay').addEventListener('click', () => {
    if (timer) { stop(); return; }
    document.getElementById('bootPlay').textContent = 'Pause';
    timer = setInterval(() => show(current + 1), 3200);
  });
  show(0);
}

function initHow() {
  const root = document.querySelector('[data-view="how"]');
  wireInfo(root);
  wireMemory();
  wireImage();
  wireBoot();
}

if (typeof document !== 'undefined') {
  initHow();
}
