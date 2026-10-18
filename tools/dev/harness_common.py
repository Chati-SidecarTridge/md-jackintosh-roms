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


def configured_disk(name):
    """A disk image named in tools/dev/dev.cfg ([disks] NAME = path). Disk
    images hold Apple's system software: they stay outside the repository
    (D-02)."""
    cfg = configparser.ConfigParser()
    cfg.read(os.path.join(HERE, "dev.cfg"))
    path = cfg.get("disks", name, fallback="").strip()
    if not path:
        raise HarnessError(f"no [disks] {name} in tools/dev/dev.cfg (see dev.cfg.example)")
    path = os.path.expanduser(path)
    if not os.path.exists(path):
        raise HarnessError(f"[disks] {name}: {path} does not exist")
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


def raw_disk_image(src, dst):
    """A raw 512-byte-block image of src at dst: a raw image as it is, a
    DiskCopy 4.2 image's data (at $54), or the same inside a MacBinary II
    wrapper (data fork at 128), as hampa.ch's System disks come. The
    DiskCopy data checksum is checked. Returns the size in bytes."""
    import struct
    with open(src, "rb") as handle:
        data = handle.read()
    if len(data) > 128 and data[0] == 0 and data[65:69] == b"dImg":
        data = data[128:]
    if len(data) > 0x54 and data[0x52:0x54] == b"\x01\x00":
        size, _, checksum = struct.unpack(">III", data[0x40:0x4C])
        raw = data[0x54:0x54 + size]
        total = 0
        for (word,) in struct.iter_unpack(">H", raw):
            total = (total + word) & 0xFFFFFFFF
            total = ((total >> 1) | (total << 31)) & 0xFFFFFFFF
        if total != checksum:
            raise HarnessError(f"{src}: DiskCopy data checksum {total:08x}, "
                               f"expected {checksum:08x}")
        data = raw
    if not data or len(data) % 512:
        raise HarnessError(f"{src}: {len(data)} bytes is not a whole number of blocks")
    with open(dst, "wb") as handle:
        handle.write(data)
    return len(data)


def partitioned_copy(raw, dst):
    """A whole-disk image around a raw HFS volume, as a SCSI disk is laid
    out: a driver descriptor ("ER") in block 0, a two-entry Apple partition
    map from block 1 (the map, then the Apple_HFS partition at block 64),
    then the volume. Returns the size in bytes."""
    import struct
    with open(raw, "rb") as handle:
        volume = handle.read()
    start, blocks = 64, len(volume) // 512
    total = start + blocks

    def entry(count, pstart, pblocks, name, ptype):
        block = struct.pack(">HHIII32s32s", 0x504D, 0, count, pstart, pblocks,
                            name.encode(), ptype.encode())
        return block.ljust(512, b"\0")

    ddm = struct.pack(">HHI", 0x4552, 512, total).ljust(512, b"\0")
    pmap = (entry(2, 1, start - 1, "Apple", "Apple_partition_map")
            + entry(2, start, blocks, "MacOS", "Apple_HFS"))
    data = ddm + pmap + b"\0" * ((start - 3) * 512) + volume
    with open(dst, "wb") as handle:
        handle.write(data)
    return len(data)


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


def read_png(path):
    """A PNG as (width, height, rows of grey levels 0-255): enough for the
    pixel probes. 8-bit greyscale, RGB, RGBA or palette; standard library
    only."""
    import struct
    import zlib
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise HarnessError(f"{path} is not a PNG")
    pos, idat, palette = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
        elif kind == b"PLTE":
            palette = chunk
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    if depth != 8:
        raise HarnessError(f"{path}: bit depth {depth} not supported")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        kind, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b, c = prev[x], (prev[x - channels] if x >= channels else 0)
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif kind == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 0xFF
        prev = line
        if colour == 3:
            rows.append([palette[3 * v] for v in line])
        else:
            rows.append([line[x] for x in range(0, stride, channels)])
    return width, height, rows


def dark_pixels(png, x0, y0, x1, y1):
    """How many pixels in [x0, x1) x [y0, y1) are dark (< 128)."""
    _, _, rows = png
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if rows[y][x] < 128)


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

    def skip(self):
        """Forget what has arrived so far: the next search starts after it."""
        self.drain(0)
        self.pos = len(self.data)

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
