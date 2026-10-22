// "How it works": the topics' interactive diagrams. Every part a visitor can
// point at carries data-info; hovering, focusing or tapping it fills the
// topic's info panel, so it works with a mouse, a keyboard and a finger.

export const TOPICS = [
  ['picture', 'The big picture'],
  ['memory', 'Memory maps'],
  ['image', 'Building an image'],
  ['boot', 'The boot'],
  ['reloc', 'System 6\'s addresses'],
  ['time', 'Interrupts and time'],
  ['chips', 'The Mac\'s chips in RAM'],
  ['buserr', 'The bus-error handler'],
  ['input', 'Keyboard and mouse'],
  ['video', 'Video'],
  ['cart', 'The Multi-device'],
  ['runs', 'What runs'],
];

function showInfo(target) {
  const topic = target.closest('.topic, .chapter');
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
  wireInfo(svg.closest('.topic, .chapter'));
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

// Topic 5: a resource loads and its addresses move.
function wireReloc() {
  let machine = 'st';
  let timer = null;
  const base = { st: 'FD', ste: 'E2' };
  const toggles = document.querySelectorAll('[data-reloc]');
  for (const button of toggles) {
    button.addEventListener('click', () => {
      machine = button.dataset.reloc;
      for (const b of toggles) b.classList.toggle('current', b === button);
      document.getElementById('relocNew').textContent = `$${base[machine]}xxxx`;
    });
  }
  for (const card of document.querySelectorAll('.reloc-card')) {
    card.addEventListener('click', () => {
      clearInterval(timer);
      const count = Number(card.dataset.count);
      const info = document.getElementById('relocInfo');
      const bar = document.getElementById('relocBar');
      let done = 0;
      info.innerHTML = `<strong>${card.dataset.title}</strong> Loaded; type, ID, size and SHA-256 match the table: moving its addresses&hellip;`;
      timer = setInterval(() => {
        done = Math.min(count, done + Math.max(1, Math.round(count / 40)));
        bar.style.width = `${(done / count) * 100}%`;
        const low = (0x1000 + done * 97).toString(16).toUpperCase().slice(-4);
        document.getElementById('relocOld').textContent = `$40${low}`;
        document.getElementById('relocNew').textContent = `$${base[machine]}${low}`;
        if (done === count) {
          clearInterval(timer);
          info.innerHTML = `<strong>${card.dataset.title}</strong> ${card.dataset.info} ${count} moved.`;
        }
      }, 40);
    });
  }
}

// Topic 6: two ticks side by side, in real time.
function wireTicks() {
  const start = performance.now();
  const mac = document.getElementById('tickMac');
  const jk = document.getElementById('tickJk');
  const frame = (now) => {
    const seconds = (now - start) / 1000;
    mac.textContent = Math.floor(seconds * 60.15).toLocaleString();
    jk.textContent = Math.floor(seconds * 60.235).toLocaleString();
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

// Topic 8: the bus-error handler, step by step.
function wireBusError() {
  const steps = [
    [1, 'The write.', 'A0 is 0: the program asked for memory it did not get. MOVE.L D0,4(A0) writes to $000004. On a Mac that is RAM and nothing happens; on an Atari $0-$7 is ROM.'],
    [1, 'Bus error.', 'The Atari raises a bus error. The 68000 stacks the address it tried ($000004), whether it was a read or a write, and the program counter, which by then is past the instruction.'],
    [1, 'Walk back.', 'The glue\'s handler decodes the instructions just before the stacked PC, looking for one that writes memory through A0 at this address: MOVE.L D0,4(A0), 4 bytes long, ending right at the stacked PC.'],
    [2, 'Skip and resume.', 'Writes to $0-$7 (and to the Plus\'s ROM space) are skipped: the handler resumes after the instruction, as if the write had landed. The program goes on as it would on a Mac.'],
    [2, 'Reads too.', 'A read from the Plus\'s ROM space ($400000-$57FFFF) is re-run against our copy of the Mac ROM, one instruction under the 68000\'s trace mode, so programs that peek at the ROM at its Plus address still find it.'],
  ];
  let current = 0;
  const show = (n) => {
    current = Math.max(0, Math.min(steps.length - 1, n));
    const [line, title, text] = steps[current];
    for (const span of document.querySelectorAll('#beCode [data-line]')) {
      span.classList.toggle('lit', Number(span.dataset.line) === line);
    }
    document.getElementById('beInfo').innerHTML = `<strong>${current + 1}/${steps.length} ${title}</strong> ${text}`;
  };
  document.getElementById('bePrev').addEventListener('click', () => show(current - 1));
  document.getElementById('beNext').addEventListener('click', () => show(current + 1));
  show(0);
}

// Topic 10: the two screen sizes.
function wireVideo() {
  const buttons = document.querySelectorAll('[data-video]');
  const mac = document.getElementById('macScreen');
  const info = document.getElementById('videoInfo');
  for (const button of buttons) {
    button.addEventListener('click', () => {
      for (const b of buttons) b.classList.toggle('current', b === button);
      const plus = button.dataset.video === 'plus';
      mac.setAttribute('x', plus ? 74 : 10);
      mac.setAttribute('y', plus ? 39 : 10);
      mac.setAttribute('width', plus ? 512 : 640);
      mac.setAttribute('height', plus ? 342 : 400);
      document.querySelector('.video .menubar').setAttribute('x', plus ? 74 : 10);
      document.querySelector('.video .menubar').setAttribute('y', plus ? 39 : 10);
      document.querySelector('.video .menubar').setAttribute('width', plus ? 512 : 640);
      document.querySelector('.video .menutext').setAttribute('x', plus ? 104 : 40);
      document.querySelector('.video .menutext').setAttribute('y', plus ? 54 : 25);
      info.innerHTML = plus
        ? '<strong>512x342 in a black frame.</strong> The Mac\'s classic screen, 64 pixels in and 29 rows down: programs that assume a Plus screen draw where they expect.'
        : '<strong>640x400.</strong> The whole monochrome screen: the Mac ROM is told its size, and the desktop and windows use all of it.';
    });
  }
}

// Topic 11: a disk read over the cartridge.
function wireCart() {
  const steps = [
    ['out', 'The command.', 'To read sectors the glue sends a command, one 16-bit word at a time: a header ($ABCD), the command ($0700, READ), the payload size, a random token, the unit and count, the block, a checksum. Each word w goes out as a read of $FB0000 + (w XOR $8000).'],
    ['', 'The RP2040 listens.', 'The Multi-device sees every address on the cartridge bus. It decodes the words, checks the checksum, and reads the sectors from the image file on its microSD card.'],
    ['back', 'The answer.', 'It writes up to 32 sectors into its window, and the token, echoed, at $FA2004, with a new seed at $FA2008: the sign that the answer is complete.'],
    ['back', 'The copy.', 'The glue, which waited for the token, copies the sectors out of the window at $FA0100 into the Mac\'s buffer: the Mac\'s Read call returns.'],
    ['', 'Disks that change.', 'A unit table in the window tells which images are in which drive, each with a change counter and an image identity: so the Mac learns of a new disk, and an RP restart never passes one image off as another.'],
  ];
  let current = 0;
  const show = (n) => {
    current = Math.max(0, Math.min(steps.length - 1, n));
    const [lit, title, text] = steps[current];
    document.getElementById('cartOut').classList.toggle('active', lit === 'out');
    document.getElementById('cartBack').classList.toggle('active', lit === 'back');
    document.getElementById('cartInfo').innerHTML = `<strong>${current + 1}/${steps.length} ${title}</strong> ${text}`;
  };
  document.getElementById('cartPrev').addEventListener('click', () => show(current - 1));
  document.getElementById('cartNext').addEventListener('click', () => show(current + 1));
  show(0);
}

// First steps: the two ways to a hard disk, as tabs.
function wireRoutes() {
  const tabs = document.querySelectorAll('.route[data-route]');
  for (const tab of tabs) {
    tab.addEventListener('click', () => {
      for (const t of tabs) {
        const on = t === tab;
        t.classList.toggle('current', on);
        t.setAttribute('aria-selected', String(on));
      }
      for (const panel of document.querySelectorAll('[data-route-panel]')) {
        panel.hidden = panel.dataset.routePanel !== tab.dataset.route;
      }
    });
  }
}

// The tour's selector: the chapters in reading order, each with its
// address; exactly one is lit, the chapter being read.
const tour = { machine: 'st', screen: '$3F7F00', pinned: null };

function chapters() {
  return [...document.querySelectorAll('.chapter')];
}

function chapterAddress(chapter) {
  if (chapter.id === 'topic-video') return tour.screen;
  const a = chapter.querySelector('.ch-addr');
  return a ? a.dataset[tour.machine] : '';
}

function drawMap() {
  const list = document.getElementById('tourMap');
  if (!list) return;
  list.innerHTML = '';
  for (const chapter of chapters()) {
    const li = document.createElement('li');
    li.className = 'map-seg' + (chapter.dataset.ghost ? ' ghost' : '');
    li.dataset.chapter = chapter.id;
    li.innerHTML = `<button type="button"><span class="seg-name">${chapter.querySelector('h3').textContent}</span><span class="seg-addr">${chapterAddress(chapter)}</span></button>`;
    li.querySelector('button').addEventListener('click', () => {
      history.replaceState(null, '', `#/how/${chapter.id.replace('topic-', '')}`);
      tour.pinned = chapter;
      markCurrent();
      chapter.scrollIntoView({ behavior: 'smooth', block: 'start' });
    });
    list.appendChild(li);
  }
  markCurrent();
}

// The chapter being read: the last one whose top has passed the upper third
// of the window; the last chapter at the bottom of the page.
function currentChapter() {
  const all = chapters();
  if (!all.length) return null;
  if (tour.pinned) return tour.pinned; // chosen in the selector or by a link
  if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 4) return all[all.length - 1];
  const line = window.innerHeight / 3;
  let current = all[0];
  for (const c of all) {
    if (c.getBoundingClientRect().top <= line) current = c;
  }
  return current;
}

function markCurrent() {
  const view = document.querySelector('[data-view="how"]');
  if (!view || view.hidden) return;
  const current = currentChapter();
  for (const c of chapters()) c.classList.toggle('current', c === current);
  for (const li of document.querySelectorAll('#tourMap .map-seg')) {
    li.classList.toggle('on', current !== null && li.dataset.chapter === current.id);
  }
}

function setMachine(machine) {
  tour.machine = machine;
  for (const b of document.querySelectorAll('.map-toggle [data-machine]')) b.classList.toggle('current', b.dataset.machine === machine);
  for (const a of document.querySelectorAll('.ch-addr[data-st]')) {
    a.textContent = a.closest('#topic-video') ? tour.screen : a.dataset[machine];
  }
  drawMap();
  const imageButton = document.querySelector(`[data-imagemap="${machine}"]`);
  if (imageButton) imageButton.click();
  const relocButton = document.querySelector(`[data-reloc="${machine}"]`);
  if (relocButton) relocButton.click();
}

function wireMap() {
  if (!document.getElementById('tourMap')) return;
  for (const b of document.querySelectorAll('.map-toggle [data-machine]')) {
    b.addEventListener('click', () => setMachine(b.dataset.machine));
  }
  drawMap();
  let queued = false;
  const onScroll = () => {
    if (queued) return;
    queued = true;
    requestAnimationFrame(() => { queued = false; markCurrent(); });
  };
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', onScroll);
  // A chapter chosen stays lit until the reader scrolls by hand.
  const unpin = () => { if (tour.pinned) { tour.pinned = null; onScroll(); } };
  for (const type of ['wheel', 'touchstart', 'keydown', 'mousedown']) {
    window.addEventListener(type, (e) => {
      if (type === 'mousedown' && e.target.closest && e.target.closest('#tourMap')) return;
      unpin();
    }, { passive: true });
  }
  const pinFromHash = () => {
    const id = (window.location.hash.split('/')[2] || '');
    tour.pinned = id ? document.getElementById(`topic-${id}`) : null;
    setTimeout(markCurrent, 50);
  };
  window.addEventListener('hashchange', pinFromHash);
  pinFromHash();
}

// The RAM chapter: where the screen lands for the RAM installed.
function wireRam() {
  const hex = (v) => '$' + v.toString(16).toUpperCase().padStart(6, '0');
  for (const b of document.querySelectorAll('[data-ram]')) {
    b.addEventListener('click', () => {
      for (const o of document.querySelectorAll('[data-ram]')) o.classList.toggle('current', o === b);
      const top = Number(b.dataset.ram);
      tour.screen = hex(top - 0x8100);
      document.getElementById('ramCalc').textContent = `MemTop ${hex(top)}, the screen at ${tour.screen}`;
      for (const el of document.querySelectorAll('.screen-addr')) el.textContent = tour.screen;
      const video = document.querySelector('#topic-video .ch-addr');
      if (video) video.textContent = tour.screen;
      drawMap();
    });
  }
}

function initHow() {
  const root = document.querySelector('[data-view="how"]');
  wireInfo(root);
  wireMemory();
  wireImage();
  wireBoot();
  wireReloc();
  wireTicks();
  wireBusError();
  wireVideo();
  wireCart();
  wireRoutes();
  wireMap();
  wireRam();
}

if (typeof document !== 'undefined') {
  initHow();
}
