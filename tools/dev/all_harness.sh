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
  "--image ste --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --launch 85,56 --then keys:Diego --then click:98,458 --then wait:3 --then keys:Hello --then cmd:s --then wait:3 --then keys:Jackintosh\\n --then wait:4 --then shot:saved --then expect:@@\sdisk2\swrite"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --swap @excel103 --then click:105,600 --then cmd:e --then wait:6 --then shot:swapped"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @norton111 --launch 82,50 --then click:116,170 --then wait:5 --then shot:disk-doctor --then dark:370,130,430,190,0,20"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart valid-at=2,zone=3600"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart id=0x12340001"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --input"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fixed --fake-cart"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --swap @excel103 --fake-cart --then click:105,600 --then cmd:e --then wait:6 --then shot:swapped"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --then fake-restart:5 --then wait:4 --then click:45,600 --then cmd:o --then wait:3 --then shot:after --then dark:200,75,260,130,0,20"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --then fake-down --then click:45,600 --then cmd:o --then wait:8 --then fake-up --then wait:6 --then shot:after --then expect:@@\sdisk\scart\swait --then expect:@@\sdisk\scart\sback --then dark:200,75,260,130,0,20"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --then wait:3 --then shot:desk --then expect:@@\sdisk\sdrive\s3\stype\s8\sblocks\s3200 --then expect:@@\sdisk2\sinserted"
  "--image st --memsize 4096 --type debug-test --fake-cart card=1"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --fake-tree /jackintosh/SystemTools.dsk.rw=0 --fake-tree /jackintosh/apps/Word.dsk.rw=1 --fake-set 1=/jackintosh/SystemTools.dsk.rw --fake-set 11= --menu b --menu return --menu down --menu space --menu shot:after --menu e --then wait:3 --then shot:desk --then expect:@@\smenu\sset\s11\s/jackintosh/apps/Word.dsk.rw\s0 --then expect:@@\sdisk2\sinserted"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --fake-tree /jackintosh/HD.dsk=0 --fake-tree /jackintosh/Word.dsk.rw=1 --fake-set 12=/jackintosh/HD.dsk --fake-set 1= --fake-set 11= --menu ctrl+a --menu 2 --menu down --menu space --menu shot:slots --menu m --menu e --then wait:3 --then select --then wait:4 --then shot:selected --then expect:@@\smenu\sset\s2\s/jackintosh/Word.dsk.rw\s0 --then expect:@@\sfake\sselect\sslot\s2 --then expect:@@\sdisk\scart\sunit\s0\sinserted\s1600 --then expect:@@\sdisk1\sinserted"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --fake-tree /jackintosh/HD.dsk=0 --fake-tree /jackintosh/A.dsk.rw=1 --fake-tree /jackintosh/B.dsk.rw=1 --fake-set 12=/jackintosh/HD.dsk --fake-set 1=/jackintosh/A.dsk.rw --fake-set 2=/jackintosh/B.dsk.rw --fake-set 11= --then wait:3 --then click:105,600 --then cmd:e --then wait:3 --then select --then wait:4 --then expect:@@\sfake\sselect\sslot\s2 --then fake-restart:3 --then wait:3 --then expect:@@\sdisk\scart\sunit\s0\sother\simage --then shot:other"
  "--image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --fake-cart --fake-tree /jackintosh/SystemTools.dsk.rw=0 --fake-tree /macos/Word.dsk.rw=1 --fake-set 1=/jackintosh/SystemTools.dsk.rw --fake-set 11= --menu l --menu down --menu space --menu shot:main --menu e --then expect:@@\smenu\sset\s0\s/macos\s0"
  "--image ste --memsize 4096 --type debug-test --disk build/hd/hd0-small.raw --fixed --disk2 @word40a --fake-cart --fake-tree /jackintosh/HD.dsk=0 --fake-tree /jackintosh/Word.dsk.rw=1 --fake-set 12=/jackintosh/HD.dsk --fake-set 1=/jackintosh/Word.dsk.rw --fake-set 11= --then wait:4 --then shot:fd0 --then expect:@@\sdisk1\sinserted"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --menu u --menu type:2 --menu return --menu h --menu type:time.apple.com --menu return --menu p --menu type:abc --menu return --menu r --menu shot:rtc --menu e --then expect:@@\smenu\soffset\s2 --then expect:@@\smenu\sset\s14\stime.apple.com\s0 --then expect:@@\smenu\sset\s13\sfalse\s0"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart valid-at=99 --fake-set 13=false"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --menu o --menu c --menu r --menu shot:options --menu esc --menu e --then expect:@@\smenu\sset\s16\s10\s0 --then expect:@@\smenu\sset\s18\scmd\s0"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 17=2048 --then wait:2 --then shot:mac2m"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 16=5 --menu expect:@@\smenu\scountdown\s5 --menu shot:five --menu expect:@@\smenu\sboot"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 18=double --then rclick:45,600 --then wait:3 --then shot:rdouble --then dark:200,75,260,130,0,20"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 19=512x342 --then click:45,472 --then cmd:o --then wait:3 --then shot:plus"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 20=fr --menu q --menu shot:fd0 --menu esc --menu e --launch 155,48 --then wait:2 --then st:10,s10,03,08,0A,0B,28,07,s0C,s29,1A,12,s1A,17,a1A,60,s60,02,s02,32,s32,2C,1E,a12,12 --then wait:4 --then shot:fr --then mackeys:01,81,71,01,81,f1,75,1d,9d,f5,1d,9d,75,65,e5,f5,1d,9d,75,11,91,f5,75,65,e5,f5,01,81,75,65,e5,f5,41,c1,75,2d,ad,f5,71,75,39,b9,f5,f1,71,f1,75,29,a9,71,f5,f1,75,45,c5,f5,1d,9d,71,f1,75,41,c1,71,f5,f1,45,c5,75,f5,43,c3,75,f5,71,57,d7,f1,71,5f,df,f1,71,35,b5,f1,71,f1,25,a5,71,f1,57,d7,71,59,d9,f1,1b,9b,19,99,75,1d,9d,f5,1d,9d"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 20=de --launch 155,48 --then wait:2 --then st:0C,1A,s1A,27,28,s28,15,2C,s03,s04,a1A,a27,a28,29,2B,s0D,s29 --then wait:4 --then shot:de --then mackeys:75,03,83,f5,75,41,c1,f5,41,c1,71,f1,75,41,c1,71,f5,41,c1,f1,75,41,c1,f5,3f,bf,75,41,c1,f5,01,81,71,f1,75,41,c1,71,f5,01,81,f1,0d,8d,21,a1,71,4f,cf,f1,71,f1,75,2d,ad,71,f5,f1,75,71,f5,27,a7,f1,75,f5,75,f5,43,c3,75,f5,75,f5,3d,bd,75,f5,71,29,a9,f1,71,65,e5,f1,71,f1,65,e5,71,f1,71,2d,ad,f1"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 21=10 --menu o --menu t --menu shift+t --menu shift+t --menu shot:mouse --menu esc --menu e --then click:200,300 --then move:16,16=240,340"
  "--image st --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 21=1 --then click:200,300 --then move:16,-16=204,296 --then move:1,1=204,296 --then move:1,1=204,296 --then move:1,1=204,296 --then move:1,1=205,297"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --fake-cart --fake-set 22=16c --input"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --disk2 @tetris1 --sound --launch 85,40 --then wait:3 --then click:200,300 --then wait:3 --then rec --then click:243,290 --then wait:6 --then heard"
  "--image ste --memsize 4096 --type debug-test --disk @system608 --sound --then wait:2 --then press:10,20 --then release:75,70 --then wait:3 --then rec --then press:228,459 --then release:180,459 --then wait:3 --then heard"
  "--image st --monitor rgb"
  "--image ste --monitor rgb"
)
# "all_harness.sh megaste" (EPIC-14): the ste runs only, on Hatari's Mega STE
# with 4 MB, the common size; "megaste16" also at 16 MHz with the cache, the
# runs without the fake's menu through --megaste-ctrl.
if [[ "${1:-}" == megaste* ]]; then
  mega=()
  for run in "${runs[@]}"; do
    [[ "$run" == "--image ste"* ]] || continue
    run="$(sed -E 's/ --memsize [0-9]+//' <<< "$run")"
    run="${run/--image ste/--image ste --machine megaste --memsize 4096}"
    if [[ "$1" == megaste16 ]]; then
      if [[ "$run" == *--fake-cart* ]]; then
        [[ "$run" == *"--fake-set 22="* ]] || run+=" --fake-set 22=16c"
      else
        run+=" --megaste-ctrl 3"
      fi
    fi
    mega+=("$run")
  done
  runs=("${mega[@]}")
fi
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
