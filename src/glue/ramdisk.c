/**
 * File: src/glue/ramdisk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The RAM-disk backend of test builds (EPIC-03 STORY-02). The
 * Hatari harness loads a raw disk image below the top of RAM and a 16-byte
 * descriptor in the last 16 bytes: "JKRD", the image's base, its size in
 * bytes, flags (bit 0 a fixed disk, bit 1 write-protected). A second image,
 * for drive 2 (EPIC-09 STORY-01), sits below the first, its descriptor in
 * the 16 bytes under the first image's base. The glue puts the Mac's MemTop
 * at the lowest image's base, so the Mac never touches them. A restart
 * keeps them: glueReset's $FF fill stops at the images (startup.s).
 *
 * Disk swapping (EPIC-09): when the Mac ejects a drive, the harness may
 * load another image into its place and write glueTestDisk (the drive, the
 * new size and flags, then a new seq); ramDiskChanged reports it and
 * the driver announces the disk.
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

static struct {
  unsigned char *base;
  unsigned long blocks;
  unsigned long flags;
  int present;
} ramDisks[DISK_DRIVES];
static int ramDiskCount;

/* The harness's swap mailbox: seq last, after the other fields. */
struct GlueTestDisk {
  volatile unsigned short seq;
  unsigned short drive; /* 0 = the Mac's drive 1 */
  unsigned long bytes;
  unsigned long flags;
};
struct GlueTestDisk glueTestDisk;
static unsigned short ramDiskSwapSeen;

unsigned long diskRamFind(unsigned long ramTop) {
  unsigned long top = ramTop;
  ramDiskCount = 0;
  while (ramDiskCount < DISK_DRIVES) {
    const unsigned long *desc = (const unsigned long *)(top - kDescriptorBytes);
    const unsigned long base = desc[1];
    const unsigned long bytes = desc[2];
    if (desc[0] != kMagic || (base & kAlignMask) != 0 || bytes == 0 ||
        (bytes & (DISK_BLOCK_BYTES - 1)) != 0 ||
        base + bytes > top - kDescriptorBytes) {
      break;
    }
    ramDisks[ramDiskCount].base = (unsigned char *)base;
    ramDisks[ramDiskCount].blocks = bytes >> kBlockShift;
    ramDisks[ramDiskCount].flags = desc[3];
    ramDisks[ramDiskCount].present = 1;
    if (ramDiskCount == 0) {
      TRACE("disk ramdisk %lu base %06lx flags %lx", bytes, base, desc[3]);
    } else {
      TRACE("disk ramdisk%d %lu base %06lx flags %lx", ramDiskCount + 1, bytes,
            base, desc[3]);
    }
    ++ramDiskCount;
    top = base;
  }
  if (ramDiskCount == 0) {
    TRACE("disk ramdisk none");
  }
  return top;
}

static int ramDiskDrives(void) { return ramDiskCount > 1 ? ramDiskCount : 1; }

static void ramDiskInfo(int drive, struct DiskInfo *info) {
  const int present = drive < ramDiskCount && ramDisks[drive].present;
  info->present = present;
  info->fixed = present && (ramDisks[drive].flags & kFlagFixed) != 0;
  info->writeProtected =
      present && (ramDisks[drive].flags & kFlagWriteProtect) != 0;
  info->blocks = present ? ramDisks[drive].blocks : 0;
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

static short ramDiskRead(int drive, unsigned long block, unsigned long count,
                         void *buffer) {
  if (drive >= ramDiskCount || !ramDisks[drive].present) {
    return MAC_OFFLINERR;
  }
  if (block + count > ramDisks[drive].blocks) {
    return MAC_PARAMERR;
  }
  ramDiskCopy((unsigned char *)buffer,
              ramDisks[drive].base + (block << kBlockShift),
              count << kBlockShift);
  return MAC_NOERR;
}

static short ramDiskWrite(int drive, unsigned long block, unsigned long count,
                          const void *buffer) {
  if (drive >= ramDiskCount || !ramDisks[drive].present) {
    return MAC_OFFLINERR;
  }
  if (block + count > ramDisks[drive].blocks) {
    return MAC_PARAMERR;
  }
  ramDiskCopy(ramDisks[drive].base + (block << kBlockShift),
              (const unsigned char *)buffer, count << kBlockShift);
  return MAC_NOERR;
}

static void ramDiskEject(int drive) {
  if (drive < ramDiskCount) {
    ramDisks[drive].present = 0;
  }
}

void diskRamInsert(int drive) {
  if (drive >= 0 && drive < ramDiskCount) {
    ramDisks[drive].present = 1;
  }
}

static int ramDiskChanged(void) {
  const unsigned drive = glueTestDisk.drive;
  if (glueTestDisk.seq == ramDiskSwapSeen) {
    return 0;
  }
  ramDiskSwapSeen = glueTestDisk.seq;
  if (drive >= (unsigned)ramDiskCount) {
    return 0;
  }
  ramDisks[drive].blocks = glueTestDisk.bytes >> kBlockShift;
  ramDisks[drive].flags = glueTestDisk.flags;
  ramDisks[drive].present = 1;
  TRACE("disk%u swapped in %lu", drive + 1, glueTestDisk.bytes);
  return 1 << drive;
}

const struct DiskBackend diskRamBackend = {
    "ramdisk",    ramDiskDrives, ramDiskInfo,   ramDiskRead,
    ramDiskWrite, ramDiskEject,  ramDiskChanged};
