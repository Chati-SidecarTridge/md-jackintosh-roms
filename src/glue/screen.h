/**
 * File: src/glue/screen.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's Shifter setup.
 */

#pragma once

#define SCREEN_BYTES 32000UL

/* Points the Shifter at base (256-byte aligned) and sets the resolution in
   the vertical blank: 640x400 on a mono monitor, 640x200 medium on a colour
   one. Returns 1 for mono. */
int screenInit(unsigned long base);

/* 1 when the Shifter is an STE's. */
int screenIsSte(void);

/* Waits for frames vertical blanks of the screen at base (71.2 a second on
   a mono monitor). */
void screenWaitFrames(unsigned long base, int frames);

/* 1 the first time it is called after a new frame started (the video
   counter fell back to the base), without waiting: for loops that poll
   something else meanwhile. */
int screenNewFrame(void);
