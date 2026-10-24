# Changelog

## v1.0.0 (2026-10-24) - Jackintosh ROMs

The first release. A boot ROM for the SidecarTridge TOS Emulator that
turns an Atari ST, STE, Mega ST or Mega STE into a Macintosh Plus at
power-on: no TOS, no GEM, no emulator program. The Atari's own 68000 runs
the genuine Mac Plus v3 ROM, and our glue stands in for the Mac hardware
the Atari lacks. The disks, the clock and the parameter RAM come from the
SidecarTridge Multi-device running md-jackintosh v1.0.0, released with
this one.

Seen on the bench, where the ST, Mega ST, STE and Mega STE behave the
same but for sound: System 6.0.8 boots from floppy and hard disk images;
PageMaker 4.2 and Microsoft Excel 4.0 install from
their floppies and run; Acrobat Reader 1.0, Word, ClarisWorks, MacPaint,
SimCity and others run. SOFTWARE.md lists every program tried and
what stops the ones that do not run.

### Get your image

The release is the image builder at <https://jackintosh.sidecartridge.com/>.
The Mac Plus ROM is Apple's, so no image is published: you bring your own
Mac Plus v3 ROM (131072 bytes, checksum `$4D1F8172`), the page merges it
with this release's glue and patch table in your browser, and you get
`JACKINTOSH-ST-v1.0.0.img` or `JACKINTOSH-STE-v1.0.0.img` for the TOS
Emulator. Your ROM never leaves your computer.

### What it does

- **Two images**: 192 KB at `$FC0000` for the ST, STF, STFM and Mega ST;
  256 KB at `$E00000` for the STE and Mega STE.
- **The Mac Plus ROM, patched in place**: 102 same-length patches keep
  every ROM offset, so System 6.0.7 and 6.0.8 load their patches
  unchanged. Their absolute ROM addresses are relocated as they load.
- **The Mac hardware in software**: RAM sizing up to 4 MB; the VIA's
  tick, one-second interrupt and keyboard; the RTC with 256 bytes of
  PRAM; the IKBD as the Mac's keyboard and mouse; a block driver for two
  floppies and a hard disk; the Plus's ROM and device addresses behave as
  on a Plus when programs read or write them directly.
- **Video**: 640x400 on any monochrome monitor (SM124, or a mono VGA
  adapter), or the Plus's 512x342 at the top left in a black border.
- **The disk chooser** before the Mac starts, laid out as md-drives-emulator's:
  browse the microSD card, choose FD0, FD1 and HD0, and queue up to ten
  floppies in FD0's slots (SELECT on the Multi-device puts the next one
  in). A slot's browser opens in the previous slot's folder.
- **Options**, kept on the Multi-device: the countdown, the Mac's RAM,
  the right mouse button, the screen, the keyboard layout (US, fr, de,
  es, uk; experimental), the mouse's speed, the clock (NTP server, port,
  time zone).
- **Mega STE**: 8 MHz, 16 MHz, or 16 MHz with the cache.
- **Sound on the STE and Mega STE** (experimental): the Plus's sound
  buffer played on the DMA sound, with the Mac's volume.

### Known limits

- Monochrome only. No serial port, no printing, no AppleTalk.
- No sound on the ST.
- Programs that write the screen with the Plus's 64-byte rows (Dark
  Castle) or drive the floppy chip directly (Copy II, key-disk copy
  protection) do not run.
- DiskCopy images are read-only.
