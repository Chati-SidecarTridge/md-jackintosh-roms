# tools/dev/toolchain.sh: sourced by every build script. The pinned toolchain
# and the build ID, kept in one place (EPIC-00 STORY-01). The same arrangement
# as romemul-switcher-rom's tools/dev/toolchain.sh.

# The glue and mac_rom both build with the Atari GCC toolchain (GCC 4.6.4,
# binutils 2.30): Diego's atarist-toolkit-docker, pinned by tag and digest
# (D-10). Its entrypoint changes directory, so every call passes --entrypoint.
ATARI_IMAGE="logronoide/atarist-toolkit-docker-x86_64:1.4.0@sha256:016434131cf53f627c365e55262845fba53f30a7de87a1f122d58f8fa6aeeebe"

# The paths whose changes make a build "dirty".
JACK_BUILD_ID_PATHS=(src Makefile version.txt)

# jack_build_id <repo> <release|debug|test|debug-test>: <sha7>, then
# -dirty.<diff7> when the paths above differ from HEAD (untracked files
# included), then +debug and +test. Computed on the host and passed to make
# as BUILD_ID, so it does not depend on git inside a container.
jack_build_id() {
  local repo=$1 type=$2 id dirty
  id="$(git -C "$repo" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)"
  dirty="$(git -C "$repo" status --porcelain --untracked-files=all -- "${JACK_BUILD_ID_PATHS[@]}" 2>/dev/null)"
  if [[ -n "$dirty" ]]; then
    id+="-dirty.$(
      {
        git -C "$repo" diff HEAD -- "${JACK_BUILD_ID_PATHS[@]}"
        git -C "$repo" ls-files --others --exclude-standard -z -- "${JACK_BUILD_ID_PATHS[@]}" \
          | sort -z | while IFS= read -r -d '' f; do printf '%s\n' "$f"; cat "$repo/$f"; done
      } | shasum | cut -c1-7
    )"
  fi
  case "$type" in
    debug) id+="+debug" ;;
    test) id+="+test" ;;
    debug-test) id+="+debug+test" ;;
  esac
  printf '%s' "$id"
}
