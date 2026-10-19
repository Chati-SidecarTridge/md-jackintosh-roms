/**
 * File: src/glue/cart.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's side of the Multi-device (EPIC-05), as agreed
 * with md-jackintosh's session on 2026-10-19. With no TOS nothing runs the
 * cartridge's own code, so before the Mac starts our ROM says hello itself
 * (CMD_ST_HELLO), checks that md-jackintosh answered (the identity long:
 * any app built on the template answers every command), loads the Mac's
 * PRAM from its file and takes the time (UTC from NTP, plus the Mac's own
 * time zone in XPRAM). Then every tick: the time again until NTP has it,
 * and the PRAM written back a few seconds after the Mac's last change. No
 * answer, or another firmware, and the Mac starts with no disks, its
 * default PRAM and its default clock.
 *
 * Test builds can answer from a fake Multi-device in RAM (glueTestCart),
 * which the harness fills before the glue runs: Hatari has no SidecarTridge
 * (C-02), and this way everything but the bus is checked there.
 */

#include "cart.h"

#include "disk.h"
#include "mac.h"
#include "macrom.h"
#include "trace.h"

enum {
  /* About 3 s with no cartridge. On the bench the RP answers 1.0-1.2 s after
     power-on and our first hello goes out about 4 s after it: one try was
     always enough (nine cold power-ons, 2026-10-19). */
  kHelloAttempts = 2,
  /* How long PENDING may last: about 3 s (the RP's attempt takes 0.1-0.2 s,
     up to about 1 s with a slow card; the loop is about as long as the
     senders', cart.h). */
  kCardWaitSpins = CART_TIMEOUT_SPINS * 2,
  kTimeRetryTicks = 600, /* ask for the time every 10 s ... */
  kTimeRetryLimit = 12,  /* ... for 2 minutes */
  kPramWriteTicks = 180, /* the PRAM written 3 s after its last change */
  kIdMagicShift = 16,
  kIdVersionMask = 0xFFFF,
  kUnitMask = 0xFFFF
};

#define LOWMEM_LONG(addr) (*(volatile unsigned long *)(unsigned long)(addr))

int glueCartState;
unsigned long glueCartCard = CART_CARD_OK;
volatile unsigned char *glueCartStatus = /* the status block */
    (volatile unsigned char *)CART_APP_FREE_ADDR;
int glueCartMaxReadSectors = CART_MAX_READ_SECTORS;
static int cartTimeKnown;
static int cartTimeRetries;
static unsigned long cartTimeAsked;

#define STATUS_LONG(offset) \
  (*(volatile unsigned long *)(glueCartStatus + (offset)))

#if defined(_TEST) && (_TEST > 0)
/* The fake Multi-device: when enabled holds kFakeMagic, the commands are
   answered here as md-jackintosh would answer them, from these fields, and
   its disks are the harness's RAM disks (FD0 the first, or HD0 when it is a
   fixed disk; FD1 the second). The fields up to pram are the harness's
   saved state (hatari_harness.py, fake_cart_files). */
enum {
  kFakeMagic = 0x46414B45, /* "FAKE" */
  kFakeReadSectors = 2,    /* the most a READ may ask of the fake */
  kFakeNoDrive = -1
};
struct GlueTestCart {
  unsigned long enabled;
  unsigned long id;          /* written at +$0C */
  unsigned long seconds;     /* UTC */
  unsigned long timeValidAt; /* the TIME query from which it is valid */
  unsigned long pramPresent;
  unsigned long timeQueries;
  unsigned long pramWrites;
  unsigned long card;                  /* CART_CARD_*, written at +$40 */
  unsigned char pram[CART_PRAM_BYTES]; /* PRAM.BIN */
  int unitDrive[CART_UNITS];           /* the RAM disk behind each unit */
  unsigned char status[CART_BUFFER_OFFSET +
                       (kFakeReadSectors * CART_SECTOR_BYTES)]; /* window */
};
struct GlueTestCart glueTestCart;

#define FAKE_LONG(offset) (*(unsigned long *)(glueTestCart.status + (offset)))
#define FAKE_UNIT(unit, field) \
  FAKE_LONG(CART_UNIT_TABLE_OFFSET + ((unit) * CART_UNIT_ENTRY_BYTES) + (field))

/* A unit's entry from its RAM disk, its change counter moved. */
static void cartFakeUnit(int unit) {
  struct DiskInfo info;
  const int drive = glueTestCart.unitDrive[unit];
  info.present = 0;
  info.writeProtected = 0;
  info.blocks = 0;
  if (drive != kFakeNoDrive) {
    diskRamBackend.info(drive, &info);
  }
  FAKE_UNIT(unit, CART_UNIT_FLAGS) =
      (info.present ? CART_UNIT_FLAG_INSERTED : 0) |
      (info.writeProtected ? CART_UNIT_FLAG_READONLY : 0);
  FAKE_UNIT(unit, CART_UNIT_SECTORS) = info.present ? info.blocks : 0;
  ++FAKE_UNIT(unit, CART_UNIT_CHANGES);
}

/* The units at each boot: md-jackintosh looks up its images again at every
   CMD_ST_HELLO for the units left empty, so the floppies the Mac's Restart
   ejected come back (agreed 2026-10-19); the RAM disks are all back after a
   restart, so the fake looks up every unit. */
static void cartFakeUnits(void) {
  struct DiskInfo info;
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    glueTestCart.unitDrive[unit] = kFakeNoDrive;
  }
  FAKE_LONG(CART_CARD_OFFSET) = glueTestCart.card;
  diskRamBackend.info(0, &info);
  if (glueTestCart.card != CART_CARD_OK) {
    info.present = 0; /* md-jackintosh stops at a card error: no disks */
  }
  if (info.present) {
    glueTestCart.unitDrive[info.fixed ? CART_UNIT_HD0 : 0] = 0;
  }
  if (diskRamBackend.drives() > 1 && glueTestCart.card == CART_CARD_OK) {
    glueTestCart.unitDrive[1] = 1;
  }
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    cartFakeUnit(unit);
  }
}

void glueCartFakePoll(void) {
  const int changed = diskRamBackend.changed(); /* a --swap image */
  if (glueTestCart.enabled != kFakeMagic) {
    return;
  }
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    const int drive = glueTestCart.unitDrive[unit];
    if (drive != kFakeNoDrive && (changed & (1 << drive))) {
      cartFakeUnit(unit);
    }
  }
}

/* A RAM disk's result as md-jackintosh's. */
static long cartFakeResult(short macResult) {
  switch (macResult) {
    case MAC_NOERR:
      return CART_RESULT_OK;
    case MAC_OFFLINERR:
      return CART_RESULT_NOMEDIUM;
    case MAC_PARAMERR:
      return CART_RESULT_RANGE;
    default:
      return CART_RESULT_IO;
  }
}

static long cartFake(long command, unsigned long first, unsigned long second,
                     const void *data) {
  unsigned char *buffer = glueTestCart.status + CART_BUFFER_OFFSET;
  const unsigned unit = first & kUnitMask;
  const unsigned long count = first >> CART_COUNT_SHIFT;
  const int drive =
      unit < CART_UNITS ? glueTestCart.unitDrive[unit] : kFakeNoDrive;
  long result = CART_RESULT_OK;
  FAKE_LONG(CART_ID_OFFSET) = glueTestCart.id;
  FAKE_LONG(CART_CARD_OFFSET) = glueTestCart.card;
  if (glueTestCart.card != CART_CARD_OK && command != CART_CMD_ST_HELLO) {
    /* A card error: the disks and the PRAM have no medium, the time is not
       valid (no NTP). */
    FAKE_LONG(CART_TIME_FLAGS_OFFSET) = 0;
    FAKE_LONG(CART_RESULT_OFFSET) =
        (unsigned long)(command == CART_CMD_TIME ? CART_RESULT_OK
                                                 : CART_RESULT_NOMEDIUM);
    return CART_OK;
  }
  switch (command) {
    case CART_CMD_ST_HELLO:
      return CART_OK;
    case CART_CMD_TIME:
      ++glueTestCart.timeQueries;
      FAKE_LONG(CART_TIME_SECONDS_OFFSET) = glueTestCart.seconds;
      FAKE_LONG(CART_TIME_FLAGS_OFFSET) =
          glueTestCart.timeQueries >= glueTestCart.timeValidAt
              ? CART_TIME_FLAG_VALID
              : 0;
      break;
    case CART_CMD_PRAM_READ:
      if (!glueTestCart.pramPresent) {
        result = CART_RESULT_NOMEDIUM;
        break;
      }
      for (int i = 0; i < CART_PRAM_BYTES; ++i) {
        buffer[i] = glueTestCart.pram[i];
      }
      break;
    case CART_CMD_PRAM_WRITE:
      for (int i = 0; i < CART_PRAM_BYTES; ++i) {
        glueTestCart.pram[i] = ((const unsigned char *)data)[i];
      }
      glueTestCart.pramPresent = 1;
      ++glueTestCart.pramWrites;
      break;
    case CART_CMD_DISK_READ:
      if (unit >= CART_UNITS || count == 0 || count > kFakeReadSectors) {
        result = CART_RESULT_PARAM;
      } else if (drive == kFakeNoDrive) {
        result = CART_RESULT_NOMEDIUM;
      } else {
        result =
            cartFakeResult(diskRamBackend.read(drive, second, count, buffer));
      }
      break;
    case CART_CMD_DISK_WRITE:
      if (unit >= CART_UNITS || count == 0 || count > CART_MAX_WRITE_SECTORS) {
        result = CART_RESULT_PARAM;
      } else if (drive == kFakeNoDrive) {
        result = CART_RESULT_NOMEDIUM;
      } else if (FAKE_UNIT(unit, CART_UNIT_FLAGS) & CART_UNIT_FLAG_READONLY) {
        result = CART_RESULT_READONLY;
      } else {
        result =
            cartFakeResult(diskRamBackend.write(drive, second, count, data));
      }
      break;
    case CART_CMD_DISK_EJECT:
      if (unit >= CART_UNITS) {
        result = CART_RESULT_PARAM;
      } else if (drive != kFakeNoDrive) {
        diskRamBackend.eject(drive);
        cartFakeUnit(unit);
      }
      break;
    default:
      result = CART_RESULT_PARAM;
      break;
  }
  FAKE_LONG(CART_RESULT_OFFSET) = (unsigned long)result;
  return CART_OK;
}
#endif

long glueCartCommand(long command, long payloadBytes, unsigned long first,
                     unsigned long second, unsigned long third,
                     unsigned long fourth) {
#if defined(_TEST) && (_TEST > 0)
  if (glueTestCart.enabled == kFakeMagic) {
    return cartFake(command, first, second, 0);
  }
#endif
  return glueCartSend(command, payloadBytes, first, second, third, fourth);
}

long glueCartCommandWrite(long command, unsigned long first,
                          unsigned long second, unsigned long third,
                          const void *data, long bytes) {
#if defined(_TEST) && (_TEST > 0)
  if (glueTestCart.enabled == kFakeMagic) {
    return cartFake(command, first, second, data);
  }
#endif
  return glueCartSendWrite(command, first, second, third, data, bytes);
}

/* A command with no payload but the token. */
static long cartCommand(long command) {
  return glueCartCommand(command, 0, 0, 0, 0, 0);
}

/* The time from the Multi-device: UTC, plus the Mac's time zone. */
static void cartTime(void) {
  unsigned long seconds;
  unsigned long flags;
  if (cartCommand(CART_CMD_TIME) != CART_OK) {
    return; /* busy or no answer: the next tick tries again */
  }
  seconds = STATUS_LONG(CART_TIME_SECONDS_OFFSET);
  flags = STATUS_LONG(CART_TIME_FLAGS_OFFSET);
  if (!(flags & CART_TIME_FLAG_VALID)) {
    TRACE("cart time not yet");
    return;
  }
  seconds += (unsigned long)glueRtcGmtDelta();
  glueRtcSetSeconds(seconds);
  cartTimeKnown = 1;
  TRACE("cart time %08lx", seconds);
}

/* The microSD card's state, waiting while the RP has not tried it yet.
   Still PENDING after the wait, or a value we do not know: boot as if it
   were OK (the units say what is there). */
static unsigned long cartCard(void) {
  unsigned long card = STATUS_LONG(CART_CARD_OFFSET);
  if (card == CART_CARD_PENDING) {
    TRACE("cart card pending");
    for (unsigned long spins = kCardWaitSpins;
         spins != 0 && card == CART_CARD_PENDING; --spins) {
      card = STATUS_LONG(CART_CARD_OFFSET);
    }
  }
  TRACE("cart card %lu", card);
  return card <= CART_CARD_NO_FOLDER ? card : CART_CARD_OK;
}

int glueCartBoot(void) {
  long result = CART_TIMEOUT;
  unsigned long identity;
#if defined(_TEST) && (_TEST > 0)
  if (glueTestCart.enabled == kFakeMagic) {
    glueCartStatus = glueTestCart.status;
    glueCartMaxReadSectors = kFakeReadSectors;
    cartFakeUnits();
  }
#endif
  TRACE("cart hello");
#if defined(_DEBUG) && (_DEBUG > 0)
  /* Debug builds run in Hatari only (NatFeats traps on hardware): once. */
  result = cartCommand(CART_CMD_ST_HELLO);
#else
  for (int attempt = 0; attempt < kHelloAttempts && result != CART_OK;
       ++attempt) {
    result = cartCommand(CART_CMD_ST_HELLO);
  }
#endif
  if (result != CART_OK) {
    glueCartState = CART_STATE_NONE;
    TRACE("cart hello none");
    return glueCartState;
  }
  identity = STATUS_LONG(CART_ID_OFFSET);
  if ((identity >> kIdMagicShift) != CART_ID_MAGIC ||
      (identity & kIdVersionMask) != CART_ID_VERSION) {
    glueCartState = CART_STATE_OTHER;
    TRACE("cart hello other %08lx", identity);
    return glueCartState;
  }
  glueCartState = CART_STATE_OK;
  TRACE("cart hello ok %08lx", identity);
  glueCartCard = cartCard();

  /* The PRAM before the time: the time zone is in it. */
  if (cartCommand(CART_CMD_PRAM_READ) == CART_OK &&
      (long)STATUS_LONG(CART_RESULT_OFFSET) == 0) {
    glueRtcLoadPram(glueCartStatus + CART_BUFFER_OFFSET);
    TRACE("cart pram loaded");
  } else {
    TRACE("cart pram none");
  }
  glueRtcPramChanged = 0;
  cartTime();
  cartTimeAsked = 0;
  return glueCartState;
}

void glueCartTick(void) {
  const unsigned long ticks = LOWMEM_LONG(MAC_TICKS);
  if (glueCartState != CART_STATE_OK) {
    return;
  }
  if (!cartTimeKnown && cartTimeRetries < kTimeRetryLimit &&
      ticks - cartTimeAsked >= kTimeRetryTicks) {
    cartTimeAsked = ticks;
    ++cartTimeRetries;
    cartTime();
  }
  if (glueRtcPramChanged && ticks - glueRtcPramChangedTick >= kPramWriteTicks) {
    const long result = glueCartCommandWrite(
        CART_CMD_PRAM_WRITE, 0, 0, 0, glueRtcPramBytes(), CART_PRAM_BYTES);
    if (result == CART_BUSY) {
      return; /* inside another command: the next tick */
    }
    glueRtcPramChanged = 0;
    TRACE("cart pram saved %ld %ld", result,
          (long)STATUS_LONG(CART_RESULT_OFFSET));
  }
}
