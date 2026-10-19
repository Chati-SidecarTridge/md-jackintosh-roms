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
#include "mem.h"
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
   its disks are the harness's RAM disks: with a tree, the images its
   settings name (EPIC-06's chooser); without one, FD0 the first (HD0 when
   it is a fixed disk) and FD1 the second. The fields up to tree are the
   harness's saved state (hatari_harness.py, fake_cart_files). */
enum {
  kFakeMagic = 0x46414B45, /* "FAKE" */
  kFakeReadSectors = 2,    /* the most a READ may ask of the fake */
  kFakeNoDrive = -1,
  kFakeTreeMax = 8,
  kFakeBuffer = 1800, /* a LIST page (1676 bytes), the settings (1704) */
  kFakeListMax = kFakeTreeMax + 1, /* its entries and ".." */
  kFakeCaseBit = 0x20
};
struct GlueTestImage {
  char path[CART_PATH_SIZE]; /* "/jackintosh/disk.dsk.rw" */
  long drive;                /* the RAM disk */
  unsigned long size;
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
  unsigned long selectSeq;             /* the harness's SELECT presses */
  unsigned long slot;                  /* FD0's current slot, 1-10 */
  unsigned long treeCount;
  char settings[CART_SETTINGS_ALL][CART_PATH_SIZE];
  char options[CART_OPTIONS][CART_OPTION_SIZE]; /* EPIC-12 */
  struct GlueTestImage tree[kFakeTreeMax];
  int unitDrive[CART_UNITS]; /* the RAM disk behind each unit */
  unsigned char status[CART_BUFFER_OFFSET + kFakeBuffer]; /* window */
  unsigned long selectSeen;
};
struct GlueTestCart glueTestCart;

#define FAKE_LONG(offset) (*(unsigned long *)(glueTestCart.status + (offset)))
#define FAKE_UNIT(unit, field) \
  FAKE_LONG(CART_UNIT_TABLE_OFFSET + ((unit) * CART_UNIT_ENTRY_BYTES) + (field))

static unsigned cartFakeLength(const char *text) {
  unsigned length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

static int cartFakeLower(int character) {
  return (character >= 'A' && character <= 'Z') ? character | kFakeCaseBit
                                                : character;
}

/* strcasecmp's sign. */
static int cartFakeCompare(const char *left, const char *right) {
  for (; *left != '\0' && cartFakeLower(*left) == cartFakeLower(*right);
       ++left, ++right) {
  }
  return cartFakeLower(*left) - cartFakeLower(*right);
}

/* Whether path is prefix + "/" + something (any prefix for "/"). */
static int cartFakeUnder(const char *path, const char *prefix) {
  const unsigned length = cartFakeLength(prefix);
  if (length == 1 && prefix[0] == '/') {
    return path[0] == '/' && path[1] != '\0';
  }
  for (unsigned i = 0; i < length; ++i) {
    if (path[i] != prefix[i]) {
      return 0;
    }
  }
  return path[length] == '/';
}

static int cartFakeSame(const char *left, const char *right) {
  for (; *left != '\0' && *left == *right; ++left, ++right) {
  }
  return *left == *right;
}

/* The tree's image at path, or -1. */
static int cartFakeImage(const char *path) {
  for (unsigned long i = 0; i < glueTestCart.treeCount; ++i) {
    if (path[0] != '\0' && cartFakeSame(glueTestCart.tree[i].path, path)) {
      return (int)i;
    }
  }
  return -1;
}

/* The path a unit takes from the settings. */
static const char *cartFakeUnitPath(int unit) {
  return glueTestCart.settings[unit == 0   ? glueTestCart.slot
                               : unit == 1 ? CART_KEY_FD1
                                           : CART_KEY_HD0];
}

/* md-jackintosh's .rw rule: a floppy unit's image is read-only unless its
   name ends in .rw. */
static int cartFakeReadOnlyName(int unit) {
  const char *path = cartFakeUnitPath(unit);
  const unsigned length = cartFakeLength(path);
  return glueTestCart.treeCount != 0 && unit != CART_UNIT_HD0 &&
         !(length >= 3 && path[length - 3] == '.' &&
           cartFakeLower(path[length - 2]) == 'r' &&
           cartFakeLower(path[length - 1]) == 'w');
}

/* A unit's entry from its RAM disk, its change counter moved. */
static void cartFakeUnit(int unit) {
  struct DiskInfo info;
  const int drive = glueTestCart.unitDrive[unit];
  info.present = 0;
  info.writeProtected = 0;
  info.blocks = 0;
  if (drive != kFakeNoDrive) {
    diskRamBackend.info(drive, &info);
    info.writeProtected |= cartFakeReadOnlyName(unit);
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
/* A unit's image from the settings, mounted. */
static void cartFakeMount(int unit) {
  const int image = cartFakeImage(cartFakeUnitPath(unit));
  glueTestCart.unitDrive[unit] =
      image < 0 ? kFakeNoDrive : (int)glueTestCart.tree[image].drive;
  if (image >= 0) {
    diskRamInsert((int)glueTestCart.tree[image].drive);
  }
  cartFakeUnit(unit);
}

static void cartFakeUnits(void) {
  struct DiskInfo info;
  for (int unit = 0; unit < CART_UNITS; ++unit) {
    glueTestCart.unitDrive[unit] = kFakeNoDrive;
  }
  FAKE_LONG(CART_CARD_OFFSET) = glueTestCart.card;
  glueTestCart.selectSeen = glueTestCart.selectSeq;
  if (glueTestCart.treeCount != 0 && glueTestCart.card == CART_CARD_OK) {
    for (int unit = 0; unit < CART_UNITS; ++unit) {
      cartFakeMount(unit);
    }
    return;
  }
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

/* md-jackintosh's SELECT in a Mac session: with FD0 empty, the next
   non-empty slot after the current one, wrapping. */
static void cartFakeSelect(void) {
  glueTestCart.selectSeen = glueTestCart.selectSeq;
  if (FAKE_UNIT(0, CART_UNIT_FLAGS) & CART_UNIT_FLAG_INSERTED) {
    TRACE("fake select ignored");
    return;
  }
  for (unsigned long step = 1; step <= CART_FD0_SLOTS; ++step) {
    const unsigned long slot = glueTestCart.slot + step > CART_FD0_SLOTS
                                   ? glueTestCart.slot + step - CART_FD0_SLOTS
                                   : glueTestCart.slot + step;
    if (glueTestCart.settings[slot][0] != '\0') {
      glueTestCart.slot = slot;
      cartFakeMount(0);
      TRACE("fake select slot %lu", slot);
      return;
    }
  }
}

void glueCartFakePoll(void) {
  const int changed = diskRamBackend.changed(); /* a --swap image */
  if (glueTestCart.enabled != kFakeMagic) {
    return;
  }
  if (glueTestCart.selectSeq != glueTestCart.selectSeen) {
    cartFakeSelect();
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

/* LIST: the folder's entries as md-jackintosh sorts them (".." first
   below the top, folders, then files, case-insensitive). */
static long cartFakeList(const char *folder, unsigned long first,
                         int wholeCard) {
  /* Only the menu lists, before the Mac: its scratch RAM, past the menu's
     own buffers (menu.c). */
  char (*names)[CART_PATH_SIZE] = (char (*)[CART_PATH_SIZE])(
      GLUE_SCRATCH_ADDR + (GLUE_SCRATCH_SIZE_BYTES / 2));
  unsigned long flags[kFakeListMax];
  unsigned long sizes[kFakeListMax];
  unsigned char *buffer = glueTestCart.status + CART_BUFFER_OFFSET;
  const char *top = wholeCard ? "/" : glueTestCart.settings[CART_KEY_FOLDER];
  const unsigned folderLength = cartFakeLength(folder);
  const unsigned skip = folderLength == 1 ? 1 : folderLength + 1;
  unsigned long count = 0;
  int exists = cartFakeSame(folder, top) || cartFakeSame(folder, "/");
  if (!cartFakeSame(folder, top) && !cartFakeUnder(folder, top)) {
    return CART_RESULT_PARAM;
  }
  if (!cartFakeSame(folder, top)) {
    names[count][0] = '.';
    names[count][1] = '.';
    names[count][2] = '\0';
    flags[count] = CART_ENTRY_FOLDER;
    sizes[count++] = 0;
  }
  for (unsigned long i = 0; i < glueTestCart.treeCount; ++i) {
    const char *path = glueTestCart.tree[i].path;
    unsigned length = 0;
    unsigned long known = 0;
    if (!cartFakeUnder(path, folder)) {
      continue;
    }
    exists = 1;
    path += skip;
    while (path[length] != '\0' && path[length] != '/') {
      ++length;
    }
    for (unsigned j = 0; j < length; ++j) {
      names[count][j] = path[j];
    }
    names[count][length] = '\0';
    flags[count] = path[length] == '/' ? CART_ENTRY_FOLDER : 0;
    sizes[count] = flags[count] ? 0 : glueTestCart.tree[i].size;
    if (!flags[count] && length >= 3 &&
        cartFakeLower(path[length - 1]) == 'w' &&
        cartFakeLower(path[length - 2]) == 'r' && path[length - 3] == '.') {
      flags[count] |= CART_ENTRY_RW;
    }
    for (unsigned long j = 0; j < count; ++j) {
      known |= cartFakeSame(names[j], names[count]);
    }
    if (!known) {
      ++count;
    }
  }
  if (!exists || (first != 0 && first >= count)) {
    return CART_RESULT_PARAM;
  }
  /* Insertion sort, ".." staying first. */
  for (unsigned long i = cartFakeSame(names[0], "..") ? 2 : 1; i < count; ++i) {
    for (unsigned long j = i; j > 0 && !cartFakeSame(names[j - 1], ".."); --j) {
      const int folders = (int)(flags[j] & CART_ENTRY_FOLDER) -
                          (int)(flags[j - 1] & CART_ENTRY_FOLDER);
      if (folders < 0 ||
          (folders == 0 && cartFakeCompare(names[j - 1], names[j]) <= 0)) {
        break;
      }
      for (unsigned k = 0; k < CART_PATH_SIZE; ++k) {
        const char swap = names[j][k];
        names[j][k] = names[j - 1][k];
        names[j - 1][k] = swap;
      }
      {
        const unsigned long swapFlags = flags[j];
        const unsigned long swapSize = sizes[j];
        flags[j] = flags[j - 1];
        sizes[j] = sizes[j - 1];
        flags[j - 1] = swapFlags;
        sizes[j - 1] = swapSize;
      }
    }
  }
  *(unsigned long *)(buffer + CART_LIST_TOTAL) = count;
  *(unsigned long *)(buffer + CART_LIST_FLAGS) = 0;
  {
    unsigned long returned = 0;
    for (unsigned long i = first; i < count && returned < CART_LIST_PAGE;
         ++i, ++returned) {
      unsigned char *entry =
          buffer + CART_LIST_ENTRIES + (returned * CART_LIST_ENTRY_SIZE);
      for (unsigned k = 0; k < CART_PATH_SIZE; ++k) {
        entry[k] = (unsigned char)names[i][k];
      }
      *(unsigned long *)(entry + CART_ENTRY_FLAGS) = flags[i];
      *(unsigned long *)(entry + CART_ENTRY_SIZE) = sizes[i];
    }
    *(unsigned long *)(buffer + CART_LIST_RETURNED) = returned;
  }
  return CART_RESULT_OK;
}

/* SET_SETTING: the path must exist under FOLDER (a folder for FOLDER);
   a unit's image is mounted at once. */
static long cartFakeSet(unsigned long key, const char *path) {
  const char *folder = glueTestCart.settings[CART_KEY_FOLDER];
  if (key >= CART_KEY_OPTION && key < CART_KEY_OPTION + CART_OPTIONS) {
    /* A ROM option: stored, not read (md-jackintosh's checks). */
    char *option = glueTestCart.options[key - CART_KEY_OPTION];
    if (cartFakeLength(path) >= CART_OPTION_SIZE) {
      return CART_RESULT_PARAM;
    }
    for (unsigned i = 0; i < CART_OPTION_SIZE; ++i) {
      option[i] = i <= cartFakeLength(path) ? path[i] : '\0';
    }
    return CART_RESULT_OK;
  }
  if (key >= CART_SETTINGS_ALL) {
    return CART_RESULT_PARAM;
  }
  if (key == CART_KEY_RTC_ENABLED) {
    if (!cartFakeSame(path, "true") && !cartFakeSame(path, "false")) {
      return CART_RESULT_PARAM;
    }
  } else if (key == CART_KEY_NTP_HOST || key == CART_KEY_NTP_PORT) {
    if (path[0] == '\0') {
      return CART_RESULT_PARAM; /* the RP's own checks are finer */
    }
  } else if (key == CART_KEY_FOLDER) {
    int exists = cartFakeSame(path, "/");
    for (unsigned long i = 0; i < glueTestCart.treeCount && !exists; ++i) {
      exists = cartFakeUnder(glueTestCart.tree[i].path, path);
    }
    if (!exists) {
      return CART_RESULT_PARAM;
    }
  } else if (path[0] != '\0' &&
             (cartFakeImage(path) < 0 || !cartFakeUnder(path, folder))) {
    return CART_RESULT_PARAM;
  }
  for (unsigned i = 0; i < CART_PATH_SIZE; ++i) {
    glueTestCart.settings[key][i] = path[i];
    if (path[i] == '\0') {
      break;
    }
  }
  if (key == glueTestCart.slot) {
    cartFakeMount(0);
  } else if (key == CART_KEY_FD1) {
    cartFakeMount(1);
  } else if (key == CART_KEY_HD0) {
    cartFakeMount(CART_UNIT_HD0);
  }
  return CART_RESULT_OK;
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
          glueTestCart.timeQueries >= glueTestCart.timeValidAt &&
                  !cartFakeSame(glueTestCart.settings[CART_KEY_RTC_ENABLED],
                                "false")
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
    case CART_CMD_LIST:
      result = cartFakeList((const char *)data, first,
                            (second & CART_LIST_WHOLE_CARD) != 0);
      break;
    case CART_CMD_GET_SETTINGS:
      for (unsigned i = 0; i < CART_SETTINGS * CART_PATH_SIZE; ++i) {
        buffer[i] = (unsigned char)glueTestCart.settings[0][i];
      }
      *(unsigned long *)(buffer + CART_SETTINGS_SLOT) = glueTestCart.slot;
      for (unsigned i = 0;
           i < (CART_SETTINGS_ALL - CART_KEY_RTC_ENABLED) * CART_PATH_SIZE;
           ++i) {
        buffer[CART_SETTINGS_RTC + i] =
            (unsigned char)glueTestCart.settings[CART_KEY_RTC_ENABLED][i];
      }
      *(unsigned long *)(buffer + CART_SETTINGS_OPTIONS) = CART_OPTIONS_MAGIC;
      for (unsigned i = 0; i < CART_OPTIONS * CART_OPTION_SIZE; ++i) {
        buffer[CART_SETTINGS_OPTION_FIELDS + i] =
            (unsigned char)glueTestCart.options[0][i];
      }
      break;
    case CART_CMD_SET_SETTING:
      result = cartFakeSet(first, (const char *)data);
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

void glueCartNoTime(void) {
  cartTimeRetries = kTimeRetryLimit; /* TIME will not become valid */
  TRACE("cart time off");
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
