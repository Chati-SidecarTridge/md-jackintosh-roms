#!/usr/bin/env python3
"""
File: tools/dev/hatari_harness.py
Author: Diego Parrilla Santamaría
Date: 2026-10-18
Copyright: 2026 - GOODDATA LABS SL
Description: Boot a debug image as the system ROM of Hatari, read the glue's
trace (NF_STDERR on Hatari's stderr), take screenshots through the control
socket, and print PASS or FAIL (EPIC-00 STORY-05). From
romemul-switcher-rom's tools/dev/hatari_harness.py.

Usage: hatari_harness.py [--image st|ste] [--memsize KB] [--type debug|debug-test]
                         [--monitor mono|rgb]
                         [--monitor mono|rgb] [--corrupt] [--disk FILE [--fixed]
                         [--write-protect]] [--interactive]

Build the glue first (tools/dev/build.sh <st|ste> debug); the harness merges
it with the Mac ROM of tools/macrom.py path. Each run gets a folder under
build/harness/ with the image, the trace, Hatari's log and the screenshots.
"""

import argparse
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness_common as hc  # noqa: E402

# ST RAM sizes Hatari takes, in KB, and the banks it fills: bank 0, bank 1
# (Hatari's stMemory.c, STMemory_RAM_SetBankSize). 2560 needs the ST's MMU:
# Hatari gives the STE chipset bank 0's setting for both banks.
BANKS = {512: (512, 0), 1024: (512, 512), 2048: (2048, 0), 2560: (2048, 512),
         4096: (2048, 2048)}
# The MMU's bank size codes (src/glue/memconf.inc); an empty bank reads as 0.
MMU_CODES = {0: 0, 128: 0, 512: 1, 2048: 2}
MAC_SCREEN_BELOW_MEMTOP = 0x8100  # src/glue/mem.h (D-19)
# Hatari's ST: 8.021247 MHz, a mono frame of 501 lines of 224 cycles
# (clocks_timings.c, video.h).
MONO_VBL_HZ = 8021247 / (501 * 224)
MAC_TICK_HZ = 60.15  # the Plus's vertical blank
RAMDISK_MAGIC = 0x4A4B5244  # "JKRD", src/glue/ramdisk.c
RAMDISK_DESCRIPTOR = 16
RAMDISK_FIXED, RAMDISK_WRITE_PROTECT = 1, 2
MAC_ROM_CHECKSUM = "4d1f8172"
MAC_ROM_VERSION = "0075"
STEP_TIMEOUT = 20


def hatari_args(image, args, run_dir, sock_path, parse_path):
    cmd = [hc.emulator_path("hatari"), "--tos", image, "--machine", args.image,
           "--memsize", str(args.memsize), "--ttram", "0",
           "--monitor", args.monitor, "--natfeats", "yes", "--sound", "off",
           "--patch-tos", "false", "--confirm-quit", "false",
           "--log-file", os.path.join(run_dir, "hatari.log")]
    if parse_path:
        cmd += ["--parse", parse_path]
    if args.interactive:
        # Drive 2's swaps need the control socket (DiskSwapper).
        return cmd + (["--control-socket", sock_path] if args.swap else [])
    # A card error holds its message 3 s: in real time, for its screenshot.
    fast = "no" if (getattr(args, "fake", None) or {}).get("card") else "yes"
    return cmd + ["--fast-forward", fast, "--fast-forward-key-repeat", "false",
                  "--statusbar", "false", "--drive-led", "false",
                  "--control-socket", sock_path,
                  "--screenshot-dir", os.path.join(run_dir, "shots")]


def glue_symbol(glue_map, name):
    """A symbol's address from the glue's link map (GLUE.MAP)."""
    symbols = hc.jackrom.glue_symbols(glue_map)
    if name not in symbols:
        raise hc.HarnessError(f"{name} not in {glue_map}")
    return symbols[name]


def ramdisk_files(run_dir, memsize, glue_map, disk, fixed, write_protect, partition=False,
                  disk2=None, swaps=()):
    """The test builds' RAM disk (src/glue/ramdisk.c): the raw image placed
    64 KB-aligned below the top of RAM, a 16-byte descriptor ("JKRD", base,
    bytes, flags) in the last 16 bytes, and the debugger scripts that load
    both when the CPU reaches _glueMain, after the glue has filled the RAM
    with $FF. A loadbin straight from --parse is lost before the first
    instruction, and a breakpoint on the reset PC never fires; a one-shot
    breakpoint with :trace (go on, no prompt) and :file (run the loadbins)
    works. A second image (disk2, drive 2: EPIC-09) goes below the first,
    its descriptor in the 16 bytes under the first image's base; its slot
    is as big as the largest of it and the swaps (--swap), which DiskSwapper
    loads there later. Returns the --parse script and the images' bases
    (memtop: the lowest)."""
    raw = os.path.join(run_dir, "disk.raw")
    size = hc.raw_disk_image(disk, raw)
    if partition:
        whole = os.path.join(run_dir, "disk-partitioned.raw")
        size = hc.partitioned_copy(raw, whole)
        raw = whole
    top = memsize * 1024
    base = (top - RAMDISK_DESCRIPTOR - size) & ~0xFFFF
    flags = (RAMDISK_FIXED if fixed else 0) | (RAMDISK_WRITE_PROTECT if write_protect else 0)
    desc = os.path.join(run_dir, "ramdisk.desc")
    with open(desc, "wb") as handle:
        handle.write(struct.pack(">IIII", RAMDISK_MAGIC, base, size, flags))
    loads = [(raw, base), (desc, top - RAMDISK_DESCRIPTOR)]
    info = {"base": base, "bytes": size, "flags": flags, "memtop": base}
    if disk2:
        disks2 = []
        for number, image in enumerate([disk2, *swaps]):
            raw_n = os.path.join(run_dir, f"disk2-{number}.raw")
            disks2.append((raw_n, hc.raw_disk_image(image, raw_n), image))
        raw2, size2, _ = disks2[0]
        slot = max(size for _, size, _ in disks2)
        base2 = (base - RAMDISK_DESCRIPTOR - slot) & ~0xFFFF
        desc2 = os.path.join(run_dir, "ramdisk2.desc")
        with open(desc2, "wb") as handle:
            handle.write(struct.pack(">IIII", RAMDISK_MAGIC, base2, size2, 0))
        loads += [(raw2, base2), (desc2, base - RAMDISK_DESCRIPTOR)]
        info.update({"base2": base2, "bytes2": size2, "memtop": base2, "disks2": disks2,
                     "desc2": base - RAMDISK_DESCRIPTOR})
    load = os.path.join(run_dir, "ramdisk-load.ini")
    with open(load, "w", encoding="ascii") as handle:
        for path, address in loads:
            handle.write(f"loadbin {path} ${address:x}\n")
    entry = glue_symbol(glue_map, "_glueMain")
    script = os.path.join(run_dir, "ramdisk.ini")
    with open(script, "w", encoding="ascii") as handle:
        handle.write(f"b pc = ${entry:x} :trace :once :file {load}\n")
    return script, info


def watch_files(run_dir, addresses, memories, parse_path):
    """--watch / --watch-mem: a breakpoint at each code address, or on each
    change of the long at a memory address, that prints the registers to
    stderr.txt and goes on (:trace); added to the --parse script."""
    regs = os.path.join(run_dir, "watch-regs.ini")
    with open(regs, "w", encoding="ascii") as handle:
        handle.write("r\n")
    script = parse_path or os.path.join(run_dir, "watch.ini")
    with open(script, "a", encoding="ascii") as handle:
        for addr in addresses or []:
            handle.write(f"b pc = ${int(addr, 0):x} :trace :file {regs}\n")
        for addr in memories or []:
            value = int(addr, 0)
            handle.write(f"b (${value:x}).l ! (${value:x}).l :trace :file {regs}\n")
    return script


# 2026-10-19 12:00 UTC in Mac seconds (since 1904-01-01): the fake's default.
FAKE_CART_SECONDS = 0xE6FBB6C0
FAKE_CART_ID = 0x4A4B0001
FAKE_CART_STATE_BYTES = 8 * 4 + 256  # glueTestCart up to its status block


def fake_cart_files(run_dir, glue_map, spec, parse_path):
    """EPIC-05: the test builds' fake Multi-device (src/glue/cart.c,
    glueTestCart), filled when the CPU reaches _glueMain like the RAM disk.
    spec: KEY=VALUE,...: id (the identity long, default $4A4B0001), seconds
    (UTC, default 2026-10-19 12:00), valid-at (the TIME query from which the
    time is valid, default 1), pram (a 256-byte file served as PRAM.BIN,
    default none), zone (seconds east of UTC: a PRAM.BIN made with that
    time zone), card (the microSD card's state at APP_FREE+$40: 0 OK, 1 none,
    2 unreadable, 3 no /jackintosh; with an error the fake serves nothing).
    Returns the --parse script and the settings."""
    fake = {"id": FAKE_CART_ID, "seconds": FAKE_CART_SECONDS, "valid-at": 1, "pram": None,
            "zone": None, "card": 0}
    for item in filter(None, (spec or "").split(",")):
        key, _, value = item.partition("=")
        if key not in fake:
            hc.die(f"--fake-cart: unknown {key!r} (id, seconds, valid-at, pram, zone, card)")
        fake[key] = value if key == "pram" else int(value, 0)
    if fake["zone"] is not None and not fake["pram"]:
        # A PRAM.BIN made here: XPRAM's 'Bugs' signature (so InitUtil keeps
        # it) and MachineLocation's gmtDelta, the classic 20 bytes invalid
        # (so InitUtil writes them and the Mac has a change to save).
        data = bytearray(256)
        data[0x0C:0x10] = b"Bugs"
        data[0xED:0xF0] = (fake["zone"] & 0xFFFFFF).to_bytes(3, "big")
        fake["pram"] = os.path.join(run_dir, "pram.bin")
        with open(fake["pram"], "wb") as handle:
            handle.write(data)
    # The fake's state (glueTestCart's fields and its PRAM.BIN, 288 bytes)
    # lives in a file, like md-jackintosh's microSD: loaded at every boot
    # (_glueMain), saved at every reset (glueReset, before its $FF fill), so
    # a Mac restart finds the Multi-device again with the PRAM it saved.
    base = glue_symbol(glue_map, "_glueTestCart")
    pram = bytes(256)
    if fake["pram"]:
        if os.path.getsize(fake["pram"]) != 256:
            hc.die(f"--fake-cart: {fake['pram']} is not 256 bytes")
        with open(fake["pram"], "rb") as handle:
            pram = handle.read()
    state = os.path.join(run_dir, "fakecart.state")
    with open(state, "wb") as handle:
        handle.write(struct.pack(">IIIIIIII", 0x46414B45, fake["id"], fake["seconds"],
                                 fake["valid-at"], 1 if fake["pram"] else 0, 0, 0,
                                 fake["card"]) + pram)
    load = os.path.join(run_dir, "fakecart-load.ini")
    with open(load, "w", encoding="ascii") as handle:
        handle.write(f"loadbin {state} ${base:x}\n")
    save = os.path.join(run_dir, "fakecart-save.ini")
    with open(save, "w", encoding="ascii") as handle:
        handle.write(f"savebin {state} ${base:x} {FAKE_CART_STATE_BYTES}\n")
    script = parse_path or os.path.join(run_dir, "fakecart.ini")
    with open(script, "a", encoding="ascii") as handle:
        handle.write(f"b pc = ${glue_symbol(glue_map, '_glueMain'):x} :trace :file {load}\n")
        handle.write(f"b pc = ${glue_symbol(glue_map, '_glueReset'):x} :trace :file {save}\n")
    return script, fake


def fake_cart_expected_seconds(fake):
    """The local time the glue must set: UTC plus MachineLocation's
    gmtDelta, the low 24 bits (signed) of the PRAM file's long at $EC."""
    delta = 0
    if fake["pram"]:
        with open(fake["pram"], "rb") as handle:
            data = handle.read()
        delta = int.from_bytes(data[0xED:0xF0], "big", signed=True)
    return (fake["seconds"] + delta) & 0xFFFFFFFF


def mac_rom_part(image, machine):
    """What the glue must report about the Mac ROM part of the merged image:
    its computed checksum (patches change it) and its reset vector."""
    _, glue_size, _ = hc.jackrom.MACHINES[machine]
    with open(image, "rb") as handle:
        rom = handle.read()[glue_size:]
    return {"computed": hc.macrom.rom_checksum(rom),
            "reset": struct.unpack(">I", rom[4:8])[0]}


def make_dumper(command, run_dir):
    """dump_memory(specs): Hatari's debugger dumps each ADDR:LEN range
    (--dump) after a crash; the dump lands in stdout.txt in the run folder."""
    def dump_memory(specs):
        if not specs:
            return
        out = os.path.join(run_dir, "stdout.txt")
        for spec in specs:
            addr, length = (int(x, 0) for x in spec.split(":"))
            command(f"hatari-debug memdump b ${addr:x}-${addr + length:x}")
            time.sleep(0.5)  # Hatari reads its control socket once a frame
        time.sleep(1.0)
        print(f"  memory dump requested into Hatari's output (see {out})", flush=True)
    return dump_memory


class DiskSwapper:
    """EPIC-09: drive 2's images in turn (--disk2, then each --swap, then the
    first again), one each time the Mac ejects drive 2 (the glue's "@@ disk2
    eject"): the next image is loaded into drive 2's slot with Hatari's
    debugger, its size written into drive 2's descriptor (a restart keeps
    it), and the glue's glueTestDisk mailbox tells the glue, which announces
    the disk. service() is called while the harness waits."""

    def __init__(self, ramdisk, command, glue_map, trace, report, need=None):
        self.disks = ramdisk["disks2"]
        self.base = ramdisk["base2"]
        self.desc = ramdisk["desc2"]
        self.mailbox = glue_symbol(glue_map, "_glueTestDisk")
        self.command, self.trace, self.report, self.need = command, trace, report, need
        self.current = 0
        self.ejects = 0
        self.seq = 0

    def service(self):
        self.trace.drain(0)
        ejects = self.trace.data.count(b"@@ disk2 eject\n")
        while self.ejects < ejects:
            self.ejects += 1
            self.current = (self.current + 1) % len(self.disks)
            raw, size, image = self.disks[self.current]
            self.command(f"hatari-debug loadbin {raw} ${self.base:x}")
            self.command(f"hatari-debug memwrite w ${self.desc + 8:x} "
                         f"${size >> 16:x} ${size & 0xFFFF:x}")
            self.command(f"hatari-debug memwrite w ${self.mailbox + 2:x} $1 "
                         f"${size >> 16:x} ${size & 0xFFFF:x} $0 $0")
            time.sleep(0.5)  # the image and fields before the seq
            self.seq += 1
            self.command(f"hatari-debug memwrite w ${self.mailbox:x} ${self.seq:x}")
            if self.need:  # the glue took it and announced the disk
                self.need(f"drive 2 swapped to {os.path.basename(image)}",
                          rf"@@ disk2 swapped in {size}\n", 20)
                self.need("drive 2 disk announced", r"@@ disk2 inserted\n", 20)
            else:
                self.report(f"drive 2 swapped to {os.path.basename(image)}")

    def wait(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self.service()
            time.sleep(0.5)


def run_diag(trace, shot, steps, args, ramdisk, mac, dump_memory=lambda specs: None,
             command=None, glue_map=None):
    """The EPIC-00 session: the diagnostic screen and its trace."""
    bank0, bank1 = BANKS[args.memsize]
    ram = args.memsize * 1024
    mmu = (MMU_CODES[bank0] << 2) | MMU_CODES[bank1]
    memtop = ramdisk["memtop"] if ramdisk else ram
    screen = memtop - MAC_SCREEN_BELOW_MEMTOP

    def need(name, pattern, timeout=STEP_TIMEOUT):
        # A crash line from the glue's debug handlers, or a halt the step does
        # not expect (an unknown System: EPIC-08), ends the session at once.
        match = trace.wait_for(rf"(?:{pattern})|(@@ (?:crash|halt|mac syserr) [^\n]*\n)", timeout)
        if match is not None and match.group(1):
            dump_memory(args.dump)
        if match is None or match.group(1):
            path = shot(f"fail-{name.replace(' ', '-')}")
            why = (match.group(1).decode().strip() if match is not None
                   else f"no {pattern!r} within {timeout} s")
            steps.fail(name, why + (f"; screenshot {path}" if path else ""))
        steps.ok(name)

    machine = args.image.upper()
    need("boot", rf"@@ boot {machine} {hc.version()} \S+\n")
    need("ram", rf"@@ ram {ram} mmu {mmu:02x}\n")
    if ramdisk:
        need("ram disk", rf"@@ disk ramdisk {ramdisk['bytes']} base {ramdisk['base']:06x} "
             rf"flags {ramdisk['flags']:x}\n")
        if "base2" in ramdisk:
            need("ram disk 2", rf"@@ disk ramdisk2 {ramdisk['bytes2']} "
                 rf"base {ramdisk['base2']:06x} flags 0\n")
    elif args.type == "debug-test":
        need("no ram disk", r"@@ disk ramdisk none\n")
    ste = int(args.image == "ste")
    if args.monitor == "rgb":
        # A colour monitor gets one line asking for the mono monitor (D-04).
        need("screen", rf"@@ screen colour {screen:06x} vbl ok ste {ste}\n")
        need("idle on colour", r"@@ idle colour\n")
        path = shot("colour")
        if path is None:
            steps.fail("screenshot", "Hatari wrote no screenshot")
        steps.ok(f"screenshot {os.path.relpath(path, hc.REPO)}")
        return
    need("screen", rf"@@ screen mono {screen:06x} vbl ok ste {ste}\n")
    if args.corrupt:
        need("image checksum fails", r"@@ image fail [0-9a-f]{8} [0-9a-f]{8}\n")
    else:
        need("image checksum", r"@@ image ok [0-9a-f]{8} [0-9a-f]{8}\n")
    need("mac rom", rf"@@ macrom {MAC_ROM_CHECKSUM} {mac['computed']:08x} {MAC_ROM_VERSION} "
         rf"reset {mac['reset']:06x}\n")
    if args.corrupt:
        need("halt on a damaged image", r"@@ halt image\n")
        path = shot("diag")
        if path is None:
            steps.fail("screenshot", "Hatari wrote no screenshot")
        steps.ok(f"screenshot {os.path.relpath(path, hc.REPO)}")
        return
    # EPIC-05: the hello to the Multi-device times out (Hatari has none,
    # C-02) and the boot goes on; or the test builds' fake one answers.
    fake = getattr(args, "fake", None)
    if fake and ramdisk:
        # The fake's units are the RAM disks: FD0 the first (HD0 if fixed),
        # FD1 the second (src/glue/cart.c, cartFakeUnits).
        units = {(2 if ramdisk["flags"] & RAMDISK_FIXED else 0):
                 (1 | (2 if ramdisk["flags"] & RAMDISK_WRITE_PROTECT else 0),
                  ramdisk["bytes"] // 512)}
        if "bytes2" in ramdisk:
            units[1] = (1, ramdisk["bytes2"] // 512)
        FAKE_CTX.update(base=glue_symbol(glue_map, "_glueTestCart"), units=units)
    if fake:
        known = (fake["id"] >> 16) == 0x4A4B and (fake["id"] & 0xFFFF) == 1
        need("Multi-device", rf"@@ cart hello {'ok' if known else 'other'} {fake['id']:08x}\n", 10)
        if known:
            need("microSD card", rf"@@ cart card {fake['card']}\n", 10)
        if known and fake["card"]:
            # md-jackintosh stopped at its card: the reason on the screen
            # for 3 s, no PRAM, no time, then the Mac with no disks.
            need("no PRAM", r"@@ cart pram none\n", 10)
            need("no time", r"@@ cart time not yet\n", 10)
            path = shot("card-error")
            if path is None:
                steps.fail("screenshot", "Hatari wrote no screenshot")
            steps.ok(f"screenshot {os.path.relpath(path, hc.REPO)}")
        elif known:
            need("PRAM from the Multi-device",
                 rf"@@ cart pram {'loaded' if fake['pram'] else 'none'}\n", 10)
            if fake["valid-at"] <= 1:
                need("time from the Multi-device",
                     rf"@@ cart time {fake_cart_expected_seconds(fake):08x}\n", 10)
            else:
                need("time not known yet", r"@@ cart time not yet\n", 10)
    else:
        need("no Multi-device", r"@@ cart hello none\n", 10)
    if args.type == "debug-test":
        # EPIC-04: writes to $0-$7 skipped by the glue's bus-error handler.
        need("bus error self-test", r"@@ buserr selftest ok fixes [1-9]\d*\n")
    # EPIC-02: into the Mac ROM, to the "?" disk and its boot loop.
    need("mac enter", rf"@@ mac enter [0-9a-f]{{6}} memtop {memtop:06x} screen {screen:06x}\n")
    need("disk driver", r"@@ mac sony 0 dce [0-9a-f]{6}\n")
    # EPIC-05 STORY-07: the cartridge when md-jackintosh answered, else the
    # RAM disks of test builds, else none.
    cart_ok = fake and (fake["id"] >> 16) == 0x4A4B and (fake["id"] & 0xFFFF) == 1
    backend = ("cartridge" if cart_ok else
               "ramdisk" if args.type == "debug-test" else "none")
    need("disk backend", rf"@@ disk backend {backend}\n")
    # EPIC-08 STORY-03: HWCfgFlags $40 (XPRAM, no SCSI), and InitUtil's
    # 'Bugs' signature written into the glue's XPRAM.
    need("xpram", r"@@ mac xpram hwcfg 40 sig 42756773\n")
    if args.type == "debug-test":
        # EPIC-08 STORY-06: _SetPtrSize keeps a nil master pointer (the
        # bus-error handler skips SetSize's flag writes through it).
        need("set pointer size", r"@@ mm setptrsize grow -108 shrink 0\n")
    need("glue hook", r"@@ mac hook\n")
    if ramdisk:
        run_disk_boot(trace, shot, steps, need, ramdisk, dump_memory, args.dump)
        if cart_ok and not fake["card"]:
            # EPIC-05: the time once NTP has it (asked every 600 ticks), and
            # the PRAM the Mac changed written back 180 ticks after.
            # The two come in either order: look for each in the whole trace.
            late = [("PRAM written back", rb"@@ cart pram saved 0 0\n")]
            if fake["valid-at"] > 1:
                late.append(("time from the Multi-device, retried",
                             rf"@@ cart time {fake_cart_expected_seconds(fake):08x}\n".encode()))
            for name, pattern in late:
                end = time.time() + 120
                while not re.search(pattern, trace.data) and time.time() < end:
                    trace.drain(0.5)
                if not re.search(pattern, trace.data):
                    steps.fail(name, f"no {pattern!r} within 120 s")
                steps.ok(name)
        if "base2" in ramdisk:
            run_disk2(trace, shot, steps, need)
            swapper = (DiskSwapper(ramdisk, command, glue_map, trace, steps.ok, need)
                       if len(ramdisk["disks2"]) > 1 else None)
            if args.launch:
                run_launch(trace, shot, steps, need, command, glue_map, args.launch,
                           dump_memory, args.dump, args.then or (), swapper=swapper)
        elif args.launch:
            run_launch(trace, shot, steps, need, command, glue_map, args.launch,
                       dump_memory, args.dump, args.then or (), disk_v=45)
        if args.then and not args.launch:
            run_actions(trace, shot, steps, need, command, glue_map, args.then, dump_memory,
                        args.dump, swapper if "base2" in ramdisk else None)
        if args.input:
            def restart():
                # The same boot again, the RAM disk kept (EPIC-08 STORY-01).
                need("restart", rf"@@ boot {machine} {hc.version()} \S+\n", 60)
                need("ram disk kept", rf"@@ disk ramdisk {ramdisk['bytes']} "
                     rf"base {ramdisk['base']:06x} flags {ramdisk['flags']:x}\n")
                need("mac enter again", rf"@@ mac enter [0-9a-f]{{6}} memtop {memtop:06x} "
                     rf"screen {screen:06x}\n")
                run_disk_boot(trace, shot, steps, need, ramdisk, dump_memory, args.dump)

            run_input(trace, shot, steps, need, command, glue_map, restart)
        return
    plot = trace.wait_for(r"@@ mac plotdisk [0-9a-f]{6} vbl (\d+)\n", 60, consume=False)
    need("question-mark disk", r"@@ mac plotdisk [0-9a-f]{6} vbl \d+\n", 60)
    steps.boot_seconds = int(plot.group(1)) / MONO_VBL_HZ if plot else None
    need("boot loop polls the drive", r"@@ mac read offline\n", 60)
    # The "?" blinks: shoot until one frame shows it in the disk's label
    # (src/glue/mac.c: the icon at x 304, y 174; the symbol 15 rows down,
    # 1 byte right, 16x15 pixels). Each shot follows a trace line that drew
    # the icon (an address, not 000000), not a random phase of the blink.
    for attempt in range(16):
        trace.skip()
        trace.wait_for(r"@@ mac plotdisk (?!000000)[0-9a-f]{6} vbl \d+\n", 10)
        path = shot(f"question-disk-{attempt}")
        if path is None:
            steps.fail("screenshot", "Hatari wrote no screenshot")
        if hc.dark_pixels(hc.read_png(path), 312, 189, 328, 204) > 0:
            steps.ok(f"\"?\" disk on screen {os.path.relpath(path, hc.REPO)}")
            break
    else:
        steps.fail("\"?\" disk on screen", f"no \"?\" in the disk's label in 16 shots; last {path}")
    # The Mac's tick rate against Hatari's mono VBL (EPIC-02 STORY-03).
    first = trace.wait_for(r"@@ ticks (\d+) vbl (\d+)\n", 60)
    second = trace.wait_for(r"@@ ticks (\d+) vbl (\d+)\n", 60)
    if first is None or second is None:
        steps.fail("tick rate", "fewer than two @@ ticks lines")
    ticks = int(second.group(1)) - int(first.group(1))
    seconds = (int(second.group(2)) - int(first.group(2))) / MONO_VBL_HZ
    rate = ticks / seconds
    if abs(rate - MAC_TICK_HZ) / MAC_TICK_HZ > 0.01:
        steps.fail("tick rate", f"{rate:.3f} Hz, not within 1 % of {MAC_TICK_HZ} Hz")
    steps.ok(f"tick rate {rate:.3f} Hz over {seconds:.1f} s; boot to the \"?\" "
             f"{steps.boot_seconds:.2f} s emulated")
    return


def finder_on_screen(png):
    """The Finder's menu bar: row 19 a black line across the screen, row 10
    mostly white with the menu titles (the grey desktop has 320 dark pixels
    on every row)."""
    return hc.dark_pixels(png, 0, 19, 640, 20) > 600 and hc.dark_pixels(png, 0, 10, 640, 11) < 200


MOUSE_SEQ = [0]
# The fake Multi-device's window and units, for --then fake-restart (set by
# run_diag when --fake-cart serves RAM disks).
FAKE_CTX = {}
FAKE_CART_WINDOW = FAKE_CART_STATE_BYTES + 3 * 4  # after the units' drives


def fake_down(command):
    """EPIC-05: the fake behaves like an RP that restarts while the Mac
    runs (md-jackintosh, 2026-10-19): its window reads as fresh (no
    identity, every unit empty) and commands go to the real bus, where they
    time out, until fake_up."""
    base = FAKE_CTX["base"]
    window = base + FAKE_CART_WINDOW
    command(f"hatari-debug memwrite w ${base:x} $0 $0")                # enabled
    command(f"hatari-debug memwrite w ${window + 0x0C:x} $0 $0")       # identity
    for unit in range(3):
        command(f"hatari-debug memwrite w ${window + 0x10 + unit * 0x10:x} "
                + " ".join(["$0"] * 8))


def fake_up(command):
    """The restarted RP: the identity and the units back, change counters 1.
    The fake answers first, so a command sent once the identity is back
    reaches it."""
    base, units = FAKE_CTX["base"], FAKE_CTX["units"]
    window = base + FAKE_CART_WINDOW
    command(f"hatari-debug memwrite w ${base:x} $4641 $4b45")         # "FAKE"
    for unit, (flags, sectors) in units.items():
        command(f"hatari-debug memwrite w ${window + 0x10 + unit * 0x10:x} "
                f"$0 ${flags:x} ${sectors >> 16:x} ${sectors & 0xFFFF:x} $0 $1 $0 $1")
    command(f"hatari-debug memwrite w ${window + 0x0C:x} $4a4b $1")
    time.sleep(0.5)


def input_helpers(need, command, glue_map):
    """The keyboard and mouse of the input sessions (EPIC-04): keys through
    Hatari's control socket and the real IKBD path, the cursor and button
    through the test builds' mailbox (glueTestInput), since the control
    socket cannot move the mouse. Returns key(code, down), chord(*codes) and
    mouse(v, h, buttons)."""
    mailbox = glue_symbol(glue_map, "_glueTestInput")
    seq = MOUSE_SEQ  # one count per session: the glue takes any new value

    # Press and release go out back to back, the pause after: Hatari runs
    # fast-forward, and a key held for a fraction of a real second is held
    # past the Mac's auto-repeat threshold. The glue queues the transitions
    # in order, so a chord's modifiers still apply.
    def key(code, down):
        command(f"hatari-event {'keydown' if down else 'keyup'} 0x{code:02x}")

    def chord(*codes):
        for code in codes:
            key(code, True)
        for code in reversed(codes):
            key(code, False)
        time.sleep(0.3)

    def mouse(v, h, buttons):
        seq[0] += 1
        command(f"hatari-debug memwrite w ${mailbox + 2:x} ${v & 0xFFFF:x} "
                f"${h & 0xFFFF:x} ${buttons:x}")
        time.sleep(0.3)
        command(f"hatari-debug memwrite w ${mailbox:x} ${seq[0]:x}")
        need(f"cursor at {v},{h} buttons {buttons}",
             rf"@@ input {seq[0]} at {v},{h} buttons {buttons}\n", 20)
        time.sleep(0.5)

    return key, chord, mouse


# ST scancodes for --then keys: letters, digits, space, Return, and a few marks.
ST_KEYS = dict(zip("qwertyuiop", range(0x10, 0x1A)))
ST_KEYS.update(zip("asdfghjkl", range(0x1E, 0x27)))
ST_KEYS.update(zip("zxcvbnm", range(0x2C, 0x33)))
ST_KEYS.update(zip("1234567890", range(0x02, 0x0C)))
ST_KEYS.update({" ": 0x39, "\n": 0x1C, ",": 0x33, ".": 0x34, "-": 0x0C, "=": 0x0D,
                ";": 0x27, "'": 0x28, "/": 0x35, "\t": 0x0F})
ST_SHIFT = 0x2A
# Shifted marks (a US layout): the key, typed with Shift.
ST_SHIFTED = {"(": 0x0A, ")": 0x0B, ":": 0x27, '"': 0x28, "!": 0x02, "?": 0x35,
              "_": 0x0C, "+": 0x0D, "*": 0x09, "<": 0x33, ">": 0x34}


def type_text(key, text):
    """Types text through the IKBD, Shift for capitals."""
    for char in text:
        shifted = char.isupper() or char in ST_SHIFTED
        code = ST_SHIFTED.get(char, ST_KEYS.get(char.lower()))
        if code is None:
            hc.die(f"--then keys: no key for {char!r}")
        if shifted:
            key(ST_SHIFT, True)
        key(code, True)
        key(code, False)
        if shifted:
            key(ST_SHIFT, False)
        time.sleep(0.15)


def run_actions(trace, shot, steps, need, command, glue_map, actions, dump_memory, dumps,
                swapper=None):
    """EPIC-09: the --then actions, in order: keys:TEXT (typed; \\n for
    Return), cmd:KEY (Command-KEY), click:V,H, press:V,H and release:V,H (a
    menu), wait:SECONDS (drive 2's swaps served meanwhile), shot:NAME, regs
    and mem:ADDR,LEN (the registers or memory into stderr.txt),
    fake-restart:SECONDS (--fake-cart: the RP restarts), or fake-down and
    fake-up around other actions (the Mac uses its disk meanwhile); a crash during
    them fails the run."""
    key, chord, mouse = input_helpers(need, command, glue_map)
    for action in actions:
        verb, _, arg = action.partition(":")
        if verb == "keys":
            type_text(key, arg.replace("\\n", "\n"))
        elif verb == "cmd":
            chord(0x1D, ST_KEYS[arg.lower()])  # Control is the Mac's Command
        elif verb == "click":
            cv, ch = (int(x) for x in arg.split(","))
            mouse(cv, ch, 0)
            mouse(cv, ch, 1)
            mouse(cv, ch, 0)
        elif verb in ("press", "release"):  # a menu: press, release on an item
            cv, ch = (int(x) for x in arg.split(","))
            mouse(cv, ch, 1 if verb == "press" else 0)
        elif verb == "wait":
            if swapper:
                swapper.wait(float(arg))  # serving drive 2's swaps meanwhile
            else:
                time.sleep(float(arg))
        elif verb in ("fake-restart", "fake-down", "fake-up"):  # --fake-cart
            if not FAKE_CTX:
                hc.die(f"--then {verb} needs --fake-cart and --disk")
            if verb != "fake-up":
                fake_down(command)
            if verb == "fake-restart":  # fake-restart:SECONDS
                (swapper.wait if swapper else time.sleep)(float(arg))
            if verb != "fake-down":
                fake_up(command)
        elif verb == "regs":  # the CPU's registers into stderr.txt, for debugging
            command("hatari-debug r")
            time.sleep(0.5)
        elif verb == "mem":  # mem:ADDR,LEN: memory into stderr.txt, for debugging
            address, length = arg.split(",")
            command(f"hatari-debug m ${int(address, 0):x} {int(length, 0)}")
            time.sleep(0.5)
        elif verb == "shot":
            path = shot(arg)
            if path is None:
                steps.fail(action, "Hatari wrote no screenshot")
            steps.ok(f"{arg} {os.path.relpath(path, hc.REPO)}")
        else:
            hc.die(f"--then: unknown action {action!r}")
        if swapper:
            swapper.service()
        crash = trace.wait_for(r"(@@ (?:crash|halt|mac syserr) [^\n]*\n)", 1)
        if crash is not None:
            dump_memory(dumps)
            steps.fail(action, crash.group(1).decode().strip() +
                       f"; screenshot {shot('crash')}")


def run_launch(trace, shot, steps, need, command, glue_map, where, dump_memory, dumps,
               actions=(), disk_v=105, swapper=None):
    """EPIC-09: open drive 2's disk (or the startup disk, disk_v 45), then
    the program whose icon is at where ("v,h", in that window) as a person
    would: click it, Command-O. It
    has launched when the menu bar is no longer the Finder's (rows 2-18);
    a crash line fails the step at once. Then the actions (run_actions)."""
    key, chord, mouse = input_helpers(need, command, glue_map)
    # where: "v,h", or "v,h/v,h/...": folders to open on the way, the
    # program last.
    steps_to = [tuple(int(x) for x in point.split(",")) for point in where.split("/")]
    v, h = steps_to[-1]

    def menu_bar(png):
        _, _, rows = png
        return [bytes(1 if rows[y][x] < 128 else 0 for x in range(640)) for y in range(2, 18)]

    # The disk opened first (drive 2's icon under the startup disk's); a
    # window already open just comes to the front.
    mouse(disk_v, 600, 0)
    mouse(disk_v, 600, 1)
    mouse(disk_v, 600, 0)
    chord(0x1D, 0x18)
    time.sleep(2)
    finder = menu_bar(hc.read_png(shot("disk-window")))
    for number, (fv, fh) in enumerate(steps_to[:-1], 1):
        mouse(fv, fh, 0)
        mouse(fv, fh, 1)
        mouse(fv, fh, 0)
        chord(0x1D, 0x18)
        time.sleep(2)
        shot(f"folder-{number}")
    mouse(v, h, 0)
    mouse(v, h, 1)
    mouse(v, h, 0)
    chord(0x1D, 0x18)  # Control (Command) + O
    path = None
    for _ in range(45):
        crash = trace.wait_for(r"(@@ (?:crash|halt|mac syserr) [^\n]*\n)", 1)
        path = shot("launched")
        if path is None:
            steps.fail("launch", "Hatari wrote no screenshot")
        if crash is not None:
            dump_memory(dumps)
            steps.fail("launch", crash.group(1).decode().strip() + f"; screenshot {path}")
        if menu_bar(hc.read_png(path)) != finder:
            time.sleep(3)
            path = shot("launched")
            if path is None:
                steps.fail("launch", "Hatari wrote no screenshot")
            steps.ok(f"launched {os.path.relpath(path, hc.REPO)}")
            run_actions(trace, shot, steps, need, command, glue_map, actions, dump_memory,
                        dumps, swapper)
            return
    steps.fail("launch", f"the Finder's menu bar after 45 s; last screenshot {path}")


def run_input(trace, shot, steps, need, command, glue_map, restart):
    """EPIC-04: the IKBD as the Mac's keyboard and mouse, on the Finder.
    Keys go through Hatari's control socket and the real IKBD path; the
    cursor goes through the test builds' mailbox (glueTestInput), since the
    control socket cannot move the mouse. The Finder is used as a person
    would: select the disk, open it with Command-O, open the Apple menu,
    the Chooser and AppleTalk (which fails cleanly), then eject the disk
    with Command-Shift-1; then Special > Restart, and the Finder again from
    the same RAM disk (EPIC-08)."""
    _, chord, mouse = input_helpers(need, command, glue_map)

    def screen_dark(name):
        path = shot(name)
        return path, hc.dark_pixels(hc.read_png(path), 0, 20, 640, 400)

    _, desktop = screen_dark("desktop")

    # Select the disk (its icon at the top right) and open it.
    mouse(45, 600, 0)
    mouse(45, 600, 1)
    mouse(45, 600, 0)
    chord(0x1D, 0x18)  # Control (Command) + O
    need("Command-O", r"@@ key 3f\n", 20)
    time.sleep(1.5)
    path, dark = screen_dark("disk-window")
    if dark > desktop * 0.85:
        steps.fail("disk window", f"no window opened ({dark} dark pixels, desktop "
                   f"{desktop}) in {path}")
    steps.ok(f"disk window open {os.path.relpath(path, hc.REPO)}")

    # File > Open by the mouse, on the disk's window: press on File, drag to
    # Open (the menu's first item), release.
    target = os.environ.get("JH_OPEN")  # "v,h" of an icon in the disk's window
    if target:
        v, h = (int(x) for x in target.split(","))
        mouse(v, h, 0)
        mouse(v, h, 1)
        mouse(v, h, 0)
        chord(0x1D, 0x18)  # Command-O
        time.sleep(3)
        shot("opened")

    # The Apple menu.
    mouse(10, 20, 0)
    mouse(10, 20, 1)
    path = shot("apple-menu")
    box = (12, 22, 150, 82)
    if hc.dark_pixels(hc.read_png(path), *box) > (box[2] - box[0]) * (box[3] - box[1]) // 4:
        steps.fail("apple menu", f"no menu below the Apple in {path}")
    steps.ok(f"Apple menu open {os.path.relpath(path, hc.REPO)}")

    # EPIC-08 STORY-04: the Chooser, from the Apple menu. AppleTalk Active
    # fails cleanly (the glue marks both serial ports in use: no SCC), then
    # the Chooser closes. A window or alert is seen by its frame, a column
    # dark from row 90 to 240.
    def framed(name, *columns):
        path = shot(name)
        png = hc.read_png(path)
        return path, all(hc.dark_pixels(png, x, 90, x + 1, 240) == 150 for x in columns)

    mouse(59, 50, 1)  # drag to Chooser, release
    mouse(59, 50, 0)
    time.sleep(3)
    path, ok = framed("chooser", 121, 518)
    if not ok:
        steps.fail("chooser", f"no Chooser window in {path}")
    steps.ok(f"Chooser open {os.path.relpath(path, hc.REPO)}")
    for name, v, h in (("appletalk-active", 272, 395), ("appletalk-continue", 220, 291)):
        mouse(v, h, 0)
        mouse(v, h, 1)
        mouse(v, h, 0)
        time.sleep(3)
    path, ok = framed("appletalk-alert", 137, 445)
    if not ok:
        steps.fail("appletalk", f"no \"AppleTalk cannot be opened\" alert in {path}")
    steps.ok(f"AppleTalk fails cleanly {os.path.relpath(path, hc.REPO)}")
    # Continue, then Continue on "make sure that the AppleTalk network is
    # disconnected", as the Chooser goes back to Inactive.
    for _ in range(2):
        mouse(220, 291, 0)
        mouse(220, 291, 1)
        mouse(220, 291, 0)
        time.sleep(2)
    mouse(61, 133, 0)  # the Chooser's close box
    mouse(61, 133, 1)
    mouse(61, 133, 0)
    time.sleep(2)
    path, ok = framed("chooser-closed", 121)
    if ok:
        steps.fail("chooser closed", f"the Chooser is still open in {path}")
    steps.ok(f"Chooser closed {os.path.relpath(path, hc.REPO)}")

    # Command-Shift-1 (Control, Shift, 1): the Mac's own "eject disk 1",
    # through the glue's driver.
    chord(0x1D, 0x2A, 0x02)
    need("Command down", r"@@ key 6f\n", 20)
    need("Shift down", r"@@ key 71\n", 20)
    need("1 down", r"@@ key 25\n", 20)
    need("disk ejected", r"@@ disk eject\n", 20)

    # EPIC-08: Special > Restart. Press on Special, drag to Restart,
    # release; the Mac restarts through glueReset, which keeps the RAM disk.
    mouse(10, 185, 0)
    mouse(10, 185, 1)
    path = shot("special-menu")
    steps.ok(f"Special menu {os.path.relpath(path, hc.REPO)}")
    mouse(107, 190, 1)
    mouse(107, 190, 0)
    restart()


def run_disk2(trace, shot, steps, need):
    """EPIC-09: the disk in drive 2 is announced with a disk-inserted event
    and mounts: its icon appears under the startup disk's, mostly white
    against the desktop's 50 % grey (x 585-615, rows 85-117; the label's
    width depends on the name)."""
    # Posted as soon as the event queue exists, before the System loads, so
    # the line may already be behind the steps that matched since.
    if b"@@ disk2 inserted\n" in trace.data:
        steps.ok("disk 2 inserted")
    else:
        need("disk 2 inserted", r"@@ disk2 inserted\n", 30)
    path = None
    for _ in range(10):
        path = shot("disk2")
        if hc.dark_pixels(hc.read_png(path), 585, 85, 615, 117) < 0.4 * 30 * 32:
            steps.ok(f"disk 2 on the desktop {os.path.relpath(path, hc.REPO)}")
            return
        time.sleep(1)
    steps.fail("disk 2 on the desktop", f"no second disk icon in {path}")


def run_disk_boot(trace, shot, steps, need, ramdisk, dump_memory, dumps):
    """EPIC-03: the Mac boots from the RAM disk, past the Happy Mac, the
    System's patches (relocated: D-15) and the Finder's launch, to the
    Finder's desktop. A crash fails the step that was waiting."""
    need("boot blocks read", r"@@ disk\d? read 0\+2 0\n", 60)  # from any drive
    need("happy mac", r"@@ mac happy\n", 60)
    need("PTCH 117 relocated", r"@@ reloc 19012 bytes at [0-9a-f]{6} 299 of 299\n", 60)
    start = time.time()
    path = None
    while time.time() - start < 90:
        crash = trace.wait_for(r"(@@ crash [^\n]*\n)", 2)
        path = shot("finder")
        if crash is not None:
            dump_memory(dumps)
            steps.fail("finder", crash.group(1).decode().strip() + f"; screenshot {path}")
        if path and finder_on_screen(hc.read_png(path)):
            steps.ok(f"Finder's desktop {os.path.relpath(path, hc.REPO)}")
            return
    steps.fail("finder", f"no menu bar within 90 s; last screenshot {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("Usage:")[0])
    parser.add_argument("--image", choices=["st", "ste"], default="st",
                        help="the image, and the machine Hatari emulates")
    parser.add_argument("--memsize", type=int, choices=sorted(BANKS), default=1024,
                        help="ST RAM in KB (D-12: 1024, 2560, 4096)")
    parser.add_argument("--type", choices=["debug", "debug-test"], default="debug")
    parser.add_argument("--monitor", choices=["mono", "rgb"], default="mono",
                        help="rgb: the glue must ask for the mono monitor")
    parser.add_argument("--corrupt", action="store_true",
                        help="flip one byte of the fill: the image checksum must fail")
    parser.add_argument("--disk", metavar="FILE",
                        help="boot from this disk image in the RAM disk (raw, DiskCopy 4.2, "
                             "or MacBinary II around DiskCopy 4.2), or @NAME for tools/dev/dev.cfg's "
                             "[disks] NAME; needs --type debug-test")
    parser.add_argument("--disk2", metavar="FILE",
                        help="a second disk image, in drive 2 (EPIC-09), FILE or @NAME as "
                             "--disk; needs --disk")
    parser.add_argument("--swap", action="append", metavar="FILE",
                        help="another image for drive 2 (repeatable), FILE or @NAME: each time "
                             "the Mac ejects drive 2 the next goes in, then the first again "
                             "(EPIC-09, for Installers); also with --interactive")
    parser.add_argument("--launch", metavar="V,H[/V,H...]",
                        help="open the program whose icon is at V,H in drive 2's window, "
                             "or the startup disk's without --disk2 (click, Command-O), and "
                             "wait for its menu bar (EPIC-09); V,H/V,H opens folders on the "
                             "way; needs --disk")
    parser.add_argument("--then", action="append", metavar="ACTION",
                        help="after --launch, in order (repeatable): keys:TEXT (letters, "
                             "digits, space and marks; \\n for Return, \\t for Tab), cmd:KEY, click:V,H, "
                             "press:V,H, release:V,H, regs, mem:ADDR,LEN, fake-restart:SECONDS, fake-down, fake-up, "
                             "wait:SECONDS, shot:NAME")
    parser.add_argument("--save-disk", metavar="FILE",
                        help="at the end of a passing run, save the RAM disk (drive 1) as the "
                             "Mac left it, raw, for a later --disk (EPIC-09)")
    parser.add_argument("--fake-cart", nargs="?", const="", metavar="KEY=VALUE,...",
                        help="test builds: a fake Multi-device answers the glue (EPIC-05): "
                             "id=HEX, seconds=N (UTC), valid-at=N, pram=FILE (256 bytes), "
                             "zone=SECONDS")
    parser.add_argument("--fixed", action="store_true", help="the RAM disk is a hard disk")
    parser.add_argument("--partition", action="store_true",
                        help="wrap the volume in an Apple partition map, as a whole hard disk")
    parser.add_argument("--write-protect", action="store_true",
                        help="the RAM disk is write-protected")
    parser.add_argument("--input", action="store_true",
                        help="after the Finder, use it: open the disk, the Apple menu, "
                             "Command-Shift-1, then Special > Restart to the Finder again "
                             "(EPIC-04, EPIC-08); needs --disk")
    parser.add_argument("--dump", action="append", metavar="ADDR:LEN",
                        help="after a crash, have Hatari's debugger dump this memory range "
                             "(repeatable; e.g. 0xB30:32)")
    parser.add_argument("--watch", action="append", metavar="ADDR",
                        help="print the registers to stderr.txt whenever the CPU reaches ADDR "
                             "(repeatable)")
    parser.add_argument("--watch-mem", action="append", metavar="ADDR",
                        help="print the registers whenever the long at ADDR changes "
                             "(repeatable; slows the emulation)")
    parser.add_argument("--interactive", action="store_true",
                        help="open a window and hand over; no PASS/FAIL")
    args = parser.parse_args()
    if args.disk and args.disk.startswith("@"):
        try:
            args.disk = hc.configured_disk(args.disk[1:])
        except hc.HarnessError as err:
            hc.die(str(err))
    if args.disk2 and args.disk2.startswith("@"):
        try:
            args.disk2 = hc.configured_disk(args.disk2[1:])
        except hc.HarnessError as err:
            hc.die(str(err))
    if args.swap:
        try:
            args.swap = [hc.configured_disk(x[1:]) if x.startswith("@") else x
                         for x in args.swap]
        except hc.HarnessError as err:
            hc.die(str(err))
        if not args.disk2:
            hc.die("--swap needs --disk2")
    if args.disk2 and not args.disk:
        hc.die("--disk2 needs --disk")
    if args.save_disk and (not args.disk or args.interactive or args.partition):
        hc.die("--save-disk needs --disk, and neither --interactive nor --partition")
    if args.launch and not args.disk:
        hc.die("--launch needs --disk")
    if args.disk2 and args.input:
        hc.die("--input scripts the startup disk's desktop alone; drop --disk2")
    if args.disk and args.type != "debug-test":
        hc.die("--disk needs --type debug-test (the RAM disk is in test builds only)")
    if args.memsize == 2560 and args.image != "st":
        hc.die("2560 KB needs two different banks: only the st machine has them")

    label = f"hatari-{args.image}-ram{args.memsize}-{args.type}" + \
        ("-rgb" if args.monitor == "rgb" else "") + \
        ("-corrupt" if args.corrupt else "") + ("-disk" if args.disk else "") + \
        ("-hd" if args.partition else "") + ("-disk2" if args.disk2 else "") + \
        ("-interactive" if args.interactive else "")
    run_dir = hc.new_run_dir(label)
    os.makedirs(os.path.join(run_dir, "shots"))
    os.makedirs(os.path.join(run_dir, "home"))
    try:
        image = hc.merge_image(args.image, args.type, run_dir)
    except hc.HarnessError as err:
        hc.die(str(err))
    if args.corrupt:
        image = hc.corrupt_copy(image, args.image, os.path.join(run_dir, "corrupt.img"))
    glue_map = os.path.join(hc.REPO, "build", f"{args.image}-{args.type}", "GLUE.MAP")
    parse_path, ramdisk = (ramdisk_files(run_dir, args.memsize, glue_map, args.disk,
                                         args.fixed, args.write_protect, args.partition,
                                         args.disk2, args.swap or ())
                           if args.disk else (None, None))
    args.fake = None
    if args.fake_cart is not None:
        if args.type != "debug-test":
            hc.die("--fake-cart needs --type debug-test (the fake is in test builds only)")
        parse_path, args.fake = fake_cart_files(run_dir, glue_map, args.fake_cart, parse_path)
    if args.watch or args.watch_mem:
        parse_path = watch_files(run_dir, args.watch, args.watch_mem, parse_path)

    # A clean HOME: Hatari would otherwise load the user's saved configuration.
    env = dict(os.environ, HOME=os.path.join(run_dir, "home"))
    if not args.interactive:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

    # macOS caps a Unix socket path at 104 bytes; the run folder can be longer.
    sock_dir = tempfile.mkdtemp(prefix="jh", dir="/tmp")
    sock_path = os.path.join(sock_dir, "ctl.sock")
    server = None
    if not args.interactive or args.swap:
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(sock_path)
        server.listen(1)
        server.settimeout(20)

    stderr_path = os.path.join(run_dir, "stderr.txt")
    with open(stderr_path, "wb") as stderr_file, \
            open(os.path.join(run_dir, "stdout.txt"), "wb") as stdout_file:
        proc = subprocess.Popen(
            hatari_args(image, args, run_dir, sock_path, parse_path),
            env=env, stdout=stdout_file, stderr=stderr_file)
    reader = open(stderr_path, "rb")
    trace = hc.Trace(reader.read)
    steps = hc.Steps()
    print(f"{label}: {os.path.relpath(image, hc.REPO)}", flush=True)

    if args.interactive:
        print("Hatari is open; close it (or press Ctrl-C here) to end the run.", flush=True)
        try:
            if args.swap:
                # Serve drive 2's swaps while the user works.
                try:
                    conn, _ = server.accept()
                    swapper = DiskSwapper(
                        ramdisk, lambda text: conn.sendall((text + "\n").encode()),
                        glue_map, trace, lambda line: print(line, flush=True))
                    while proc.poll() is None:
                        swapper.service()
                        time.sleep(0.5)
                except (OSError, socket.timeout) as err:
                    print(f"drive 2 swaps stopped: {err}", flush=True)
            proc.wait()
        except KeyboardInterrupt:
            proc.send_signal(signal.SIGTERM)  # --confirm-quit false: no dialog
            try:
                proc.wait(10)
            except subprocess.TimeoutExpired:
                proc.kill()
        trace.drain(0.5)
        return hc.finish(label, run_dir, steps, None, trace, interactive=True)

    error = None
    conn = None
    try:
        conn, _ = server.accept()

        def shot(name):
            shots = os.path.join(run_dir, "shots")
            before = set(os.listdir(shots))
            # Fast-forward skips frames: give Hatari time to render one after
            # the glue's last drawing, or the shot shows a stale frame.
            time.sleep(0.3)
            conn.sendall(b"hatari-shortcut screenshot\n")
            end = time.time() + 5
            while time.time() < end:
                new = [f for f in os.listdir(shots) if f not in before]
                if new:
                    time.sleep(0.2)  # let Hatari finish writing it
                    path = os.path.join(shots, f"{name}.png")
                    os.replace(os.path.join(shots, new[0]), path)
                    return path
                time.sleep(0.05)
            return None

        def command(text):
            conn.sendall((text + "\n").encode())

        run_diag(trace, shot, steps, args, ramdisk, mac_rom_part(image, args.image),
                 make_dumper(command, run_dir), command, glue_map)
        if args.save_disk:
            # The RAM disk as the Mac left it, for the next session's --disk.
            out = os.path.abspath(args.save_disk)
            if os.path.exists(out):
                os.remove(out)
            command(f"hatari-debug savebin {out} ${ramdisk['base']:x} ${ramdisk['bytes']:x}")
            end = time.time() + 30
            while time.time() < end and not (os.path.exists(out) and
                                             os.path.getsize(out) == ramdisk["bytes"]):
                time.sleep(0.5)
            if not os.path.exists(out) or os.path.getsize(out) != ramdisk["bytes"]:
                steps.fail("save disk", f"Hatari wrote no {out}")
            steps.ok(f"disk saved to {os.path.relpath(out, hc.REPO)}")
    except (hc.HarnessError, socket.timeout, OSError) as err:
        error = str(err)
    finally:
        if conn is not None:
            try:
                conn.sendall(b"hatari-shortcut quit\n")
            except OSError:
                pass
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            # A fatal dialog (an image Hatari refuses) never quits by itself.
            proc.send_signal(signal.SIGKILL)
            proc.wait(5)
        trace.drain(0.5)
        reader.close()
        shutil.rmtree(sock_dir, ignore_errors=True)
    if proc.poll() is None:
        error = (error or "") + "; Hatari still running"
    return hc.finish(label, run_dir, steps, error, trace)


if __name__ == "__main__":
    sys.exit(main())
