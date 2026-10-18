#!/usr/bin/env python3
"""tools/macrom.py: the Mac Plus v3 ROM as an input (EPIC-00 STORY-02).

The ROM is Apple's code and never enters the repository (D-02). It comes from
one of two places:

- a dump of the user's own, named in tools/dev/dev.cfg ([macrom] file), or
- the mac_rom submodule (external/mac_rom, pinned by commit, D-10), assembled
  in the pinned toolchain image into build/macrom/macplus_v3.rom.

Either way it must be the Plus v3 ROM: 131072 bytes, stored and computed
checksum $4D1F8172, version $0075, and its known SHA-256.

Usage:
  tools/macrom.py build          assemble the submodule's Plus v3, then check it
  tools/macrom.py check <file>   check a ROM file
  tools/macrom.py path           print the ROM the build uses (building it if needed)
"""

import configparser
import hashlib
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SUBMODULE = os.path.join("external", "mac_rom")
BUILT_ROM = os.path.join("build", "macrom", "macplus_v3.rom")
DEV_CFG = os.path.join(HERE, "dev", "dev.cfg")

ROM_SIZE = 0x20000
ROM_CHECKSUM = 0x4D1F8172
ROM_VERSION = 0x0075
# The Plus v3 ROM is one known file: the archive.org reference and the mac_rom
# build are byte-identical (EPIC-00 STORY-02). The additive checksum alone can
# be fooled by two compensating changes.
ROM_SHA256 = "dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7"
ROM_TARGET_PLUS_V3 = 5  # mac_rom include/rom_target.inc


class MacRomError(Exception):
    pass


def rom_checksum(data):
    """The Mac ROM checksum: the 16-bit big-endian words from offset 4, summed
    into 32 bits."""
    words = struct.unpack(f">{(len(data) - 4) // 2}H", data[4:])
    return sum(words) & 0xFFFFFFFF


def check_rom(data):
    """Raise MacRomError unless data is the Plus v3 ROM; return its SHA-256."""
    if len(data) != ROM_SIZE:
        raise MacRomError(f"size is {len(data)} bytes, not {ROM_SIZE}")
    stored = struct.unpack(">I", data[:4])[0]
    computed = rom_checksum(data)
    version = struct.unpack(">H", data[8:10])[0]
    if stored != ROM_CHECKSUM or computed != ROM_CHECKSUM:
        raise MacRomError(
            f"checksum stored ${stored:08X}, computed ${computed:08X}; "
            f"the Plus v3 ROM has ${ROM_CHECKSUM:08X}")
    if version != ROM_VERSION:
        raise MacRomError(f"version ${version:04X}, not ${ROM_VERSION:04X}")
    digest = hashlib.sha256(data).hexdigest()
    if digest != ROM_SHA256:
        raise MacRomError(
            f"checksum and version match, but the SHA-256 is {digest}, not the "
            f"Plus v3 ROM's {ROM_SHA256}")
    return digest


def check_file(path):
    with open(path, "rb") as handle:
        data = handle.read()
    try:
        return check_rom(data)
    except MacRomError as err:
        raise MacRomError(f"{path}: {err}") from None


def atari_image():
    """The pinned toolchain image, read from tools/dev/toolchain.sh so it is
    named in one place."""
    with open(os.path.join(HERE, "dev", "toolchain.sh"), encoding="utf-8") as handle:
        match = re.search(r'^ATARI_IMAGE="([^"]+)"', handle.read(), re.M)
    if not match:
        raise MacRomError("ATARI_IMAGE not found in tools/dev/toolchain.sh")
    return match.group(1)


def check_submodule():
    """Refuse a missing submodule, or one away from its pinned commit."""
    status = subprocess.run(
        ["git", "-C", REPO, "submodule", "status", SUBMODULE],
        capture_output=True, text=True, check=False).stdout
    if not status or status[0] == "-":
        raise MacRomError(
            f"{SUBMODULE} is not checked out: git submodule update --init {SUBMODULE}")
    if status[0] == "+":
        raise MacRomError(
            f"{SUBMODULE} is not at its pinned commit: "
            f"git submodule update {SUBMODULE}")


def build():
    """Assemble the submodule's Plus v3 into build/macrom/; return the path."""
    check_submodule()
    out_dir = os.path.dirname(BUILT_ROM)
    obj = os.path.join(out_dir, "macplus_v3.o")
    # The folder is emptied and written inside one container, on the same
    # side of Docker's file sharing as the assembler (CLAUDE.md).
    script = (
        f"rm -rf /work/{out_dir} && mkdir -p /work/{out_dir} && "
        f"m68k-atari-mint-as -m68000 --register-prefix-optional -Iinclude "
        f"--defsym ROM_TARGET={ROM_TARGET_PLUS_V3} -o /work/{obj} rom_plus.s && "
        f"m68k-atari-mint-objcopy -O binary /work/{obj} /work/{BUILT_ROM}")
    subprocess.run(
        ["docker", "run", "--rm", "--platform", "linux/x86_64",
         "-v", f"{REPO}:/work", "-w", f"/work/{SUBMODULE}",
         "--user", f"{os.getuid()}:{os.getgid()}",
         "--entrypoint", "/bin/sh", atari_image(), "-c", script],
        check=True)
    return os.path.join(REPO, BUILT_ROM)


def configured_rom():
    """The user's own dump from tools/dev/dev.cfg, or None."""
    if not os.path.exists(DEV_CFG):
        return None
    cfg = configparser.ConfigParser()
    cfg.read(DEV_CFG)
    path = cfg.get("macrom", "file", fallback="").strip()
    return os.path.abspath(os.path.expanduser(path)) if path else None


def rom_path():
    """The checked ROM the build uses: dev.cfg's dump, else the mac_rom build
    (assembled when missing)."""
    path = configured_rom()
    if path is None:
        path = os.path.join(REPO, BUILT_ROM)
        if not os.path.exists(path):
            build()
    check_file(path)
    return path


def main(argv):
    try:
        if len(argv) == 2 and argv[1] == "build":
            path = build()
        elif len(argv) == 3 and argv[1] == "check":
            path = argv[2]
        elif len(argv) == 2 and argv[1] == "path":
            print(rom_path())
            return 0
        else:
            print(__doc__.strip().split("\n\n")[-1], file=sys.stderr)
            return 2
        print(f"ok: {path} is the Mac Plus v3 ROM, SHA-256 {check_file(path)}")
        return 0
    except (MacRomError, OSError, subprocess.CalledProcessError) as err:
        print(f"macrom: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
