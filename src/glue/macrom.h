/**
 * File: src/glue/macrom.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: What the glue needs to know about the Mac Plus v3 ROM: label
 * offsets (from the mac_rom build's symbols, tools/macrom.py, mac_rom
 * 1d1014e), low-memory globals (mac_rom include/lowmem.inc and sysequ.inc)
 * and trap words. Offsets and addresses only, never ROM bytes (D-02).
 * Shared by C and assembly.
 */

#pragma once

#include "mem.h"

/* ROM labels, as offsets from the ROM's start (MAC_ROM_BASE_ADDR). */
#define MACROM_E_SOFTRESET 0x03A8       /* plus/boot/vectors.s:659 */
#define MACROM_SYSERRINIT 0x1036        /* plus/debugger/debugger.s:48 */
#define MACROM_ICON_MAC_OUTLINE 0x0F3E  /* plus/boot/sad_mac.s:128 */
#define MACROM_ICON_DISK 0x0FA2         /* plus/boot/sad_mac.s */
#define MACROM_DT_MHAPPYMAC 0x0FD2      /* plus/boot/sad_mac.s */
#define MACROM_DEREFHANDLE_CONT 0x10470 /* plus/os/memory_mgr.s:1117 */
/* DEREFHANDLE_CONT_4: memWZErr, the "free block" exit. */
#define MACROM_DEREFHANDLE_CONT_4 0x10492
#define MACROM_SETSIZE 0x10902 /* plus/os/memory_mgr.s:1983 */

#define MACROM_ADDR(offset) (MAC_ROM_BASE_ADDR + (offset))

/* Low-memory globals. */
#define MAC_LVL1DT 0x0192 /* 16 longs: VIA interrupt handlers */
#define MAC_PWMVALUE 0x0138
#define MAC_MEMTOP 0x0108
#define MAC_BUFPTR 0x010C
#define MAC_TICKS 0x016A
#define MAC_UTABLEBASE 0x011C
#define MAC_SOUNDBASE 0x0266
#define MAC_PWMBUF2 0x0312
#define MAC_SCRNBASE 0x0824
#define MAC_JIODONE 0x08FC
#define MAC_PWMBUF1 0x0B0A
#define MAC_SCSIDRVRS 0x0B2E
#define MAC_LO3BYTES 0x031A
#define MAC_SONYVARS 0x0134
#define MAC_SONYVARS_BYTES \
  0x310 /* DiskVarLth, plus/resources/res_drvr_sony.s:100 */
#define MAC_SONY_TIMEQEL 0x118 /* TimeQEl(SonyVars), res_drvr_sony.s:132 */
#define MAC_TMADDR 6           /* a TMTask's completion routine */

/* Lvl1DT slots the glue drives (plus/boot/vectors.s:751-765). */
#define MAC_LVL1_ONESEC 0 /* OneSecInt_VIA, VIA CA2, 1 Hz */
#define MAC_LVL1_VBL 1    /* VBLInt_VIA, VIA CA1, 60.15 Hz */

/* Sound and PWM buffers below MemTop (plus/boot/vectors.s:644-653). */
#define MAC_PWMBUF_BELOW_MEMTOP 0x02FF
#define MAC_SOUNDBASE_BELOW_MEMTOP 0x0300

/* The boot screen's icons on a 512x342 Mac (plus/boot/vectors.s:1158-1182,
   plus/boot/sad_mac.s): the icon at row 145 byte 30, the disk's symbol 15
   rows down and 1 byte right, the Mac's face 6 rows down and 1 byte right.
   The glue centres them on 640x400 (gluePlotDisk). */
#define MAC_ICON_ROWS 32
#define MAC_ICON_BYTES 4
#define MAC_DISK_SYMBOL_ROWS 15
#define MAC_FACE_ROWS 10

/* Driver and drive queue (plus/boot/boot_install.s, boot-map.md 5.4). */
#define MAC_SONY_REFNUM (-5) /* unit 4, the .Sony slot */
#define MAC_SONY_UNIT 4
#define MAC_DCE_FLAGS 4
#define MAC_DCE_REFNUM 24
#define MAC_DCE_SIZE 40

/* Result codes. */
#define MAC_NOERR 0
#define MAC_CONTROLERR (-17)
#define MAC_WPRERR (-44)
#define MAC_PARAMERR (-50)
#define MAC_STATUSERR (-18)
#define MAC_OFFLINERR (-65)

/* The emulated clock starts at 2026-10-18 00:00 in Mac time (seconds since
   1904-01-01) until a real clock exists (Iteration 3). */
#define MAC_CLOCK_START 0xE6F9BC80UL
