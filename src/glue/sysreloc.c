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
 * is left alone (D-15). A System whose PTCH for this ROM (PTCH 117) is not
 * in the table would crash at its first $40xxxx address; the glue stops
 * with a message instead (EPIC-08 STORY-05).
 */

#include "sysreloc.h"

#include "macrom.h"
#include "text.h"
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
  kByteBits = 8,
  /* The resource map (plus/toolbox/resource_mgr.s): the type list's offset,
     then per type its code, its count - 1 and its reference list's offset
     from the type list; 12-byte references, the ID first. */
  kMapTypeList = 24,
  kTypeEntryBytes = 8,
  kTypeCount = 4,
  kTypeRefs = 6,
  kRefEntryBytes = 12,
  kTypePtch = 0x50544348UL, /* 'PTCH' */
  kRomVersion = 8           /* the ROM's version word, PTCH's ID */
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

/* The type of the reference at ref in the resource map at map; 0 if it is
   not in the map's reference lists. */
static unsigned long glueRefType(const unsigned char *map,
                                 const unsigned char *ref) {
  const unsigned char *types =
      map + *(const unsigned short *)(map + kMapTypeList);
  const unsigned short count = *(const unsigned short *)types + 1;
  for (unsigned short i = 0; i < count; ++i) {
    const unsigned char *entry = types + 2 + (i * kTypeEntryBytes);
    const unsigned char *refs =
        types + *(const unsigned short *)(entry + kTypeRefs);
    const unsigned long refsBytes =
        ((unsigned long)*(const unsigned short *)(entry + kTypeCount) + 1) *
        kRefEntryBytes;
    if (ref >= refs && ref < refs + refsBytes) {
      return *(const unsigned long *)entry;
    }
  }
  return 0;
}

/* The System's PTCH for this ROM is not one the table knows: say so on the
   Mac's screen and stop, rather than crash at its first ROM address. */
static void glueSysUnknown(unsigned long length, unsigned long sum) {
  TRACE("halt system ptch %lu bytes sum %08lx", length, sum);
  textClear();
  textAt(0, 0);
  textPrintf("This System is not supported by this Jackintosh ROM.\n\n");
  textPrintf(
      "Its PTCH %u (%lu bytes, word sum $%08lX) holds absolute Mac ROM\n",
      (unsigned)*(const unsigned short *)(MAC_ROM_BASE_ADDR + kRomVersion),
      length, sum);
  textPrintf("addresses, and the ROM has no relocation table for it.\n");
  for (;;) {
    __asm__ volatile("stop #0x2700");
  }
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
  data = (unsigned char *)(*(const unsigned long *)handle & kAddressMask);
  for (const struct SysRelocEntry *entry = sysRelocTable; entry->length != 0;
       ++entry) {
    int moved = 0;
    if (entry->length != length) {
      continue;
    }
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
  if (glueRefType((const unsigned char *)*mapHandle, ref) == kTypePtch &&
      *(const unsigned short *)ref ==
          *(const unsigned short *)(MAC_ROM_BASE_ADDR + kRomVersion)) {
    glueSysUnknown(length, glueWordSum((const unsigned short *)data, length));
  }
}
