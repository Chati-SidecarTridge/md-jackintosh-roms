"""
File: tools/dev/harness_common.py
Author: Diego Parrilla Santamaría
Date: 2026-10-18
Copyright: 2026 - GOODDATA LABS SL
Description: What the harness sessions share (EPIC-00 STORY-05): the
configuration, the run folder, the merged image, the trace reader and the
step record. From romemul-switcher-rom's tools/dev/harness_common.py.
Standard library only.
"""

import configparser
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
import jackrom  # noqa: E402  (the image layout, written once)
import macrom  # noqa: E402

DEFAULT_PATHS = {"hatari": "/usr/local/bin/hatari"}


class HarnessError(Exception):
    pass


def emulator_path(name):
    """The emulator binary from tools/dev/dev.cfg ([emulators]), else the default."""
    path = DEFAULT_PATHS[name]
    cfg_path = os.path.join(HERE, "dev.cfg")
    if os.path.exists(cfg_path):
        cfg = configparser.ConfigParser()
        cfg.read(cfg_path)
        path = cfg.get("emulators", name, fallback=path) or path
    if not os.access(path, os.X_OK):
        raise HarnessError(
            f"{name} not found at {path}; set it in tools/dev/dev.cfg "
            "(see dev.cfg.example)")
    return path


def version():
    with open(os.path.join(REPO, "version.txt"), encoding="utf-8") as handle:
        return handle.read().strip().lstrip("vV")


def new_run_dir(label):
    """A run folder under build/harness/ (git-ignored: the images and, later,
    the screenshots hold Apple's code and pictures, D-02)."""
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = os.path.join(REPO, "build", "harness", f"{stamp}-{label}")
    os.makedirs(path)
    return path


def merge_image(machine, build_type, run_dir):
    """The image under test: the glue tools/dev/build.sh made, merged with the
    configured Mac ROM into the run folder. Returns its path."""
    glue = os.path.join(REPO, "build", f"{machine}-{build_type}", "GLUE.BIN")
    if not os.path.exists(glue):
        raise HarnessError(
            f"{glue} is missing; build it with: tools/dev/build.sh {machine} {build_type}")
    image = os.path.join(run_dir, f"JACKINTOSH_{machine}.img")
    try:
        jackrom.merge(machine, glue, None, image)
    except (jackrom.JackRomError, macrom.MacRomError) as err:
        raise HarnessError(str(err)) from None
    return image


def corrupt_copy(image, machine, dst):
    """A copy with one byte flipped where the image checksum covers it but no
    code runs: 64 bytes before the end of the glue area, in the fill pattern."""
    _, glue_size, _ = jackrom.MACHINES[machine]
    with open(image, "rb") as handle:
        data = bytearray(handle.read())
    data[glue_size - 64] ^= 0xFF
    with open(dst, "wb") as handle:
        handle.write(data)
    return dst


class Trace:
    """The glue's trace as it arrives, searched forward from the last match."""

    def __init__(self, read_more):
        self._read_more = read_more  # returns new bytes, or b"" when none yet
        self.data = b""
        self.pos = 0

    def wait_for(self, pattern, timeout, consume=True):
        regex = re.compile(pattern.encode())
        end = time.time() + timeout
        while True:
            match = regex.search(self.data, self.pos)
            if match:
                if consume:
                    self.pos = match.end()
                return match
            if time.time() >= end:
                return None
            more = self._read_more()
            if more:
                self.data += more
            else:
                time.sleep(0.02)

    def drain(self, seconds):
        """Read whatever arrives for this long, matching nothing."""
        end = time.time() + seconds
        while True:
            more = self._read_more()
            if more:
                self.data += more
            elif time.time() >= end:
                return
            else:
                time.sleep(0.02)

    def lines(self):
        return [line for line in self.data.decode("latin-1").splitlines()
                if line.startswith("@@")]


class Steps:
    """Step-by-step PASS/FAIL record, printed as the session goes."""

    def __init__(self):
        self.done = 0
        self.start = time.time()

    def ok(self, name):
        self.done += 1
        print(f"  {time.time() - self.start:6.1f}s  ok    {name}", flush=True)

    def fail(self, name, why):
        print(f"  {time.time() - self.start:6.1f}s  FAIL  {name}: {why}", flush=True)
        raise HarnessError(f"{name}: {why}")

    def elapsed(self):
        return time.time() - self.start


def finish(label, run_dir, steps, error, trace, interactive=False):
    with open(os.path.join(run_dir, "trace.txt"), "w", encoding="latin-1") as handle:
        handle.write("\n".join(trace.lines()) + "\n")
    if interactive:
        print(f"ENDED {label}: interactive run, {len(trace.lines())} trace lines "
              f"({run_dir})")
        return 0
    if error is None:
        print(f"PASS {label}: {steps.done} steps, {steps.elapsed():.0f} s ({run_dir})")
        return 0
    print(f"FAIL {label}: {error} ({run_dir})")
    return 1


def die(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(2)
