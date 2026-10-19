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

/* The most drives a backend serves: two floppies, as a Plus with an
   external drive (EPIC-09 STORY-01). */
#define DISK_DRIVES 2

/* A backend: drives 0 to diskBackendDrives() - 1, the Mac's drives 1 and up.
   Results are Mac result codes (0 = noErr). */
int diskBackendDrives(void);
void diskBackendInfo(int drive, struct DiskInfo *info);
short diskBackendRead(int drive, unsigned long block, unsigned long count,
                      void *buffer);
short diskBackendWrite(int drive, unsigned long block, unsigned long count,
                       const void *buffer);
void diskBackendEject(int drive);
/* The drives whose medium was inserted since the last call, one bit each
   (bit 0 the Mac's drive 1); polled every tick. */
int diskBackendChanged(void);

/* The test builds' RAM disk: finds the harness's descriptors below the top
   of RAM, one per image; returns the lowest image's base (the Mac's MemTop
   goes below it), or ramTop when there is none. */
unsigned long diskRamFind(unsigned long ramTop);

/* Installs the driver at unit 4 (.Sony's slot, refNum -5) with the
   backend's drives, called from InitIOMgr (glueSonyOpen). */
void glueInstallDisk(void);

/* Every tick: announces disks other than the startup one (disk.c). */
void glueDiskTick(void);
