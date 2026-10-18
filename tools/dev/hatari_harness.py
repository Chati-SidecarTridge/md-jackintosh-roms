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
                         [--corrupt] [--preload] [--interactive]

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
SCREEN_BYTES = 32000
PRELOAD_BELOW_TOP = 0x40000  # src/glue/mem.h, GLUE_PRELOAD_BELOW_TOP_BYTES
PRELOAD_MAGIC = 0x4A4B504C   # "JKPL", GLUE_PRELOAD_MAGIC
PRELOAD_LENGTH = 4096
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


def preload_files(run_dir, memsize, glue_map):
    """A blob for the glue's preload probe, and the debugger scripts that load
    it when the CPU reaches _glueMain (from the glue's link map). A loadbin
    straight from --parse runs before the emulation starts and its bytes are
    gone by the first instruction, and a breakpoint on the reset PC itself
    never fires; a one-shot breakpoint on _glueMain with :trace (go on, no
    prompt) and :file (run the loadbin) works. Returns the --parse script and
    the trace line the glue must print."""
    payload = bytes((i * 7 + (i >> 8)) & 0xFF for i in range(PRELOAD_LENGTH))
    blob = os.path.join(run_dir, "preload.bin")
    with open(blob, "wb") as handle:
        handle.write(struct.pack(">II", PRELOAD_MAGIC, len(payload)) + payload)
    address = memsize * 1024 - PRELOAD_BELOW_TOP
    load = os.path.join(run_dir, "preload-load.ini")
    with open(load, "w", encoding="ascii") as handle:
        handle.write(f"loadbin {blob} ${address:x}\n")
    entry = glue_symbol(glue_map, "_glueMain")
    script = os.path.join(run_dir, "preload.ini")
    with open(script, "w", encoding="ascii") as handle:
        handle.write(f"b pc = ${entry:x} :trace :once :file {load}\n")
    return script, f"@@ preload {len(payload)} {sum(payload):08x}\n"


def mac_rom_part(image, machine):
    """What the glue must report about the Mac ROM part of the merged image:
    its computed checksum (patches change it) and its reset vector."""
    _, glue_size, _ = hc.jackrom.MACHINES[machine]
    with open(image, "rb") as handle:
        rom = handle.read()[glue_size:]
    return {"computed": hc.macrom.rom_checksum(rom),
            "reset": struct.unpack(">I", rom[4:8])[0]}


def run_diag(trace, shot, steps, args, preload_line, mac):
    """The EPIC-00 session: the diagnostic screen and its trace."""
    bank0, bank1 = BANKS[args.memsize]
    ram = args.memsize * 1024
    mmu = (MMU_CODES[bank0] << 2) | MMU_CODES[bank1]
    screen = (ram - SCREEN_BYTES) & ~0xFF

    def need(name, pattern, timeout=STEP_TIMEOUT):
        if trace.wait_for(pattern, timeout) is None:
            path = shot(f"fail-{name.replace(' ', '-')}")
            steps.fail(name, f"no {pattern!r} within {timeout} s"
                       + (f"; screenshot {path}" if path else ""))
        steps.ok(name)

    machine = args.image.upper()
    need("boot", rf"@@ boot {machine} {hc.version()} \S+\n")
    need("ram", rf"@@ ram {ram} mmu {mmu:02x}\n")
    if preload_line:
        need("preload", preload_line)
    else:
        need("no preload", r"@@ preload none\n")
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
    need("idle", r"@@ idle\n")
    path = shot("diag")
    if path is None:
        steps.fail("screenshot", "Hatari wrote no screenshot")
    steps.ok(f"screenshot {os.path.relpath(path, hc.REPO)}")


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
    parser.add_argument("--preload", action="store_true",
                        help="preload a blob below the top of RAM: the glue must sum it")
    parser.add_argument("--interactive", action="store_true",
                        help="open a window and hand over; no PASS/FAIL")
    args = parser.parse_args()
    if args.memsize == 2560 and args.image != "st":
        hc.die("2560 KB needs two different banks: only the st machine has them")

    label = f"hatari-{args.image}-ram{args.memsize}-{args.type}" + \
        ("-rgb" if args.monitor == "rgb" else "") + \
        ("-corrupt" if args.corrupt else "") + ("-preload" if args.preload else "") + \
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
    parse_path, preload_line = (preload_files(run_dir, args.memsize, glue_map)
                                if args.preload else (None, None))

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

        run_diag(trace, shot, steps, args, preload_line, mac_rom_part(image, args.image))
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
