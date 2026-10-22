# Software for the Jackintosh

What runs on a Jackintosh: a Macintosh Plus with **System 6.0.8** (or 6.0.7), a 68000, up to 4 MB and a
monochrome screen.

- **Mac Plus**: whether the program can run on a Macintosh Plus at all. *no* when it needs a 68020 or later, or
  colour; a dash when not known.
- **System**: the System it needs. *6*: runs on System 6, the Jackintosh's System. *7*: needs System 7, which
  a Jackintosh does not run.
- **Tested**: where it was run on a Jackintosh: in Hatari (the emulator every change is checked in) or on a
  real Atari (the bench).
- **Works**: what happened there. *yes*, *no*, or a dash when not tested.

A program that needs System 7, a 68020 or Color QuickDraw fails on a Jackintosh as it fails on a real Mac
Plus.

## Applications

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Acrobat Reader 1.0 | yes | 6 | Hatari | no | Its Installer hangs after the splash, and the Reader will not start without the ATM it installs. To look at. |
| Aldus PageMaker 3.5 | yes | 6 | bench | yes | Installed from 5 floppies swapped in with the Multi-device's SELECT button. |
| Aldus PageMaker 4.2 | yes | 6 | Hatari | — | The Installer runs through every dialog; installing needs 5.7 MB of disk, more than the Hatari tests hold: for the bench. |
| Aldus PageMaker 5.0a | no | 7 | no | — | Needs a 68020. |
| ClarisWorks 1.0 | yes | 6 | Hatari | yes | Word processing, typing. Also on System 6.0.7. |
| ClarisWorks 2.0 | yes | 6 | Hatari | yes | Word processing, typing. |
| ClarisWorks 2.1 | yes | 6 | Hatari | yes | Word processing, typing. |
| ClarisWorks 4.0 | — | 7 | no | — | |
| MacWrite Pro 1.5 | yes | 6 | Hatari | yes | A document, typing. |
| Maple 6.01 | no | 7 | no | — | Needs a 68020 or later. |
| Microsoft Excel 1.03 | yes | 6 | Hatari | yes | Run from a floppy swapped in after the Finder ejects the first. |
| Microsoft Excel 4.0 (Office 2.5.1) | yes | 6 | no | — | Only Microsoft Setup unpacks it, from 7 disks: for the bench. |
| Microsoft PowerPoint 2.01 (Office 2.5.1) | yes | 6 | Hatari | yes | A new presentation. |
| Microsoft Word 4.0a | yes | 6 | Hatari, bench | yes | Typing, menus and Save. |
| Microsoft Word 5.0 (Office 2.5.1) | yes | 6 | Hatari | yes | Asks for its Install disk at launch (Cancel), then typing works. |
| Microsoft Word 5.1a | yes | 6 | bench | yes | Installed from its 6 floppies. |
| Voyager II 1.00 | yes | 6 | Hatari | yes | The sky chart, its panels and menus. |
| WordPerfect 3.0a | yes | 6 | Hatari | — | The Installer runs; installing does not fit the Hatari tests: for the bench. |
| WordPerfect 3.5 | — | 7 | no | — | |

## Development

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| MPW 3.2.3 | yes | 6 | bench | yes | The shell. |
| Symantec C++ 6.0 | — | 7 | no | — | |
| THINK C 4.0 | yes | 6 | Hatari | yes | Builds and runs a program. |
| THINK C 5.0 | yes | 6 | Hatari | yes | Builds and runs a program. |
| THINK C 5.0.4 Update | yes | 6 | Hatari | yes | The update patches THINK C 5.0, which then builds and runs a program. |
| THINK C 6.0.1 | — | 7 | no | — | |
| THINK Pascal 4.0.2 | yes | 6 | Hatari | yes | Builds and runs a program. |

## Disk tools

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Copy II 7.0 (floppy copier) | yes | 6 | Hatari | no | It drives the Mac's floppy chip directly, which an Atari does not have; most launches crash. |
| Copy II Hard Disk 7.0 | yes | 6 | Hatari | yes | The main window. |
| DART 1.5.3 | yes | 6 | Hatari | yes | Opens after an "MFM Status error -18" alert (the Jackintosh's disk driver does not answer that call); copying not tried. |
| Disk Copy 4.2 | yes | 6 | Hatari | yes | The main window. |
| Disk Copy 6.3.3 | — | 7 | no | — | |
| ShrinkWrap 1.3.1, 1.3.2, 1.4.2, 2.0.1, 3.5.1 | — | 7 | no | — | |

## Games

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Blockout | yes | 6 | Hatari | yes | |
| Tetris (Spectrum HoloByte, 1988) | yes | 6 | Hatari, bench | yes | Sound on the STE and Mega STE. |

## Utilities

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Norton Utilities 1.1.1 (800K) | yes | 6 | Hatari | yes | Disk Doctor. |
| Norton Utilities 1.1 (1440K) | yes | 6 | Hatari | yes | The main window. |
| Norton Utilities 2.0 (800K) | yes | 6 | Hatari | yes | Disk Doctor diagnoses the hard disk. |
| Norton Utilities 3.1, 3.1.3, 3.2 | — | 7 | no | — | |

## Archivers

| Program | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| MacGzip 1.1.3 | yes | 7 | Hatari | no | Stops with "System 7 required", as on a real Plus. |
| mar 0.0.4, 0.1.1 | yes | 6 | Hatari | yes | Its menus work. |

## Systems

| System | Mac Plus | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| 6.0.8 (US 800K, US 1440K, Z 1440K) | yes | 6 | Hatari, bench | yes | The Jackintosh's System. |
| 6.0.7 (US 800K, US 1440K, Z 800K) | yes | 6 | Hatari | yes | Shares 6.0.8's relocation table; a full session, and ClarisWorks on a 6.0.7 hard disk. |
| 5.0, 5.1, 6.0, 6.0.2 to 6.0.5 | yes | 6 | no | no | The glue relocates only the System patches it knows: an unknown System stops with a message. |
| 7.0, 7.0.1, 7.1, 7.1.1 | yes | 7 | no | no | As above. |
