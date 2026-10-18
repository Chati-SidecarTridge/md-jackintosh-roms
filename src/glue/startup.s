/**
 * File: src/glue/startup.s
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Reset entry of the glue, at the start of the image. The first
 * bytes are the 68000's reset SSP and PC (the ST mirrors them at 0) and a
 * TOS-like header Hatari checks before it boots an image (C-05): the version
 * word at bytes 2-3 and the base address at bytes 8-11.
 */

.globl _os_entry
.extern _glueMain
.extern _glueRamBytes
.extern _glueRamMmu
.extern __data_rom_start
.extern __data_start
.extern __data_end
.extern __bss_start
.extern __bss_end

#include "mem.h"

/* The version word Hatari reads. 0x0104 keeps Hatari in ST mode, 0x0206
   lets it run an STE (romemul-switcher-rom's startup_st.s / startup_ste.s). */
#if (ROM_BASE_ADDR_UL == 0x00FC0000UL)
#define OS_VERSION 0x0104
#else
#define OS_VERSION 0x0206
#endif

.text
/* The TOS header layout (os_header), so Hatari reads deliberate values: it
   takes the country and PAL/NTSC from os_conf at offset 0x1C. Fields TOS
   itself would use are zero. */
_os_entry:
    bra.s   _reset              /* with the version: the reset SSP */
    .word   OS_VERSION          /* os_version */
    .long   _reset              /* reseth: the reset PC */
    .long   ROM_BASE_ADDR_UL    /* os_beg */
    .long   0                   /* os_end */
    .long   _reset              /* os_res1 */
    .long   0                   /* os_magic */
    .long   0x20261018          /* os_date */
    .word   0x0001              /* os_conf: country 0 (USA), PAL */
    .word   0                   /* os_dosdate */
    .long   0                   /* os_root */
    .long   0                   /* os_kbshift */
    .long   0                   /* os_run */
    .long   0                   /* os_dummy */

_reset:
    move.w  #0x2700, %sr
    move.l  #GLUE_STACK_TOP_ADDR, %sp
    reset

    /* Size the two RAM banks with TOS's algorithm, before any RAM is used:
       d5 holds the bytes and d6 the MMU value until .bss is cleared. The
       MMU keeps that value: the Mac gets all the RAM there is. */
#include "memconf.inc"
    move.b  %d6, 0xffff8001.w

    /* Copy initialised data from ROM to RAM. */
    lea     __data_rom_start, %a0
    lea     __data_start, %a1
    lea     __data_end, %a2
0:
    cmpa.l  %a2, %a1
    bcc.s   1f
    move.b  %a0@+, %a1@+
    bra.s   0b
1:
    /* Clear .bss. */
    lea     __bss_start, %a0
    lea     __bss_end, %a1
2:
    cmpa.l  %a1, %a0
    bcc.s   3f
    clr.b   %a0@+
    bra.s   2b
3:
    move.l  %d5, _glueRamBytes
    move.b  %d6, _glueRamMmu
    jmp     _glueMain
