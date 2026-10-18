/**
 * File: src/glue/main.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: C entry of the glue, called by startup.s with interrupts
 * masked, the RAM sized and filled, .data copied and .bss cleared. It shows
 * the diagnostic screen (EPIC-00 STORY-04), then enters the Mac ROM (EPIC-02).
 */

#include "mac.h"
#include "mem.h"
#include "screen.h"
#include "text.h"
#include "trace.h"

/* Set by startup.s from memconf.inc. */
unsigned long glueRamBytes;
unsigned char glueRamMmu;

enum {
  kKilobyte = 1024,
  kMmuBank0Shift = 2,
  kMmuBankMask = 0x3,
  kMacRomVersionOffset = 8,
  kBytesPerWord = 2,
  kBytesPerLong = 4,
  kValueColumn = 10,
  kImageRow = 6
};

/* The MMU's bank size code (memconf.inc): 0 128 KB, 1 512 KB, 2 2 MB. */
static unsigned long glueBankBytes(unsigned char code) {
  static const unsigned long kBankBytes[] = {
      128UL * kKilobyte, 512UL * kKilobyte, 2048UL * kKilobyte, 0};
  return kBankBytes[code & kMmuBankMask];
}

/* The 16-bit big-endian words from first to last (exclusive), summed into
   32 bits; the long at skip, if inside, is left out. */
static unsigned long glueSumWords(unsigned long first, unsigned long last,
                                  unsigned long skip) {
  unsigned long sum = 0;
  for (unsigned long addr = first; addr < last; addr += kBytesPerWord) {
    if (addr == skip) {
      addr += kBytesPerLong - kBytesPerWord;
      continue;
    }
    sum += *(const volatile unsigned short *)addr;
  }
  return sum;
}

#if defined(_DEBUG) && (_DEBUG > 0)
/* The harness's preloaded blob, if any (mem.h): its length and the 32-bit
   sum of its bytes, traced for the harness to compare. */
static void gluePreloadProbe(void) {
  const unsigned long at = glueRamBytes - GLUE_PRELOAD_BELOW_TOP_BYTES;
  const volatile unsigned long *head = (const volatile unsigned long *)at;
  const volatile unsigned char *data;
  unsigned long sum = 0;
  if (head[0] != GLUE_PRELOAD_MAGIC) {
    TRACE("preload none");
    return;
  }
  data = (const volatile unsigned char *)(at + 2 * kBytesPerLong);
  for (unsigned long i = 0; i < head[1]; ++i) {
    sum += data[i];
  }
  TRACE("preload %lu %08lx", head[1], sum);
}
#endif

void glueMain(void) {
  /* The Mac gets all the RAM; its screen sits below the sound buffer
     (D-19). The diagnostic screen uses it until the Mac draws there. */
  const unsigned long memTop = glueRamBytes;
  const unsigned long screen = memTop - MAC_SCREEN_BELOW_MEMTOP;
  const unsigned long field = ROM_BASE_ADDR_UL + GLUE_CHECKSUM_OFFSET_BYTES;
  const unsigned long imageStored = *(const volatile unsigned long *)field;
  unsigned long imageComputed;
  const unsigned long macStored =
      *(const volatile unsigned long *)MAC_ROM_BASE_ADDR;
  unsigned long macComputed;
  const unsigned long macReset =
      *(const volatile unsigned long *)(MAC_ROM_BASE_ADDR + kBytesPerLong);
  const unsigned short macVersion =
      *(const volatile unsigned short *)(MAC_ROM_BASE_ADDR +
                                         kMacRomVersionOffset);
  const unsigned long bank0 = glueBankBytes(glueRamMmu >> kMmuBank0Shift);
  const char *machine = MACHINE_NAME;
  int mono;

  TRACE_INIT();
  TRACE("boot %s %s %s", machine, APP_VERSION_STR, BUILD_ID_STR);
  TRACE("ram %lu mmu %02x", glueRamBytes, (unsigned)glueRamMmu);
#if defined(_DEBUG) && (_DEBUG > 0)
  gluePreloadProbe();
#endif

  mono = screenInit(screen);
  textInit((volatile unsigned char *)screen, mono);
  textClear();
  if (!mono) {
    static const char kNeedMono[] =
        "Jackintosh needs a monochrome monitor. Connect one and reset.";
    textAt((TEXT_COLUMNS - (sizeof(kNeedMono) - 1)) / 2, TEXT_ROWS / 2);
    textPuts(kNeedMono);
    TRACE("idle colour");
    for (;;) {
      __asm__ volatile("stop #0x2700");
    }
  }

  textAt(0, 0);
  textPrintf("Jackintosh %s  (build %s)\n", APP_VERSION_STR, BUILD_ID_STR);
  textPrintf("%s image, %lu KB at $%06lX\n\n", machine,
             (unsigned long)IMAGE_SIZE_BYTES / kKilobyte, ROM_BASE_ADDR_UL);
  textPrintf("Machine   Atari %s (%s Shifter)\n", machine,
             screenIsSte() ? "STE" : "ST");
  textPrintf("RAM       %lu KB: bank 0 %lu KB, bank 1 %lu KB (MMU $%02X)\n",
             glueRamBytes / kKilobyte, bank0 / kKilobyte,
             (glueRamBytes - bank0) / kKilobyte, (unsigned)glueRamMmu);
  textPrintf("Screen    640x400 mono at $%06lX\n", screen);
  textPuts("Image     checksum ...");

  imageComputed = glueSumWords(ROM_BASE_ADDR_UL,
                               ROM_BASE_ADDR_UL + IMAGE_SIZE_BYTES, field);
  TRACE("image %s %08lx %08lx", imageComputed == imageStored ? "ok" : "fail",
        imageStored, imageComputed);
  textAt(kValueColumn, kImageRow);
  if (imageComputed == imageStored) {
    textPrintf("checksum $%08lX ok\n", imageComputed);
  } else {
    textPrintf("checksum FAIL: stored $%08lX, computed $%08lX\n", imageStored,
               imageComputed);
  }

  macComputed = glueSumWords(MAC_ROM_BASE_ADDR + kBytesPerLong,
                             MAC_ROM_BASE_ADDR + MAC_ROM_SIZE_BYTES, 0);
  TRACE("macrom %08lx %08lx %04x reset %06lx", macStored, macComputed,
        (unsigned)macVersion, macReset);
  textPrintf("Mac ROM   at $%06lX, version $%04X, checksum $%08lX %s\n",
             MAC_ROM_BASE_ADDR, (unsigned)macVersion, macComputed,
             macComputed == macStored ? "ok" : "(patched)");
  textPrintf("          reset vector $%06lX\n", macReset);

  if (imageComputed != imageStored) {
    /* A damaged image must not run the Mac. */
    TRACE("halt image");
    for (;;) {
      __asm__ volatile("stop #0x2700");
    }
  }
  glueMacBoot(memTop, screen);
}
