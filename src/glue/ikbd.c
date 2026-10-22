/**
 * File: src/glue/ikbd.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The Atari keyboard and mouse as the Mac's (EPIC-04). The
 * IKBD's bytes arrive through the ACIA and MFP GPIP 4 (glueAciaIsr); mouse
 * packets move the Mac's MTemp and set CrsrNew, as its SCC mouse handlers
 * do (plus/hw/interrupts.s:286-330), and the left button is port B bit 3
 * of the VIA shadow, which VBLInt_VIA reads. Keys become Mac Plus key
 * transitions, and the glue plays the keyboard on the VIA shift register:
 * each tick (glueKbdTick, in level-1 context) it completes the command the
 * ROM shifted out (Inquiry, Instant, Model) and answers with a queued key,
 * the model number, or a "no key" ($7B) after a quarter second, which also
 * keeps KybdTask from clearing KeyMap (plus/os/device_mgr.s:894-1115).
 * With a layout other than US (EPIC-12, layout.h), a key types the
 * character it shows on the Mac's US System: its strokes set Shift and
 * Option as each needs, around the physical ones. The mouse's speed
 * (EPIC-12) scales its counts before the Mac's own tracking.
 */

#include "hw.h"
#include "layout.h"
#include "lib.h"
#include "macrom.h"
#include "menu.h"
#include "trace.h"

extern unsigned char glueViaShadow[];
extern void glueCallLvl1(long slot);

enum {
  /* ACIA (keyboard) */
  kAciaReset = 0x03,
  kAciaConfig = 0x96, /* divide by 64, 8 bits, 1 stop, RX interrupt */
  kAciaRxFull = 0x01,
  kAciaOverrun = 0x20,
  kAciaTxEmpty = 0x02,
  kAciaTxSpins = 0x4000, /* a byte takes 1.3 ms at 7812.5 baud */
  /* MFP: GPIP 4 is the ACIAs' interrupt line, channel 6 */
  kMfpGpip4 = 0x10,
  kMfpIerbGpip4 = 0x40,
  /* IKBD packets */
  kHeaderStatus = 0xF6,
  kHeaderAbsMouse = 0xF7,
  kHeaderRelMouseLow = 0xF8,
  kHeaderRelMouseHigh = 0xFB,
  kHeaderTime = 0xFC,
  kHeaderJoysticks = 0xFD,
  kHeaderJoystick0 = 0xFE,
  kKeyBreak = 0x80,
  kKeyCodeMask = 0x7F,
  kLeftButton = 0x02, /* in a relative mouse header */
  kRightButton = 0x01,
  kMacCommand = 0x37, /* the Mac Plus keyboard's Command key */
  kMacShift = 0x38,
  kMacOption = 0x3A,
  kScanLeftShift = 0x2A,
  kScanRightShift = 0x36,
  kScanAlt = 0x38,
  kScanCapsLock = 0x3A,
  kDoubleTicks = 4,  /* each half of the right button's double-click: the
                        ROM debounces 3 ticks, checks every other one */
  kDoublePhases = 4, /* down, up, down, up */
  /* The IKBD's reset command and its reply (TOS sends the same) */
  kIkbdReset = 0x80,
  kIkbdResetArg = 0x01,
  kIkbdVersionLow = 0xF0,
  kIkbdVersionHigh = 0xF1,
  kIkbdSyncTicks = 60,
  /* The VIA shadow */
  kViaPortB = 0x0000,
  kViaSr = 0x1400,
  kViaAcr = 0x1600,
  kAcrShiftOut = 0x10,
  kPortBButtonUp = 0x08,
  /* The Mac keyboard's protocol */
  kCmdInquiry = 0x10,
  kCmdInstant = 0x14,
  kCmdModel = 0x16,
  kReplyNull = 0x7B,
  kReplyKeypad = 0x79,
  kModelPlus = 0x0B, /* KbdType Spectre reported */
  kNullAfterTicks = 15,
  kLvl1Keyboard = 2,
  kQueueSize = 64, /* a translated key takes up to 10 transitions */
  kQueueMask = kQueueSize - 1,
  kKeypadFlag = 0x80, /* in the key table: send $79 first */
  kKeypadBase = 0x40,
  kNoKey = 0xFF,
  kPacketMax = 8,
  kLongPacket = 7, /* status, time of day */
  kAbsMousePacket = 6,
  /* Low memory */
  kMTempV = 0x0828,
  kMTempH = 0x082A,
  kRawMouseV = 0x082C,
  kRawMouseH = 0x082E,
  kMouseV = 0x0830,
  kMouseH = 0x0832,
  kCrsrNew = 0x08CE,
  kCrsrCouple = 0x08CF,
  kTestRelative = 0x04 /* glueTestInput.buttons: v, h are counts */
};

#define ACIA_CONTROL HW_REG8(0x00FFFC00UL)
#define ACIA_DATA HW_REG8(0x00FFFC02UL)
#define MFP_REG(offset) HW_REG8(0x00FFFA01UL + (offset))
#define MFP_AER MFP_REG(0x02)
#define MFP_DDR MFP_REG(0x04)
#define MFP_IERB MFP_REG(0x08)
#define MFP_IMRB MFP_REG(0x14)
#define LOWMEM_WORD(addr) (*(volatile short *)(unsigned long)(addr))
#define LOWMEM_BYTE(addr) (*(volatile unsigned char *)(unsigned long)(addr))

/* ST scancode → Mac Plus key code (US). Keypad codes carry kKeypadFlag
   and the code the ROM sees after its $40 offset; kNoKey: no Mac key.
   Control is Command and Alternate is Option, as in Spectre. */
static const unsigned char kKeyTable[128] = {
    /* 00 */ kNoKey,
    0x32,
    0x12,
    0x13,
    0x14,
    0x15,
    0x17,
    0x16,
    /* 08 */ 0x1A,
    0x1C,
    0x19,
    0x1D,
    0x1B,
    0x18,
    0x33,
    0x30,
    /* 10 */ 0x0C,
    0x0D,
    0x0E,
    0x0F,
    0x11,
    0x10,
    0x20,
    0x22,
    /* 18 */ 0x1F,
    0x23,
    0x21,
    0x1E,
    0x24,
    0x37,
    0x00,
    0x01,
    /* 20 */ 0x02,
    0x03,
    0x05,
    0x04,
    0x26,
    0x28,
    0x25,
    0x29,
    /* 28 */ 0x27,
    0x32,
    0x38,
    0x2A,
    0x06,
    0x07,
    0x08,
    0x09,
    /* 30 */ 0x0B,
    0x2D,
    0x2E,
    0x2B,
    0x2F,
    0x2C,
    0x38,
    kNoKey,
    /* 38 */ 0x3A,
    0x31,
    0x39,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    /* 40 */ kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    0xC7,
    /* 48 */ 0xCD,
    kNoKey,
    0xCE,
    0xC6,
    kNoKey,
    0xC2,
    0xC5,
    kNoKey,
    /* 50 */ 0xC8,
    kNoKey,
    kNoKey,
    0x33,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    /* 58 */ kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    /* 60 */ kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    0xCB,
    0xC3,
    0xD9,
    /* 68 */ 0xDB,
    0xDC,
    0xD6,
    0xD7,
    0xD8,
    0xD3,
    0xD4,
    0xD5,
    /* 70 */ 0xD2,
    0xC1,
    0xCC,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    /* 78 */ kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey,
    kNoKey};

/* Key transitions waiting for the ROM's Inquiry or Instant. */
static volatile unsigned char glueKeyQueue[kQueueSize];
static volatile unsigned glueKeyHead;
static volatile unsigned glueKeyTail;

/* The IKBD packet being collected. */
static unsigned char gluePacket[kPacketMax];
static unsigned gluePacketLength;
static unsigned gluePacketNeeded;

/* The keyboard side of the shift register. */
static int glueKbdWaiting;
/* Ticks left to wait for the reset's reply; bytes are dropped until it
   comes (0: the stream is in step). */
static volatile unsigned glueIkbdSyncTicks;
/* After an overrun: bytes are dropped until a packet's header, since a
   lost byte would make a mouse packet's deltas read as keys. */
static int glueIkbdResync;
static unsigned char glueKbdCommand;
static unsigned glueKbdNullTicks;

#if defined(_TEST) && (_TEST > 0)
/* Test builds: the harness writes a cursor position and buttons here
   through Hatari's debugger, then bumps seq (EPIC-04 STORY-04); with
   buttons bit 2, v and h are a mouse packet's counts instead (EPIC-12). */
struct GlueTestInput {
  unsigned short seq;
  short v;
  short h;
  unsigned short buttons;
};
volatile struct GlueTestInput glueTestInput;
static unsigned short glueTestSeen;
#endif

static void glueKeyPush(unsigned char byte) {
  const unsigned next = (glueKeyHead + 1) & kQueueMask;
  if (next != glueKeyTail) {
    glueKeyQueue[glueKeyHead] = byte;
    glueKeyHead = next;
  }
}

static int glueKeyPop(void) {
  int byte;
  if (glueKeyTail == glueKeyHead) {
    return -1;
  }
  byte = glueKeyQueue[glueKeyTail];
  glueKeyTail = (glueKeyTail + 1) & kQueueMask;
  return byte;
}

static void glueSetButton(int down) {
  if (down) {
    glueViaShadow[kViaPortB] &= (unsigned char)~kPortBButtonUp;
  } else {
    glueViaShadow[kViaPortB] |= kPortBButtonUp;
  }
}

/* The mouse's speed (glueOptions.mouseSpeed, 1-10): eighths of a count
   per count, 8 at 5 (as the ST moves); what a packet leaves of a count is
   kept for the next, so slow movements are not lost. */
enum { kMouseGainOne = 8 };
static const unsigned char kMouseGain[GLUE_MOUSE_SPEED_MAX] = {
    2, 3, 4, 6, 8, 10, 12, 16, 20, 24};
static int glueMouseRestH;
static int glueMouseRestV;

static int glueMouseScale(int delta, int *rest) {
  const int eighths =
      (int)mulS16((short)delta, (short)kMouseGain[glueOptions.mouseSpeed - 1]) +
      *rest;
  const int counts = eighths / kMouseGainOne;
  *rest = eighths - (counts * kMouseGainOne);
  return counts;
}

static void glueMouseMove(int deltaH, int deltaV) {
  deltaH = glueMouseScale(deltaH, &glueMouseRestH);
  deltaV = glueMouseScale(deltaV, &glueMouseRestV);
  LOWMEM_WORD(kMTempH) = (short)(LOWMEM_WORD(kMTempH) + deltaH);
  LOWMEM_WORD(kMTempV) = (short)(LOWMEM_WORD(kMTempV) + deltaV);
  LOWMEM_BYTE(kCrsrNew) = LOWMEM_BYTE(kCrsrCouple);
}

static void glueMacKey(unsigned char code, int release) {
  glueKeyPush((unsigned char)(((code << 1) | 1) | (release ? kKeyBreak : 0)));
}

/* Shift and Alt as held on the ST, and Shift and Option as the Mac has
   them; the translated key held down (its Mac key), 0 for none. */
static int glueLeftShift;
static int glueRightShift;
static int glueAlt;
/* Caps Lock as the Plus's locking key: down from one press to the next
   (EPIC-16 INPUT-01; the ST's key is a plain make/break key). */
static int glueCapsLocked;
static int glueMacShift;
static int glueMacOption;
static unsigned char glueHeldScan;
static unsigned char glueHeldKey;

static void glueSetModifiers(int shift, int option) {
  if (glueMacShift != shift) {
    glueMacShift = shift;
    glueMacKey(kMacShift, !shift);
  }
  if (glueMacOption != option) {
    glueMacOption = option;
    glueMacKey(kMacOption, !option);
  }
}

/* The translated key up, and the Mac's modifiers back to the ST's. */
static void glueHeldRelease(void) {
  if (glueHeldScan != 0) {
    glueMacKey(glueHeldKey, 1);
    glueHeldScan = 0;
  }
  glueSetModifiers(glueLeftShift || glueRightShift, glueAlt);
}

static void glueStroke(unsigned char stroke) {
  glueSetModifiers((stroke & GLUE_STROKE_SHIFT) != 0,
                   (stroke & GLUE_STROKE_OPTION) != 0);
  glueMacKey(stroke & GLUE_STROKE_KEY, 0);
}

/* A key in the chosen layout: a dead key's stroke first, then the last
   stroke stays down until the ST key is released (so that it repeats). */
static void glueLayoutKey(unsigned code, int release,
                          const unsigned char *strokes) {
  if (release) {
    if (code == glueHeldScan) {
      glueHeldRelease();
    }
    return;
  }
  glueHeldRelease();
  if (strokes[1] != GLUE_STROKE_NONE) {
    glueStroke(strokes[0]);
    glueMacKey(strokes[0] & GLUE_STROKE_KEY, 1);
    ++strokes;
  }
  glueStroke(strokes[0]);
  glueHeldScan = (unsigned char)code;
  glueHeldKey = strokes[0] & GLUE_STROKE_KEY;
}

static void glueKey(unsigned char scancode) {
  const unsigned code = scancode & kKeyCodeMask;
  const unsigned char mac = kKeyTable[code];
  const int release = (scancode & kKeyBreak) != 0;
  const unsigned char *strokes;
  if (code == kScanLeftShift || code == kScanRightShift || code == kScanAlt) {
    if (code == kScanLeftShift) {
      glueLeftShift = !release;
    } else if (code == kScanRightShift) {
      glueRightShift = !release;
    } else {
      glueAlt = !release;
    }
    glueHeldRelease(); /* sets the Mac's Shift and Option to the ST's */
    return;
  }
  if (code == kScanCapsLock) {
    if (!release) {
      glueCapsLocked = !glueCapsLocked;
      glueMacKey(mac, !glueCapsLocked);
    }
    return;
  }
  strokes = glueLayoutStrokes(
      glueOptions.keyboard, code,
      ((glueLeftShift || glueRightShift) ? GLUE_LAYER_SHIFT : 0) |
          (glueAlt ? GLUE_LAYER_ALT : 0));
  if (strokes != 0) {
    glueLayoutKey(code, release, strokes);
    return;
  }
  if (mac == kNoKey) {
    return;
  }
  if (!release) {
    glueHeldRelease(); /* another key: the Mac's modifiers as held */
  }
  if (mac & kKeypadFlag) {
    glueKeyPush(kReplyKeypad);
    glueMacKey((unsigned char)((mac & ~kKeypadFlag) - kKeypadBase), release);
  } else {
    glueMacKey(mac, release);
  }
}

/* The right mouse button (EPIC-12, glueOptions.rightButton): the left
   button's state, and what the right one adds to it. */
static int glueLeftDown;
static int glueRightDown;
static int glueRightHeld;
static int glueRightCommandPending; /* Command sent, the click waits for it */
static int glueDoublePhase;
static int glueDoubleTicks;

static void glueButtons(void) { glueSetButton(glueLeftDown || glueRightHeld); }

static void glueRight(int down) {
  const unsigned char command = (kMacCommand << 1) | 1;
  if (down == glueRightDown) {
    return;
  }
  glueRightDown = down;
  if (glueOptions.rightButton == GLUE_RIGHT_COMMAND) {
    if (down) {
      glueKeyPush(command); /* the click waits until the Mac has it */
      glueRightCommandPending = 1;
    } else {
      glueRightCommandPending = 0;
      glueRightHeld = 0;
      glueButtons();
      glueKeyPush(command | kKeyBreak);
    }
  } else if (glueOptions.rightButton == GLUE_RIGHT_DOUBLE && down &&
             glueDoublePhase == 0) {
    glueDoublePhase = 1;
    glueDoubleTicks = kDoubleTicks;
  }
}

/* Each tick: the right button's Command-click or double-click. */
static void glueRightTick(void) {
  if (glueRightCommandPending && glueKeyHead == glueKeyTail) {
    glueRightCommandPending = 0;
    glueRightHeld = 1;
    glueButtons();
  }
  if (glueDoublePhase != 0 && ++glueDoubleTicks >= kDoubleTicks) {
    glueDoubleTicks = 0;
    glueRightHeld = glueDoublePhase & 1; /* phases 1 and 3 down */
    glueButtons();
    if (++glueDoublePhase > kDoublePhases) {
      glueDoublePhase = 0;
    }
  }
}

static void glueIkbdByte(unsigned char byte) {
  if (glueIkbdResync) {
    if (byte < kHeaderStatus) {
      return;
    }
    glueIkbdResync = 0;
    gluePacketNeeded = 0;
  }
  if (glueIkbdSyncTicks != 0) {
    /* A reset of the ST leaves the IKBD running: what arrives before the
       reply is the rest of a packet already under way, and its delta
       bytes ($F8-$FB) would read as headers with a button down. */
    if (byte == kIkbdVersionLow || byte == kIkbdVersionHigh) {
      glueIkbdSyncTicks = 0;
    }
    return;
  }
  if (gluePacketNeeded == 0) {
    if (byte >= kHeaderRelMouseLow && byte <= kHeaderRelMouseHigh) {
      gluePacketNeeded = 3;
    } else if (byte == kHeaderStatus) {
      gluePacketNeeded = kPacketMax; /* the header and 7 bytes */
    } else if (byte == kHeaderTime) {
      gluePacketNeeded = kLongPacket;
    } else if (byte == kHeaderAbsMouse) {
      gluePacketNeeded = kAbsMousePacket;
    } else if (byte == kHeaderJoysticks) {
      gluePacketNeeded = 3;
    } else if (byte >= kHeaderJoystick0) {
      gluePacketNeeded = 2;
    } else {
      glueKey(byte);
      return;
    }
    gluePacketLength = 0;
  }
  gluePacket[gluePacketLength++] = byte;
  if (gluePacketLength < gluePacketNeeded) {
    return;
  }
  gluePacketNeeded = 0;
  if (gluePacket[0] >= kHeaderRelMouseLow &&
      gluePacket[0] <= kHeaderRelMouseHigh) {
    glueLeftDown = (gluePacket[0] & kLeftButton) != 0;
    glueButtons();
    glueRight((gluePacket[0] & kRightButton) != 0);
    glueMouseMove((signed char)gluePacket[1], (signed char)gluePacket[2]);
  }
}

/* The ACIA interrupt (macglue.s glueAciaIsr): every byte waiting. */
void glueIkbdIsr(void) {
  for (;;) {
    const unsigned char status = ACIA_CONTROL;
    if (!(status & kAciaRxFull)) {
      break;
    }
    if (status & kAciaOverrun) {
      glueIkbdResync = 1; /* a byte was lost before this one */
    }
    glueIkbdByte(ACIA_DATA);
  }
}

static void glueIkbdSend(unsigned char byte) {
  for (unsigned spins = kAciaTxSpins;
       spins != 0 && !(ACIA_CONTROL & kAciaTxEmpty); --spins) {
  }
  ACIA_DATA = byte;
}

void glueIkbdInit(void) {
  ACIA_CONTROL = kAciaReset;
  ACIA_CONTROL = kAciaConfig;
  (void)ACIA_DATA;
  MFP_DDR &= (unsigned char)~kMfpGpip4; /* input */
  MFP_AER &= (unsigned char)~kMfpGpip4; /* falling edge */
  MFP_IMRB |= kMfpIerbGpip4;
  MFP_IERB |= kMfpIerbGpip4;
  glueKeyHead = glueKeyTail = 0;
  glueLeftShift = glueRightShift = glueAlt = 0;
  glueCapsLocked = 0;
  glueMacShift = glueMacOption = 0;
  glueHeldScan = 0;
  glueMouseRestH = glueMouseRestV = 0;
  gluePacketNeeded = 0;
  glueKbdWaiting = 0;
  glueLeftDown = glueRightDown = glueRightHeld = 0;
  glueRightCommandPending = glueDoublePhase = 0;
  glueSetButton(0);
  glueIkbdSyncTicks = kIkbdSyncTicks;
  glueIkbdSend(kIkbdReset);
  glueIkbdSend(kIkbdResetArg);
  TRACE("mac ikbd");
}

/* Each tick, in level-1 context (macglue.s glueLevel1): the keyboard's half
   of the shift register. */
void glueKbdTick(void) {
  int reply = -1;
  glueRightTick();
  if (glueIkbdSyncTicks != 0 && --glueIkbdSyncTicks == 0) {
    TRACE("ikbd no reset reply");
  }
#if defined(_TEST) && (_TEST > 0)
  if (glueTestInput.seq != glueTestSeen) {
    glueTestSeen = glueTestInput.seq;
    if (glueTestInput.buttons & kTestRelative) {
      glueMouseMove(glueTestInput.h, glueTestInput.v);
    } else {
      LOWMEM_WORD(kMTempV) = LOWMEM_WORD(kRawMouseV) = LOWMEM_WORD(kMouseV) =
          glueTestInput.v;
      LOWMEM_WORD(kMTempH) = LOWMEM_WORD(kRawMouseH) = LOWMEM_WORD(kMouseH) =
          glueTestInput.h;
      LOWMEM_BYTE(kCrsrNew) = LOWMEM_BYTE(kCrsrCouple);
    }
    glueLeftDown = glueTestInput.buttons & 1;
    glueButtons();
    glueRight((glueTestInput.buttons & 2) != 0); /* bit 1: the right one */
    TRACE("input %u at %d,%d buttons %u", (unsigned)glueTestSeen,
          (int)glueTestInput.v, (int)glueTestInput.h,
          (unsigned)glueTestInput.buttons);
    if (glueTestInput.buttons & kTestRelative) {
      TRACE("input moved to %d,%d", (int)LOWMEM_WORD(kMTempV),
            (int)LOWMEM_WORD(kMTempH));
    }
  }
#endif
  if (!glueKbdWaiting && (glueViaShadow[kViaAcr] & kAcrShiftOut)) {
    /* The ROM shifted a command out: its "shift done" interrupt switches
       the register to input. */
    glueKbdCommand = glueViaShadow[kViaSr];
    glueCallLvl1(kLvl1Keyboard);
    glueKbdWaiting = 1;
    glueKbdNullTicks = 0;
  }
  if (!glueKbdWaiting) {
    return;
  }
  switch (glueKbdCommand) {
    case kCmdModel:
      reply = kModelPlus;
      break;
    case kCmdInstant:
      reply = glueKeyPop();
      if (reply < 0) {
        reply = kReplyNull;
      }
      break;
    case kCmdInquiry:
      reply = glueKeyPop();
      if (reply < 0 && ++glueKbdNullTicks >= kNullAfterTicks) {
        reply = kReplyNull;
      }
      break;
    default:
      reply = kReplyNull;
      break;
  }
  if (reply < 0) {
    return;
  }
  if (reply != kReplyNull) {
    TRACE("key %02x", (unsigned)reply);
  }
  glueKbdWaiting = 0;
  glueViaShadow[kViaSr] = (unsigned char)reply;
  glueCallLvl1(kLvl1Keyboard);
}
