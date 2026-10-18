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

/* A backend: one medium for now. Results are Mac result codes (0 = noErr). */
void diskBackendInfo(struct DiskInfo *info);
short diskBackendRead(unsigned long block, unsigned long count, void *buffer);
short diskBackendWrite(unsigned long block, unsigned long count,
                       const void *buffer);
void diskBackendEject(void);

/* The test builds' RAM disk: finds the harness's descriptor below the top
   of RAM; returns the image's base (the Mac's MemTop goes below it), or
   ramTop when there is none. */
unsigned long diskRamFind(unsigned long ramTop);

/* Installs the driver at unit 4 (.Sony's slot, refNum -5) with one drive,
   called from InitIOMgr (glueSonyOpen). */
void glueInstallDisk(void);
