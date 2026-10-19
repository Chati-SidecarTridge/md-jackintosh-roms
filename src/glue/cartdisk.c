/**
 * File: src/glue/cartdisk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The driver's cartridge backend (EPIC-05 STORY-08): the Mac's
 * drives 1, 2, 3 are md-jackintosh's units FD0, FD1, HD0, served from its
 * microSD over its app $07 disk commands (rp/src/include/macdisk.h there):
 * the unit table in the status block says what is inserted, its size and
 * whether it is read-only; READ fills the window's buffer (up to 32
 * sectors), WRITE carries up to 2; EJECT empties a unit until the ST boots
 * again (the hello mounts the empty units' images again). A unit whose change
 * counter moves is reported as inserted. While the RP restarts (its window
 * reads as fresh or as nothing: no identity) nothing is reported, the units
 * keep the state last read, and a read or write waits for the RP to come
 * back (a reflash takes about 25 s): a unit gone offline would set off the
 * File Manager's disk switch, which ejects it (seen on the bench,
 * 2026-10-19). A unit that comes back with the same size stays mounted
 * (md-jackintosh's advice, 2026-10-19). The copy out of the window
 * is md-drives-emulator's (glueCartCopy, cart.s), as Diego asked: "reuse as
 * much as possible the md-drives-emulator microfirmware".
 */

#include "cart.h"
#include "disk.h"
#include "macrom.h"
#include "trace.h"

enum { kBlockShift = 9, kIdMagicShift = 16, kIdVersionMask = 0xFFFF };

/* How long a disk command waits for a restarting RP: about 60 s, the loop
   being about as long as the senders' (cart.h). */
#define CART_DISK_WAIT_SPINS (CART_TIMEOUT_SPINS * 40UL)

#define UNIT_LONG(unit, field)                                           \
  (*(volatile unsigned long *)(glueCartStatus + CART_UNIT_TABLE_OFFSET + \
                               ((unit) * CART_UNIT_ENTRY_BYTES) + (field)))
#define STATUS_LONG(offset) \
  (*(volatile unsigned long *)(glueCartStatus + (offset)))

/* The change counters and sizes the Mac was last told about. */
static unsigned long cartDiskChanges[CART_UNITS];
static unsigned long cartDiskSectors[CART_UNITS];
/* Each unit's flags and size as last read while the RP was up. */
static unsigned long cartDiskFlags[CART_UNITS];
static unsigned long cartDiskBlocks[CART_UNITS];

/* Whether the RP's window is md-jackintosh's (not mid-restart). */
static int cartDiskUp(void) {
  const unsigned long identity = STATUS_LONG(CART_ID_OFFSET);
  return (identity >> kIdMagicShift) == CART_ID_MAGIC &&
         (identity & kIdVersionMask) == CART_ID_VERSION;
}

/* Whether the RP is up, waiting for it while it restarts. */
static int cartDiskWaitUp(void) {
  if (cartDiskUp()) {
    return 1;
  }
  TRACE("disk cart wait");
  for (unsigned long spins = CART_DISK_WAIT_SPINS; spins != 0; --spins) {
    if (cartDiskUp()) {
      TRACE("disk cart back");
      return 1;
    }
  }
  TRACE("disk cart gone");
  return 0;
}

static int cartDiskDrives(void) {
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    cartDiskChanges[unit] = UNIT_LONG(unit, CART_UNIT_CHANGES);
    cartDiskSectors[unit] = UNIT_LONG(unit, CART_UNIT_SECTORS);
    cartDiskFlags[unit] = UNIT_LONG(unit, CART_UNIT_FLAGS);
    cartDiskBlocks[unit] = cartDiskSectors[unit];
  }
  return CART_UNITS;
}

static void cartDiskInfo(int drive, struct DiskInfo *info) {
  if (cartDiskUp()) {
    cartDiskFlags[drive] = UNIT_LONG(drive, CART_UNIT_FLAGS);
    cartDiskBlocks[drive] = UNIT_LONG(drive, CART_UNIT_SECTORS);
  }
  info->present = (cartDiskFlags[drive] & CART_UNIT_FLAG_INSERTED) != 0;
  info->fixed = drive == CART_UNIT_HD0;
  info->writeProtected = (cartDiskFlags[drive] & CART_UNIT_FLAG_READONLY) != 0;
  info->blocks = info->present ? cartDiskBlocks[drive] : 0;
}

/* A command's outcome as a Mac result code. */
static short cartDiskResult(long sent) {
  if (sent != CART_OK) {
    return MAC_IOERR; /* no answer, or another command under way */
  }
  switch ((long)STATUS_LONG(CART_RESULT_OFFSET)) {
    case CART_RESULT_OK:
      return MAC_NOERR;
    case CART_RESULT_NOMEDIUM:
      return MAC_OFFLINERR;
    case CART_RESULT_READONLY:
      return MAC_WPRERR;
    case CART_RESULT_PARAM:
    case CART_RESULT_RANGE:
      return MAC_PARAMERR;
    default:
      return MAC_IOERR;
  }
}

static short cartDiskRead(int drive, unsigned long block, unsigned long count,
                          void *buffer) {
  unsigned char *dst = (unsigned char *)buffer;
  while (count != 0) {
    const unsigned long chunk = count < (unsigned long)glueCartMaxReadSectors
                                    ? count
                                    : (unsigned long)glueCartMaxReadSectors;
    long sent;
    short result;
    if (!cartDiskWaitUp()) {
      return MAC_IOERR;
    }
    sent = glueCartCommand(
        CART_CMD_DISK_READ, (long)(2 * sizeof(unsigned long)),
        (unsigned long)drive | (chunk << CART_COUNT_SHIFT), block, 0, 0);
    if (sent == CART_TIMEOUT && !cartDiskUp()) {
      continue; /* the RP restarted under the command: again once back */
    }
    result = cartDiskResult(sent);
    if (result != MAC_NOERR) {
      return result;
    }
    glueCartCopy(dst, glueCartStatus + CART_BUFFER_OFFSET,
                 (long)(chunk << kBlockShift));
    dst += chunk << kBlockShift;
    block += chunk;
    count -= chunk;
  }
  return MAC_NOERR;
}

static short cartDiskWrite(int drive, unsigned long block, unsigned long count,
                           const void *buffer) {
  const unsigned char *src = (const unsigned char *)buffer;
  while (count != 0) {
    const unsigned long chunk =
        count < CART_MAX_WRITE_SECTORS ? count : CART_MAX_WRITE_SECTORS;
    long sent;
    short result;
    if (!cartDiskWaitUp()) {
      return MAC_IOERR;
    }
    sent = glueCartCommandWrite(
        CART_CMD_DISK_WRITE, (unsigned long)drive | (chunk << CART_COUNT_SHIFT),
        block, 0, src, (long)(chunk << kBlockShift));
    if (sent == CART_TIMEOUT && !cartDiskUp()) {
      continue; /* the RP restarted under the command: again once back */
    }
    result = cartDiskResult(sent);
    if (result != MAC_NOERR) {
      return result;
    }
    src += chunk << kBlockShift;
    block += chunk;
    count -= chunk;
  }
  return MAC_NOERR;
}

static void cartDiskEject(int drive) {
  const long sent =
      glueCartCommand(CART_CMD_DISK_EJECT, (long)sizeof(unsigned long),
                      (unsigned long)drive, 0, 0, 0);
  TRACE("disk cart eject %d %ld", drive, sent);
  if (sent == CART_OK) {
    cartDiskChanges[drive] = UNIT_LONG(drive, CART_UNIT_CHANGES);
  }
}

static int cartDiskChanged(void) {
  int changed = 0;
#if defined(_TEST) && (_TEST > 0)
  glueCartFakePoll();
#endif
  if (!cartDiskUp()) {
    return 0; /* the RP restarting: the Mac keeps its view */
  }
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    const unsigned long changes = UNIT_LONG(unit, CART_UNIT_CHANGES);
    const unsigned long sectors = UNIT_LONG(unit, CART_UNIT_SECTORS);
    const int inserted =
        (UNIT_LONG(unit, CART_UNIT_FLAGS) & CART_UNIT_FLAG_INSERTED) != 0;
    if (changes == cartDiskChanges[unit]) {
      continue;
    }
    cartDiskChanges[unit] = changes;
    if (inserted && sectors == cartDiskSectors[unit] && changes == 1) {
      continue; /* back after an RP restart: the same medium, still mounted */
    }
    cartDiskSectors[unit] = sectors;
    if (inserted) {
      changed |= 1 << unit;
      TRACE("disk cart unit %d inserted %lu", unit, sectors);
    }
  }
  return changed;
}

const struct DiskBackend diskCartBackend = {
    "cartridge",   cartDiskDrives, cartDiskInfo,   cartDiskRead,
    cartDiskWrite, cartDiskEject,  cartDiskChanged};
