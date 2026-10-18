/**
 * File: src/glue/mac.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's C side of the Mac (EPIC-02): entering the ROM at
 * E_SoftReset (D-16), the hook InitIOMgr calls (vectors, MFP, Timer C: D-17,
 * D-18), the RTC and PRAM, and the boot icons on the 640x400 screen. The
 * routines the ROM patches call come through macglue.s.
 */

#include "mac.h"

#include "hw.h"
#include "macrom.h"
#include "mem.h"
#include "sysreloc.h"
#include "text.h"
#include "trace.h"

/* macglue.s */
extern unsigned char glueViaShadow[];
extern void glueCallSysErrInit(void);
extern void glueJumpSoftReset(void);
extern void glueRte(void);
extern void glueHbl(void);
extern void glueVbl(void);
extern void glueTimerC(void);
extern void glueAciaIsr(void);
extern void glueBusError(void);
extern unsigned long glueBusPrevious;
extern unsigned long glueBusFixes;
#if defined(_TEST) && (_TEST > 0)
extern long glueBusSelfTest(void);
#endif
extern void glueIkbdInit(void);
#if defined(_DEBUG) && (_DEBUG > 0)
extern void glueCrashBus(void);
extern void glueCrashAddress(void);
extern void glueCrashIllegal(void);
extern void glueCrashOther(void);
#endif

/* Shared with macglue.s's interrupt code. */
volatile unsigned short glueTicksPending;
volatile unsigned short glueSecondsPending;
volatile unsigned long glueSecondAcc;
volatile unsigned char glueInLevel1;
volatile unsigned long glueVblCount;

enum {
  kVectorSpurious = 0x60,
  kVectorHbl = 0x68,
  kVectorVbl = 0x70,
  kMfpVectorBase = 0x30, /* vectors 48-63 at $C0-$FC (D-17) */
  kMfpVectorCount = 16,
  kMfpTimerC = 5,
  kMfpAcia = 6, /* GPIP 4 */
  kMfpIerbTimerC = 0x20,
  kTimerCPrescale200 = 0x70, /* TCDCR bits 6-4: divide by 200 */
  kTimerCData = 204,         /* 2457600 / 200 / 204 = 60.235 Hz */
  kTimerDBits = 0x0F,
  kViaPortBButtonUp = 0x08, /* port B bit 3: the mouse button, 1 = up */
  kViaPortBSoundOff = 0x80, /* port B bit 7: sound disabled */
  kLongBytes = 4,
  kCrashCodeBefore = 40,
  kCrashCodeWords = 32,
  kCrashCodePerLine = 8,
  kShortFrameWords = 3,
  kCrashStackWords = 48,
  kPwmSilence = 0xFFFF
};

#define MFP_REG(offset) HW_REG8(0x00FFFA01UL + (offset))
#define MFP_IERA MFP_REG(0x06)
#define MFP_IERB MFP_REG(0x08)
#define MFP_IMRA MFP_REG(0x12)
#define MFP_IMRB MFP_REG(0x14)
#define MFP_VR MFP_REG(0x16)
#define MFP_TCDCR MFP_REG(0x1C)
#define MFP_TCDR MFP_REG(0x22)

#define LOWMEM_LONG(addr) (*(volatile unsigned long *)(unsigned long)(addr))
#define LOWMEM_WORD(addr) (*(volatile unsigned short *)(unsigned long)(addr))
#define VECTOR(addr) LOWMEM_LONG(addr)

/* ------------------------------------------------------------------------
 * Entering the ROM
 */

void glueMacBoot(unsigned long memTop, unsigned long screen) {
  /* The VIA shadow's port B: mouse button up, sound off. */
  glueViaShadow[0] = kViaPortBButtonUp | kViaPortBSoundOff;

  /* SysErrInit installs exception vectors 2-63 (the patch stops it at 47:
     the MFP's), clears SERegs (patched into the glue) and sets MacJmp. */
  glueCallSysErrInit();
#if defined(_DEBUG) && (_DEBUG > 0)
  /* Debug builds trace faults instead of bombing (glueCrash). */
  VECTOR(0x08) = (unsigned long)glueCrashBus;
  VECTOR(0x0C) = (unsigned long)glueCrashAddress;
  VECTOR(0x10) = (unsigned long)glueCrashIllegal;
  VECTOR(0x14) = (unsigned long)glueCrashOther; /* divide by zero */
  VECTOR(0x18) = (unsigned long)glueCrashOther; /* CHK */
  VECTOR(0x1C) = (unsigned long)glueCrashOther; /* TRAPV */
  VECTOR(0x20) = (unsigned long)glueCrashOther; /* privilege */
  VECTOR(0x2C) = (unsigned long)glueCrashOther; /* line F */
#endif

  /* Writes to $0-$7 skipped, in every build (buserr.c); other bus errors
     go on to the handler installed above. */
  glueBusPrevious = VECTOR(0x08);
  VECTOR(0x08) = (unsigned long)glueBusError;
#if defined(_TEST) && (_TEST > 0)
  {
    const long balanced = glueBusSelfTest();
    TRACE("buserr selftest %s fixes %lu", balanced ? "ok" : "unbalanced",
          glueBusFixes);
  }
#endif

  /* INIT_MEMORY_MAP's globals (plus/boot/vectors.s:644-655), with the
     screen moved for 640x400 (D-19). */
  LOWMEM_LONG(MAC_MEMTOP) = memTop;
  LOWMEM_LONG(MAC_PWMBUF2) = memTop - MAC_PWMBUF_BELOW_MEMTOP;
  LOWMEM_LONG(MAC_PWMBUF1) = memTop - MAC_PWMBUF_BELOW_MEMTOP;
  LOWMEM_WORD(MAC_PWMVALUE) = kPwmSilence;
  LOWMEM_LONG(MAC_SOUNDBASE) = memTop - MAC_SOUNDBASE_BELOW_MEMTOP;
  LOWMEM_LONG(MAC_BUFPTR) = screen;

  TRACE("mac enter %06lx memtop %06lx screen %06lx",
        (unsigned long)MACROM_ADDR(MACROM_E_SOFTRESET), memTop, screen);
  glueJumpSoftReset();
}

/* ------------------------------------------------------------------------
 * The hook InitIOMgr calls (D-17, D-18), on every boot and warm restart:
 * the SCSI driver count cleared as EXTRA_CONT would, the glue's vectors,
 * and Timer C started. Interrupts stay masked until ENABLE_INTERRUPTS.
 */

void glueHook(void) {
  LOWMEM_WORD(MAC_SCSIDRVRS) = 0;

  MFP_IERA = 0;
  MFP_IERB = 0;
  MFP_VR = kMfpVectorBase; /* automatic end of interrupt */
  for (int i = 0; i < kMfpVectorCount; ++i) {
    VECTOR((kMfpVectorBase + i) * kLongBytes) = (unsigned long)glueRte;
  }
  VECTOR((kMfpVectorBase + kMfpTimerC) * kLongBytes) =
      (unsigned long)glueTimerC;
  VECTOR((kMfpVectorBase + kMfpAcia) * kLongBytes) = (unsigned long)glueAciaIsr;
  VECTOR(kVectorSpurious) = (unsigned long)glueRte;
  VECTOR(kVectorHbl) = (unsigned long)glueHbl;
  VECTOR(kVectorVbl) = (unsigned long)glueVbl;

  glueTicksPending = 0;
  glueSecondsPending = 0;
  glueSecondAcc = 0;
  glueInLevel1 = 0;
  glueVblCount = 0;
  MFP_TCDCR = MFP_TCDCR & kTimerDBits;
  MFP_TCDR = kTimerCData;
  MFP_TCDCR = (MFP_TCDCR & kTimerDBits) | kTimerCPrescale200;
  MFP_IMRB = kMfpIerbTimerC;
  MFP_IERB = kMfpIerbTimerC;
  glueSysRelocInstall();
  glueIkbdInit();
  TRACE("mac hook");
}

/* ------------------------------------------------------------------------
 * The RTC and its 20 bytes of PRAM (plus/boot/vectors.s:367-443, rtc.s).
 * Commands: bit 7 set to read; z0aa0001 / z001aa01 the seconds bytes (the
 * two counters), z010aa01 PRAM $10-$13, z1aaaa01 PRAM $00-$0F, $31 the test
 * register, $35 the write-protect register. The Plus's HWCfgFlags stay 0,
 * so the extended (XPRAM) commands never come.
 */

enum {
  kRtcRead = 0x80,
  kRtcSelectMask = 0x43, /* bit 6 and the 01 tail */
  kRtcSeconds = 0x01,    /* z0xxaa01 */
  kRtcRegisterGroup = 0x30,
  kRtcPramLow = 0x20,
  kRtcPramLowMask = 0x63,
  kRtcPramLowCmd = 0x21, /* z010aa01: PRAM $10-$13 */
  kRtcPramCmd = 0x41,    /* z1aaaa01: PRAM $00-$0F */
  kRtcCmdMask = 0x7F,
  kRtcPramBytes = 20,
  kRtcPramHigh = 0x10,
  kRtcWriteProtect = 0x35,
  kRtcTest = 0x31,
  kRtcProtectBit = 0x80,
  kByteMask = 0xFF,
  kByteBits = 8,
  kTwoBits = 3,
  kFourBits = 0xF
};

static unsigned char glueRtcPram[kRtcPramBytes];
static unsigned char glueRtcProtect;
static unsigned long glueRtcSeconds = MAC_CLOCK_START;

unsigned long glueRtc(unsigned long command, unsigned long data) {
  const unsigned cmd = (unsigned)(command & kRtcCmdMask);
  const int read = (command & kRtcRead) != 0;
  unsigned char *cell = 0;
  unsigned char value = (unsigned char)(data & kByteMask);

  if ((cmd & kRtcSelectMask) == kRtcSeconds &&
      (cmd & kRtcRegisterGroup) != kRtcRegisterGroup &&
      (cmd & kRtcPramLow) == 0) {
    /* Seconds: z00xaa01, byte aa, least significant first. */
    const unsigned shift = ((cmd >> 2) & kTwoBits) * kByteBits;
    if (read) {
      return (glueRtcSeconds >> shift) & kByteMask;
    }
    if (!(glueRtcProtect & kRtcProtectBit)) {
      glueRtcSeconds = (glueRtcSeconds & ~((unsigned long)kByteMask << shift)) |
                       ((unsigned long)value << shift);
    }
    return 0;
  }
  if (cmd == kRtcWriteProtect) {
    glueRtcProtect = value;
    return 0;
  }
  if (cmd == kRtcTest) {
    return 0;
  }
  if ((cmd & kRtcPramLowMask) == kRtcPramLowCmd) {
    cell = &glueRtcPram[kRtcPramHigh + ((cmd >> 2) & kTwoBits)];
  } else if ((cmd & kRtcSelectMask) == kRtcPramCmd) {
    cell = &glueRtcPram[(cmd >> 2) & kFourBits];
  }
  if (cell == 0) {
    TRACE("mac rtc unknown %02lx", command);
    return 0;
  }
  if (read) {
    return *cell;
  }
  if (!(glueRtcProtect & kRtcProtectBit)) {
    *cell = value;
  }
  return 0;
}

/* ------------------------------------------------------------------------
 * The boot icons, decoded from the ROM at run time as DeviceLoop and
 * PutSymbol do (plus/boot/sad_mac.s:28-122), with 80-byte rows and centred
 * on 640x400: the icon at row 174, byte 38 (the Plus draws it at row 145,
 * byte 30 of 512x342).
 */

enum {
  kScreenRowBytes = 80,
  kIconRow = 174,
  kIconByte = 38,
  kIconSourceRows = 16,
  kDiskSymbolRow = 15,
  kFaceRow = 6,
  kSymbolByte = 1,
  kSymbolBytes = 2,
  kFlagSentinel = 0x100,
  kTickProbeMask = 511
};

static unsigned char *glueIconAt(int row, int byte) {
  return (unsigned char *)LOWMEM_LONG(MAC_SCRNBASE) +
         ((unsigned long)row * kScreenRowBytes) + byte;
}

/* DeviceLoop: 16 flag bytes; each bit, least significant first, gives one
   screen byte: 0 copies the next data byte, 1 writes a zero (the bit is the
   one LSR shifts out into the carry). 4 bytes a row; then each of the 31
   rows below is XORed with the one above. */
static void glueDrawIcon(const unsigned char *src) {
  unsigned char *row = glueIconAt(kIconRow, kIconByte);
  unsigned char *dst = row;
  int count = 0;
  for (int i = 0; i < kIconSourceRows; ++i) {
    unsigned flags = kFlagSentinel | *src++;
    for (;;) {
      const unsigned bit = flags & 1;
      flags >>= 1;
      if (flags == 0) {
        break;
      }
      *dst++ = bit ? 0 : *src++;
      if (++count == MAC_ICON_BYTES) {
        count = 0;
        dst += kScreenRowBytes - MAC_ICON_BYTES;
      }
    }
  }
  for (int i = 1; i < MAC_ICON_ROWS; ++i) {
    unsigned char *next = row + kScreenRowBytes;
    for (int col = 0; col < MAC_ICON_BYTES; ++col) {
      next[col] ^= row[col];
    }
    row = next;
  }
}

/* PutSymbol: rows of 2 bytes copied into the icon. */
static void glueDrawSymbol(const unsigned char *src, int rowOffset, int rows) {
  unsigned char *dst =
      glueIconAt(kIconRow + rowOffset, kIconByte + kSymbolByte);
  for (int i = 0; i < rows; ++i) {
    dst[0] = *src++;
    dst[1] = *src++;
    dst += kScreenRowBytes;
  }
}

#if defined(_DEBUG) && (_DEBUG > 0)
/* Every 512 Mac ticks, Ticks against the Atari VBLs counted since the hook
   (glueVbl): the harness turns the pair into the tick rate. */
void glueTickProbe(void) {
  const unsigned long ticks = LOWMEM_LONG(MAC_TICKS);
  if ((ticks & kTickProbeMask) == 0) {
    TRACE("ticks %lu vbl %lu", ticks, glueVblCount);
  }
}
#endif

void gluePlotDiskC(const unsigned char *symbol) {
  glueDrawIcon((const unsigned char *)MACROM_ADDR(MACROM_ICON_DISK));
  if (symbol != 0) {
    glueDrawSymbol(symbol, kDiskSymbolRow, MAC_DISK_SYMBOL_ROWS);
  }
  TRACE("mac plotdisk %06lx vbl %lu", (unsigned long)symbol, glueVblCount);
}

void glueShowHappyMacC(void) {
  glueDrawIcon((const unsigned char *)MACROM_ADDR(MACROM_ICON_MAC_OUTLINE));
  glueDrawSymbol((const unsigned char *)MACROM_ADDR(MACROM_DT_MHAPPYMAC),
                 kFaceRow, MAC_FACE_ROWS);
  TRACE("mac happy");
}

#if defined(_DEBUG) && (_DEBUG > 0)
/* ------------------------------------------------------------------------
 * Debug builds: a fault the Mac would turn into a bomb. frame points at the
 * exception frame; a bus or address error has the function code, the
 * access address and the instruction register in front of SR and PC.
 */
void glueCrash(long vector, const unsigned short *frame,
               const unsigned long *regs) {
  unsigned long access = 0;
  unsigned short sr;
  unsigned long pc;
  const unsigned short *code;
  if (vector == 2 || vector == 3) {
    access = ((unsigned long)frame[1] << 16) | frame[2];
    frame += 4;
  }
  sr = frame[0];
  pc = ((unsigned long)frame[1] << 16) | frame[2];
  TRACE("crash vector %ld pc %06lx sr %04x access %06lx", vector, pc,
        (unsigned)sr, access);
  TRACE("crash d0-d3 %08lx %08lx %08lx %08lx", regs[0], regs[1], regs[2],
        regs[3]);
  TRACE("crash d4-d7 %08lx %08lx %08lx %08lx", regs[4], regs[5], regs[6],
        regs[7]);
  TRACE("crash a0-a3 %08lx %08lx %08lx %08lx", regs[8], regs[9], regs[10],
        regs[11]);
  TRACE("crash a4-a7 %08lx %08lx %08lx %08lx", regs[12], regs[13], regs[14],
        regs[15]);
  /* The words around the PC; a bus error's PC is a few bytes past the
     faulting instruction. */
  code = (const unsigned short *)((pc & ~1UL) - kCrashCodeBefore);
  for (int i = 0; i < kCrashCodeWords; i += kCrashCodePerLine) {
    TRACE("crash code %06lx %04x %04x %04x %04x %04x %04x %04x %04x",
          (unsigned long)(code + i), code[i], code[i + 1], code[i + 2],
          code[i + 3], code[i + 4], code[i + 5], code[i + 6], code[i + 7]);
  }
  {
    /* The stack the faulting code was using: right after the frame. */
    const unsigned short *after = frame + kShortFrameWords;
    for (int i = 0; i < kCrashStackWords; i += kCrashCodePerLine) {
      TRACE("crash stack %06lx %04x%04x %04x%04x %04x%04x %04x%04x",
            (unsigned long)(after + i), after[i], after[i + 1], after[i + 2],
            after[i + 3], after[i + 4], after[i + 5], after[i + 6],
            after[i + 7]);
    }
  }
  textAt(0, TEXT_ROWS - 1);
  textPrintf("CRASH vector %ld pc $%06lX sr $%04X access $%06lX", vector, pc,
             (unsigned)sr, access);
  for (;;) {
    __asm__ volatile("stop #0x2700");
  }
}
#endif
