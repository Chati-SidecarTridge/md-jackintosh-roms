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
        return cmd
    return cmd + ["--fast-forward", "yes", "--fast-forward-key-repeat", "false",
                  "--statusbar", "false", "--drive-led", "false",
                  "--control-socket", sock_path,
                  "--screenshot-dir", os.path.join(run_dir, "shots")]


def glue_symbol(glue_map, name):
    """A symbol's address from the glue's link map (GLUE.MAP)."""
    symbols = hc.jackrom.glue_symbols(glue_map)
    if name not in symbols:
        raise hc.HarnessError(f"{name} not in {glue_map}")
    return symbols[name]


def ramdisk_files(run_dir, memsize, glue_map, disk, fixed, write_protect, partition=False):
    """The test builds' RAM disk (src/glue/ramdisk.c): the raw image placed
    64 KB-aligned below the top of RAM, a 16-byte descriptor ("JKRD", base,
    bytes, flags) in the last 16 bytes, and the debugger scripts that load
    both when the CPU reaches _glueMain, after the glue has filled the RAM
    with $FF. A loadbin straight from --parse is lost before the first
    instruction, and a breakpoint on the reset PC never fires; a one-shot
    breakpoint with :trace (go on, no prompt) and :file (run the loadbins)
    works. Returns the --parse script and the image's base."""
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
    load = os.path.join(run_dir, "ramdisk-load.ini")
    with open(load, "w", encoding="ascii") as handle:
        handle.write(f"loadbin {raw} ${base:x}\n")
        handle.write(f"loadbin {desc} ${top - RAMDISK_DESCRIPTOR:x}\n")
    entry = glue_symbol(glue_map, "_glueMain")
    script = os.path.join(run_dir, "ramdisk.ini")
    with open(script, "w", encoding="ascii") as handle:
        handle.write(f"b pc = ${entry:x} :trace :once :file {load}\n")
    return script, {"base": base, "bytes": size, "flags": flags}


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


def run_diag(trace, shot, steps, args, ramdisk, mac, dump_memory=lambda specs: None,
             command=None, glue_map=None):
    """The EPIC-00 session: the diagnostic screen and its trace."""
    bank0, bank1 = BANKS[args.memsize]
    ram = args.memsize * 1024
    mmu = (MMU_CODES[bank0] << 2) | MMU_CODES[bank1]
    memtop = ramdisk["base"] if ramdisk else ram
    screen = memtop - MAC_SCREEN_BELOW_MEMTOP

    def need(name, pattern, timeout=STEP_TIMEOUT):
        # A crash line from the glue's debug handlers ends the session at once.
        match = trace.wait_for(rf"(?:{pattern})|(@@ crash [^\n]*\n)", timeout)
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
    if args.type == "debug-test":
        # EPIC-04: writes to $0-$7 skipped by the glue's bus-error handler.
        need("bus error self-test", r"@@ buserr selftest ok fixes [1-9]\d*\n")
    # EPIC-02: into the Mac ROM, to the "?" disk and its boot loop.
    need("mac enter", rf"@@ mac enter [0-9a-f]{{6}} memtop {memtop:06x} screen {screen:06x}\n")
    need("disk driver", r"@@ mac sony 0 dce [0-9a-f]{6}\n")
    need("glue hook", r"@@ mac hook\n")
    if ramdisk:
        run_disk_boot(trace, shot, steps, need, ramdisk, dump_memory, args.dump)
        if args.input:
            run_input(trace, shot, steps, need, command, glue_map)
        return
    plot = trace.wait_for(r"@@ mac plotdisk [0-9a-f]{6} vbl (\d+)\n", 60, consume=False)
    need("question-mark disk", r"@@ mac plotdisk [0-9a-f]{6} vbl \d+\n", 60)
    steps.boot_seconds = int(plot.group(1)) / MONO_VBL_HZ if plot else None
    need("boot loop polls the drive", r"@@ mac read offline\n", 60)
    # The "?" blinks: shoot until one frame shows it in the disk's label
    # (src/glue/mac.c: the icon at x 304, y 174; the symbol 15 rows down,
    # 1 byte right, 16x15 pixels).
    for attempt in range(8):
        path = shot(f"question-disk-{attempt}")
        if path is None:
            steps.fail("screenshot", "Hatari wrote no screenshot")
        if hc.dark_pixels(hc.read_png(path), 312, 189, 328, 204) > 0:
            steps.ok(f"\"?\" disk on screen {os.path.relpath(path, hc.REPO)}")
            break
    else:
        steps.fail("\"?\" disk on screen", f"no \"?\" in the disk's label in 8 shots; last {path}")
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


def run_input(trace, shot, steps, need, command, glue_map):
    """EPIC-04: the IKBD as the Mac's keyboard and mouse, on the Finder.
    Keys go through Hatari's control socket and the real IKBD path; the
    cursor goes through the test builds' mailbox (glueTestInput), since the
    control socket cannot move the mouse. The Finder is used as a person
    would: select the disk, open it with Command-O, open the Apple menu,
    then eject the disk with Command-Shift-1."""
    mailbox = glue_symbol(glue_map, "_glueTestInput")
    seq = [0]

    def key(code, down):
        command(f"hatari-event {'keydown' if down else 'keyup'} 0x{code:02x}")
        time.sleep(0.2)

    def chord(*codes):
        for code in codes:
            key(code, True)
        for code in reversed(codes):
            key(code, False)

    def mouse(v, h, buttons):
        seq[0] += 1
        command(f"hatari-debug memwrite w ${mailbox + 2:x} ${v & 0xFFFF:x} "
                f"${h & 0xFFFF:x} ${buttons:x}")
        time.sleep(0.3)
        command(f"hatari-debug memwrite w ${mailbox:x} ${seq[0]:x}")
        need(f"cursor at {v},{h} buttons {buttons}",
             rf"@@ input {seq[0]} at {v},{h} buttons {buttons}\n", 20)
        time.sleep(0.5)

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
    mouse(10, 20, 0)

    # Command-Shift-1 (Control, Shift, 1): the Mac's own "eject disk 1",
    # through the glue's driver.
    chord(0x1D, 0x2A, 0x02)
    need("Command down", r"@@ key 6f\n", 20)
    need("Shift down", r"@@ key 71\n", 20)
    need("1 down", r"@@ key 25\n", 20)
    need("disk ejected", r"@@ disk eject\n", 20)


def run_disk_boot(trace, shot, steps, need, ramdisk, dump_memory, dumps):
    """EPIC-03: the Mac boots from the RAM disk, past the Happy Mac, the
    System's patches (relocated: D-15) and the Finder's launch, to the
    Finder's desktop. A crash fails the step that was waiting."""
    need("boot blocks read", r"@@ disk read 0\+2 0\n", 60)
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
    parser.add_argument("--fixed", action="store_true", help="the RAM disk is a hard disk")
    parser.add_argument("--partition", action="store_true",
                        help="wrap the volume in an Apple partition map, as a whole hard disk")
    parser.add_argument("--write-protect", action="store_true",
                        help="the RAM disk is write-protected")
    parser.add_argument("--input", action="store_true",
                        help="after the Finder, type Command-Shift-1 and open the Apple menu "
                             "(EPIC-04); needs --disk")
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
    if args.disk and args.type != "debug-test":
        hc.die("--disk needs --type debug-test (the RAM disk is in test builds only)")
    if args.memsize == 2560 and args.image != "st":
        hc.die("2560 KB needs two different banks: only the st machine has them")

    label = f"hatari-{args.image}-ram{args.memsize}-{args.type}" + \
        ("-rgb" if args.monitor == "rgb" else "") + \
        ("-corrupt" if args.corrupt else "") + ("-disk" if args.disk else "") + \
        ("-hd" if args.partition else "") + \
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
                                         args.fixed, args.write_protect, args.partition)
                           if args.disk else (None, None))
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
    if not args.interactive:
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
