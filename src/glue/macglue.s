/**
 * File: src/glue/macglue.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's assembly side of the Mac (EPIC-02): entering the
 * ROM, the interrupt handlers (D-17, D-18), the routines the ROM patches
 * call with the Mac's register conventions, the stub disk driver, and the
 * debug-build crash handlers. The logic lives in mac.c.
 */

#include "macrom.h"

    .text

/* ------------------------------------------------------------------------
 * Entering the ROM (D-16)
 */

/* void glueCallSysErrInit(void): the ROM's SysErrInit, which returns
   through A6 (plus/debugger/debugger.s:48-84). */
    .globl _glueCallSysErrInit
_glueCallSysErrInit:
    movem.l %d2-%d7/%a2-%a6, -(%sp)
    lea     1f(%pc), %a6
    jmp     MACROM_ADDR(MACROM_SYSERRINIT)
1:
    movem.l (%sp)+, %d2-%d7/%a2-%a6
    rts

/* void glueJumpSoftReset(void): never returns. E_SoftReset sets SR, SP, A3
   and A6 itself (plus/boot/vectors.s:659-675). */
    .globl _glueJumpSoftReset
_glueJumpSoftReset:
    jmp     MACROM_ADDR(MACROM_E_SOFTRESET)

/* ------------------------------------------------------------------------
 * Interrupts (D-17, D-18)
 */

/* Spurious interrupt ($60) and every MFP channel the glue does not use. */
    .globl _glueRte
_glueRte:
    rte

/* The Atari VBL (level 4, $70): counted, an independent clock for the
   harness's tick-rate check (EPIC-02 STORY-03). */
    .globl _glueVbl
_glueVbl:
    addq.l  #1, _glueVblCount
    rte

/* The Atari HBL (level 2, $68). Left alone it would interrupt about 35,800
   times a second at IPL 0. It raises the interrupted code's IPL by 2 (0 to
   2, 1 to 3), so it does not come back until the Mac lowers its IPL again.
   The Plus ROM never sets IPL 2 itself, so a stacked IPL of 2 still means
   "the Mac would take a level-1 interrupt now" (glueTimerC). */
    .globl _glueHbl
_glueHbl:
    ori.w   #0x0200, (%sp)
    rte

/* MFP Timer C, 60.235 Hz (D-18). Counts a tick and, every 2457600 / 40800
   ticks, a second; then, if the interrupted code runs at IPL 0 or 2 and no
   delivery is under way, rewrites its return so that the CPU goes to
   glueLevel1 at IPL 1 first, as a Mac VIA interrupt would. */
    .globl _glueTimerC
_glueTimerC:
    move.l  %d0, -(%sp)
    addq.w  #1, _glueTicksPending
    move.l  _glueSecondAcc, %d0
    add.l   #40800, %d0
    cmp.l   #2457600, %d0
    bcs.s   1f
    sub.l   #2457600, %d0
    addq.w  #1, _glueSecondsPending
1:
    move.l  %d0, _glueSecondAcc
    tst.b   _glueInLevel1
    bne.s   9f
    move.w  4(%sp), %d0                 /* the interrupted SR */
    andi.w  #0x0500, %d0                /* IPL 0 or 2: bits 8 and 10 clear */
    bne.s   9f
    move.l  (%sp)+, %d0
    subq.l  #6, %sp                     /* a frame for glueLevel1 under it */
    move.w  6(%sp), (%sp)
    andi.w  #0xF8FF, (%sp)
    ori.w   #0x0100, (%sp)
    move.l  #_glueLevel1, 2(%sp)
    rte
9:
    move.l  (%sp)+, %d0
    rte

/* The Mac's level-1 work, entered from glueTimerC's rewritten frame with
   the interrupted code's own frame on the stack: calls Lvl1DT[1]
   (VBLInt_VIA) once per pending tick and Lvl1DT[0] (OneSecInt_VIA) once per
   pending second, with A1 = the VIA shadow and D0-D3/A0-A3 saved, as
   VIA_DISPATCH does (plus/hw/interrupts.s:73-90). */
_glueLevel1:
    st      _glueInLevel1
    movem.l %d0-%d3/%a0-%a3, -(%sp)
1:
    tst.w   _glueTicksPending
    beq.s   2f
    subq.w  #1, _glueTicksPending
    lea     _glueViaShadow, %a1
    movea.l MAC_LVL1DT + 4 * MAC_LVL1_VBL, %a0
    jsr     (%a0)
#if defined(_DEBUG) && (_DEBUG > 0)
    jsr     _glueTickProbe
#endif
    bra.s   1b
2:
    tst.w   _glueSecondsPending
    beq.s   3f
    subq.w  #1, _glueSecondsPending
    lea     _glueViaShadow, %a1
    movea.l MAC_LVL1DT + 4 * MAC_LVL1_ONESEC, %a0
    jsr     (%a0)
    bra.s   1b
3:
    movem.l (%sp)+, %d0-%d3/%a0-%a3
    sf      _glueInLevel1
    rte

/* ------------------------------------------------------------------------
 * Routines the ROM patches call (src/patches/plus_v3.ini)
 */

/* rtc-clknomem: the RTC transaction (plus/boot/vectors.s:367-443). In: D1 the
   command byte, D2 the data byte for a write; out: D2 the byte read; return
   through A5. */
    .globl _glueClkNoMem
_glueClkNoMem:
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    move.l  %d2, -(%sp)
    move.l  %d1, -(%sp)
    jsr     _glueRtc
    addq.l  #8, %sp
    move.l  %d0, %d2
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    jmp     (%a5)

/* boot-sony-open: replaces the LEA, MOVE and _Open of ".Sony" in InitIOMgr
   (plus/boot/boot_install.s:107-109). A0, the parameter block, must
   survive for the _Open of ".Sound" that follows. */
    .globl _glueSonyOpen
_glueSonyOpen:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    jsr     _glueInstallDisk
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rts

/* scsi-boot-hook: InitIOMgr's JSR to the v3 SCSI patch
   (plus/boot/boot_install.s:128) comes here, on every boot and warm
   restart. */
    .globl _glueScsiHook
_glueScsiHook:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    jsr     _glueHook
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rts

/* boot-plotdisk: draw the disk icon with the symbol D7 points at (0 to
   erase the symbol), once Ticks has changed, as PlotDisk does
   (plus/boot/vectors.s:1158-1192). */
    .globl _gluePlotDisk
_gluePlotDisk:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    .short  0xA850                      /* _HideCursor */
    move.l  MAC_TICKS, %d0
1:
    cmp.l   MAC_TICKS, %d0
    beq.s   1b
    move.l  %d7, -(%sp)
    jsr     _gluePlotDiskC
    addq.l  #4, %sp
    .short  0xA853                      /* _ShowCursor */
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rts

/* boot-happy-mac: the Happy Mac (plus/boot/sad_mac.s:107-109); returns
   through A5. */
    .globl _glueShowHappyMac
_glueShowHappyMac:
    movem.l %d0-%d7/%a0-%a4/%a6, -(%sp)
    jsr     _glueShowHappyMacC
    movem.l (%sp)+, %d0-%d7/%a0-%a4/%a6
    jmp     (%a5)

/* ------------------------------------------------------------------------
 * OS traps for the C side
 */

/* long glueDrvrInstall(long refNum) */
    .globl _glueDrvrInstall
_glueDrvrInstall:
    move.l  4(%sp), %d0
    .short  0xA03D                      /* _DrvrInstall */
    ext.l   %d0
    rts

/* void glueAddDrive(long driveAndRefNum, void *dqe) */
    .globl _glueAddDrive
_glueAddDrive:
    move.l  4(%sp), %d0
    movea.l 8(%sp), %a0
    .short  0xA04E                      /* _AddDrive */
    rts

/* ------------------------------------------------------------------------
 * The disk driver (EPIC-02: no disk yet; EPIC-03 gives it a backend). A
 * DRVR header (boot-map.md 5.4): flags read, write, control and status
 * enabled, need lock; the entry points as offsets from the header.
 */
    .even
    .globl _glueDriver
_glueDriver:
    .short  0x4F00
    .short  0, 0, 0
    .short  drvrOpen - _glueDriver
    .short  drvrPrime - _glueDriver
    .short  drvrControl - _glueDriver
    .short  drvrStatus - _glueDriver
    .short  drvrClose - _glueDriver
    .byte   5
    .ascii  ".Sony"
    .even

drvrOpen:
drvrClose:
    moveq   #MAC_NOERR, %d0
    rts

/* Prime, Control and Status end through IODone with D0 = the result
   (plus/os/device_mgr.s:140-208), A1 = the DCE. */
drvrPrime:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverPrime
    addq.l  #4, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

drvrControl:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverControl
    addq.l  #4, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

drvrStatus:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverStatus
    addq.l  #4, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

#if defined(_DEBUG) && (_DEBUG > 0)
/* ------------------------------------------------------------------------
 * Debug builds: bus error, address error and the other faults the Mac would
 * turn into a bomb are traced with the frame, then the machine stops
 * (glueCrash). The 68000's bus and address error frames have 4 more words
 * in front of SR and PC.
 */
    .globl _glueCrashBus
_glueCrashBus:
    move.l  %sp, -(%sp)
    move.l  #2, -(%sp)
    jsr     _glueCrash

    .globl _glueCrashAddress
_glueCrashAddress:
    move.l  %sp, -(%sp)
    move.l  #3, -(%sp)
    jsr     _glueCrash

    .globl _glueCrashIllegal
_glueCrashIllegal:
    move.l  %sp, -(%sp)
    move.l  #4, -(%sp)
    jsr     _glueCrash

    .globl _glueCrashOther
_glueCrashOther:
    move.l  %sp, -(%sp)
    move.l  #0, -(%sp)
    jsr     _glueCrash
#endif
