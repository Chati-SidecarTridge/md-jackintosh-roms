/**
 * File: src/glue/sound.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: One tick of the Mac's sound buffer as STE DMA samples
 * (sound.h, EPIC-15). The Plus plays the high byte of each of SoundBase's
 * 370 words at 22,255 Hz, unsigned; the STE plays signed bytes at 25,033 Hz.
 * Two ways, the [A]udio option's:
 * - fast: movep.l reads four high bytes at once, eor.l makes them signed,
 *   and every 16 samples become 18, the two added ones the averages of
 *   their neighbours (1.125 for 1.1248);
 * - smooth: every 8 samples become 9, each output interpolated between its
 *   two neighbours in ninths: output j of a group mixes s(j-1) and s(j) as
 *   (j, 9 - j) ninths, from glueSoundTable (sound.c), which holds for each
 *   raw sample value its signed value, doubled and saturated, times w / 9
 *   for w 0-9, 16 bytes a row. About four times the fast one's work.
 */

    .text

/*
 * long glueSoundConvertFast(const unsigned char *src, unsigned char *dst,
 *                           long extra)
 */
    .globl _glueSoundConvertFast
_glueSoundConvertFast:
    movem.l %d2-%d7, -(%sp)
    move.l  28(%sp), %a0            /* src: SoundBase */
    move.l  32(%sp), %a1            /* dst, even */
    move.l  #0x80808080, %d7        /* unsigned to signed */
    moveq   #22, %d6                /* 23 groups of 16 samples */
1:
    movep.l 0(%a0), %d0             /* s0 s1 s2 s3 */
    movep.l 8(%a0), %d1             /* s4 s5 s6 s7 */
    movep.l 16(%a0), %d2            /* s8 s9 s10 s11 */
    movep.l 24(%a0), %d3            /* s12 s13 s14 s15 */
    eor.l   %d7, %d0
    eor.l   %d7, %d1
    eor.l   %d7, %d2
    eor.l   %d7, %d3
    move.l  %d0, (%a1)+
    move.l  %d1, (%a1)+
    move.b  %d1, %d5                /* (s7 + s8) / 2 */
    ext.w   %d5
    move.l  %d2, %d4
    rol.l   #8, %d4
    ext.w   %d4
    add.w   %d4, %d5
    asr.w   #1, %d5
    move.l  %d2, %d4
    move.b  %d5, %d4                /* s8 s9 s10 avg */
    ror.l   #8, %d4                 /* avg s8 s9 s10 */
    move.l  %d4, (%a1)+
    move.l  %d3, %d4
    move.b  %d2, %d4                /* s12 s13 s14 s11 */
    ror.l   #8, %d4                 /* s11 s12 s13 s14 */
    move.l  %d4, (%a1)+
    move.b  32(%a0), %d4            /* (s15 + s16) / 2, s16 the next group's */
    eor.b   %d7, %d4
    ext.w   %d4
    move.b  %d3, %d5
    ext.w   %d5
    add.w   %d4, %d5
    asr.w   #1, %d5
    move.b  %d3, %d4
    lsl.w   #8, %d4
    move.b  %d5, %d4                /* s15 avg */
    move.w  %d4, (%a1)+
    lea     32(%a0), %a0
    dbf     %d6, 1b
    move.l  #414, %d0
    tst.l   36(%sp)                 /* extra */
    beq.s   2f
    movep.w 0(%a0), %d1             /* s368 s369 */
    eor.w   %d7, %d1
    move.w  %d1, (%a1)+
    addq.l  #2, %d0
2:
    movem.l (%sp)+, %d2-%d7
    rts                             /* d0-d1, a0-a1: the scratch set */

/* index = sample * 16, the sample being the high byte of the word at off */
.macro LOAD reg, off
    moveq   #0, \reg
    move.b  \off(%a0), \reg
    lsl.w   #4, \reg
.endm

/* one output: the row at ra weighted wa ninths, plus rb's wb ninths */
.macro MIX wa, ra, wb, rb
    move.b  \wa(%a2,\ra\().w), %d2
    add.b   \wb(%a2,\rb\().w), %d2
    move.b  %d2, (%a1)+
.endm

/*
 * long glueSoundConvertSmooth(const unsigned char *src, unsigned char *dst,
 *                             long extra)
 */
    .globl _glueSoundConvertSmooth
_glueSoundConvertSmooth:
    movem.l %d2/%d7/%a2, -(%sp)
    move.l  16(%sp), %a0            /* src: SoundBase */
    move.l  20(%sp), %a1            /* dst */
    movea.l _glueSoundTable, %a2
    moveq   #45, %d7                /* 46 groups of 8 samples: 368 */
1:
    LOAD    %d0, 0
    move.b  9(%a2,%d0.w), (%a1)+    /* s0 */
    LOAD    %d1, 2
    MIX     1, %d0, 8, %d1
    LOAD    %d0, 4
    MIX     2, %d1, 7, %d0
    LOAD    %d1, 6
    MIX     3, %d0, 6, %d1
    LOAD    %d0, 8
    MIX     4, %d1, 5, %d0
    LOAD    %d1, 10
    MIX     5, %d0, 4, %d1
    LOAD    %d0, 12
    MIX     6, %d1, 3, %d0
    LOAD    %d1, 14
    MIX     7, %d0, 2, %d1
    LOAD    %d0, 16                 /* s8: the next group's first */
    MIX     8, %d1, 1, %d0
    lea     16(%a0), %a0
    dbf     %d7, 1b
    move.l  #414, %d0
    tst.l   24(%sp)                 /* extra: samples 368 and 369 */
    beq.s   2f
    LOAD    %d1, 0
    move.b  9(%a2,%d1.w), (%a1)+
    LOAD    %d1, 2
    move.b  9(%a2,%d1.w), (%a1)+
    addq.l  #2, %d0
2:
    movem.l (%sp)+, %d2/%d7/%a2
    rts                             /* d0-d1, a0-a1: the scratch set */
