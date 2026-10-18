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

void diskBackendInfo(struct DiskInfo *info) {
  info->present = 0;
  info->fixed = 0;
  info->writeProtected = 0;
  info->blocks = 0;
}

short diskBackendRead(unsigned long block, unsigned long count, void *buffer) {
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

short diskBackendWrite(unsigned long block, unsigned long count,
                       const void *buffer) {
  (void)block;
  (void)count;
  (void)buffer;
  return MAC_OFFLINERR;
}

void diskBackendEject(void) {}
