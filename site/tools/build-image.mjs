#!/usr/bin/env node
// The page's builder from the command line (the same core, src/jackrom.js):
//   node tools/build-image.mjs --machine st|ste --rom ROM --data data/st.json -o OUT.img
// tools/parity.sh compares its output with tools/jackrom.py merge.

import { readFileSync, writeFileSync } from 'node:fs';
import { buildImage, JackRomError } from '../src/jackrom.js';

function arg(name) {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : undefined;
}

const rom = arg('--rom');
const dataPath = arg('--data');
const out = arg('-o');
if (!rom || !dataPath || !out) {
  console.error('usage: build-image.mjs --rom ROM --data data/MACHINE.json -o OUT.img');
  process.exit(2);
}
try {
  const data = JSON.parse(readFileSync(dataPath, 'utf8'));
  const image = buildImage(new Uint8Array(readFileSync(rom)), data, (line) => console.log(`  ${line}`));
  writeFileSync(out, image);
  console.log(`image: ${out}`);
} catch (err) {
  if (!(err instanceof JackRomError)) throw err;
  console.error(`build-image: ${err.message}`);
  process.exit(1);
}
