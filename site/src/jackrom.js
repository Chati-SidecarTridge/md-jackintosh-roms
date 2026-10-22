// The image builder's core (EPIC-11, D-20): the user's Mac Plus ROM merged
// with our glue and patch table exactly as tools/jackrom.py merge does, in
// the browser. The data (data/<machine>.json, tools/jackrom.py export) holds
// no Apple byte; the ROM never leaves the user's computer.

import { sha256Hex } from './sha256.js';

// ROMs people may drop instead of the Plus v3, by their stored checksum.
export const KNOWN_ROMS = {
  0x28BA61CE: 'the Macintosh 128K ROM',
  0x28BA4E50: 'the Macintosh 512K ROM',
  0x4D1EEEE1: 'the Mac Plus v1 ROM (also the 512Ke\'s)',
  0x4D1EEAE1: 'the Mac Plus v2 ROM',
};

export const MACHINE_FILES = {
  st: (release) => `JACKINTOSH-ST-${release}.img`,
  ste: (release) => `JACKINTOSH-STE-${release}.img`,
};

export class JackRomError extends Error {}

const hex = (value, digits) => '$' + (value >>> 0).toString(16).toUpperCase().padStart(digits, '0');

function word(data, offset) {
  return (data[offset] << 8) | data[offset + 1];
}

function long(data, offset) {
  return ((data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3]) >>> 0;
}

// tools/jackrom.py fill_word: the free space's word at an even offset.
export function fillWord(offset) {
  let x = (offset ^ 0x9E3779B9) >>> 0;
  x = (x + (x << 10)) >>> 0;
  x = (x ^ (x >>> 6)) >>> 0;
  x = (x + (x << 3)) >>> 0;
  x = (x ^ (x >>> 11)) >>> 0;
  x = (x + (x << 15)) >>> 0;
  return (x ^ (x >>> 16)) & 0xFFFF;
}

export function fillPattern(image, offset, length) {
  for (let o = offset; o < offset + length; o++) {
    const w = fillWord(o & ~1);
    image[o] = (o & 1) === 0 ? (w >> 8) : (w & 0xFF);
  }
}

// tools/macrom.py rom_checksum: the words from offset 4, summed in 32 bits.
export function romChecksum(data) {
  let sum = 0;
  for (let o = 4; o + 1 < data.length; o += 2) sum = (sum + word(data, o)) >>> 0;
  return sum;
}

// tools/jackrom.py image_checksum: every word but the long at field.
export function imageChecksum(image, field) {
  let sum = 0;
  for (let o = 0; o + 1 < image.length; o += 2) {
    if (o >= field && o < field + 4) continue;
    sum = (sum + word(image, o)) >>> 0;
  }
  return sum;
}

// tools/macrom.py check_rom, with a name for the ROMs we know.
export function checkRom(rom, info) {
  if (rom.length !== info.size) {
    throw new JackRomError(`size is ${rom.length} bytes, not ${info.size}: the Mac Plus ROM is 128 KB`
      + (rom.length === 2 * info.size ? ' (a 256 KB file may be the Plus ROM written twice, or another Mac\'s)' : ''));
  }
  const stored = long(rom, 0);
  const computed = romChecksum(rom);
  if (stored !== info.checksum || computed !== info.checksum) {
    const known = KNOWN_ROMS[stored];
    throw new JackRomError(`checksum stored ${hex(stored, 8)}, computed ${hex(computed, 8)}; `
      + `the Plus v3 ROM has ${hex(info.checksum, 8)}`
      + (known && stored === computed ? `: this is ${known}, the Jackintosh needs the Plus v3` : ''));
  }
  const version = word(rom, 8);
  if (version !== info.version) {
    throw new JackRomError(`version ${hex(version, 4)}, not ${hex(info.version, 4)}`);
  }
  const digest = sha256Hex(rom);
  if (digest !== info.sha256) {
    throw new JackRomError(`checksum and version match, but the SHA-256 is ${digest}, not the Plus v3 ROM's ${info.sha256}`);
  }
  return digest;
}

function decodeBase64(text) {
  const binary = atob(text);
  const out = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) out[i] = binary.charCodeAt(i);
  return out;
}

function hexBytes(text) {
  const out = new Uint8Array(text.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(text.substr(i * 2, 2), 16);
  return out;
}

// tools/jackrom.py merge: the glue, its fill, the ROM, the patches checked
// and applied, the checksum. log(text) reports each step.
export function buildImage(rom, data, log = () => {}) {
  const glue = decodeBase64(data.glue);
  if (sha256Hex(glue) !== data.glueSha256) throw new JackRomError('the glue data is damaged (its SHA-256 differs): reload the page');
  const headVersion = word(glue, 2);
  const headBase = long(glue, 8);
  if (headBase !== data.base || headVersion !== data.versionWord) {
    throw new JackRomError(`the glue's header says base ${hex(headBase, 6)}, version ${hex(headVersion, 4)}; `
      + `the ${data.machine} image needs ${hex(data.base, 6)}, ${hex(data.versionWord, 4)}`);
  }
  const field = data.glueSize - data.checksumSize;
  if (glue.length > field) throw new JackRomError(`the glue is ${glue.length} bytes; the ${data.machine} image has room for ${field}`);

  log(`ROM: ${rom.length} bytes`);
  const digest = checkRom(rom, data.rom);
  log(`ROM: the Mac Plus v3 (checksum ${hex(data.rom.checksum, 8)}, version ${hex(data.rom.version, 4)}, SHA-256 ${digest.slice(0, 16)}…)`);

  const image = new Uint8Array(data.glueSize + rom.length);
  image.set(glue, 0);
  fillPattern(image, glue.length, field - glue.length);
  image.set(rom, data.glueSize);
  log(`glue: ${glue.length} of ${field} bytes at ${hex(data.base, 6)}, the Mac ROM at ${hex(data.base + data.glueSize, 6)}`);

  for (const patch of data.patches) {
    const site = rom.subarray(patch.offset, patch.offset + patch.length);
    const found = sha256Hex(site);
    if (found !== patch.sha256) {
      throw new JackRomError(`patch ${patch.name}: the Mac ROM differs at ${hex(patch.offset, 5)} `
        + `(sha256 ${found.slice(0, 16)}…, expected ${patch.sha256.slice(0, 16)}…)`);
    }
    const replacement = hexBytes(patch.replace);
    if (replacement.length !== patch.length) throw new JackRomError(`patch ${patch.name}: the replacement is ${replacement.length} bytes, not ${patch.length}`);
    image.set(replacement, data.glueSize + patch.offset);
  }
  log(`patches: ${data.patches.length} applied, each checked against the bytes it replaces`);

  const checksum = imageChecksum(image, field);
  image[field] = checksum >>> 24;
  image[field + 1] = (checksum >>> 16) & 0xFF;
  image[field + 2] = (checksum >>> 8) & 0xFF;
  image[field + 3] = checksum & 0xFF;
  log(`image: ${image.length / 1024} KB, checksum ${hex(checksum, 8)}, SHA-256 ${sha256Hex(image)}`);
  return image;
}
