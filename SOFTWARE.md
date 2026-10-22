# Software for the Jackintosh

What runs on a Jackintosh: a Macintosh Plus with **System 6.0.8** (or 6.0.7), a 68000, up to 4 MB and a
monochrome screen.

- **Model**: the Mac the program runs on here: a Macintosh Plus with its v3 ROM, the ROM the Jackintosh uses.
- **System**: the System it needs. Only software for System 6, the Jackintosh's System, is listed.
- **Tested**: whether it was run on a Jackintosh.
- **Works**: what happened. *Yes*, *No*, or a dash when not tested.

A program that needs a 68020 or Color QuickDraw fails on a Jackintosh as it fails on a real Mac Plus.

## Applications

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Acrobat Reader 1.0 | Mac Plus v3 ROM | 6 | Yes | No | Its Installer hangs after the splash, and the Reader will not start without the ATM it installs. To look at. |
| Aldus PageMaker 3.5 | Mac Plus v3 ROM | 6 | Yes | Yes | Installed from 5 floppies swapped in with the Multi-device's SELECT button. |
| Aldus PageMaker 4.2 | Mac Plus v3 ROM | 6 | Yes | Yes | Installed with the Aldus Installer (PM 1, Utilities) from 7 floppies queued in FD0's slots and stepped with SELECT: the 5 install disks and the 2 font disks. |
| ClarisWorks 1.0 | Mac Plus v3 ROM | 6 | Yes | Yes | Word processing, typing. Also on System 6.0.7. |
| ClarisWorks 2.0 | Mac Plus v3 ROM | 6 | Yes | Yes | Word processing, typing. |
| ClarisWorks 2.1 | Mac Plus v3 ROM | 6 | Yes | Yes | Word processing, typing. |
| MacWrite Pro 1.5 | Mac Plus v3 ROM | 6 | Yes | Yes | A document, typing. |
| Microsoft Excel 1.03 | Mac Plus v3 ROM | 6 | Yes | No | Blocked by its key-disk copy protection: it checks the original floppy through the Mac's floppy chip, which a disk image cannot pass. |
| Microsoft Excel 4.0 (Office 2.5.1) | Mac Plus v3 ROM | 6 | Yes | No | Microsoft Setup, the only way to unpack it, stops with a bus error at launch. To look at. |
| Microsoft PowerPoint 2.01 (Office 2.5.1) | Mac Plus v3 ROM | 6 | Yes | Yes | A new presentation. |
| Microsoft Word 4.0a | Mac Plus v3 ROM | 6 | Yes | Yes | Typing, menus and Save. |
| Microsoft Word 5.0 (Office 2.5.1) | Mac Plus v3 ROM | 6 | Yes | Yes | Asks for its Install disk at launch (Cancel), then typing works. |
| Microsoft Word 5.1a | Mac Plus v3 ROM | 6 | Yes | Yes | Installed from its 6 floppies. |
| Voyager II 1.00 | Mac Plus v3 ROM | 6 | Yes | Yes | The sky chart, its panels and menus. |
| WordPerfect 3.0a | Mac Plus v3 ROM | 6 | Yes | Yes | Installed with its Installer from its 5 floppies (1440K), queued in FD0's slots and stepped with SELECT. |

## Development

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| MPW 3.2.3 | Mac Plus v3 ROM | 6 | Yes | Yes | The shell. |
| THINK C 4.0 | Mac Plus v3 ROM | 6 | Yes | Yes | Builds and runs a program. |
| THINK C 5.0 | Mac Plus v3 ROM | 6 | Yes | Yes | Builds and runs a program. |
| THINK C 5.0.4 Update | Mac Plus v3 ROM | 6 | Yes | Yes | The update patches THINK C 5.0, which then builds and runs a program. |
| THINK Pascal 4.0.2 | Mac Plus v3 ROM | 6 | Yes | Yes | Builds and runs a program. |

## Disk tools

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Copy II 7.0 (floppy copier) | Mac Plus v3 ROM | 6 | Yes | No | It drives the Mac's floppy chip directly, which an Atari does not have; most launches crash. |
| Copy II Hard Disk 7.0 | Mac Plus v3 ROM | 6 | Yes | Yes | The main window. |
| DART 1.5.3 | Mac Plus v3 ROM | 6 | Yes | Yes | Opens after an "MFM Status error -18" alert (the Jackintosh's disk driver does not answer that call); copying not tried. |
| Disk Copy 4.2 | Mac Plus v3 ROM | 6 | Yes | Yes | The main window. |

## Games

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Blockout | Mac Plus v3 ROM | 6 | Yes | Yes |  |
| Crystal Quest 2.2.5 | Mac Plus v3 ROM | 6 | Yes | Yes | Its title screen, Hall of Fame and demo run: animation, scoring, the attract cycle. |
| Dark Castle 1.2 | Mac Plus v3 ROM | 6 | Yes | No | Stops on a bus error at launch: it probes $F80030, the Plus's diagnostic-ROM space, which an Atari does not answer. To fix. |
| Airborne! (1984) | Mac Plus v3 ROM | 6 | Yes | No | A self-booting disk: the game is its "Finder". Booting it runs its own 1985 System, which stops reading the ROM at its Plus address ($400008); from System 6.0.8 the Finder will not open a program named Finder. To retry renamed. |
| Lode Runner (1984) | Mac Plus v3 ROM | 6 | Yes | Yes | From a 400K MFS floppy; its window, menus and levels play (demo from level 1 to 3). |
| Space Invaders (1985) | Mac Plus v3 ROM | 6 | Yes | Yes | From a 400K MFS floppy; Play starts level 1, the invaders march. A large black shape in the lower right corner is to check against a real Plus. |
| Tetris (Spectrum HoloByte, 1988) | Mac Plus v3 ROM | 6 | Yes | Yes | Sound on the STE and Mega STE. |

## Utilities

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| Norton Utilities 1.1.1 (800K) | Mac Plus v3 ROM | 6 | Yes | Yes | Disk Doctor. |
| Norton Utilities 1.1 (1440K) | Mac Plus v3 ROM | 6 | Yes | Yes | The main window. |
| Norton Utilities 2.0 (800K) | Mac Plus v3 ROM | 6 | Yes | Yes | Disk Doctor diagnoses the hard disk. |

## Archivers

| Program | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| mar 0.0.4, 0.1.1 | Mac Plus v3 ROM | 6 | Yes | Yes | Its menus work. |

## Systems

| System | Model | System | Tested | Works | Notes |
|---|---|---|---|---|---|
| 6.0.8 (US 800K, US 1440K, Z 1440K) | Mac Plus v3 ROM | 6 | Yes | Yes | The Jackintosh's System. |
| 6.0.7 (US 800K, US 1440K, Z 800K) | Mac Plus v3 ROM | 6 | Yes | Yes | Shares 6.0.8's relocation table; a full session, and ClarisWorks on a 6.0.7 hard disk. |
