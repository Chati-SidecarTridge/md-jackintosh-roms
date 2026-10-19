#!/usr/bin/env python3
"""tools/dev/mkhd.py: a hard-disk image for the harness's RAM disk, built on
the host from items of Mac disk images (EPIC-09 STORY-04).

Programs that install, or build, need a writable volume with room; the
floppies they ship on have none. This copies chosen top-level items (files
and folders, with their resource forks, types and creators) of each source
disk into one HFS volume. A "System Folder" with System and Finder makes it
bootable: machfs writes the boot blocks from the System's 'boot' 1 and
blesses that folder. The image holds Apple's and others' software: write it
under build/ (git-ignored, D-02).

It needs machfs (MIT, https://github.com/elliotnunn/machfs), installed in a
virtual environment so the system Python stays as it is:

  python3 -m venv build/venv && build/venv/bin/pip install machfs==1.3
  build/venv/bin/python tools/dev/mkhd.py -o build/hd/pascal.raw --size 2304 \\
      --name "Jackintosh HD" "@system608:System Folder" \\
      "@thinkpascal402:THINK Pascal 4.0.2" "THINK Pascal 2.image.bin:Interfaces & Libs.sea"

  python3 tools/dev/hatari_harness.py --image st --memsize 4096 --type debug-test \\
      --disk build/hd/pascal.raw --fixed

Usage: mkhd.py -o OUT --size KB [--name NAME] DISK:ITEM ...
DISK is an image (raw, DiskCopy 4.2, or MacBinary II around it) or @NAME from
tools/dev/dev.cfg's [disks]; ITEM a file or folder on it, FOLDER/NAME for one
inside a folder; each lands at the top of the new volume.
"""

import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harness_common as hc  # noqa: E402

try:
    from machfs import Volume
except ImportError:
    hc.die("mkhd.py needs machfs: python3 -m venv build/venv && "
           "build/venv/bin/pip install machfs==1.3, then run it with build/venv/bin/python")


def read_volume(disk):
    if disk.startswith("@"):
        disk = hc.configured_disk(disk[1:])
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "disk.raw")
        hc.raw_disk_image(disk, raw)
        with open(raw, "rb") as handle:
            data = handle.read()
    volume = Volume()
    volume.read(data)
    return volume


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--size", type=int, required=True, metavar="KB",
                        help="the volume's size in KB")
    parser.add_argument("--name", default="Jackintosh HD")
    parser.add_argument("items", nargs="+", metavar="DISK:ITEM")
    args = parser.parse_args()

    out = Volume()
    out.name = args.name
    volumes = {}
    for spec in args.items:
        disk, sep, item = spec.rpartition(":")
        if not sep or not disk:
            hc.die(f"{spec}: expected DISK:ITEM")
        if disk not in volumes:
            try:
                volumes[disk] = read_volume(disk)
            except (hc.HarnessError, OSError, ValueError) as err:
                hc.die(f"{disk}: {err}")
        node = volumes[disk]
        for part in item.split("/"):
            if not hasattr(node, "items") or part not in node:
                listing = ", ".join(node) if hasattr(node, "items") else "a file"
                hc.die(f"{disk}: no {item!r} ({part!r} not in: {listing})")
            node = node[part]
        out[item.split("/")[-1]] = node
    # The icons keep their places from the source disks, which may be out of
    # the new window's view: lay the top level out on a grid instead (the
    # Finder's location is (v, h); machfs packs x first).
    for index, item in enumerate(out.values()):
        item.x = 30 + 60 * (index // 5)
        item.y = 20 + 90 * (index % 5)
    try:
        image = out.write(size=args.size * 1024)
    except ValueError as err:
        hc.die(f"{err}: make --size larger")
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as handle:
        handle.write(image)
    print(f"{args.output}: \"{args.name}\", {args.size} KB, {len(args.items)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
