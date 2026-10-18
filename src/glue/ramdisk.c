/**
 * File: src/glue/ramdisk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The RAM-disk backend of test builds (EPIC-03 STORY-02). The
 * Hatari harness loads a raw disk image below the top of RAM and a 16-byte
 * descriptor in the last 16 bytes: "JKRD", the image's base, its size in
 * bytes, flags (bit 0 a fixed disk, bit 1 write-protected). The glue puts
 * the Mac's MemTop at the image's base, so the Mac never touches it. A cold
 * restart refills the RAM with $FF and loses the image (test builds only).
 */

#include "disk.h"
#include "macrom.h"
#include "trace.h"

enum {
  kDescriptorBytes = 16,
  kMagic = 0x4A4B5244, /* "JKRD" */
  kFlagFixed = 1,
  kFlagWriteProtect = 2,
  kAlignMask = 0xFF,
  kLongMask = 3,
  kBlockShift = 9
};

static unsigned char *ramDiskBase;
static unsigned long ramDiskBlocks;
static unsigned long ramDiskFlags;
static int ramDiskPresent;

unsigned long diskRamFind(unsigned long ramTop) {
  const unsigned long *desc =
      (const unsigned long *)(ramTop - kDescriptorBytes);
  const unsigned long base = desc[1];
  const unsigned long bytes = desc[2];
  if (desc[0] != kMagic || (base & kAlignMask) != 0 || bytes == 0 ||
      (bytes & (DISK_BLOCK_BYTES - 1)) != 0 ||
      base + bytes > ramTop - kDescriptorBytes) {
    TRACE("disk ramdisk none");
    return ramTop;
  }
  ramDiskBase = (unsigned char *)base;
  ramDiskBlocks = bytes >> kBlockShift;
  ramDiskFlags = desc[3];
  ramDiskPresent = 1;
  TRACE("disk ramdisk %lu base %06lx flags %lx", bytes, base, ramDiskFlags);
  return base;
}

void diskBackendInfo(struct DiskInfo *info) {
  info->present = ramDiskPresent;
  info->fixed = (ramDiskFlags & kFlagFixed) != 0;
  info->writeProtected = (ramDiskFlags & kFlagWriteProtect) != 0;
  info->blocks = ramDiskPresent ? ramDiskBlocks : 0;
}

/* Longs when both ends allow it, bytes otherwise (a Mac buffer may be at
   an odd address). */
static void ramDiskCopy(unsigned char *dst, const unsigned char *src,
                        unsigned long bytes) {
  if ((((unsigned long)dst | (unsigned long)src) & kLongMask) == 0) {
    unsigned long *dstLong = (unsigned long *)dst;
    const unsigned long *srcLong = (const unsigned long *)src;
    for (unsigned long i = bytes >> 2; i != 0; --i) {
      *dstLong++ = *srcLong++;
    }
    return;
  }
  for (unsigned long i = 0; i < bytes; ++i) {
    dst[i] = src[i];
  }
}

short diskBackendRead(unsigned long block, unsigned long count, void *buffer) {
  if (!ramDiskPresent) {
    return MAC_OFFLINERR;
  }
  if (block + count > ramDiskBlocks) {
    return MAC_PARAMERR;
  }
  ramDiskCopy((unsigned char *)buffer, ramDiskBase + (block << kBlockShift),
              count << kBlockShift);
  return MAC_NOERR;
}

short diskBackendWrite(unsigned long block, unsigned long count,
                       const void *buffer) {
  if (!ramDiskPresent) {
    return MAC_OFFLINERR;
  }
  if (block + count > ramDiskBlocks) {
    return MAC_PARAMERR;
  }
  ramDiskCopy(ramDiskBase + (block << kBlockShift),
              (const unsigned char *)buffer, count << kBlockShift);
  return MAC_NOERR;
}

void diskBackendEject(void) { ramDiskPresent = 0; }
