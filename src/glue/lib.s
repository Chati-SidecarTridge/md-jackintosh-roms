/**
 * File: src/glue/lib.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Freestanding helpers in assembly (lib.h).
 */

    .text

/*
 * unsigned long divU32U16(unsigned long n, unsigned short d,
 *                         unsigned short *remainder)
 *
 * The high word is divided first; its remainder (below d) goes in front of
 * the low word, so neither divu.w can overflow.
 */
    .globl _divU32U16
_divU32U16:
    move.l  4(%sp), %d1         /* n */
    move.l  %d1, %d0
    clr.w   %d0
    swap    %d0                 /* the high word of n */
    divu.w  10(%sp), %d0        /* by d (promoted to int): remainder:quotient */
    move.l  %d0, %a0            /* keep r1:q1 */
    move.w  %d1, %d0            /* r1:low word */
    divu.w  10(%sp), %d0        /* remainder:quotient of the low part */
    move.l  12(%sp), %a1
    move.l  %d0, %d1
    swap    %d1
    move.w  %d1, (%a1)          /* *remainder */
    move.l  %a0, %d1
    swap    %d1                 /* q1:r1 */
    move.w  %d0, %d1            /* q1:q0 */
    move.l  %d1, %d0
    rts                         /* only d0, d1, a0, a1 used: the scratch set */

/*
 * int probeRead(unsigned long address)
 *
 * Reads one byte at address with a temporary bus-error handler: returns 1
 * when the read completes, 0 when it faults. From romemul-switcher-rom's
 * src/st/probe.s; interrupts must be masked.
 */
    .globl _probeRead
_probeRead:
    move.l  4(%sp), %a0         /* the address to probe */
    move.l  0x8.w, %a1          /* save the bus-error vector */
    move.l  %sp, %d1            /* the stack to come back to */
    move.l  #probe_fault, 0x8.w
    moveq   #1, %d0
    tst.b   (%a0)               /* may fault */
    bra.s   probe_done
probe_fault:
    move.l  %d1, %sp            /* drop the bus-error frame */
    moveq   #0, %d0
probe_done:
    move.l  %a1, 0x8.w          /* restore the vector */
    rts
