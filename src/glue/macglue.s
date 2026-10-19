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

/* The MFP's vectors the glue serves, from its vector base $30 (mac.c,
   kMfpVectorBase): Timer C (channel 5) and the ACIAs (channel 6). */
    .equ    VEC_MFP_TIMERC, (0x30 + 5) * 4
    .equ    VEC_MFP_ACIA, (0x30 + 6) * 4

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
   "the Mac would take a level-1 interrupt now" (glueTimerC). It also puts
   back the vectors the glue serves, which Mac programs take for their own:
   a debugger installs its interrupt-switch handler at $70 and FPU
   exception handlers at vectors 48-54, where the MFP's Timer C and ACIA
   sit (THINK Pascal does both: its $70 handler arms a user break on a
   level-4 interrupt at IPL 0, and a Timer C tick in its FPU handler aborts
   the running program, EPIC-10). Coming down from IPL 2 the pending HBL is
   taken before any VBL or MFP interrupt can find IPL 0, so a foreign
   vector lasts microseconds; glueTimerC repeats it every tick. */
    .globl _glueHbl
_glueHbl:
    move.l  #_glueBusError, 8.w
    move.l  #_glueVbl, 0x70.w
    move.l  #_glueTimerC, VEC_MFP_TIMERC.w
    move.l  #_glueAciaIsr, VEC_MFP_ACIA.w
    ori.w   #0x0200, (%sp)
    rte

/* MFP Timer C, 60.235 Hz (D-18). First puts back the vectors the glue
   serves (bus error, HBL, VBL, Timer C, ACIA) if Mac code took them; Timer C
   gets here through a program's handler that passes it on, or the HBL
   handler has restored it already. A Plus never takes a bus error, so
   nothing there minds a write to $8 (Blockout copies through a nil pointer
   over $0-$E; Spectre re-installed its handler every tick too); levels 2
   and 4 and vectors 48-54 are the SCC, the interrupt switch and the FPU on
   a Mac but the HBL, the VBL and the MFP here (THINK Pascal's debugger takes
   them all: EPIC-10). Then counts a tick and, every 2457600 / 40800 ticks,
   a second, which the RTC's seconds count too; then, if the interrupted
   code runs at IPL 0 or 2 and no delivery is under way, rewrites its return
   so that the CPU goes to glueLevel1 at IPL 1 first, as a Mac VIA
   interrupt would. */
    .globl _glueTimerC
_glueTimerC:
    move.l  #_glueBusError, 8.w
    move.l  #_glueHbl, 0x68.w
    move.l  #_glueVbl, 0x70.w
    move.l  #_glueTimerC, VEC_MFP_TIMERC.w
    move.l  #_glueAciaIsr, VEC_MFP_ACIA.w
    move.l  %d0, -(%sp)
    addq.w  #1, _glueTicksPending
    move.l  _glueSecondAcc, %d0
    add.l   #40800, %d0
    cmp.l   #2457600, %d0
    bcs.s   1f
    sub.l   #2457600, %d0
    addq.w  #1, _glueSecondsPending
    addq.l  #1, _glueRtcSeconds         /* the clock chip counts (mac.c) */
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
    jsr     _glueKbdTick                /* the keyboard (ikbd.c) */
    jsr     _glueDiskTick               /* disks to announce (disk.c) */
    jsr     _glueCartTick               /* the Multi-device (cart.c) */
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

/* The bus error vector (buserr.c): a write to $0-$7 is skipped and the
   code resumes after the instruction; any other fault goes to the handler
   the vector held before, with the frame as it came. */
    .globl _glueBusError
_glueBusError:
    subq.l  #4, %sp                     /* room for A7, filled by glueBusFix */
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  %sp, -(%sp)                 /* the registers, D0-D7/A0-A7 */
    pea     64 + 4(%sp)                 /* the frame */
    jsr     _glueBusFix
    addq.l  #8, %sp
    tst.l   %d0
    beq.s   1f
    move.l  %d0, 64 + 10(%sp)           /* the frame's PC */
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    lea     4 + 8(%sp), %sp             /* A7's room; status, address, IR */
    rte
1:
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    addq.l  #4, %sp
    move.l  _glueBusPrevious, -(%sp)
    rts

/* The trace exception after a read re-run against our Mac ROM (buserr.c):
   glueBusTraceFix moves the register back, clears the trace bit and puts
   the trace vector back. */
    .globl _glueBusTrace
_glueBusTrace:
    movem.l %d0-%d7/%a0-%a6, -(%sp)
    move.l  %sp, -(%sp)                 /* the registers, D0-D7/A0-A6 */
    pea     60 + 4(%sp)                 /* the frame: SR, PC */
    jsr     _glueBusTraceFix
    addq.l  #8, %sp
    movem.l (%sp)+, %d0-%d7/%a0-%a6
    rte

#if defined(_TEST) && (_TEST > 0)
/* long glueBusSelfTest(void): writes to address 0 in the forms Mac code
   uses on nil handles; each must be skipped. Returns 1 when the stack came
   back balanced. */
    .globl _glueBusSelfTest
_glueBusSelfTest:
    move.l  %d2, -(%sp)
    move.l  %sp, %d2
    suba.l  %a0, %a0
    moveq   #0x55, %d0
    move.b  %d0, (%a0)                  /* MOVE.B Dn,(An) */
    bset    #7, (%a0)                   /* BSET #imm,(An) */
    move.b  %d0, -(%sp)
    move.b  (%sp)+, (%a0)               /* MOVE.B (SP)+,(An) */
    move.l  %d0, 4(%a0)                 /* MOVE.L Dn,d16(An) */
    clr.w   (%a0)                       /* CLR.W (An) */
    move.w  #0x1234, 2(%a0)             /* MOVE.W #imm,d16(An) */
    /* The Plus's ROM space (EPIC-09): a write skipped, then a read that
       must see our Mac ROM's first word, A1 left as it was. */
    movea.l #0x500000, %a1
    move.w  #1, 2(%a1)                  /* MOVE.W #imm,d16(An) */
    move.w  (%a1), %d1                  /* MOVE.W (An),Dn */
    moveq   #0, %d0
    cmp.w   MAC_ROM_BASE_ADDR, %d1
    bne.s   2f
    cmpa.l  #0x500000, %a1
    bne.s   2f
    cmp.l   %sp, %d2
    bne.s   2f
    moveq   #1, %d0
2:
    move.l  (%sp)+, %d2
    rts
#endif

/* The ACIAs' interrupt (MFP GPIP 4): the IKBD's bytes (ikbd.c). */
    .globl _glueAciaIsr
_glueAciaIsr:
    movem.l %d0-%d1/%a0-%a1, -(%sp)
    jsr     _glueIkbdIsr
    movem.l (%sp)+, %d0-%d1/%a0-%a1
    rte

/* void glueCallLvl1(long slot): calls Lvl1DT[slot] as VIA_DISPATCH would,
   A1 = the VIA shadow; from level-1 context only. The Mac's handlers may
   use D0-D3/A0-A3. */
    .globl _glueCallLvl1
_glueCallLvl1:
    move.l  4(%sp), %d0
    movem.l %d2-%d3/%a2-%a3, -(%sp)
    lsl.l   #2, %d0
    lea     MAC_LVL1DT, %a0
    movea.l 0(%a0,%d0.l), %a0
    lea     _glueViaShadow, %a1
    jsr     (%a0)
    movem.l (%sp)+, %d2-%d3/%a2-%a3
    rts

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

/* The Resource Manager's jCheckLoad vector ($7F0) comes here (sysreloc.c,
   D-15). On entry A2 is the resource's reference entry and A4 the map's
   handle (plus/toolbox/resource_mgr.s:607-617). The ROM's CheckLoad runs
   first; then glueSysRelocCheck relocates a known resource. Every register
   and the condition codes CheckLoad returned are kept. */
    .globl _glueCheckLoad
_glueCheckLoad:
    move.l  %a4, -(%sp)                 /* the map's handle */
    move.l  (%a4), -(%sp)               /* the map's base on entry */
    move.l  %a2, -(%sp)                 /* the reference entry on entry */
    movea.l _glueOrigCheckLoad, %a0
    jsr     (%a0)
    move    %sr, -(%sp)
    movem.l %d0-%d2/%a0-%a1, -(%sp)
    move.l  4 * 5 + 2 + 8(%sp), -(%sp)  /* the map's handle */
    move.l  4 * 6 + 2 + 4(%sp), -(%sp)  /* the map's base */
    move.l  4 * 7 + 2(%sp), -(%sp)      /* the reference entry */
    jsr     _glueSysRelocCheck
    lea     12(%sp), %sp
    movem.l (%sp)+, %d0-%d2/%a0-%a1
    move    (%sp)+, %ccr
    lea     12(%sp), %sp
    rts

/* mm-deref-nil: the Memory Manager's pointer check (DEREFHANDLE_CONT,
   plus/os/memory_mgr.s:1117-1127) reads the block header 8 bytes below the
   pointer. For nil that is $FFFFF8: harmless garbage on a Plus, a bus error
   in the ST's I/O space. A nil pointer takes the ROM's own "bad block"
   exit (memWZErr) instead; anything else goes on as before. Replaces the
   first 6 bytes, MOVE.L A0,D0 and AND.L (Lo3Bytes).W,D0. */
    .globl _glueDerefCheck
_glueDerefCheck:
    move.l  %a0, %d0
    and.l   MAC_LO3BYTES, %d0
    cmp.l   #8, %d0
    bcc.s   1f
    movea.l %d0, %a0
    subq.l  #8, %a0                     /* the exit adds the 8 back */
    jmp     MACROM_ADDR(MACROM_DEREFHANDLE_CONT_4)
1:
    jmp     MACROM_ADDR(MACROM_DEREFHANDLE_CONT + 6)

/* ------------------------------------------------------------------------
 * OS traps for the C side
 */

/* long gluePostDiskInserted(long drive): a disk-inserted event for the
   drive, as .Sony posts when it sees a disk (Event Manager, diskEvt 7);
   the result code (queue full). */
    .globl _gluePostDiskInserted
_gluePostDiskInserted:
    move.l  4(%sp), %d0
    movea.w #7, %a0                     /* diskEvt */
    .short  0xA02F                      /* _PostEvent */
    ext.l   %d0
    rts

/* void glueInsTime(void *task) */
    .globl _glueInsTime
_glueInsTime:
    movea.l 4(%sp), %a0
    .short  0xA058                      /* _InsTime */
    rts

/* A routine that does nothing, for tasks the glue installs only to be
   present. */
    .globl _glueRts
_glueRts:
    rts

/* A trap that does nothing and succeeds (D0 = noErr): _FlushCodeCache on
   a 68000, which has no cache (mac.c). */
    .globl _glueTrapNoErr
_glueTrapNoErr:
    moveq   #0, %d0
    rts

/* void *glueNewPtrSysClear(unsigned long size) */
    .globl _glueNewPtrSysClear
_glueNewPtrSysClear:
    move.l  4(%sp), %d0
    .short  0xA71E                      /* _NewPtrSysClear */
    move.l  %a0, %d0
    rts

#if defined(_TEST) && (_TEST > 0)
/* long glueSetPtrSize(void *ptr, unsigned long size): the result code.
   Test builds only (mac.c's Memory Manager self-test). */
    .globl _glueSetPtrSize
_glueSetPtrSize:
    movea.l 4(%sp), %a0
    move.l  8(%sp), %d0
    .short  0xA020                      /* _SetPtrSize */
    ext.l   %d0
    rts

/* void glueDisposPtr(void *ptr) */
    .globl _glueDisposPtr
_glueDisposPtr:
    movea.l 4(%sp), %a0
    .short  0xA01F                      /* _DisposPtr */
    rts
#endif

/* unsigned long glueGetHandleSize(unsigned long handle) */
    .globl _glueGetHandleSize
_glueGetHandleSize:
    movea.l 4(%sp), %a0
    .short  0xA025                      /* _GetHandleSize */
    rts

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
 * The disk driver (disk.c). A DRVR header (boot-map.md 5.4): flags read, write, control and status
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

/* Prime, Control and Status (disk.c, with the parameter block and the DCE)
   end through IODone with D0 = the result (plus/os/device_mgr.s:140-208),
   A1 = the DCE. */
drvrPrime:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverPrime
    addq.l  #8, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

drvrControl:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverControl
    addq.l  #8, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

drvrStatus:
    movem.l %d1-%d2/%a0-%a1, -(%sp)
    move.l  %a1, -(%sp)
    move.l  %a0, -(%sp)
    jsr     _glueDriverStatus
    addq.l  #8, %sp
    movem.l (%sp)+, %d1-%d2/%a0-%a1
    move.l  MAC_JIODONE, -(%sp)
    rts

#if defined(_DEBUG) && (_DEBUG > 0)
/* ------------------------------------------------------------------------
 * Debug builds: bus error, address error and the other faults the Mac would
 * turn into a bomb are traced with the frame and the registers, then the
 * machine stops (glueCrash). The 68000's bus and address error frames have
 * 4 more words in front of SR and PC.
 */
    .globl _glueCrashBus
_glueCrashBus:
    move.l  #2, -(%sp)
    bra.s   glueCrashCommon

    .globl _glueCrashAddress
_glueCrashAddress:
    move.l  #3, -(%sp)
    bra.s   glueCrashCommon

    .globl _glueCrashIllegal
_glueCrashIllegal:
    move.l  #4, -(%sp)
    bra.s   glueCrashCommon

    .globl _glueCrashOther
_glueCrashOther:
    move.l  #0, -(%sp)

/* Stack: the vector number, then the exception frame. Saves D0-D7/A0-A7
   (A7 as it was before the exception) and calls
   glueCrash(vector, frame, registers). */
glueCrashCommon:
    movem.l %d0-%d7/%a0-%a7, -(%sp)
    lea     64 + 4(%sp), %a0            /* the exception frame */
    move.l  %a0, 60(%sp)                /* A7 before the vector push */
    move.l  %sp, -(%sp)                 /* the registers */
    move.l  %a0, -(%sp)                 /* the frame */
    move.l  64 + 8(%sp), -(%sp)         /* the vector number */
    jsr     _glueCrash
#endif
