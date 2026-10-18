#!/usr/bin/env python3
"""tools/jackrom.py: merge the glue and a Mac Plus v3 ROM into a boot image
(EPIC-00 STORY-03), on the user's computer (D-02).

The image (D-09): the glue at the ROM base, the 128 KB Mac ROM after it.

  st   192 KB at $FC0000: glue $FC0000-$FCFFFF, Mac ROM $FD0000-$FEFFFF
  ste  256 KB at $E00000: glue $E00000-$E1FFFF, Mac ROM $E20000-$E3FFFF

The free space after the glue is a computed pattern, never zeros, and the
last long of the glue area holds the image checksum: the 16-bit big-endian
words of the whole image except that long, summed into 32 bits. The glue
checks it at boot.

Usage:
  tools/jackrom.py merge --machine st|ste --glue GLUE.BIN [--mac-rom FILE] -o IMAGE

Without --mac-rom the ROM comes from tools/macrom.py path (tools/dev/dev.cfg,
else the mac_rom submodule build).
"""

import argparse
import hashlib
import os
import struct
import sys

import macrom

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

MACHINES = {
    # machine: (ROM base, glue size, Hatari version word)
    "st": (0x00FC0000, 0x10000, 0x0104),
    "ste": (0x00E00000, 0x20000, 0x0206),
}
CHECKSUM_SIZE = 4


class JackRomError(Exception):
    pass


def fill_word(offset):
    """The word written at an even offset of the free space, mixed from the
    offset. romemul-switcher-rom's fill_word (scripts/finalize_rom.py), so a
    stuck address line cannot pass for good data. The glue's checks mirror it."""
    m = 0xFFFFFFFF
    x = (offset ^ 0x9E3779B9) & m
    x = (x + (x << 10)) & m
    x ^= x >> 6
    x = (x + (x << 3)) & m
    x ^= x >> 11
    x = (x + (x << 15)) & m
    return (x ^ (x >> 16)) & 0xFFFF


def fill_pattern(image, offset, length):
    for o in range(offset, offset + length):
        word = fill_word(o & ~1)
        image[o] = (word >> 8) if (o & 1) == 0 else (word & 0xFF)


def image_checksum(image, field):
    """The 16-bit big-endian words of image, except the long at field, summed
    into 32 bits."""
    data = bytes(image[:field]) + bytes(image[field + CHECKSUM_SIZE:])
    return sum(struct.unpack(f">{len(data) // 2}H", data)) & 0xFFFFFFFF


def check_glue(glue, machine):
    """Refuse a glue built for the other machine or too big for its area."""
    base, glue_size, version = MACHINES[machine]
    if len(glue) < 12:
        raise JackRomError(f"the glue is {len(glue)} bytes: no reset header")
    head_version, = struct.unpack(">H", glue[2:4])
    head_base, = struct.unpack(">I", glue[8:12])
    if head_base != base or head_version != version:
        raise JackRomError(
            f"the glue's header says base ${head_base:06X}, version "
            f"${head_version:04X}; the {machine} image needs ${base:06X}, "
            f"${version:04X} (built for the other machine?)")
    limit = glue_size - CHECKSUM_SIZE
    if len(glue) > limit:
        raise JackRomError(
            f"the glue is {len(glue)} bytes; the {machine} image has room for {limit}")


def apply_patches(image, machine):
    """The patch table (EPIC-01 STORY-05). Empty until then; returns the
    number of patches applied."""
    del image, machine
    return 0


def check_output_path(path):
    """D-02: a merged image holds Apple's code, so it never lands in the
    repository, except under the git-ignored build/."""
    out = os.path.realpath(path)
    repo = os.path.realpath(REPO)
    build = os.path.join(repo, "build")
    if (out == repo or out.startswith(repo + os.sep)) and not out.startswith(build + os.sep):
        raise JackRomError(
            f"{path} is inside the repository; write images under build/ or "
            "outside the repository (D-02)")


def merge(machine, glue_path, mac_rom_path, out_path):
    base, glue_size, _ = MACHINES[machine]
    check_output_path(out_path)
    with open(glue_path, "rb") as handle:
        glue = handle.read()
    check_glue(glue, machine)
    mac_rom_path = mac_rom_path or macrom.rom_path()
    with open(mac_rom_path, "rb") as handle:
        mac_rom = handle.read()
    try:
        macrom.check_rom(mac_rom)
    except macrom.MacRomError as err:
        raise JackRomError(f"{mac_rom_path}: {err}") from None

    image = bytearray(glue_size + macrom.ROM_SIZE)
    image[:len(glue)] = glue
    field = glue_size - CHECKSUM_SIZE
    fill_pattern(image, len(glue), field - len(glue))
    image[glue_size:] = mac_rom
    patches = apply_patches(image, machine)
    checksum = image_checksum(image, field)
    image[field:glue_size] = struct.pack(">I", checksum)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as handle:
        handle.write(image)
    print(f"image: {out_path}")
    print(f"  {machine}, {len(image) // 1024} KB at ${base:06X}, Mac ROM at "
          f"${base + glue_size:06X}")
    print(f"  glue {len(glue)} of {field} bytes ({field - len(glue)} free), "
          f"{patches} patches")
    print(f"  checksum ${checksum:08X}, SHA-256 {hashlib.sha256(image).hexdigest()}")


def main(argv):
    parser = argparse.ArgumentParser(
        prog="jackrom.py", description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("merge", help="merge the glue and a Mac Plus v3 ROM")
    m.add_argument("--machine", required=True, choices=sorted(MACHINES))
    m.add_argument("--glue", required=True, help="GLUE.BIN from tools/dev/build.sh")
    m.add_argument("--mac-rom", help="the Mac Plus v3 ROM (default: tools/macrom.py path)")
    m.add_argument("-o", "--output", required=True, help="the image to write")
    args = parser.parse_args(argv[1:])
    try:
        merge(args.machine, args.glue, args.mac_rom, args.output)
        return 0
    except (JackRomError, macrom.MacRomError, OSError) as err:
        print(f"jackrom: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
