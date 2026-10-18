/**
 * File: src/glue/shadows.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: RAM that stands in for the Mac's chips and its top-of-RAM
 * save area (EPIC-02 STORY-02, STORY-05). The patches in
 * src/patches/plus_v3.ini point the Mac's VIA, IWM, SCC and SERegs addresses
 * here, so its accesses land in harmless RAM. Linked first in .bss
 * (rom.ld.S); cleared at boot, then set up by mac.c.
 */

    .bss
    .even

/* The VIA: registers at base + n * $200 for n = 0..15 (port B at +0, port A
   at +$1E00). The VIA uses even addresses only, so the IWM, whose registers
   sit at odd addresses with the same spacing, shares the block at +1 and
   the two never collide (.Sony, the IWM's only user, is replaced). */
    .globl _glueViaShadow
_glueViaShadow:
    .skip 0x1E10

/* The SCC: reads at base + 0, 2, 4, 6 (channel B control, A control, B data,
   A data), writes at the same offsets of an odd base. Reads and writes
   interleave in one block, so the write base is the read base + 1. */
    .globl _glueSccShadow
_glueSccShadow:
    .skip 16

/* SERegs: the system-error register save area the Plus keeps at the
   absolute $3FFC80, which is empty address space below 4 MB on an ST
   (C-08). */
    .globl _glueSERegs
_glueSERegs:
    .skip 128

    .globl _glueIwmShadow
    .set _glueIwmShadow, _glueViaShadow + 1
    .globl _glueViaShadowPortA
    .set _glueViaShadowPortA, _glueViaShadow + 0x1E00
    .globl _glueSccRdShadow
    .set _glueSccRdShadow, _glueSccShadow
    .globl _glueSccWrShadow
    .set _glueSccWrShadow, _glueSccShadow + 1
    .globl _glueSccRdShadowA
    .set _glueSccRdShadowA, _glueSccShadow + 2
    .globl _glueSccWrShadowA
    .set _glueSccWrShadowA, _glueSccShadow + 3

    .globl _glueSERegs20
    .set _glueSERegs20, _glueSERegs + 0x20
    .globl _glueSERegs3C
    .set _glueSERegs3C, _glueSERegs + 0x3C
    .globl _glueSERegs40
    .set _glueSERegs40, _glueSERegs + 0x40
    .globl _glueSERegs44
    .set _glueSERegs44, _glueSERegs + 0x44
    .globl _glueSERegs4A
    .set _glueSERegs4A, _glueSERegs + 0x4A
    .globl _glueSERegs4C
    .set _glueSERegs4C, _glueSERegs + 0x4C
    .globl _glueSERegs50
    .set _glueSERegs50, _glueSERegs + 0x50
    .globl _glueSERegs54
    .set _glueSERegs54, _glueSERegs + 0x54
    .globl _glueSERegs5A
    .set _glueSERegs5A, _glueSERegs + 0x5A
    .globl _glueSERegs5E
    .set _glueSERegs5E, _glueSERegs + 0x5E
    .globl _glueSERegs60
    .set _glueSERegs60, _glueSERegs + 0x60
    .globl _glueSERegs76
    .set _glueSERegs76, _glueSERegs + 0x76
    .globl _glueSERegs78
    .set _glueSERegs78, _glueSERegs + 0x78
    .globl _glueSERegs7C
    .set _glueSERegs7C, _glueSERegs + 0x7C
