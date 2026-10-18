/**
 * File: src/glue/sysreloc.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: System 6's patches for the Plus hold the ROM's routines as
 * absolute $400000+offset addresses (docs/macplus/system6-offsets.md); our
 * ROM sits at ROMBase. A few Mac hardware addresses come with them (the
 * diagnostic-ROM probe, SERegs, the SCC), which go to the glue's
 * equivalents. The Resource Manager reads every resource through
 * the jCheckLoad vector ($7F0, plus/toolbox/resource_mgr.s:598-600), which
 * the glue wraps (glueCheckLoad, macglue.s): after the ROM's CheckLoad, a
 * resource whose length and word sum match the table gets ROMBase - $400000
 * added to each listed long that still holds a ROM address (and the other
 * kinds moved likewise), so a resource loaded twice, or already relocated,
 * is left alone (D-15).
 */

#include "sysreloc.h"

#include "macrom.h"
#include "trace.h"

extern void glueCheckLoad(void);
extern unsigned long glueGetHandleSize(unsigned long handle);
extern unsigned char glueSERegs[];
extern unsigned char glueSccShadow[];

/* The ROM's CheckLoad, as the vector held it before the glue's wrapper. */
unsigned long glueOrigCheckLoad;

enum {
  kJCheckLoad = 0x07F0,
  kRefHandle = 8,              /* the handle in a reference-list entry */
  kAddressMask = 0x00FFFFFFUL, /* 24-bit master pointers carry flags above */
  kRomLow = 0x400000UL,
  kRomHigh = 0x420000UL,
  kDiagLow = 0xF80000UL,
  kDiagHigh = 0xF90000UL,
  kSERegsLow = 0x3FFC80UL,
  kSERegsHigh = 0x3FFD00UL,
  kSccReadLow = 0x9FFFF0UL,
  kSccReadHigh = 0xA00000UL,
  kSccWriteLow = 0xBFFFF0UL,
  kSccWriteHigh = 0xC00000UL,
  kSccOffsetMask = 7,
  kWordShift = 1,
  kByteBits = 8
};

void glueSysRelocInstall(void) {
  volatile unsigned long *vector = (volatile unsigned long *)kJCheckLoad;
  if (*vector != (unsigned long)glueCheckLoad) {
    glueOrigCheckLoad = *vector;
    *vector = (unsigned long)glueCheckLoad;
  }
  TRACE("mac checkload %06lx", glueOrigCheckLoad);
}

static unsigned long glueWordSum(const unsigned short *words,
                                 unsigned long length) {
  unsigned long sum = 0;
  for (unsigned long i = length >> kWordShift; i != 0; --i) {
    sum += *words++;
  }
  if (length & 1) {
    sum += (unsigned long)(*(const unsigned char *)words) << kByteBits;
  }
  return sum;
}

/* Called by glueCheckLoad after the ROM's CheckLoad, with the reference
   entry and the map's base as they were on entry and the map's handle: the
   map may have moved while the resource was read. */
void glueSysRelocCheck(unsigned long refAtEntry, unsigned long mapAtEntry,
                       const unsigned long *mapHandle) {
  const unsigned char *ref =
      (const unsigned char *)(*mapHandle + (refAtEntry - mapAtEntry));
  const unsigned long handle = *(const unsigned long *)(ref + kRefHandle);
  unsigned long length;
  unsigned char *data;

  if (handle == 0 || (*(const unsigned long *)handle & kAddressMask) == 0) {
    return; /* not loaded, or purged */
  }
  length = glueGetHandleSize(handle);
  for (const struct SysRelocEntry *entry = sysRelocTable; entry->length != 0;
       ++entry) {
    int moved = 0;
    if (entry->length != length) {
      continue;
    }
    data = (unsigned char *)(*(const unsigned long *)handle & kAddressMask);
    if (glueWordSum((const unsigned short *)data, length) != entry->sum) {
      continue;
    }
    for (unsigned i = 0; i < entry->count; ++i) {
      const struct SysRelocOperand *operandInfo = &entry->operands[i];
      unsigned long *operand = (unsigned long *)(data + operandInfo->offset);
      const unsigned long value = *operand;
      unsigned long movedTo = value;
      switch (operandInfo->kind) {
        case SYSRELOC_ROM:
          if (value >= kRomLow && value < kRomHigh) {
            movedTo = value - kRomLow + MAC_ROM_BASE_ADDR;
          }
          break;
        case SYSRELOC_DIAG:
          if (value >= kDiagLow && value < kDiagHigh) {
            movedTo = MAC_ROM_BASE_ADDR;
          }
          break;
        case SYSRELOC_SEREGS:
          if (value >= kSERegsLow && value < kSERegsHigh) {
            movedTo = (unsigned long)glueSERegs + (value - kSERegsLow);
          }
          break;
        case SYSRELOC_SCC:
          if ((value >= kSccReadLow && value < kSccReadHigh) ||
              (value >= kSccWriteLow && value < kSccWriteHigh)) {
            /* Read base $9FFFF8, write base $BFFFF9: the shadow interleaves
               them, so the low 3 bits are the offset into it. */
            movedTo = (unsigned long)glueSccShadow + (value & kSccOffsetMask);
          }
          break;
        default:
          break;
      }
      if (movedTo != value) {
        *operand = movedTo;
        ++moved;
      }
    }
    TRACE("reloc %lu bytes at %06lx %d of %u", length, (unsigned long)data,
          moved, (unsigned)entry->count);
    return;
  }
}
