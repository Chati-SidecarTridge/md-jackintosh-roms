/**
 * File: src/glue/cart.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The Multi-device's command senders (EPIC-05), ported from the
 * template's send_sync and send_write_sync (md-jackintosh's
 * target/atarist/src/inc/sidecart_functions.s): the magic word, the command,
 * the payload size, the random token (the seed read from the window), the
 * payload words and a 16-bit checksum go out as reads of
 * $FB8000 + word sign-extended; the answer is the token echoed at $FA2004
 * with the seed at $FA2008 moved on (the two-phase check). Unlike the
 * template these run from our ROM (the TOS Emulator's sockets, not the
 * cartridge), with the C calling convention, interrupts masked only while
 * the words go out, and a busy flag so that a command never starts inside
 * another one (an interrupt that wants to send gets CART_BUSY and defers).
 */

#include "cart.h"

    .text

/* long glueCartSend(long command, long payloadBytes, unsigned long first,
                     unsigned long second, unsigned long third,
                     unsigned long fourth) */
    .globl _glueCartSend
_glueCartSend:
    movem.l %d2-%d7/%a2, -(%sp)         /* 28 bytes */
    move.w  %sr, -(%sp)                 /* 2 more: the arguments at 34 */
    ori.w   #0x0700, %sr
    tst.b   _glueCartBusy
    beq.s   1f
    move.w  (%sp)+, %sr
    moveq   #CART_BUSY, %d0
    bra     9f
1:
    st      _glueCartBusy
    move.l  34(%sp), %d0                /* command */
    move.l  38(%sp), %d1                /* payload bytes after the token */
    move.l  42(%sp), %d3
    move.l  46(%sp), %d4
    move.l  50(%sp), %d5
    move.l  54(%sp), %d6
    move.l  CART_TOKEN_SEED_ADDR, %d2   /* the token: the seed as it is now */
    addq.w  #4, %d1                     /* the token is payload too */
    lea     CART_TOKEN_ADDR, %a1
    movea.l #CART_ROM3_MIDDLE, %a0

    move.w  #CART_MAGIC, %d7
    tst.b   (%a0, %d7.w)                /* the magic word */
    clr.l   %d7                         /* the checksum */
    add.w   %d0, %d7
    tst.b   (%a0, %d0.w)                /* the command */
    add.w   %d1, %d7
    tst.b   (%a0, %d1.w)                /* the payload size */
    add.w   %d2, %d7
    tst.b   (%a0, %d2.w)                /* the token, low word */
    swap    %d2
    add.w   %d2, %d7
    tst.b   (%a0, %d2.w)                /* the token, high word */
    cmp.w   #4, %d1
    beq.s   2f
    add.w   %d3, %d7
    tst.b   (%a0, %d3.w)
    cmp.w   #6, %d1
    beq.s   2f
    swap    %d3
    add.w   %d3, %d7
    tst.b   (%a0, %d3.w)
    cmp.w   #8, %d1
    beq.s   2f
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    cmp.w   #10, %d1
    beq.s   2f
    swap    %d4
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    cmp.w   #12, %d1
    beq.s   2f
    add.w   %d5, %d7
    tst.b   (%a0, %d5.w)
    cmp.w   #14, %d1
    beq.s   2f
    swap    %d5
    add.w   %d5, %d7
    tst.b   (%a0, %d5.w)
    cmp.w   #16, %d1
    beq.s   2f
    add.w   %d6, %d7
    tst.b   (%a0, %d6.w)
    cmp.w   #18, %d1
    beq.s   2f
    swap    %d6
    add.w   %d6, %d7
    tst.b   (%a0, %d6.w)
2:
    tst.b   (%a0, %d7.w)                /* the checksum */
    move.w  (%sp)+, %sr                 /* interrupts on while the RP works */
    swap    %d2                         /* the token as it was read */
    bsr.s   glueCartWait
    sf      _glueCartBusy
9:
    movem.l (%sp)+, %d2-%d7/%a2
    rts

/* The wait for the answer: the token echoed at (a1) and the seed moved on
   (a token match alone could be the previous answer, seen between the RP's
   two stores). d2 = the token. Returns d0 = CART_OK or CART_TIMEOUT. */
glueCartWait:
    move.l  #CART_TIMEOUT_SPINS, %d7
    moveq   #CART_OK, %d0
1:
    cmp.l   (%a1), %d2
    bne.s   2f
    cmp.l   CART_TOKEN_SEED_ADDR, %d2
    bne.s   3f
2:
    subq.l  #1, %d7
    bne.s   1b
    moveq   #CART_TIMEOUT, %d0
3:
    rts

/* long glueCartSendWrite(long command, unsigned long first,
                          unsigned long second, unsigned long third,
                          const void *data, long bytes) */
    .globl _glueCartSendWrite
_glueCartSendWrite:
    movem.l %d2-%d7/%a2, -(%sp)         /* 28 bytes */
    move.w  %sr, -(%sp)                 /* 2 more: the arguments at 34 */
    ori.w   #0x0700, %sr
    tst.b   _glueCartBusy
    beq.s   1f
    move.w  (%sp)+, %sr
    moveq   #CART_BUSY, %d0
    bra     9f
1:
    st      _glueCartBusy
    move.l  34(%sp), %d0                /* command */
    move.l  38(%sp), %d3
    move.l  42(%sp), %d4
    move.l  46(%sp), %d5
    movea.l 50(%sp), %a2                /* data */
    move.l  54(%sp), %d6                /* bytes */
    move.l  CART_TOKEN_SEED_ADDR, %d2
    and.l   #0xFFFF, %d6
    moveq   #16, %d1                    /* the token and d3-d5, always */
    add.l   %d6, %d1
    addq.l  #1, %d1                     /* rounded up to whole words */
    lsr.l   #1, %d1
    lsl.l   #1, %d1
    lea     CART_TOKEN_ADDR, %a1
    movea.l #CART_ROM3_MIDDLE, %a0

    move.w  #CART_MAGIC, %d7
    tst.b   (%a0, %d7.w)
    clr.l   %d7
    add.w   %d0, %d7
    tst.b   (%a0, %d0.w)
    add.w   %d1, %d7
    tst.b   (%a0, %d1.w)
    add.w   %d2, %d7
    tst.b   (%a0, %d2.w)
    swap    %d2
    add.w   %d2, %d7
    tst.b   (%a0, %d2.w)
    add.w   %d3, %d7
    tst.b   (%a0, %d3.w)
    swap    %d3
    add.w   %d3, %d7
    tst.b   (%a0, %d3.w)
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    swap    %d4
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    add.w   %d5, %d7
    tst.b   (%a0, %d5.w)
    swap    %d5
    add.w   %d5, %d7
    tst.b   (%a0, %d5.w)

    /* The data, a word at a time from an even or odd address; an odd
       count ends with its last byte in the high half of a word. d6 the
       header's checksum from here, d7 the data's. */
    move.l  %d6, %d5
    move.l  %d7, %d6
    clr.l   %d7
    btst    #0, %d5
    bne.s   5f
    addq.l  #1, %d5
    lsr.w   #1, %d5
    subq.w  #1, %d5
    bmi.s   8f                          /* no data */
    move.l  %a2, %d4
    btst    #0, %d4
    beq.s   4f
3:
    move.b  (%a2)+, %d3                 /* odd address, even count */
    lsl.w   #8, %d3
    move.b  (%a2)+, %d3
    tst.b   (%a0, %d3.w)
    add.w   %d3, %d7
    dbf     %d5, 3b
    bra.s   8f
4:
    move.w  (%a2)+, %d4                 /* even address, even count */
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    dbf     %d5, 4b
    bra.s   8f
5:
    addq.l  #1, %d5                     /* odd count */
    lsr.w   #1, %d5
    move.l  %a2, %d4
    btst    #0, %d4
    beq.s   7f
    subq.w  #1, %d5                     /* odd address */
    beq.s   61f
    subq.w  #1, %d5
6:
    move.b  (%a2)+, %d3
    lsl.w   #8, %d3
    move.b  (%a2)+, %d3
    tst.b   (%a0, %d3.w)
    add.w   %d3, %d7
    dbf     %d5, 6b
61:
    move.b  (%a2)+, %d4
    lsl.w   #8, %d4
    and.w   #0xFF00, %d4
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    bra.s   8f
7:
    subq.w  #1, %d5                     /* even address */
    beq.s   71f
    subq.w  #1, %d5
70:
    move.w  (%a2)+, %d4
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
    dbf     %d5, 70b
71:
    move.w  (%a2)+, %d4
    and.w   #0xFF00, %d4
    add.w   %d4, %d7
    tst.b   (%a0, %d4.w)
8:
    add.w   %d7, %d6
    tst.b   (%a0, %d6.w)                /* the checksum */
    move.w  (%sp)+, %sr
    swap    %d2
    bsr     glueCartWait
    sf      _glueCartBusy
9:
    movem.l (%sp)+, %d2-%d7/%a2
    rts

/* void glueCartCopy(void *dst, const volatile void *src, long bytes): the
   sectors READ left in the window, into the Mac's buffer. bytes is whole
   sectors (a multiple of 32), src the window (even). The loops are
   md-drives-emulator's (target/atarist/src/acsi.s, .acsi_copy_sector_even_32
   and .acsi_copy_sector_odd_loop): 32 bytes a turn to an even address, 16
   bytes one at a time to an odd one (a Mac buffer may be odd). */
    .globl _glueCartCopy
_glueCartCopy:
    movea.l 4(%sp), %a1                 /* dst */
    movea.l 8(%sp), %a0                 /* src */
    move.l  12(%sp), %d0                /* bytes */
    move.l  %a1, %d1
    btst    #0, %d1
    bne.s   2f
    lsr.l   #5, %d0
    subq.w  #1, %d0
1:
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    move.l  (%a0)+, (%a1)+
    dbf     %d0, 1b
    rts
2:
    lsr.l   #4, %d0
    subq.w  #1, %d0
3:
    .rept   16
    move.b  (%a0)+, (%a1)+
    .endr
    dbf     %d0, 3b
    rts

    .bss
    .globl _glueCartBusy
_glueCartBusy:
    .skip   2
