/**
 * File: src/glue/mem.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Image and RAM layout of the glue (D-09). Included by C, by
 * the startup assembly and by the linker script template, so values carry
 * no UL suffix.
 */

#pragma once

#ifndef ROM_BASE_ADDR_UL
#error "ROM_BASE_ADDR_UL must be defined before including mem.h"
#endif

/* The image: the glue first, the 128 KB Mac Plus ROM after it (D-09). */
#if (ROM_BASE_ADDR_UL == 0x00FC0000UL)
#define GLUE_ROM_SIZE_BYTES 0x00010000 /* st: 64 KB */
#define MACHINE_NAME "ST"
#elif (ROM_BASE_ADDR_UL == 0x00E00000UL)
#define GLUE_ROM_SIZE_BYTES 0x00020000 /* ste: 128 KB */
#define MACHINE_NAME "STE"
#else
#error "Unsupported ROM base address: 0x00FC0000UL (st) or 0x00E00000UL (ste)"
#endif

#define MAC_ROM_SIZE_BYTES 0x00020000
#define MAC_ROM_BASE_ADDR (ROM_BASE_ADDR_UL + GLUE_ROM_SIZE_BYTES)
#define IMAGE_SIZE_BYTES (GLUE_ROM_SIZE_BYTES + MAC_ROM_SIZE_BYTES)

/* The image checksum: the last long of the glue area, written by
   tools/jackrom.py (EPIC-00 STORY-03). */
#define GLUE_CHECKSUM_SIZE_BYTES 4
#define GLUE_CHECKSUM_OFFSET_BYTES \
  (GLUE_ROM_SIZE_BYTES - GLUE_CHECKSUM_SIZE_BYTES)

/* The glue's RAM (D-19): a fixed block below the Mac's system heap, which a
   patch moves up to $5400 (src/patches/plus_v3.ini, boot-sysheap-start). The
   RAM shadows of the Mac's chips come first (shadows.s), then .data and
   .bss; the stack at the top is used only before the glue enters the Mac
   ROM, which sets its own. */
#define GLUE_RAM_BASE_ADDR 0x00001400
#define GLUE_RAM_SIZE_BYTES 0x00004000
#define GLUE_STACK_TOP_ADDR (GLUE_RAM_BASE_ADDR + GLUE_RAM_SIZE_BYTES)
#define GLUE_STACK_SIZE_BYTES 0x00000800
#define GLUE_RAM_DATA_SIZE_BYTES (GLUE_RAM_SIZE_BYTES - GLUE_STACK_SIZE_BYTES)

/* The Mac's screen: 32000 bytes for 640x400, 256-byte aligned, below the
   sound buffer at MemTop - $300 (D-19). */
#define MAC_SCREEN_BELOW_MEMTOP 0x00008100

/* Debug builds: a blob the Hatari harness may preload this far below the top
   of RAM (EPIC-00 STORY-05), "JKPL", its length, then its bytes. EPIC-03's
   RAM disk replaces it. */
#define GLUE_PRELOAD_BELOW_TOP_BYTES 0x00040000
#define GLUE_PRELOAD_MAGIC 0x4A4B504C
