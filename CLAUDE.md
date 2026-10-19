# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. It is the single source of project context for coding agents; there is no separate `AGENTS.md`.

## What this is

**Jackintosh ROMs**: boot ROM images that turn an **Atari ST** or **Atari STE** into a **Macintosh Plus** at power-on, in the spirit of Spectre GCR / Spectre 128 but with no TOS, no GEM and no emulator program. The Atari's own 68000 runs the genuine Mac Plus ROM code natively; a layer of our own code (the **glue**) replaces everything the Mac ROM expects of Macintosh hardware that the Atari does not have.

The pieces of the product:

- **This repository**: the glue (Atari boot, RAM sizing, interrupts and timing, Shifter video, IKBD keyboard and mouse, a Mac disk driver, the cartridge protocol), the **patch table** applied to the Mac Plus ROM, and the host tool that merges a Mac Plus ROM with them into one boot image.
- **The SidecarTridge TOS Emulator** (firmware `../sidecartos`) sits in the TOS ROM sockets and serves our image as the system ROM: 192 KB at `$FC0000` on the ST, 256 KB at `$E00000` on the STE.
- **The SidecarTridge Multi-device** in the cartridge port runs the **md-jackintosh** microfirmware (`../md-jackintosh`), which serves Macintosh floppy and hard-disk images from its microSD card over the cartridge bus (ROM3 commands at `$FB0000`, ROM4 shared window at `$FA0000`).

Design reference: `jonathanschilling/mac_rom` (<https://github.com/jonathanschilling/mac_rom>), a commented, buildable 68000 source tree that assembles bit-identical Mac 128K, 512K and Plus v1–v3 ROMs. Spectre is a reference to study (Diego, 2026-10-18: "we can have a look at Spectre to implement our own jackintosh"): its manuals and binaries may be examined to see how it solved each Atari-vs-Mac problem before we decide ours, but none of its code or bytes enters this repository (it is proprietary).

### The Apple bytes rule (D-02)

The Mac Plus ROM is Apple's copyrighted code, also when it is assembled from mac_rom (whose MIT licence covers the annotations, not Apple's code). Therefore:

- **No Apple-derived byte is ever committed or published**: no ROM dump, no assembled mac_rom output, no merged boot image, no disk image with Apple system software, and no hex dump of ROM regions in docs or code. A doc may quote a few instructions by mnemonic, with their mac_rom file:line, to explain a patch.
- The repository and its releases carry only our glue, our patch table and the merge tool. The user supplies the Mac Plus v3 ROM (their own dump, or a local mac_rom build) and the tool checks it before merging: 131072 bytes, stored and computed checksum `$4D1F8172`, version word `$0075` at offset 8.
- `.gitignore` excludes `*.ROM`, `*.rom`, `*.img`, `*.dsk`, `*.dc42`, `roms/`, `disks/` and `build/`. Do not weaken it. Local ROMs and disk images live outside the tree or under `build/`, named in `tools/dev/dev.cfg` (git-ignored).

### Sibling repositories

- `../md-jackintosh`: the Multi-device microfirmware. It owns the RP side of the cartridge protocol and the disk images. **The two repositories are developed in sync** (D-11): a change to what they share lands in both, agreed first.
- `../sidecartos`: the TOS Emulator firmware; its `CLAUDE.md` is the authority on how an image reaches the TOS ROM sockets (sizes, names, the rescue switcher).
- `../romemul-switcher-rom`: another bare-metal ROM for the same TOS Emulator, written by the same team. Its build arrangement (pinned Docker toolchain, out-of-tree builds, build ID), its Hatari harness (`tools/dev/hatari_harness.py`), its RAM sizing (`src/st/memconf.inc`), its Shifter setup and its NatFeats trace are the models for ours. Copy from it rather than reinventing; read-only from here. Unlike there, our backlog is never committed (D-01).
- `../md-microfirmware-template`, `../md-drives-emulator`: the Multi-device template and the drives emulator, for the cartridge protocol as the ST side speaks it under TOS.

### The shared contract with md-jackintosh

- The cartridge command protocol as the template defines it (`../md-jackintosh/programming.md`, "The Transmision Protocol"): each 16-bit word `w` is sent as a read of `$FB8000` plus `w` sign-extended, i.e. `$FB0000 + (w XOR $8000)` (the template's sender sets `a0 = $FB8000` and reads `tst.b (a0,dN.w)`, `target/atarist/src/inc/sidecart_functions.s` 164–165; the RP XORs with `CHANDLER_ADDRESS_HIGH_BIT`, `rp/src/chandler.c:229`); reuse that addressing as it is; header `$ABCD`, command, payload size, random token, payload, checksum; the answer is the token echoed at `RANDOM_TOKEN_ADDR` (`$FA2004`) plus a moved seed at `RANDOM_TOKEN_SEED_ADDR` (`$FA2008`).
- The shared 64 KB window layout (`rp/src/include/chandler.h` there): sentinel `$FA2000`, shared variables `$FA2010`, `APP_FREE` from `$FA2300`, framebuffer `$FAE0C0`. Never hard-code an offset we do not also define symbolically.
- md-jackintosh's app `$07` (`rp/src/include/macdisk.h` there, agreed 2026-10-19, mirrored in our `src/glue/cart.h`): `$0700`–`$0702` disk READ/WRITE/EJECT, `$0703` TIME, `$0704`/`$0705` PRAM; the status block at `APP_FREE` (result, time, identity `$4A4B0001`, the unit table at `+$10`) and the buffer at `+$100`.
- Under TOS the cartridge's own m68k code (`BOOT.BIN`) runs from the cartridge header. **Here nothing runs it**: there is no TOS. Our ROM speaks the protocol itself, starting with `CMD_ST_HELLO` (`$FF01`) at boot. md-jackintosh must not depend on its cartridge code having run.

How to keep in sync: before changing anything above, read md-jackintosh's current source (read-only). If its Claude session is running, find it with `ListAgents` and tell it with `SendMessage` what changes, with exact values; verify what it reports against its source. If it is not running, tell Diego which md-jackintosh files must change and leave that repo untouched. Cite its backlog items as "md-jackintosh EPIC-NN / D-NN"; it cites this one as "md-jackintosh-roms EPIC-NN".

## Architecture (planned; EPIC-00 and EPIC-01 confirm it)

One image per machine, the glue first so that the image starts with an Atari reset header:

| machine | image | glue | Mac Plus ROM (`ROMBase`) | free for glue |
|---|---|---|---|---|
| `st` (ST/STF/STFM) | 192 KB at `$FC0000` | `$FC0000`–`$FCFFFF` | `$FD0000`–`$FEFFFF` | 64 KB |
| `ste` (STE) | 256 KB at `$E00000` | `$E00000`–`$E1FFFF` | `$E20000`–`$E3FFFF` | 128 KB |

- **Reset header**: the first 8 bytes are the 68000's SSP and PC (the ST mirrors them at `$0`). Hatari also requires a TOS-like header: version word at bytes 2–3 in `$0100`–`$06FF` and the base address at bytes 8–11 (`$FC0000` or `$E00000`), and at most 224 KB at `$FC0000` (`../hatari/src/tos.c`, `TOS_LoadImage`) (C-05).
- **The Mac ROM keeps every byte offset** (D-08). System 6.0.8's patch set for the Plus (`PTCH` 117) depends on 248 exact ROM offsets and checks return addresses, so the ROM part of the image is the Plus v3 ROM with same-length patches only. A patch is usually a `JMP`/`JSR` absolute into the glue, padded to the original length.
- **System 6's absolute ROM addresses are relocated at load time** (D-15). `PTCH` 117 holds its ROM targets as absolute `$40xxxx` addresses (295 of them, never through `ROMBase`), which point at nothing once the ROM sits at `$FD0000`/`$E20000`. The glue wraps `jCheckLoad` (`$7F0`) and adds `ROMBase − $400000` at listed offsets of known resources (type, ID, SHA-256). Details: `docs/macplus/system6-offsets.md` (local, not committed).
- **Patch table** (`src/patches/`, EPIC-01): each patch is an offset into the Mac ROM, the length and a hash of the original bytes it expects (the hash, not the bytes, per D-02), the replacement of the same length, and the reason with a mac_rom file:line. The merge tool refuses a ROM whose bytes differ at any patch site.
- **What the glue replaces**: RAM sizing (the ST MMU, as TOS does) and Mac low memory; the Mac's `$40xxxx` literals and its ROM-mirror and SCSI probes (they read `$420000`/`$440000`, which is a bus error on an ST); the VIA (60.15 Hz tick, one-second interrupt, keyboard shift register, RTC/PRAM), the SCC (mouse), the IWM (`.Sony`), the SCSI chip and the sound buffer; video (`ScrnBase`, `ScreenRow` 80, 640x400 on a monochrome monitor (an SM124 or any mono display, VGA adapters included), D-04); IKBD keyboard and mouse into Mac events; a Mac block-device driver with a RAM-disk backend for Hatari and a cartridge backend for md-jackintosh.

Known hazards of running Mac code on Atari hardware. EPIC-01 checked each against the source; the full study is in `docs/macplus/` (local, never committed: `boot-map.md`, `hardware-use.md`, `atari-conflicts.md`, `system6-offsets.md`), and the decisions are D-15 to D-19:

- `$0`–`$7` is ROM on the ST and `$0`–`$7FF` is supervisor-only. Mac code runs in supervisor mode, so the second is harmless; writes to `$0`–`$7` are a bus error. `Launch` writes `ResetStackPtr` (`$0000`) on every application launch (C-09, patched), and Mac programs write through nil handles: the glue's bus-error handler (`buserr.c`) skips any write to `$0`–`$7` and resumes after the instruction, found by decoding back from the stacked PC (Hatari's instruction register is the next opcode for read-modify-write instructions).
- The MFP's vectored interrupts go to `$100`–`$13F` under TOS, which on the Mac is low memory (`ScrVRes`, `ScreenRow`, `MemTop` …). The MFP vector register points at `$30` instead (vectors 48–63 at `$C0`–`$FC`), and `SysErrInit`'s vector fill, which writes `$80`–`$FF`, stops at vector 47 (D-17).
- Autovector levels mean different things: the Atari's HBL is level 2, where the Mac has the SCC; its VBL is level 4, where the Mac has the interrupt switch, and left to the ROM `$70` is the "unexpected interrupt" system error. The Mac runs at IPL 0, so the HBL fires about 15,700 (colour) or 35,800 (mono: 501 lines at 71.2 Hz) times a second; the glue's HBL handler raises the interrupted IPL to 2 (D-17).
- Accesses above the installed RAM or to the Mac's device addresses (`$EFE1FE` VIA, `$9FFFF8`/`$BFFFF9` SCC, `$DFE1FF` IWM, `$580000` SCSI) are bus errors on the ST, but on the STE the VIA reads silently return ROM bytes (C-07). Most run-time access goes through the low-memory pointers `VIA`, `SCCRd`, `SCCWr`, `IWM`, which the glue points at RAM shadows; the literal accesses are patched. Prefer patching the code over emulating faulting accesses (Spectre did some of the latter; it is slow and fragile on a 68000).
- 44 operands use the absolute top-of-RAM area `$3FFCxx` (`SERegs`), which a real Plus mirrors into its RAM and which is empty address space below 4 MB on an ST (C-08).
- The Mac tick is 60.15 Hz; the mono VBL is 71.2 Hz. `Ticks` comes from MFP Timer C at 60.235 Hz (D-18) and must step by exactly 1 (`DragTheRgn` waits for an exact value); the keyboard driver clears `KeyMap` unless the keyboard answers at least every 32 ticks.
- The ROM's screen sits at `MemTop − $5900` (the source comment's `$5C00` is wrong); ours moves to `MemTop − $8100` for 640x400 (D-19).
- The ST Shifter needs the screen on a 256-byte boundary (word on the STE); set the resolution only inside the vertical blank (romemul-switcher-rom EPIC-03 STORY-14: set mid-frame, a real Mega ST came up wrapped by 4 bytes; Hatari does not show it).

## Common commands

```bash
tools/dev/build.sh st release      # the glue, out of tree into build/st-release/ (GLUE.BIN, GLUE.MAP, obj/)
tools/dev/build.sh ste debug-test  # machines: st | ste; types: release | debug | test | debug-test
tools/macrom.py build              # assemble the mac_rom submodule's Plus v3 into build/macrom/, then check it
tools/macrom.py check <file>       # is it the Plus v3 ROM? (131072 bytes, checksum $4D1F8172, version $0075)
tools/jackrom.py merge --machine st --glue build/st-release/GLUE.BIN -o build/st.img   # [--mac-rom FILE]
tools/dev/all_harness.sh           # build st/ste release+debug+debug-test, run every Hatari session (~4 min)
python3 tools/dev/hatari_harness.py --image ste --memsize 4096   # one session; also --monitor rgb, --corrupt, --interactive
python3 tools/dev/hatari_harness.py --image st --memsize 4096 --type debug-test --disk @system608 --input   # System 6.0.8 to the Finder, then use it
python3 tools/dev/hatari_harness.py --image st --memsize 4096 --type debug-test --disk @system608 --interactive   # the Finder in a window, with the host's mouse and keyboard
python3 tools/dev/hatari_harness.py --image st --memsize 4096 --type debug-test --disk @system608 --disk2 @word40a --launch 85,56 --then keys:Diego   # a program from drive 2
make format-check                  # clang-format dry run over src/**/*.{c,h}; also format, tidy, check
make clean                         # removes build/st-* and build/ste-*
./docs/epics/cockpit.sh            # regenerate docs/epics/STATUS.md after editing an epic or story
```

The Mac ROM the build uses is `tools/macrom.py path`: `tools/dev/dev.cfg`'s `[macrom] file` if set, else the `external/mac_rom` submodule build (pinned at `1d1014e`). The harness merges its own image into `build/harness/<stamp>-<label>/` with the trace (`trace.txt`), Hatari's log and `shots/`. It preloads RAM through a one-shot breakpoint on `_glueMain` that runs `loadbin` (a `loadbin` straight from `--parse` is lost before the first instruction). Hatari's control socket cannot move the mouse.

The patch table is `src/patches/plus_v3.ini` (one section per patch: offset, length, SHA-256 of the original bytes, replacement, mac_rom source, reason, enabled). `tools/jackrom.py patches` checks every entry's hash and overlaps; `tools/jackrom.py hash --offset N --length N` gives the hash for a new entry. EPIC-01 entered every planned patch disabled; a story enables an entry when it implements the glue routine the entry names.

How the glue boots the Mac (EPIC-02): `startup.s` sizes RAM (keeping the MMU value), fills `[$8, top)` with `$FF`, then `main.c` shows the diagnostic screen and `mac.c` `glueMacBoot` calls the ROM's `SysErrInit`, sets `MemTop`/`BufPtr`/`SoundBase` and jumps to `E_SoftReset` (D-16). The ROM then calls back into the glue through the enabled patches: `glueSonyOpen` (the disk driver at unit 4, refNum −5, one drive), `glueHook` (InitIOMgr's SCSI hook: MFP `VR` `$30`, Timer C at 60.235 Hz, `$68` HBL, `$70` VBL), `glueClkNoMem` (RTC/PRAM), `gluePlotDisk`/`glueShowHappyMac` (the boot icons, decoded from the ROM at 640x400). `glueTimerC` delivers the Mac's level-1 work (`Lvl1DT[1]` VBL, `Lvl1DT[0]` one second) through `glueLevel1` only when the interrupted IPL is 0 or 2. The VIA, IWM, SCC and `SERegs` live in RAM shadows at the start of the glue's RAM (`shadows.s`); `macrom.h` holds the ROM label offsets and low-memory addresses. Debug builds trace faults (`@@ crash vector …`) instead of letting the Mac bomb.

The disk side (EPIC-03): `disk.c` is the Mac disk driver at unit 4 (refNum −5, drive 1) over a backend (`disk.h`): `ramdisk.c` in test builds (an image the harness loads below the top of RAM with a "JKRD" descriptor in the last 16 bytes; `MemTop` goes below it), `nodisk.c` otherwise, `cartdisk.c` when md-jackintosh answered (EPIC-05). It also leaves what `.Sony`'s open would (`SonyVars`, unit 1 aliased, a queued Time Manager task), because System 6's `PTCH` 117 relies on them, and serves the first `Apple_HFS` partition of a disk with an Apple partition map. `sysreloc.c` wraps `jCheckLoad` and moves the absolute ROM and Mac hardware addresses of known System resources (D-15); `tools/sysreloc.py list|table DISK …` generates the table (`sysreloc_608.c` for System 6.0.8) from a System disk image. Patches added on the way: `mm-deref-nil` (nil pointer checks read `$FFFFF8`, a bus error on an ST), `launch-reset-stack-ptr` (C-09).

Input (EPIC-04): `ikbd.c` takes the IKBD through the ACIA (MFP GPIP 4, vector `$D8`). Mouse packets move `MTemp` and set `CrsrNew`, and the left button is port B bit 3 of the VIA shadow. Keys become Mac Plus transitions (`kKeyTable`: Control = Command, Alternate = Option), and the glue plays the Mac keyboard on the VIA shift register each tick (`glueKbdTick`): it completes the ROM's command and answers with a key, the model (`$0B`) or "no key". Test builds also read a mailbox (`glueTestInput`) that the harness's `--input` session writes with `hatari-debug memwrite`, because Hatari's control socket cannot move the mouse. A restart keeps the test builds' RAM disk (`glueReset`'s `$FF` fill stops at a valid "JKRD" image), and `--input` ends with Special / Restart back to the Finder.

Devices (EPIC-08): the RTC (`glueRtc`) has the chip's 256 bytes of PRAM, the classic 20 at their XPRAM aliases, and its extended command; `boot-scsi-branch` and `boot-hwcfg-xpram` make `HWCfgFlags` `$40` (XPRAM, no SCSI), and its seconds count with `glueTimerC`. PRAM and the clock live in RAM until EPIC-05 brings them from the Multi-device. `glueHook` marks both serial ports in use (`PortAUse`/`PortBUse` = 0), so AppleTalk and the serial drivers fail with `portInUse` instead of waiting for an SCC. A System whose `PTCH` 117 is not in the relocation table stops with a message (`@@ halt system …`); 6.0.7 shares 6.0.8's table. System 6 brings its own Time Manager on VIA Timer 2, which the glue does not drive yet (deferred until an application needs it). `_SetPtrSize` keeps its nil master pointer: the bus-error handler skips SetSize's writes through it.

Old software (EPIC-09): test builds have two floppy drives: `--disk2 FILE|@NAME` loads a second image below the first (a chain of "JKRD" descriptors), the driver serves each drive by `ioVRefNum`, and `glueDiskTick` announces drive 2 with a disk-inserted event once `_InitEvents` has made the queue. `--launch V,H` opens drive 2's disk and the program at V,H, then `--then keys:TEXT|cmd:KEY|click:V,H|wait:S|shot:NAME` drives it; a system error (`@@ mac syserr <code>`, debug builds) fails the run. The bus-error handler also gives the Plus's ROM space (`$400000`–`$57FFFF`) Plus behaviour: writes there or to our Mac ROM are skipped, and a read through an address register is re-run against our Mac ROM with the trace bit set. `glueTimerC` and the HBL handler put the glue's vectors back (`$8` bus error, `$68` HBL, `$70` VBL, `$D4` Timer C, `$D8` ACIA): programs write through nil pointers over `$8`, and debuggers take `$70` (the interrupt switch on a Plus) and vectors 48–54 (the 68881's on a Mac, the MFP's here), as THINK Pascal does (EPIC-10); `_FlushCodeCache` is a no-op. Program disks (hampa.ch) are named in `tools/dev/dev.cfg`'s `[disks]`. `tools/dev/mkhd.py` builds a bootable hard-disk image under `build/hd/` from items of disk images (machfs 1.3 in a virtual environment: `python3 -m venv build/venv && build/venv/bin/pip install machfs==1.3`, then run it with `build/venv/bin/python`); boot it with `--disk FILE --fixed`, and `--save-disk FILE` keeps what a session wrote for the next. Without `--disk2`, `--launch` opens the startup disk, and `--launch V,H/V,H` opens folders on the way to the program; `--then press:V,H`/`release:V,H` pick menu items, `regs` and `mem:ADDR,LEN` put the registers or memory into `stderr.txt`, and `--then` runs without `--launch` too. `--swap FILE|@NAME` (repeatable) queues images for drive 2: each time the Mac ejects it, the harness loads the next into drive 2's slot and writes the test builds' `glueTestDisk` mailbox, and the driver mounts it through the backend's `diskBackendChanged()` (also with `--interactive`). Installers that need every disk and a target disk in the Mac's 4 MB do not fit: they are bench tests.

The Multi-device (EPIC-05): `cart.s` ports the template's `send_sync`/`send_write_sync` (`IPL 7` only while the words go out, a busy flag so commands never nest: `CART_BUSY`); `cart.h` holds the window's layout and md-jackintosh's app `$07` commands as agreed on 2026-10-19 (`$0703` TIME in UTC, `$0704`/`$0705` PRAM read/write, the identity `$4A4B0001` at `APP_FREE+$0C`). `cart.c` `glueCartBoot` runs from `main.c` before the Mac: the hello (7 tries in release builds, once in debug builds), the identity, the PRAM into `glueRtc`, the time plus the Mac's `gmtDelta`; `glueCartTick` asks for the time again until NTP has it and writes the PRAM back 180 ticks after the Mac changes it. In Hatari the hello times out (`@@ cart hello none`); test builds' `--fake-cart KEY=VALUE,...` (id, seconds, valid-at, pram, zone) answers from RAM (`glueTestCart`) as md-jackintosh would, so all but the bus is checked there. The driver's backend (`struct DiskBackend` in `disk.h`: drives, info, read, write, eject, changed) is chosen in `glueInstallDisk` (`@@ disk backend cartridge|ramdisk|none`): `cartdisk.c` serves md-jackintosh's units FD0, FD1, HD0 as the Mac's drives 1, 2, 3 (`$0700` READ up to 32 sectors, copied out of the window by `glueCartCopy`, md-drives-emulator's `acsi.s` loops; `$0701` WRITE up to 2; `$0702` EJECT, which empties the unit until the next hello: md-jackintosh mounts its images again into the empty units at every ST boot, so the Mac's Restart, which ejects the floppies, comes back to the same disks; the unit table's change counters announce an insert). At boot the glue resets the IKBD (`$80 $01`) and drops its bytes until the `$F0`/`$F1` reply: after an ST reset the IKBD keeps running, and a mouse packet caught mid-way read as a held button, which makes the boot loop eject every drive. While the RP restarts its window reads as fresh or as nothing (no identity): the driver reports nothing, the units keep their last state and reads and writes wait for the RP (up to ~60 s; a unit gone offline would set off the File Manager's disk switch, which ejects it), and a unit that comes back with the same size and a change counter of 1 stays mounted. The fake serves its units from the harness's RAM disks (the first is FD0, or HD0 with `--fixed`; `--disk2` is FD1; `--swap` works through it), keeps its state across a Mac restart (a file the harness saves at `_glueReset` and loads at `_glueMain`), and `--then fake-restart:SECONDS` plays an RP restart (`fake-down` and `fake-up` around other actions, to use the disk meanwhile). During the ROM's boot scan (before `_InitEvents`) the driver refuses Eject on a drive with a disk: the ROM ejects a disk that is not a startup disk, and an image cannot be put back by hand, so the failed eject makes the ROM drop the drive from `BootMask` and boot the next one, and the disk is announced once the Finder runs. `all_harness.sh`'s HD0-plus-FD1 run needs `build/hd/hd0-small.raw`: `build/venv/bin/python tools/dev/mkhd.py -o build/hd/hd0-small.raw --size 1600 "@system608:System Folder"`.

Harness debugging aids: `--disk FILE|@NAME` (from `[disks]` in `tools/dev/dev.cfg`) with `--fixed`, `--partition`, `--write-protect` for the RAM disk; `--dump ADDR:LEN` (memory after a crash, into `stderr.txt`), `--watch ADDR` (registers whenever the CPU reaches ADDR), `--watch-mem ADDR` (registers whenever the long at ADDR changes). Debug builds' crash trace has the registers, the code around the PC and the stack.

The glue runs from ROM: code and read-only data at the ROM base, `.data` copied to RAM and `.bss` cleared by `src/glue/startup.s`; `src/glue/mem.h` holds the layout (D-09) and is shared by C, assembly and the linker script template `rom.ld.S`, so its values carry no `UL` suffix. The build ID is `<sha7>`, `-dirty.<diff7>` when `src/`, `Makefile` or `version.txt` differ from HEAD (untracked files included), then `+debug`/`+test`; it reaches C as `BUILD_ID_STR`. Function names are camelBack (`.clang-tidy`). Adding a `.c` file: append it to `SRCS_C` in `src/glue/Makefile`.

Checked on 2026-10-18: the sibling's pinned toolchain assembles mac_rom's Plus v3 unchanged, from a clone of mac_rom `1d1014e`, to 131072 bytes with stored and computed checksum `$4D1F8172` and version `$0075`, **byte-identical** to the reference Plus v3 ROM (SHA-256 `dd908e2b65772a6b1f0c859c24e9a0d3dcde17b1c6a24f4abd8955846d7895e7`).

Local reference files, outside the repository (D-02):

- `../macroms/archive.org/4D1F8172 - MacPlus v3.ROM`, from archive.org's `mac_rom_archive_-_as_of_8-19-2011`. Single entries of that zip download from `https://archive.org/download/mac_rom_archive_-_as_of_8-19-2011/mac_rom_archive_-_as_of_8-19-2011.zip/<name>`; Plus v1 `4D1EEEE1` and v2 `4D1EEAE1` are there too. `../macroms/macmade/Mac-Plus.ROM` (github.com/macmade/Macintosh-ROMs) is 138576 bytes, not a plain 128 KB dump, and is unexamined. macintoshrepository.org's downloads need a browser session (curl gets an HTML page).
- `../macdisks/hampa/MacOS 6.0.8 US 800K/`, from <http://hampa.ch/pub/software/MacOS/MacOS/> (PCE's author; Systems 5.0 to 7.1.1, 800K and 1440K sets). Each `diskN.image.bin` is a DiskCopy 4.2 image in a MacBinary II wrapper (data fork at 128, DiskCopy data at 128 + `0x54`, 819200 bytes, 19200 bytes of tags). The bundled `SHA256` file checks; `disk1` ("System Tools", the startup disk) has a valid DiskCopy data checksum and `LK` boot blocks, and its raw 800K extract is `disk1.raw` (SHA-256 `4089f6ed6a39987a0f56207c25936bc61d1e7bcd0c969bbe92f3f6a28cfa7f50`). macOS has no `7z`; `bsdtar -xf` extracts these archives. The image's entrypoint changes directory, so call the assembler with `--entrypoint /bin/sh`:

```bash
docker run --rm --platform linux/x86_64 --entrypoint /bin/sh -v "$PWD/build/x:/w" -w /w/mac_rom \
  logronoide/atarist-toolkit-docker-x86_64:1.4.0@sha256:016434131cf53f627c365e55262845fba53f30a7de87a1f122d58f8fa6aeeebe \
  -c 'm68k-atari-mint-as -m68000 --register-prefix-optional -Iinclude --defsym ROM_TARGET=5 -o /w/plus3.o rom_plus.s && m68k-atari-mint-objcopy -O binary /w/plus3.o /w/plus3.bin'
```

CI (`.github/workflows/build.yml`, every pull request) builds the glue of both images in release and debug, assembles and checks the mac_rom ROM, and merges both images. It uploads nothing (D-02) and does not run Hatari.

Docker Desktop on this Mac does not share the session scratchpad (`/private/tmp/...`): mount folders under the repository (`build/`, git-ignored) or the home directory.

## Testing: Hatari first (D-07)

Hardware comes late. Every code story runs in Hatari (`/usr/local/bin/hatari`, 2.6.1) before it reaches Diego's bench: the image is booted as the system ROM with `--tos`, `--machine st` or `ste`, `--monitor mono`, NatFeats on for debug builds, keys and mouse through the control socket, screenshots and the `@@` trace read by the harness, as in romemul-switcher-rom's `tools/dev/hatari_harness.py`.

Hatari has no SidecarTridge (C-02). Disks in Hatari come from a **RAM-disk backend** in test builds: the harness loads a disk image into the top of ST RAM before the ROM runs, and the glue lowers the Mac's `MemTop` below it. A harness PASS says nothing about the cartridge bus, the TOS Emulator, real Shifter timing or power-on behaviour (C-01).

## Conventions and gotchas (carried over from romemul-switcher-rom)

- **Freestanding**: no TOS, GEMDOS, BIOS or XBIOS call anywhere; no libc, no libgcc (a 32-bit `/` or `%` GCC cannot fold fails to link, at `-O0` especially, so build every change in debug too); `LIBS` stays empty.
- **No named sections**: the toolchain's objects are a.out, so `__attribute__((section(...)))` fails. Place data by file name in the linker script.
- **Word alignment**: the 68000 faults on odd word and long accesses.
- **Source header block**: every `.c`, `.h`, `.s` under `src/` starts with the `File / Author / Date / Copyright / Description` comment block, as in romemul-switcher-rom.
- **Debug images never go on hardware**: the NatFeats trace traps on a real ST.
- **Trace lines** (`TRACE("...")`, `@@ <line>` in debug builds) are matched exactly by the harness; change both together. Every blocking wait is preceded by a trace line.
- **Docker file sharing on macOS**: a file the host writes into a folder a container has just recreated can stay invisible to the next container for a while; keep write-then-read steps inside one container.
- Mac register conventions in patches: Mac OS traps take arguments in `A0`/`D0` and return `D0`; Toolbox traps use the Pascal stack convention; `D1`–`D2`/`A1` are scratch for OS traps, everything else is preserved. A patch that calls glue code saves what it touches.
- `build/`, `dist/`, `*.o`, `*.map`, `*.bin` are generated and git-ignored; exclude them from searches.

## Backlog

Work is tracked in `docs/epics/`, the same procedure as romemul-switcher-rom, sidecartos and sidecartos-config. `ITERATIONS.md` is the plan and its order, `EPIC-NN-<slug>/epic.md` and `STORY-NN-<slug>.md` hold the work, `DECISIONS.md` holds the `D-NN` decisions and `C-NN` constraints, and `STATUS.md` is generated by `./docs/epics/cockpit.sh`, run after editing any epic or story. The rules are in `docs/epics/README.md`: one epic at a time, each ending in a verification story that is Diego's checkpoint, and a task is checked only when observed.

**Nothing under `docs/` is ever committed** (D-01; `.gitignore` excludes it), so the backlog and `docs/macplus/` live only in the working tree; committed files must not rely on a reader being able to open them. Code is not committed until Diego says so.

The first release is v1.0.0, the same number as md-jackintosh's (Diego, 2026-10-19). Iteration 1 (v0.1.0) is Hatari only: the Mac Plus ROM, patched, boots System 6 to a usable Finder on an emulated ST and STE in mono. Iteration 2 takes it to the bench with md-jackintosh and the TOS Emulator.

## Working style

These behavioral guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think before coding

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity first

Minimum code that solves the problem. Nothing speculative.
- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical changes

Touch only what you must. Clean up only your own mess.
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.
- When your changes orphan an import/variable/function, remove it. Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

### 4. Goal-driven execution

Define success criteria. Loop until verified.
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan with a verification check per step. For this repo the check is the Hatari harness on both images (from EPIC-00 on).

### 5. No AI attribution

Never add AI-tool attribution to commits, PR descriptions, code comments,
docs, or any other artifact. This means **no**:
- "Generated with Claude Code", "Co-authored by Claude", "Made with ChatGPT",
  or any similar phrasing.
- `Co-Authored-By: Claude …`, `Co-Authored-By: ChatGPT …`, or any other
  AI co-author trailer.
- "AI-assisted", "written with the help of an LLM", etc., as comments or
  changelog entries.

Write the message as the human author. Do not mention AI tools used to
produce the work.
