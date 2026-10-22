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

The patch table (src/patches/plus_v3.ini) is applied to the Mac ROM part;
glue symbols come from GLUE.MAP next to GLUE.BIN.

Usage:
  tools/jackrom.py merge --machine st|ste --glue GLUE.BIN [--mac-rom FILE] -o IMAGE
  tools/jackrom.py hash --offset N --length N [--mac-rom FILE]
  tools/jackrom.py patches

Without --mac-rom the ROM comes from tools/macrom.py path (tools/dev/dev.cfg,
else the mac_rom submodule build).
"""

import argparse
import configparser
import hashlib
import base64
import json
import os
import re
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


PATCH_TABLE = os.path.join(REPO, "src", "patches", "plus_v3.ini")
NOP = b"\x4e\x71"
OPCODES = {"jmp": b"\x4e\xf9", "jsr": b"\x4e\xb9"}  # absolute long


def glue_symbols(glue_map):
    """Every symbol of the glue's link map (GLUE.MAP), as {name: address}."""
    symbols = {}
    with open(glue_map, encoding="latin-1") as handle:
        for line in handle:
            match = re.match(r"^\s+0x([0-9a-fA-F]+)\s+([A-Za-z_][\w.]*)\s*$", line)
            if match:
                symbols[match.group(2)] = int(match.group(1), 16)
    return symbols


def load_patches(path=PATCH_TABLE):
    """The patch table (EPIC-01 STORY-05): one section per patch, with an
    offset into the Mac ROM, a length, the SHA-256 of the bytes it replaces
    (the hash, never the bytes: D-02), the replacement, the reason, the
    mac_rom source and whether it is enabled. Checked here, applied by
    apply_patches()."""
    cfg = configparser.ConfigParser(interpolation=None)
    try:
        if not cfg.read(path):
            raise JackRomError(f"the patch table {path} is missing")
    except configparser.Error as err:
        raise JackRomError(f"the patch table {path}: {err}") from None
    patches = []
    for name in cfg.sections():
        sec = cfg[name]
        try:
            patch = {
                "name": name,
                "offset": int(sec["offset"], 0),
                "length": int(sec["length"], 0),
                "sha256": sec["sha256"].strip().lower(),
                "replace": sec["replace"].split(),
                "source": sec["source"],
                "reason": sec["reason"],
                "enabled": sec.getboolean("enabled", True),
            }
        except (KeyError, ValueError) as err:
            raise JackRomError(f"patch {name}: bad or missing field {err}") from None
        off, length = patch["offset"], patch["length"]
        if off % 2 or length % 2 or length <= 0 or off + length > macrom.ROM_SIZE:
            raise JackRomError(
                f"patch {name}: offset ${off:X} length {length} must be even and "
                "inside the Mac ROM")
        if not re.fullmatch(r"[0-9a-f]{64}", patch["sha256"]):
            raise JackRomError(f"patch {name}: sha256 must be 64 hex digits")
        patches.append(patch)
    enabled = sorted((p for p in patches if p["enabled"]), key=lambda p: p["offset"])
    for a, b in zip(enabled, enabled[1:]):
        if a["offset"] + a["length"] > b["offset"]:
            raise JackRomError(f"patches {a['name']} and {b['name']} overlap")
    return patches


def patch_value(token, symbols, rom_base, name):
    """A glue symbol (with or without its leading underscore), rom+<offset>
    (that Mac ROM offset in this image), or a number."""
    if token.startswith("rom+"):
        return rom_base + int(token[4:], 0)
    for candidate in (token, "_" + token):
        if candidate in symbols:
            return symbols[candidate]
    try:
        return int(token, 0)
    except ValueError:
        raise JackRomError(f"patch {name}: unknown symbol {token}") from None


def patch_bytes(patch, symbols, rom_base):
    """The replacement bytes, exactly the patch's length: nop | jmp X | jsr X
    (padded with NOPs) | long X | word X | bytes <hex>."""
    name, length, words = patch["name"], patch["length"], patch["replace"]
    if not words:
        raise JackRomError(f"patch {name}: replace is empty")
    try:
        out = replacement_bytes(name, length, words, symbols, rom_base)
    except (struct.error, ValueError) as err:
        raise JackRomError(f"patch {name}: cannot read replace = {' '.join(words)} ({err})") from None
    if len(out) != length:
        raise JackRomError(f"patch {name}: the replacement is {len(out)} bytes, not {length}")
    return out


def replacement_bytes(name, length, words, symbols, rom_base):
    kind, args = words[0], words[1:]
    if kind == "nop" and not args:
        out = NOP * (length // 2)
    elif kind in OPCODES and len(args) == 1:
        out = OPCODES[kind] + struct.pack(">I", patch_value(args[0], symbols, rom_base, name))
        if len(out) > length:
            raise JackRomError(f"patch {name}: {kind} needs 6 bytes, the patch has {length}")
        out += NOP * ((length - len(out)) // 2)
    elif kind == "long" and len(args) == 1:
        out = struct.pack(">I", patch_value(args[0], symbols, rom_base, name))
    elif kind == "word" and len(args) == 1:
        out = struct.pack(">H", patch_value(args[0], symbols, rom_base, name) & 0xFFFF)
    elif kind == "bytes" and args:
        out = bytes.fromhex("".join(args))
    else:
        raise JackRomError(f"patch {name}: cannot read replace = {' '.join(words)}")
    return out


def apply_patches(image, machine, mac_rom, patches, symbols):
    """Apply every enabled patch to the Mac ROM part of the image, after
    checking that the ROM holds the bytes each one expects. Returns the number
    applied."""
    base, glue_size, _ = MACHINES[machine]
    rom_base = base + glue_size
    count = 0
    for patch in patches:
        if not patch["enabled"]:
            continue
        off, length = patch["offset"], patch["length"]
        found = hashlib.sha256(mac_rom[off:off + length]).hexdigest()
        if found != patch["sha256"]:
            raise JackRomError(
                f"patch {patch['name']}: the Mac ROM differs at ${off:05X} "
                f"(sha256 {found[:16]}…, expected {patch['sha256'][:16]}…)")
        image[glue_size + off:glue_size + off + length] = patch_bytes(patch, symbols, rom_base)
        count += 1
    return count


def check_patch_table(path=PATCH_TABLE):
    """Every entry, enabled or not: its hash against the ROM, and overlaps
    with any other entry (planned patches must fit together too). Prints one
    line per entry; returns 0 when all hold."""
    with open(macrom.rom_path(), "rb") as handle:
        rom = handle.read()
    patches = sorted(load_patches(path), key=lambda p: p["offset"])
    bad = 0
    for i, patch in enumerate(patches):
        off, length = patch["offset"], patch["length"]
        notes = []
        if hashlib.sha256(rom[off:off + length]).hexdigest() != patch["sha256"]:
            notes.append("HASH MISMATCH")
        for other in patches[i + 1:]:
            if other["offset"] < off + length:
                notes.append(f"OVERLAPS {other['name']}")
        bad += bool(notes)
        print(f"${off:05X} {length:3d} {'on ' if patch['enabled'] else 'off'} "
              f"{patch['name']:32s} {' '.join(patch['replace']):28s} {'; '.join(notes)}")
    print(f"{len(patches)} patches, {sum(p['enabled'] for p in patches)} enabled, "
          f"{bad} with problems")
    return 1 if bad else 0


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


def merge(machine, glue_path, mac_rom_path, out_path, patch_table=PATCH_TABLE):
    base, glue_size, _ = MACHINES[machine]
    check_output_path(out_path)
    with open(glue_path, "rb") as handle:
        glue = handle.read()
    check_glue(glue, machine)
    # The glue's link map, written next to GLUE.BIN, resolves the symbols
    # patches jump to.
    glue_map = os.path.join(os.path.dirname(os.path.abspath(glue_path)), "GLUE.MAP")
    if not os.path.exists(glue_map):
        raise JackRomError(f"{glue_map} is missing: the patches need the glue's link map")
    patches = load_patches(patch_table)
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
    applied = apply_patches(image, machine, mac_rom, patches, glue_symbols(glue_map))
    checksum = image_checksum(image, field)
    image[field:glue_size] = struct.pack(">I", checksum)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as handle:
        handle.write(image)
    print(f"image: {out_path}")
    print(f"  {machine}, {len(image) // 1024} KB at ${base:06X}, Mac ROM at "
          f"${base + glue_size:06X}")
    print(f"  glue {len(glue)} of {field} bytes ({field - len(glue)} free), "
          f"{applied} patches ({len(patches) - applied} planned, not enabled)")
    print(f"  checksum ${checksum:08X}, SHA-256 {hashlib.sha256(image).hexdigest()}")


def export(machine, glue_path, out_path, patch_table=PATCH_TABLE):
    """The website's data for one machine (EPIC-11, D-20): what the page
    needs to merge a user's ROM in the browser exactly as merge() does, with
    no Apple byte (D-02): our glue, the image layout, the ROM checks (sizes,
    checksums, hashes) and each enabled patch with its replacement resolved
    against this glue's map (NOPs, our jumps, our values)."""
    base, glue_size, version = MACHINES[machine]
    with open(glue_path, "rb") as handle:
        glue = handle.read()
    check_glue(glue, machine)
    glue_map = os.path.join(os.path.dirname(os.path.abspath(glue_path)), "GLUE.MAP")
    if not os.path.exists(glue_map):
        raise JackRomError(f"{glue_map} is missing: the patches need the glue's link map")
    symbols = glue_symbols(glue_map)
    rom_base = base + glue_size
    patches = []
    for patch in sorted(load_patches(patch_table), key=lambda p: p["offset"]):
        if not patch["enabled"]:
            continue
        if patch["replace"][0] not in ("nop", "jmp", "jsr", "long", "word", "bytes"):
            raise JackRomError(f"patch {patch['name']}: unknown replacement kind")
        patches.append({
            "name": patch["name"],
            "offset": patch["offset"],
            "length": patch["length"],
            "sha256": patch["sha256"],
            "replace": patch_bytes(patch, symbols, rom_base).hex(),
        })
    with open(os.path.join(REPO, "version.txt"), encoding="utf-8") as handle:
        release = handle.read().strip()
    data = {
        "machine": machine,
        "release": release,
        "base": base,
        "glueSize": glue_size,
        "versionWord": version,
        "checksumSize": CHECKSUM_SIZE,
        "glue": base64.b64encode(glue).decode("ascii"),
        "glueSha256": hashlib.sha256(glue).hexdigest(),
        "rom": {
            "size": macrom.ROM_SIZE,
            "checksum": macrom.ROM_CHECKSUM,
            "version": macrom.ROM_VERSION,
            "sha256": macrom.ROM_SHA256,
        },
        "patches": patches,
    }
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=1)
        handle.write("\n")
    print(f"export: {out_path}: {machine}, glue {len(glue)} bytes, {len(patches)} patches")


def main(argv):
    parser = argparse.ArgumentParser(
        prog="jackrom.py", description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("merge", help="merge the glue and a Mac Plus v3 ROM")
    m.add_argument("--machine", required=True, choices=sorted(MACHINES))
    m.add_argument("--glue", required=True, help="GLUE.BIN from tools/dev/build.sh")
    m.add_argument("--mac-rom", help="the Mac Plus v3 ROM (default: tools/macrom.py path)")
    m.add_argument("-o", "--output", required=True, help="the image to write")
    e = sub.add_parser("export", help="the website's data for one machine (no Apple byte)")
    e.add_argument("--machine", required=True, choices=sorted(MACHINES))
    e.add_argument("--glue", required=True, help="GLUE.BIN from tools/dev/build.sh (release)")
    e.add_argument("-o", "--output", required=True, help="the JSON file to write")
    sub.add_parser("patches", help="check every patch entry, enabled or not, against the ROM")
    h = sub.add_parser("hash", help="the SHA-256 of Mac ROM bytes, for a patch entry")
    h.add_argument("--offset", required=True, type=lambda x: int(x, 0))
    h.add_argument("--length", required=True, type=lambda x: int(x, 0))
    h.add_argument("--mac-rom", help="the Mac Plus v3 ROM (default: tools/macrom.py path)")
    args = parser.parse_args(argv[1:])
    try:
        if args.command == "patches":
            return check_patch_table()
        if args.command == "export":
            export(args.machine, args.glue, args.output)
            return 0
        if args.command == "hash":
            path = args.mac_rom or macrom.rom_path()
            with open(path, "rb") as handle:
                data = handle.read()
            macrom.check_rom(data)
            print(hashlib.sha256(data[args.offset:args.offset + args.length]).hexdigest())
            return 0
        merge(args.machine, args.glue, args.mac_rom, args.output)
        return 0
    except (JackRomError, macrom.MacRomError, OSError) as err:
        print(f"jackrom: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
