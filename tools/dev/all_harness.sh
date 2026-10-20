#!/usr/bin/env bash
#
# tools/dev/all_harness.sh: build the release and debug glue of both images
# and run every harness session, one line per run, then an overall PASS or
# FAIL (EPIC-00 STORY-05). From romemul-switcher-rom's. Sequential: the
# machine's CPU is the limit anyway.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here/../.."

failed=0
start=$SECONDS
mkdir -p build/harness
# The release glue too: trace-only code compiles out there, and a release
# build must keep linking. debug-test carries the RAM disk (EPIC-03).
for machine in st ste; do
  for type in release debug debug-test; do
    log="build/harness/build-$machine-$type.log"
    if ! tools/dev/build.sh "$machine" "$type" > "$log" 2>&1; then
      echo "FAIL build $machine $type (see $log)"
      failed=1
    fi
  done
done
[[ $failed -eq 0 ]] || { echo "FAIL: a build failed, no session run"; exit 1; }

runs=(
  "--image st --memsize 1024"
  "--image st --memsize 2560"
  "--image st --memsize 4096"
  "--image ste --memsize 1024"
  "--image ste --memsize 4096"
  "--image st --corrupt"
  "--image ste --corrupt"
  "--image st --memsize 4096 --type debug-test --disk @system608 --input"
  "--image st --memsize 2560 --type debug-test --disk @system608"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --input"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fixed --partition"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --launch 85,56 --then keys:Diego --then click:98,458 --then wait:3 --then keys:Hello --then cmd:s --then wait:3 --then keys:Jackintosh\\n --then wait:4 --then shot:saved"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --swap @excel103 --then click:105,600 --then cmd:e --then wait:6 --then shot:swapped"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @norton111 --launch 82,50 --then click:116,170 --then wait:5 --then shot:disk-doctor"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart valid-at=2,zone=3600"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart id=0x12340001"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --input"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fixed --fake-cart"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --swap @excel103 --fake-cart --then click:105,600 --then cmd:e --then wait:6 --then shot:swapped"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --then fake-restart:5 --then wait:4 --then click:45,600 --then cmd:o --then wait:3 --then shot:after"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --then fake-down --then click:45,600 --then cmd:o --then wait:8 --then fake-up --then wait:6 --then shot:after"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --then wait:3 --then shot:desk"
  "--image st --memsize 4096 --type debug-test --fake-cart card=1"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --fake-tree /jackintosh/SystemTools.dsk.rw=0 --fake-tree /jackintosh/apps/Word.dsk.rw=1 --fake-set 1=/jackintosh/SystemTools.dsk.rw --fake-set 11= --menu b --menu return --menu down --menu space --menu shot:after --menu e --then wait:3 --then shot:desk"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --fake-tree /jackintosh/HD.dsk=0 --fake-tree /jackintosh/Word.dsk.rw=1 --fake-set 12=/jackintosh/HD.dsk --fake-set 1= --fake-set 11= --menu ctrl+a --menu 2 --menu down --menu space --menu shot:slots --menu m --menu e --then wait:3 --then select --then wait:4 --then shot:selected"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --fake-tree /jackintosh/SystemTools.dsk.rw=0 --fake-tree /macos/Word.dsk.rw=1 --fake-set 1=/jackintosh/SystemTools.dsk.rw --fake-set 11= --menu l --menu down --menu space --menu shot:main --menu e"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --fake-tree /jackintosh/HD.dsk=0 --fake-tree /jackintosh/Word.dsk.rw=1 --fake-set 12=/jackintosh/HD.dsk --fake-set 1=/jackintosh/Word.dsk.rw --fake-set 11= --then wait:4 --then shot:fd0"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --menu u --menu type:2 --menu return --menu h --menu type:time.apple.com --menu return --menu p --menu type:abc --menu return --menu r --menu shot:rtc --menu e"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart valid-at=99 --fake-set 13=false"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --menu o --menu c --menu r --menu shot:options --menu esc --menu e"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 17=2048 --then wait:2 --then shot:mac2m"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 16=5 --menu shot:five --menu e"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 18=double --then rclick:45,600 --then wait:3 --then shot:rdouble"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 19=512x342 --then click:45,472 --then cmd:o --then wait:3 --then shot:plus"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 20=fr --menu q --menu shot:fd0 --menu esc --menu e --launch 155,48 --then wait:2 --then st:10,s10,03,08,0A,0B,28,07,s0C,s29,1A,12,s1A,17,a1A,60,s60,02,s02,32,s32,2C,1E,a12,12 --then wait:4 --then shot:fr"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 20=de --launch 155,48 --then wait:2 --then st:0C,1A,s1A,27,28,s28,15,2C,s03,s04,a1A,a27,a28,29,2B,s0D,s29 --then wait:4 --then shot:de"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 21=10 --menu o --menu t --menu shift+t --menu shift+t --menu shot:mouse --menu esc --menu e --then click:200,300 --then move:16,16=240,340"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 21=1 --then click:200,300 --then move:16,-16=204,296 --then move:1,1=204,296 --then move:1,1=204,296 --then move:1,1=204,296 --then move:1,1=205,297"
  "--image st --monitor rgb"
  "--image ste --monitor rgb"
)
for run in "${runs[@]}"; do
  line="$(python3 tools/dev/hatari_harness.py $run 2>&1 | tail -1)"
  echo "${line/ (*\/build\/harness\// (build/harness/}"
  [[ "$line" == PASS* ]] || failed=1
done

if [[ $failed -eq 0 ]]; then
  echo "PASS: ${#runs[@]} runs in $((SECONDS - start)) s"
else
  echo "FAIL: at least one run failed"
fi
exit $failed
