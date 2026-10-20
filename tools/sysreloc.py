#!/usr/bin/env python3
"""tools/sysreloc.py: the relocation table for a System file's absolute
Mac addresses (D-15, EPIC-03 STORY-04).

System 6's patches for the Plus hold the ROM's routines as absolute
$400000+offset addresses (docs/macplus/system6-offsets.md). Our ROM sits at
$FD0000 or $E20000, so the glue adds ROMBase - $400000 to each of them as the
Resource Manager loads the resource (src/glue/sysreloc.c). This tool finds
them: it reads the System file's resource fork from an HFS disk image, scans
the chosen resources for longs holding a ROM address ($400000-$41FFFF) or one
of the Mac hardware addresses the glue moves (KINDS) that are the operand of a
known instruction shape (below), and writes a C table of offsets, keyed by
each resource's length and the 32-bit sum of its words. The table holds
offsets and sums, never System bytes (D-02).

Instruction shapes, the long right after the opcode word:
  JMP / JSR / PEA abs.l, LEA abs.l,An, MOVE.x abs.l,<ea>,
  MOVE.L / MOVEA.L #imm,<ea>, CMPI.L / SUBI.L / ADDI.L #imm,<ea>, CMP.L #imm,Dn,
  CMPA.L #imm,An
and MOVEM.x abs.l, whose address follows the register mask.

One more kind is not an address (EPIC-15): System 6's _SysBeep returns
silently unless the interrupt level is 0 (ANDI.W #$0700,D0 on the SR), and
the glue runs the Mac at level 2 (D-17); its long becomes ANDI.W #$0500,D0,
which lets level 2 through.

Usage:
  tools/sysreloc.py list DISK                 every resource with ROM operands
  tools/sysreloc.py table DISK NAME TYPE:ID ... -o FILE.c
"""

import argparse
import hashlib
import struct
import sys

BLOCK = 512

# The absolute Mac addresses the glue moves, by kind (src/glue/sysreloc.h):
# the ROM, the diagnostic-ROM probe at $F80000, the system-error save area
# SERegs at $3FFC80, and the SCC's read and write bases.
KINDS = (
    ("ROM", 0x400000, 0x420000),
    ("DIAG", 0xF80000, 0xF90000),
    ("SEREGS", 0x3FFC80, 0x3FFD00),
    ("SCC", 0x9FFFF0, 0xA00000),
    ("SCC", 0xBFFFF0, 0xC00000),
)
KIND_CODES = {"ROM": 0, "DIAG": 1, "SEREGS": 2, "SCC": 3, "IPL": 4}
# _SysBeep's opening (MOVEM.L, MOVE.W 60(SP),D7, MOVE SR,D0, then the
# ANDI.W #$0700,D0 the IPL kind changes).
SYSBEEP_OPENING = bytes.fromhex("48E7FF7E3E2F003C40C002400700")
SYSBEEP_IPL_AT = 10


class SysRelocError(Exception):
    pass


# --------------------------------------------------------------------------
# HFS: the MDB, the catalog B-tree's leaf chain, a file's resource fork.

def read_fork(disk, mdb, extents, length):
    al_size = struct.unpack_from(">I", mdb, 0x14)[0]
    al_start = struct.unpack_from(">H", mdb, 0x1C)[0]
    out = bytearray()
    for i in range(3):
        start, count = struct.unpack_from(">HH", extents, 4 * i)
        if count == 0:
            break
        off = al_start * BLOCK + start * al_size
        out += disk[off:off + count * al_size]
    if len(out) < length:
        raise SysRelocError("the fork continues in the extents overflow file")
    return bytes(out[:length])


def find_system_rsrc(disk):
    """The resource fork of the file named "System" of type ZSYS."""
    mdb = disk[2 * BLOCK:3 * BLOCK]
    if mdb[:2] != b"BD":
        raise SysRelocError("not an HFS volume (no BD signature at block 2)")
    cat_size = struct.unpack_from(">I", mdb, 0x92)[0]
    catalog = read_fork(disk, mdb, mdb[0x96:0xA2], cat_size)
    node_size = struct.unpack_from(">H", catalog, 0x0E + 0x12)[0]
    node = struct.unpack_from(">I", catalog, 0x0E + 0x0A)[0]  # first leaf
    while node:
        base = node * node_size
        flink, _, kind, _, nrecs = struct.unpack_from(">IIbBH", catalog, base)
        if kind == -1:  # leaf
            for r in range(nrecs):
                roff = struct.unpack_from(">H", catalog, base + node_size - 2 * (r + 1))[0]
                rec = catalog[base + roff:]
                key_len = rec[0]
                name = rec[7:7 + rec[6]]
                data = rec[(key_len + 2) & ~1:]
                if data[0] == 2 and name == b"System" and data[4:8] == b"ZSYS":
                    rlen = struct.unpack_from(">I", data, 36)[0]
                    return read_fork(disk, mdb, data[86:98], rlen)
        node = flink
    raise SysRelocError("no System file (type ZSYS) on the volume")


# --------------------------------------------------------------------------
# The resource fork.

def resources(fork):
    """{(type, id): bytes} of every resource."""
    data_off, map_off = struct.unpack_from(">II", fork, 0)
    rmap = fork[map_off:]
    type_list = struct.unpack_from(">H", rmap, 24)[0]
    count = struct.unpack_from(">H", rmap, type_list)[0] + 1
    out = {}
    for t in range(count):
        rtype, n, refs = struct.unpack_from(">4sHH", rmap, type_list + 2 + 8 * t)
        for r in range(n + 1):
            rid, _, attr_off = struct.unpack_from(">hHI", rmap, type_list + refs + 12 * r)
            off = data_off + (attr_off & 0xFFFFFF)
            length = struct.unpack_from(">I", fork, off)[0]
            out[(rtype.decode("mac_roman"), rid)] = fork[off + 4:off + 4 + length]
    return out


# --------------------------------------------------------------------------
# The scanner.

def operand_shape(op):
    """True when the long after opcode word op is an absolute address or an
    immediate long (one of the shapes in the docstring)."""
    return (op in (0x4EF9, 0x4EB9, 0x4879)            # JMP, JSR, PEA abs.l
            or (op & 0xF1FF) == 0x41F9                # LEA abs.l,An
            or (op & 0xC03F) == 0x0039 and op & 0x3000  # MOVE.x abs.l,<ea>
            or (op & 0xF03F) == 0x203C                # MOVE(A).L #imm,<ea>
            or (op & 0xFFC0) == 0x0C80                # CMPI.L #imm,<ea>
            or (op & 0xFFC0) in (0x0480, 0x0680)      # SUBI.L, ADDI.L #imm,<ea>
            or (op & 0xF1FF) == 0xB0BC                # CMP.L #imm,Dn
            or (op & 0xF1FF) == 0xB1FC)               # CMPA.L #imm,An


def kind_of(value):
    for name, low, high in KINDS:
        if low <= value < high:
            return name
    return None


def rom_operands(code):
    """(offset, kind) of the longs holding a Mac address of one of KINDS that
    follow an instruction shape, or a MOVEM's register mask (MOVEM.x abs.l).
    Even offsets only: 68000 code is word-aligned."""
    found = []
    for off in range(2, len(code) - 3, 2):
        kind = kind_of(struct.unpack_from(">I", code, off)[0])
        if kind is None:
            continue
        op = struct.unpack_from(">H", code, off - 2)[0]
        movem = off >= 4 and (struct.unpack_from(">H", code, off - 4)[0] & 0xFBBF) == 0x48B9
        if operand_shape(op) or movem:
            found.append((off, kind))
    return found


def ipl_operands(code):
    """(offset, "IPL") of _SysBeep's interrupt-level test (SYSBEEP_OPENING)."""
    at = code.find(SYSBEEP_OPENING)
    return [] if at < 0 or at & 1 else [(at + SYSBEEP_IPL_AT, "IPL")]


def word_sum(code):
    padded = code + b"\0" * (len(code) & 1)
    return sum(struct.unpack(f">{len(padded) // 2}H", padded)) & 0xFFFFFFFF


# --------------------------------------------------------------------------

def write_table(path, name, entries):
    lines = [
        "/**",
        f" * File: {path}",
        " * Author: Diego Parrilla Santamaría",
        " * Date: 2026-10-18",
        " * Copyright: 2026 - GOODDATA LABS SL",
        f" * Description: The ROM-address relocation table for {name} (D-15),",
        " * generated by tools/sysreloc.py; do not edit. Offsets into each resource",
        " * of the longs to relocate, keyed by the resource's length and the 32-bit",
        " * sum of its words. Offsets and sums only, never System bytes (D-02).",
        " */",
        "",
        '#include "sysreloc.h"',
        "",
    ]
    for rtype, rid, code, offs in entries:
        kinds = ", ".join(f"{sum(1 for _, k in offs if k == n)} {n}" for n in KIND_CODES
                          if any(k == n for _, k in offs))
        lines.append(f"/* '{rtype}' {rid}: {len(code)} bytes, {len(offs)} operands ({kinds}), "
                     f"SHA-256 {hashlib.sha256(code).hexdigest()[:16]}... */")
        lines.append(f"static const struct SysRelocOperand reloc{rtype.strip()}{rid & 0xFFFF}[] = {{")
        for i in range(0, len(offs), 4):
            lines.append("    " + " ".join(f"{{0x{o:04X}, SYSRELOC_{k}}}," for o, k in offs[i:i + 4]))
        lines.append("};")
        lines.append("")
    lines.append("const struct SysRelocEntry sysRelocTable[] = {")
    for rtype, rid, code, offs in entries:
        lines.append(f"    {{{len(code)}UL, 0x{word_sum(code):08X}UL, {len(offs)}, "
                     f"reloc{rtype.strip()}{rid & 0xFFFF}}}, /* '{rtype}' {rid} */")
    lines.append("    {0, 0, 0, 0},")
    lines.append("};")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def main(argv):
    parser = argparse.ArgumentParser(prog="sysreloc.py", description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    lst = sub.add_parser("list")
    lst.add_argument("disk")
    tab = sub.add_parser("table")
    tab.add_argument("disk")
    tab.add_argument("name", help="what the table is for, e.g. 'System 6.0.8'")
    tab.add_argument("resources", nargs="+", metavar="TYPE:ID")
    tab.add_argument("-o", "--output", required=True)
    args = parser.parse_args(argv[1:])
    try:
        with open(args.disk, "rb") as handle:
            res = resources(find_system_rsrc(handle.read()))
        if args.command == "list":
            for (rtype, rid), code in sorted(res.items()):
                offs = rom_operands(code)
                if offs:
                    kinds = ", ".join(f"{sum(1 for _, k in offs if k == n)} {n}"
                                      for n in KIND_CODES if any(k == n for _, k in offs))
                    print(f"{rtype}:{rid}\t{len(code)} bytes\t{len(offs)} operands ({kinds})")
            return 0
        entries = []
        for spec in args.resources:
            rtype, rid = spec.rsplit(":", 1)
            key = (rtype.ljust(4), int(rid))
            if key not in res:
                raise SysRelocError(f"no resource {spec}")
            offs = sorted(rom_operands(res[key]) + ipl_operands(res[key]))
            entries.append((key[0], key[1], res[key], offs))
        write_table(args.output, args.name, entries)
        for rtype, rid, _, offs in entries:
            print(f"{rtype}:{rid} {len(offs)} operands")
        return 0
    except (SysRelocError, OSError, struct.error) as err:
        print(f"sysreloc: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
