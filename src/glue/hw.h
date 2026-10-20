/**
 * File: src/glue/hw.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Atari ST and STE hardware registers the glue uses.
 */

#pragma once

#define HW_REG8(addr) (*(volatile unsigned char *)(unsigned long)(addr))
#define HW_REG16(addr) (*(volatile unsigned short *)(unsigned long)(addr))

/* MMU */
#define HW_MMU_CONFIG HW_REG8(0x00FF8001UL)

/* Shifter */
#define HW_VIDEO_BASE_HI HW_REG8(0x00FF8201UL)
#define HW_VIDEO_BASE_MID HW_REG8(0x00FF8203UL)
#define HW_VIDEO_COUNT_HI HW_REG8(0x00FF8205UL)
#define HW_VIDEO_COUNT_MID HW_REG8(0x00FF8207UL)
#define HW_VIDEO_COUNT_LO HW_REG8(0x00FF8209UL)
#define HW_PALETTE(n) HW_REG16(0x00FF8240UL + (2UL * (n)))
#define HW_SHIFTER_MODE HW_REG8(0x00FF8260UL)

/* STE Shifter only: the base's low byte, the line offset, the fine scroll. */
#define HW_STE_VIDEO_BASE_LO_ADDR 0x00FF820DUL
#define HW_STE_VIDEO_BASE_LO HW_REG8(HW_STE_VIDEO_BASE_LO_ADDR)
#define HW_STE_LINE_OFFSET HW_REG8(0x00FF820FUL)
#define HW_STE_HSCROLL HW_REG8(0x00FF8265UL)

/* The Mega STE's (and TT's) SCU: interrupt levels reach the CPU only when
   enabled here, and a reset clears both masks (EPIC-14). Absent on the ST
   and STE (a bus error). */
#define HW_SCU_SYS_INT_MASK_ADDR 0x00FF8E01UL
#define HW_SCU_SYS_INT_MASK HW_REG8(HW_SCU_SYS_INT_MASK_ADDR)
#define HW_SCU_VME_INT_MASK HW_REG8(0x00FF8E0DUL)

/* The Mega STE's cache and processor control (EPIC-14): bit 0 the 16 KB
   external cache, bit 1 16 MHz. A reset clears it (8 MHz, no cache). Only
   the Mega STE has it. */
#define HW_MSTE_CTRL_ADDR 0x00FF8E21UL
#define HW_MSTE_CTRL HW_REG8(HW_MSTE_CTRL_ADDR)
#define HW_MSTE_CACHE 0x01
#define HW_MSTE_16MHZ 0x02

/* MFP: GPIP bit 7 is the monitor type, 0 for mono. */
#define HW_MFP_GPIP HW_REG8(0x00FFFA01UL)
