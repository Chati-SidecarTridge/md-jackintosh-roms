/**
 * File: src/glue/kbd.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The keyboard before the Mac starts (EPIC-06's menu), polled
 * through the ACIA with interrupts masked: romemul-switcher-rom's
 * src/st/kbd.c, which drops the IKBD's mouse, clock and joystick packets,
 * plus the Shift and Control state its menu keys need. Keys are the US
 * keyboard's scancodes: with another layout chosen (EPIC-12), the key
 * showing a character reads as the US key for it. The IKBD is reset first,
 * as TOS does: after an ST reset it keeps sending mouse packets, and one
 * caught mid-way reads as key codes ($2A or $36 is a Shift held down, which
 * turned the menu's A and I into ejects on the bench, 2026-10-19).
 * glueIkbdInit takes the ACIA back for the Mac.
 */

#include "kbd.h"

#include "hw.h"
#include "layout.h"
#include "menu.h"

enum {
  kAciaMasterReset = 0x03,
  kAciaPolling = 0x16, /* divide by 64, 8 bits, 1 stop, no interrupt */
  kAciaRxFull = 0x01,
  kAciaTxEmpty = 0x02,
  /* Framing and parity: the byte is bad. An overrun ($20) says an earlier
     byte was lost (a key's release while the menu redraws); this one is
     good, and dropping it lost the next key in Hatari. */
  kAciaErrors = 0x50,
  kAciaOverrun = 0x20,
  kAciaTxSpins = 0x4000,
  kIkbdMouseOff = 0x12,
  kIkbdJoysticksOff = 0x1A,
  kLastKey = 0x72,       /* the keypad's Enter; above it no key's make code */
  kSettleSpins = 0x4000, /* a few ms for the IKBD to act on its commands */
  kIkbdReset = 0x80,
  kIkbdResetArg = 0x01,
  kIkbdVersionLow = 0xF0, /* the reset's reply */
  kIkbdVersionHigh = 0xF1,
  kResetReplySpins = 0x20000, /* about a second; the reply takes ~60 ms */
  kKeyBreak = 0x80,
  kHeaderRelMouseLow = 0xF8,
  kHeaderRelMouseHigh = 0xFB,
  kHeaderAbsMouse = 0xF7,
  kHeaderTime = 0xFC,
  kHeaderJoystickA = 0xFE,
  kHeaderJoystickB = 0xFF,
  kHeaderStatus = 0xF6,
  kHeaderJoysticks = 0xFD,
  kDropRelMouse = 2,
  kDropAbsMouse = 4,
  kDropTime = 6,
  kDropJoystick = 1,
  kScanLeftShift = 0x2A,
  kScanRightShift = 0x36,
  kScanControl = 0x1D
};

#define ACIA_CONTROL HW_REG8(0x00FFFC00UL)
#define ACIA_DATA HW_REG8(0x00FFFC02UL)

static unsigned kbdDrop;
static int kbdLeftShift;
static int kbdRightShift;
static int kbdControl;
static int kbdOverrun; /* a byte was lost: maybe a Shift or Control release */

static void kbdSend(unsigned char byte) {
  for (unsigned spins = kAciaTxSpins;
       spins != 0 && !(ACIA_CONTROL & kAciaTxEmpty); --spins) {
  }
  ACIA_DATA = byte;
}

void kbdInit(void) {
  ACIA_CONTROL = kAciaMasterReset;
  ACIA_CONTROL = kAciaPolling;
  kbdSend(kIkbdReset);
  kbdSend(kIkbdResetArg);
  /* Everything before the reply is a stream already under way. */
  for (unsigned long spins = kResetReplySpins; spins != 0; --spins) {
    if (ACIA_CONTROL & kAciaRxFull) {
      const unsigned char data = ACIA_DATA;
      if (data == kIkbdVersionLow || data == kIkbdVersionHigh) {
        break;
      }
    }
  }
  /* Keys only: mouse and joystick reports, which the reset turned back on,
     off. Then whatever came before they took effect is dropped. */
  kbdSend(kIkbdMouseOff);
  kbdSend(kIkbdJoysticksOff);
  for (unsigned spins = kSettleSpins; spins != 0; --spins) {
    if (ACIA_CONTROL & kAciaRxFull) {
      (void)ACIA_DATA;
    }
  }
  kbdDrop = 0;
  kbdOverrun = 0;
  kbdLeftShift = kbdRightShift = kbdControl = 0;
}

/* The next keyboard byte (a make or break code), or -1. */
static int kbdByte(void) {
  for (;;) {
    const unsigned char status = ACIA_CONTROL;
    unsigned char data;
    if (!(status & kAciaRxFull)) {
      return -1;
    }
    data = ACIA_DATA;
    if (status & kAciaOverrun) {
      kbdOverrun = 1;
    }
    if (status & kAciaErrors) {
      continue;
    }
    if (kbdDrop != 0) {
      --kbdDrop;
      continue;
    }
    if (data >= kHeaderRelMouseLow && data <= kHeaderRelMouseHigh) {
      kbdDrop = kDropRelMouse;
    } else if (data == kHeaderAbsMouse) {
      kbdDrop = kDropAbsMouse;
    } else if (data == kHeaderTime) {
      kbdDrop = kDropTime;
    } else if (data == kHeaderJoystickA || data == kHeaderJoystickB) {
      kbdDrop = kDropJoystick;
    } else if (data == kHeaderStatus || data == kHeaderJoysticks) {
      continue;
    } else {
      return data;
    }
  }
}

int kbdPoll(void) {
  for (;;) {
    const int byte = kbdByte();
    int code;
    int down;
    if (byte < 0) {
      return -1;
    }
    if (kbdOverrun) {
      /* While the menu acted on a key (a SET writes flash for ~45 ms) the
         rest of the chord came in, and the last byte, a Shift's release,
         was lost: Shift stuck (bench, 2026-10-19). A modifier still held
         comes back with its next key. */
      kbdOverrun = 0;
      kbdLeftShift = kbdRightShift = kbdControl = 0;
    }
    code = byte & ~kKeyBreak;
    down = (byte & kKeyBreak) == 0;
    if (code == kScanLeftShift) {
      kbdLeftShift = down;
    } else if (code == kScanRightShift) {
      kbdRightShift = down;
    } else if (code == kScanControl) {
      kbdControl = down;
    } else if (down && code > kLastKey) {
      /* No key sends it: a packet's middle. Its Shift or Control "press"
         may have been one too. */
      kbdLeftShift = kbdRightShift = kbdControl = 0;
    } else if (down) {
      const int held = kbdLeftShift || kbdRightShift;
      const int physical = ((long)code << 16) | (held ? KBD_PHYS_SHIFT : 0);
      int shift = held;
      code =
          (int)glueLayoutMenuKey(glueOptions.keyboard, (unsigned)code, &shift);
      return physical | code | (shift ? KBD_SHIFT : 0) |
             (kbdControl ? KBD_CONTROL : 0);
    }
  }
}
