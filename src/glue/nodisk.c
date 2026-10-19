/**
 * File: src/glue/nodisk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The backend of builds without a test RAM disk: no medium, so
 * the Mac shows the blinking "?" (EPIC-02). md-jackintosh's cartridge
 * backend replaces it in Iteration 2 (EPIC-06).
 */

#include "disk.h"
#include "macrom.h"

unsigned long diskRamFind(unsigned long ramTop) { return ramTop; }

int diskBackendDrives(void) { return 1; }

void diskBackendInfo(int drive, struct DiskInfo *info) {
  (void)drive;
  info->present = 0;
  info->fixed = 0;
  info->writeProtected = 0;
  info->blocks = 0;
}

short diskBackendRead(int drive, unsigned long block, unsigned long count,
                      void *buffer) {
  (void)drive;
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

short diskBackendWrite(int drive, unsigned long block, unsigned long count,
                       const void *buffer) {
  (void)drive;
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

void diskBackendEject(int drive) { (void)drive; }

int diskBackendChanged(void) { return 0; }
