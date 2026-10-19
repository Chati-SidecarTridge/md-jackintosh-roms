/**
 * File: src/glue/nodisk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The backend with no medium, so the Mac shows the blinking
 * "?" (EPIC-02): the driver's choice when md-jackintosh did not answer and
 * there is no test RAM disk (EPIC-05 STORY-07). Also the no-op diskRamFind
 * of builds without the RAM disk.
 */

#include "disk.h"
#include "macrom.h"

#if !defined(_TEST) || (_TEST == 0)
unsigned long diskRamFind(unsigned long ramTop) { return ramTop; }
#endif

static int noDiskDrives(void) { return 1; }

static void noDiskInfo(int drive, struct DiskInfo *info) {
  (void)drive;
  info->present = 0;
  info->fixed = 0;
  info->writeProtected = 0;
  info->blocks = 0;
}

static short noDiskRead(int drive, unsigned long block, unsigned long count,
                        void *buffer) {
  (void)drive;
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

static short noDiskWrite(int drive, unsigned long block, unsigned long count,
                         const void *buffer) {
  (void)drive;
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

static void noDiskEject(int drive) { (void)drive; }

static int noDiskChanged(void) { return 0; }

const struct DiskBackend diskNoBackend = {
    "none",      noDiskDrives, noDiskInfo,   noDiskRead,
    noDiskWrite, noDiskEject,  noDiskChanged};
