// The builder's core (src/jackrom.js) without Apple's ROM: SHA-256 against
// Node's, the fill pattern against tools/jackrom.py, the checksums and the
// ROM checks on synthetic data. tools/parity.sh checks a real ROM locally.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash, randomBytes } from 'node:crypto';
import { sha256Hex } from '../src/sha256.js';
import { fillWord, romChecksum, imageChecksum, checkRom, JackRomError } from '../src/jackrom.js';

test('sha256 matches node:crypto', () => {
  for (const size of [0, 1, 55, 56, 63, 64, 65, 1000, 131072]) {
    const data = new Uint8Array(randomBytes(size));
    assert.equal(sha256Hex(data), createHash('sha256').update(data).digest('hex'));
  }
});

test('fillWord matches tools/jackrom.py fill_word', () => {
  // python3 -c "import jackrom; print([jackrom.fill_word(o) for o in (0,2,0x8870,0x1FFFA,0xFFFE)])"
  assert.deepEqual([0, 2, 0x8870, 0x1FFFA, 0xFFFE].map(fillWord), [15623, 20018, 34327, 54081, 62060]);
});

test('checksums sum big-endian words', () => {
  const data = new Uint8Array([0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x01, 0xFF, 0xFF]);
  assert.equal(romChecksum(data), 0x10000);
  assert.equal(imageChecksum(data, 4), 0xAABB + 0xCCDD);
});

const INFO = { size: 131072, checksum: 0x4D1F8172, version: 0x0075, sha256: '0'.repeat(64) };

test('checkRom refuses a wrong size', () => {
  assert.throws(() => checkRom(new Uint8Array(1000), INFO), (e) => e instanceof JackRomError && /size is 1000 bytes/.test(e.message));
});

test('checkRom names a Plus v2 ROM', () => {
  const rom = new Uint8Array(131072);
  // A checksum of $4D1EEAE1 stored and computed: words from offset 4.
  rom.set([0x4D, 0x1E, 0xEA, 0xE1], 0);
  let left = 0x4D1EEAE1;
  for (let o = 4; left > 0; o += 2) {
    const w = Math.min(0xFFFF, left);
    rom[o] = w >> 8; rom[o + 1] = w & 0xFF; left -= w;
  }
  assert.throws(() => checkRom(rom, INFO), (e) => /Plus v2/.test(e.message));
});
