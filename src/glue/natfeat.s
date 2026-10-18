/**
 * File: src/glue/natfeat.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Hatari's native features (romemul-switcher-rom's
 * src/st/natfeat.s). Linked into debug builds only: the opcodes trap on a
 * real ST.
 */

    .text

    .globl _nfGetId
_nfGetId:
    .short 0x7300
    rts

    .globl _nfCall
_nfCall:
    .short 0x7301
    rts
