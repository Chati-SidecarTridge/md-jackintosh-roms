/**
 * File: src/glue/text.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Text on the glue's own screens (the diagnostic screen, later
 * crash reports): 8x8 glyphs, doubled to 8x16 in mono (80x25), 8x8 in
 * medium resolution (80x25). No escapes, no scrolling.
 */

#pragma once

#define TEXT_COLUMNS 80
#define TEXT_ROWS 25

void textInit(volatile unsigned char *screen, int mono);
void textClear(void);
void textAt(unsigned short column, unsigned short row);
void textPuts(const char *text);
/* Inverse video for what is written next (the menus' title, selection and
   status line, as romemul-switcher-rom draws them). */
void textInverse(int inverse);
/* The row blanked in the current video (inverse or not), the cursor at its
   start. */
void textClearRow(unsigned short row);
void textPrintf(const char *fmt, ...);

/* A small vsnprintf: %s %c %d %u %x %X, an optional 0 flag and width, and
   the l modifier. Always terminates out; returns the length written. */
int textFormat(char *out, unsigned long size, const char *fmt,
               __builtin_va_list args);
