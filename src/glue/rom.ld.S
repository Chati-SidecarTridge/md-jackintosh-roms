/**
 * File: src/glue/rom.ld.S
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Linker script template of the glue, preprocessed with
 * ROM_BASE_ADDR_UL. The code and read-only data stay in ROM; .data is copied
 * to RAM by the startup and .bss is cleared there. The link fails when the
 * glue would reach the image checksum or the Mac ROM (D-09).
 */

#include "mem.h"

ENTRY(_os_entry)

MEMORY
{
  ROM (rx)  : ORIGIN = ROM_BASE_ADDR_UL, LENGTH = GLUE_CHECKSUM_OFFSET_BYTES
  RAM (rwx) : ORIGIN = GLUE_RAM_BASE_ADDR, LENGTH = GLUE_RAM_DATA_SIZE_BYTES
}

SECTIONS
{
  .text ORIGIN(ROM) : AT(0)
  {
    *startup.s.o(.text .text.*)
    *(.text .text.*)
    *(.rodata .rodata.*)
  } > ROM

  . = ALIGN(4);

  .data ORIGIN(RAM) : AT(SIZEOF(.text))
  {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM

  __data_rom_start = ORIGIN(ROM) + LOADADDR(.data);

  .bss (NOLOAD) :
  {
    __bss_start = .;
    /* The RAM shadows of the Mac's chips first, at the start of the glue's
       RAM ($1400): the patches point the Mac at them. */
    *shadows.s.o(.bss)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > RAM

  _end = .;
  __glue_rom_end = ORIGIN(ROM) + LOADADDR(.data) + SIZEOF(.data);

  /* The ROM region's length already stops the code; this also stops
     initialised data copied from ROM. */
  ASSERT(__glue_rom_end <= ROM_BASE_ADDR_UL + GLUE_CHECKSUM_OFFSET_BYTES,
         "the glue reaches the image checksum")

  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
  }
}
