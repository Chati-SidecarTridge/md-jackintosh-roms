#!/bin/bash
# The page's core against tools/jackrom.py merge, both machines, byte for
# byte (EPIC-11 STORY-03). Needs the release glue (tools/dev/build.sh MACHINE
# release), a Mac Plus v3 ROM (tools/macrom.py path) and Node 22, here or in
# Docker (NODE="docker run --rm -v $REPO:$REPO -w $REPO/site node:22 node").
# Runs locally only: it reads Apple's ROM (D-02).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NODE="${NODE:-node}"
ROM="${ROM:-$(python3 "$REPO/tools/macrom.py" path)}"
OUT="$REPO/build/site-parity"
mkdir -p "$OUT"
cp "$ROM" "$OUT/rom.bin"
for machine in st ste; do
  python3 "$REPO/tools/jackrom.py" export --machine "$machine" \
    --glue "$REPO/build/$machine-release/GLUE.BIN" -o "$OUT/$machine.json" > /dev/null
  python3 "$REPO/tools/jackrom.py" merge --machine "$machine" \
    --glue "$REPO/build/$machine-release/GLUE.BIN" --mac-rom "$OUT/rom.bin" -o "$OUT/merge-$machine.img" > /dev/null
  $NODE tools/build-image.mjs --rom "$OUT/rom.bin" --data "$OUT/$machine.json" -o "$OUT/page-$machine.img" > /dev/null
  cmp "$OUT/merge-$machine.img" "$OUT/page-$machine.img"
  echo "parity $machine: identical ($(shasum -a 256 "$OUT/page-$machine.img" | cut -c1-16))"
done
