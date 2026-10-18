/**
 * File: src/glue/screen.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's Shifter setup, from romemul-switcher-rom's
 * src/st/screen.c: the resolution is set only in the vertical blank, after
 * two vertical syncs, as EmuTOS does (romemul-switcher-rom EPIC-03 STORY-14:
 * set mid-frame, a real Mega ST in mono came up wrapped by 4 bytes).
 */

#include "screen.h"

#include "hw.h"
#include "lib.h"
#include "trace.h"

enum {
  kShifterModeMedium = 0x01,
  kShifterModeHigh = 0x02,
  kMonitorColourBit = 0x80,
  kByteShiftHigh = 16,
  kByteShiftMid = 8,
  kByteMask = 0xFF,
  kVblStableReads = 4,      /* the counter held at the base: no line shows */
  kVblGuardPolls = 20000UL, /* several frames: never wait forever */
  kSteBaseLoTest = 90,
  kPaletteBlack = 0x000,
  kPaletteWhite = 0x777
};

static int screenSte;

/* The video address counter. The Shifter reloads it from the base at each
   vertical sync and holds it there until the first displayed line. */
static unsigned long screenVideoCounter(void) {
  return ((unsigned long)HW_VIDEO_COUNT_HI << kByteShiftHigh) |
         ((unsigned long)HW_VIDEO_COUNT_MID << kByteShiftMid) |
         (unsigned long)HW_VIDEO_COUNT_LO;
}

/* Waits for the next vertical blank: until the counter leaves the base,
   then until it holds at the base for kVblStableReads reads in a row (a torn
   read looks like the base once, never four times). Returns 0 if it never
   did. */
static int screenWaitVbl(unsigned long base) {
  unsigned long guard;
  int stable = 0;
  for (guard = 0; guard < kVblGuardPolls && screenVideoCounter() == base;
       ++guard) {
  }
  for (guard = 0; guard < kVblGuardPolls && stable < kVblStableReads; ++guard) {
    stable = (screenVideoCounter() == base) ? stable + 1 : 0;
  }
  return stable == kVblStableReads;
}

/* An STE Shifter, found as EmuTOS finds it (bios/machine.c, detect_video()):
   the base's low byte holds what is written to it. Leaves it at 0. */
static int screenDetectSte(void) {
  if (!probeRead(HW_STE_VIDEO_BASE_LO_ADDR)) {
    return 0;
  }
  HW_STE_VIDEO_BASE_LO = kSteBaseLoTest;
  (void)HW_VIDEO_BASE_MID;
  if (HW_STE_VIDEO_BASE_LO != kSteBaseLoTest) {
    return 0;
  }
  HW_STE_VIDEO_BASE_LO = 0;
  (void)HW_PALETTE(0);
  return HW_STE_VIDEO_BASE_LO == 0;
}

int screenIsSte(void) { return screenSte; }

int screenInit(unsigned long base) {
  int vbl;
  int mono;

  screenSte = screenDetectSte();
  HW_VIDEO_BASE_HI = (unsigned char)((base >> kByteShiftHigh) & kByteMask);
  HW_VIDEO_BASE_MID = (unsigned char)((base >> kByteShiftMid) & kByteMask);

  vbl = screenWaitVbl(base);
  vbl &= screenWaitVbl(base);

  /* GPIP bit 7: 0 for a mono monitor. Read once two frames have run. */
  mono = (HW_MFP_GPIP & kMonitorColourBit) == 0;
  HW_SHIFTER_MODE = mono ? kShifterModeHigh : kShifterModeMedium;
  if (screenSte) {
    /* Whatever a program left there before the reset. */
    HW_STE_LINE_OFFSET = 0;
    HW_STE_HSCROLL = 0;
  }
  /* Mono: bit 0 of colour 0 set gives white paper and black ink, as the Mac
     draws (a set pixel is black). Colour: white text on black. */
  HW_PALETTE(0) = mono ? kPaletteWhite : kPaletteBlack;
  HW_PALETTE(1) = mono ? kPaletteBlack : kPaletteWhite;
  TRACE("screen %s %06lx vbl %s ste %d", mono ? "mono" : "colour", base,
        vbl ? "ok" : "timeout", screenSte);
  return mono;
}
