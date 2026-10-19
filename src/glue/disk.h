/**
 * File: src/glue/disk.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's disk driver (disk.c) and the backend interface it
 * sits on (EPIC-03 STORY-01). Backends: the RAM disk of test builds
 * (ramdisk.c, EPIC-03 STORY-02) and, from Iteration 2, md-jackintosh over
 * the cartridge (EPIC-06).
 */

#pragma once

#define DISK_BLOCK_BYTES 512UL

/* What a backend reports about its medium. */
struct DiskInfo {
  int present;          /* a medium is inserted */
  int fixed;            /* a hard disk: not ejectable */
  int writeProtected;   /* read-only */
  unsigned long blocks; /* 512-byte blocks */
};

/* The most drives a backend serves: md-jackintosh's FD0, FD1 and HD0 (the
   Mac's drives 1, 2, 3), or the test builds' RAM disks. */
#define DISK_DRIVES 3

/* A backend (EPIC-05 STORY-07): drives 0 to drives() - 1, the Mac's drives
   1 and up. Results are Mac result codes (0 = noErr). changed() returns the
   drives whose medium was inserted since the last call, one bit each (bit 0
   the Mac's drive 1), polled every tick. */
struct DiskBackend {
  const char *name;
  int (*drives)(void);
  void (*info)(int drive, struct DiskInfo *info);
  short (*read)(int drive, unsigned long block, unsigned long count,
                void *buffer);
  short (*write)(int drive, unsigned long block, unsigned long count,
                 const void *buffer);
  void (*eject)(int drive);
  int (*changed)(void);
};

/* No disk (nodisk.c); md-jackintosh's cartridge (cartdisk.c); the test
   builds' RAM disks (ramdisk.c). The driver takes the cartridge's when
   md-jackintosh answered at boot, else the RAM disks in test builds, else
   none. */
extern const struct DiskBackend diskNoBackend;
extern const struct DiskBackend diskCartBackend;
#if defined(_TEST) && (_TEST > 0)
extern const struct DiskBackend diskRamBackend;
#endif

/* The test builds' RAM disk: finds the harness's descriptors below the top
   of RAM, one per image; returns the lowest image's base (the Mac's MemTop
   goes below it), or ramTop when there is none. */
unsigned long diskRamFind(unsigned long ramTop);

/* Installs the driver at unit 4 (.Sony's slot, refNum -5) with the
   chosen backend's drives, called from InitIOMgr (glueSonyOpen). */
void glueInstallDisk(void);

/* Every tick: announces disks other than the startup one (disk.c). */
void glueDiskTick(void);
