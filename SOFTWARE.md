# Software for the Jackintosh

What runs on a Jackintosh: a Macintosh Plus with **System 6.0.8** (or 6.0.7), a 68000, up to 4 MB, a
monochrome screen. The list follows hampa.ch's collection of classic Mac software,
<http://hampa.ch/pub/software/MacOS/> (from PCE's author), which is also where this project's test disks
come from.

- **Tested**: run on a Jackintosh, in the Hatari harness (`tools/dev/all_harness.sh`) or on a real Atari
  (the bench).
- **System 6.0.8**: whether the program can run there at all. *yes* as documented or seen; *no* when it
  needs System 7, a 68020 or colour, which a Mac Plus does not have either; *to try* when not known yet.

A program that needs System 7, a 68020 or Color QuickDraw fails on a Jackintosh as it fails on a real Mac
Plus.

## Applications

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| Acrobat Reader 1.0 | yes | Hatari | **Fails on the Jackintosh**: its Installer hangs after the splash, and the Reader will not start without the ATM it installs. Under investigation. |
| Aldus PageMaker 3.5 | yes | bench | Installed from 5 floppies swapped in with the Multi-device's SELECT button. |
| Aldus PageMaker 4.2 | to try | Hatari (Installer) | The Installer runs through every dialog; installing needs 5.7 MB of disk, more than the Hatari tests hold: a bench test. |
| Aldus PageMaker 5.0a | no | no | Needs System 7 and a 68020. |
| ClarisWorks 1.0 | yes | Hatari | Word processing, typing. Also on System 6.0.7. |
| ClarisWorks 2.0 | yes | Hatari | Word processing, typing. |
| ClarisWorks 2.1 | yes | Hatari | Word processing, typing. |
| ClarisWorks 4.0 | no | no | Needs System 7. |
| MacWrite Pro 1.5 | yes | Hatari | A document, typing. |
| Maple 6.01 | no | no | Needs a 68020 or later. |
| Microsoft Excel 1.03 | yes | Hatari | Run from a floppy swapped in after the Finder ejects the first. |
| Microsoft Office 2.5.1: PowerPoint 2.01 | yes | Hatari | A new presentation. |
| Microsoft Office 2.5.1: Word 5.0 | yes | Hatari | Asks for its Install disk at launch (Cancel), then typing works. |
| Microsoft Office 2.5.1: Excel 4.0 | to try | no | Only Microsoft Setup unpacks it, from 7 disks: a bench test. |
| Microsoft Word 4.0a | yes | Hatari, bench | Typing, menus and Save. |
| Microsoft Word 5.1a | yes | bench | Installed from its 6 floppies. |
| Voyager II 1.00 | yes | Hatari | The sky chart, its panels and menus. |
| WordPerfect 3.0a | to try | Hatari (Installer) | The Installer runs; installing does not fit the Hatari tests: a bench test. |
| WordPerfect 3.5 | no | no | Needs System 7. |

## Development

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| MPW 3.2.3 | yes | bench | The shell. |
| Symantec C++ 6.0 | no | no | Needs System 7. |
| THINK C 4.0 | yes | Hatari | Builds and runs a program. |
| THINK C 5.0 | yes | Hatari | Builds and runs a program. |
| THINK C 5.0.4 Update | yes | Hatari | The update patches THINK C 5.0, which then builds and runs a program. |
| THINK C 6.0.1 | no | no | Needs System 7. |
| THINK Pascal 4.0.2 | yes | Hatari | Builds and runs a program. |

## Disk tools

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| Copy II 7.0 (floppy copier) | yes | Hatari | **Fails on the Jackintosh**: it drives the Mac's floppy chip directly, which an Atari does not have; most launches crash. |
| Copy II Hard Disk 7.0 | yes | Hatari | The main window. |
| DART 1.5.3 | yes | Hatari | Opens after an "MFM Status error -18" alert (the Jackintosh's disk driver does not answer that call); copying not tried. |
| Disk Copy 4.2 | yes | Hatari | The main window. |
| Disk Copy 6.3.3 | no | no | Needs System 7. |
| ShrinkWrap 1.3.1, 1.3.2, 1.4.2, 2.0.1, 3.5.1 | no | no | Need System 7. |

## Games

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| Blockout | yes | Hatari | |

## Utilities

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| Norton Utilities 1.1.1 (800K) | yes | Hatari | Disk Doctor. |
| Norton Utilities 1.1 (1440K) | yes | Hatari | The main window. |
| Norton Utilities 2.0 (800K) | yes | Hatari | Disk Doctor diagnoses the hard disk. |
| Norton Utilities 3.1, 3.1.3, 3.2 | no | no | Need System 7. |

## Archivers

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| MacGzip 1.1.3 | no | Hatari | Stops with "System 7 required", as on a real Plus. |
| mar 0.0.4, 0.1.1 | yes | Hatari | hampa.ch's own archiver; its menus work. |

## Systems

| System | Runs | Tested | Notes |
|---|---|---|---|
| 6.0.8 (US 800K, US 1440K, Z 1440K) | yes | Hatari, bench | The target. |
| 6.0.7 (US 800K, US 1440K, Z 800K) | yes | Hatari | Shares 6.0.8's relocation table; a full session, and ClarisWorks on a 6.0.7 hard disk. |
| 5.0, 5.1, 6.0, 6.0.2 to 6.0.5 | no | no | The glue relocates only the System patches it knows: an unknown System stops with a message. |
| 7.0, 7.0.1, 7.1, 7.1.1 | no | no | As above. |

## Not on hampa.ch

| Program | System 6.0.8 | Tested | Notes |
|---|---|---|---|
| Tetris (Spectrum HoloByte, 1988) | yes | Hatari, bench | Sound on the STE and Mega STE. |
