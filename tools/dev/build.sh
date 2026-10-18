#!/usr/bin/env bash
#
# tools/dev/build.sh: build the glue of one image out of tree, in the pinned
# toolchain image (EPIC-00 STORY-01). Modelled on romemul-switcher-rom's.
#
# Usage: tools/dev/build.sh <st|ste> <release|debug|test|debug-test>
#
# Output goes to build/<machine>-<type>/ (git-ignored): GLUE.BIN, the glue
# image that starts at the ROM base, its link map GLUE.MAP, and the objects
# under obj/. No Mac ROM is involved here; tools/jackrom.py merges one in.
set -euo pipefail

usage() {
  echo "usage: $0 <st|ste> <release|debug|test|debug-test>" >&2
  exit 2
}
[[ $# -eq 2 ]] || usage
machine=$1
type=$2

case "$type" in
  release) debug=0 test=0 ;;
  debug) debug=1 test=0 ;;
  test) debug=0 test=1 ;;
  debug-test) debug=1 test=1 ;;
  *) echo "unknown build type '$type'" >&2; usage ;;
esac

case "$machine" in
  st) base=0x00FC0000UL ;;
  ste) base=0x00E00000UL ;;
  *) echo "unknown machine '$machine'" >&2; usage ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
cd "$repo"
source "$here/toolchain.sh"

build_id="$(jack_build_id "$repo" "$type")"
out="build/$machine-$type"   # relative to the repository root
echo "build id: $build_id"

make_args="DEBUG=$debug TEST=$test ROM_BASE_ADDR_UL=$base BUILD_ID=$build_id BUILD_DIR=/work/$out/obj"

# The folder is emptied inside the container, on the same side of Docker's
# file sharing as the compiler (romemul-switcher-rom EPIC-00 STORY-01).
docker run --rm --platform linux/x86_64 -v "$repo:/work" -w /work \
  --user "$(id -u):$(id -g)" --entrypoint bash "$ATARI_IMAGE" \
  -c "rm -rf ./$out && mkdir -p ./$out/obj && make -s -C src/glue $make_args && cp ./$out/obj/GLUE.BIN ./$out/obj/GLUE.MAP ./$out/"

echo "built: $out/GLUE.BIN ($(wc -c < "$out/GLUE.BIN" | tr -d ' ') bytes)"
