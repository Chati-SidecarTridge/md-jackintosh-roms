/**
 * File: src/glue/menu.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The setup menu at boot (EPIC-06, D-14): md-drives-emulator's
 * menu, file browser and FD0 slots (its emul.c, as Diego asked: "don't
 * reinvent the wheel, users already know the md-drives-emulator"), drawn in
 * romemul-switcher-rom's 80-column layout (its chooser.c). The images and
 * the folder are md-jackintosh's settings, read with GET_SETTINGS, listed
 * with LIST and changed with SET_SETTING, which also mounts the image. A
 * 20 s countdown boots the Mac; any key but [E] stops it.
 */

#include "menu.h"

#include "cart.h"
#include "hw.h"
#include "kbd.h"
#include "layout.h"
#include "lib.h"
#include "mac.h"
#include "mem.h"
#include "screen.h"
#include "sound.h"
#include "text.h"
#include "trace.h"

enum {
  kCountdownSeconds = 20,
  kFramesPerSecond = 71, /* 71.2 Hz on a mono monitor */
  kTitleRow = 0,
  kHeaderRow = 2,
  kCountRow = 3,
  kListRow = 4,
  kFooterRow = 22,
  kStatusRow = 24,
  kLineWidth = TEXT_COLUMNS - 1,
  kPathShown = 56, /* a path's characters on a menu line */
  kSlotFirstEditable = 2,
  kScanDigitTwo = 0x03, /* the digits 2-9, then 0 for slot 10 */
  kScanDigitZero = 0x0B
};

/* One page of a folder: the entries' names and flags, the total. */
struct MenuPage {
  unsigned long total;
  unsigned long count;
  char names[CART_LIST_PAGE][CART_PATH_SIZE];
  unsigned long flags[CART_LIST_PAGE];
};

/* The menu's buffers, in the scratch RAM the Mac takes later (mem.h): the
   glue's own RAM is too small for them. */
struct MenuScratch {
  char settings[CART_SETTINGS_ALL][CART_PATH_SIZE];
  char options[CART_OPTIONS][CART_OPTION_SIZE];
  struct MenuPage page;
};
#define MENU_SCRATCH ((struct MenuScratch *)GLUE_SCRATCH_ADDR)
#define menuSettings (MENU_SCRATCH->settings)

/* FD0's current slot (GET_SETTINGS): the one in the drive. */
static int menuSlot = 1;
/* Whether md-jackintosh has the RTC settings (EPIC-13): an older one
   answers without them, and the bytes there are not "true" or "false". */
static int menuHasRtc;
/* Whether md-jackintosh keeps the ROM's options (EPIC-12): its "OPT1"
   marker in GET_SETTINGS' answer. */
static int menuHasOptions;
#define menuOptions (MENU_SCRATCH->options)

struct GlueOptions glueOptions = {
    0, GLUE_RIGHT_OFF, 0, GLUE_LAYOUT_US, GLUE_MOUSE_SPEED_DEFAULT, 0, 0, 1};

static unsigned menuLength(const char *text) {
  unsigned length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

static void menuCopy(char *dst, const volatile unsigned char *src,
                     unsigned size) {
  unsigned length = 0;
  for (; length + 1 < size && src[length] != 0; ++length) {
    dst[length] = (char)src[length];
  }
  dst[length] = '\0';
}

static void menuFormat(char *out, unsigned long size, const char *fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  (void)textFormat(out, size, fmt, args);
  __builtin_va_end(args);
}

static int menuSame(const char *left, const char *right) {
  for (; *left != '\0' && *left == *right; ++left, ++right) {
  }
  return *left == *right;
}

static int menuGetSettings(void) {
  if (glueCartCommand(CART_CMD_GET_SETTINGS, 0, 0, 0, 0, 0) != CART_OK ||
      *(volatile long *)(glueCartStatus + CART_RESULT_OFFSET) !=
          CART_RESULT_OK) {
    return 0;
  }
  for (int key = 0; key < CART_SETTINGS; ++key) {
    menuCopy(menuSettings[key],
             glueCartStatus + CART_BUFFER_OFFSET + (key * CART_PATH_SIZE),
             CART_PATH_SIZE);
  }
  menuSlot = (int)*(const volatile unsigned long *)(glueCartStatus +
                                                    CART_BUFFER_OFFSET +
                                                    CART_SETTINGS_SLOT);
  if (menuSlot < 1 || menuSlot > CART_FD0_SLOTS) {
    menuSlot = 1;
  }
  for (int key = CART_KEY_RTC_ENABLED; key < CART_SETTINGS_ALL; ++key) {
    menuCopy(menuSettings[key],
             glueCartStatus + CART_BUFFER_OFFSET + CART_SETTINGS_RTC +
                 ((key - CART_KEY_RTC_ENABLED) * CART_PATH_SIZE),
             CART_PATH_SIZE);
  }
  menuHasRtc = menuSame(menuSettings[CART_KEY_RTC_ENABLED], "true") ||
               menuSame(menuSettings[CART_KEY_RTC_ENABLED], "false");
  menuHasOptions =
      *(const volatile unsigned long *)(glueCartStatus + CART_BUFFER_OFFSET +
                                        CART_SETTINGS_OPTIONS) ==
      CART_OPTIONS_MAGIC;
  for (int option = 0; option < CART_OPTIONS; ++option) {
    menuOptions[option][0] = '\0';
    if (menuHasOptions) {
      menuCopy(menuOptions[option],
               glueCartStatus + CART_BUFFER_OFFSET +
                   CART_SETTINGS_OPTION_FIELDS + (option * CART_OPTION_SIZE),
               CART_OPTION_SIZE);
    }
  }
  return 1;
}

/* An option's value ("" for the ROM's default). */
static const char *menuOption(int key) {
  return menuOptions[key - CART_KEY_OPTION];
}

/* The unit's setting already holding path, other than key's: the same
   volume in two drives, which the Mac will not mount. 0 when none. */
static const char *menuInUse(int key, const char *path) {
  static const char *const kUnitNames[] = {"FD0", "FD1", "HD0"};
  const int unitKeys[] = {menuSlot, CART_KEY_FD1, CART_KEY_HD0};
  for (int unit = 0; unit < (int)(sizeof(unitKeys) / sizeof(unitKeys[0]));
       ++unit) {
    const int sameUnit =
        unit == 0 ? (key >= 1 && key <= CART_FD0_SLOTS) : unitKeys[unit] == key;
    if (!sameUnit && path[0] != '\0' &&
        menuSame(menuSettings[unitKeys[unit]], path)) {
      return kUnitNames[unit];
    }
  }
  return 0;
}

/* SET_SETTING: md-jackintosh's result (CART_RESULT_*), or CART_RESULT_IO
   when it did not answer. */
static long menuSet(int key, const char *path) {
  const long sent =
      glueCartCommandWrite(CART_CMD_SET_SETTING, (unsigned long)key, 0, 0, path,
                           (long)menuLength(path) + 1);
  const long result =
      sent == CART_OK ? *(volatile long *)(glueCartStatus + CART_RESULT_OFFSET)
                      : CART_RESULT_IO;
  TRACE("menu set %d %s %ld", key, path, result);
  (void)menuGetSettings();
  return result;
}

static void menuTitle(void) {
  static const char kTitle[] = "SidecarTridge Jackintosh - v" APP_VERSION_STR;
  textClear();
  textInverse(1);
  textClearRow(kTitleRow);
  textAt((TEXT_COLUMNS - (sizeof(kTitle) - 1)) / 2, kTitleRow);
  textPuts(kTitle);
  textInverse(0);
}

static void menuStatus(const char *text) {
  textInverse(1);
  textClearRow(kStatusRow);
  textPuts(text);
  textInverse(0);
}

/* A path in at most width characters: its tail after "..", as
   md-drives-emulator's right(). */
static void menuPath(const char *path, unsigned width) {
  const unsigned length = menuLength(path);
  if (length == 0) {
    textPuts("<empty>");
  } else if (length <= width) {
    textPuts(path);
  } else {
    textPuts("..");
    textPuts(path + length - (width - 2));
  }
}

/* folder + "/" + name into out; 0 when it does not fit. */
static int menuJoin(char *out, const char *folder, const char *name) {
  const unsigned folderLength = menuLength(folder);
  const unsigned nameLength = menuLength(name);
  const int root = folderLength == 1; /* "/" */
  char joined[CART_PATH_SIZE];
  unsigned length = 0;
  if (folderLength + (root ? 0 : 1) + nameLength + 1 > CART_PATH_SIZE) {
    return 0;
  }
  for (unsigned i = 0; i < folderLength; ++i) {
    joined[length++] = folder[i];
  }
  if (!root) {
    joined[length++] = '/';
  }
  for (unsigned i = 0; i <= nameLength; ++i) {
    joined[length++] = name[i];
  }
  for (unsigned i = 0; i < length; ++i) {
    out[i] = joined[i]; /* out may be folder */
  }
  return 1;
}

/* The folder above path, in place ("/" at the top). */
static void menuParent(char *path) {
  unsigned slash = 0;
  for (unsigned i = 0; path[i] != '\0'; ++i) {
    if (path[i] == '/') {
      slash = i;
    }
  }
  path[slash == 0 ? 1 : slash] = '\0';
}

static int menuIsDotDot(const char *name) {
  return name[0] == '.' && name[1] == '.' && name[2] == '\0';
}

static int menuList(const char *folder, unsigned long first, int wholeCard,
                    struct MenuPage *page) {
  const volatile unsigned char *buffer = glueCartStatus + CART_BUFFER_OFFSET;
  long sent = glueCartCommandWrite(CART_CMD_LIST, first,
                                   wholeCard ? CART_LIST_WHOLE_CARD : 0, 0,
                                   folder, (long)menuLength(folder) + 1);
  if (sent != CART_OK ||
      *(volatile long *)(glueCartStatus + CART_RESULT_OFFSET) !=
          CART_RESULT_OK) {
    TRACE("menu list %s failed %ld", folder, sent);
    return 0;
  }
  page->total = *(const volatile unsigned long *)(buffer + CART_LIST_TOTAL);
  page->count = *(const volatile unsigned long *)(buffer + CART_LIST_RETURNED);
  if (page->count > CART_LIST_PAGE) {
    page->count = CART_LIST_PAGE;
  }
  for (unsigned long i = 0; i < page->count; ++i) {
    const volatile unsigned char *entry =
        buffer + CART_LIST_ENTRIES + (i * CART_LIST_ENTRY_SIZE);
    menuCopy(page->names[i], entry, CART_PATH_SIZE);
    page->flags[i] =
        *(const volatile unsigned long *)(entry + CART_ENTRY_FLAGS);
  }
  return 1;
}

static void menuEntry(const struct MenuPage *page, unsigned long index,
                      int selected) {
  textInverse(selected);
  textClearRow((unsigned short)(kListRow + index));
  textAt(1, (unsigned short)(kListRow + index));
  menuPath(page->names[index], kLineWidth - 2);
  if (page->flags[index] & CART_ENTRY_FOLDER) {
    textPuts("/");
  }
  textInverse(0);
}

/* md-drives-emulator's browser (emul.c:1282-1365) for a setting: Up and
   Down select, Left and Right page, Return enters a folder, SPACE saves the
   entry, Esc leaves. foldersOnly: choosing FOLDER ([L]), over the whole
   card. Returns after a save or Esc. */
static void menuBrowse(int key, const char *label, int foldersOnly) {
  struct MenuPage *const page = &MENU_SCRATCH->page;
  char folder[CART_PATH_SIZE];
  unsigned long first = 0;
  const char *notice = 0; /* shown under the page drawn next */
  unsigned long selected = 0;
  const char *start = foldersOnly ? "/" : menuSettings[CART_KEY_FOLDER];
  for (unsigned i = 0; i < CART_PATH_SIZE; ++i) {
    folder[i] = start[i];
    if (start[i] == '\0') {
      break;
    }
  }
  for (;;) {
    unsigned long shown;
    unsigned long pages;
    int code;
    if (!menuList(folder, first, foldersOnly, page)) {
      menuStatus("The Multi-device cannot list this folder. Press a key.");
      while (kbdPoll() < 0) {
      }
      return;
    }
    TRACE("menu list %s %lu of %lu", folder, page->count, page->total);
    shown = page->count;
    if (foldersOnly) {
      /* Folders come first: the page's folders only. */
      for (shown = 0;
           shown < page->count && (page->flags[shown] & CART_ENTRY_FOLDER) != 0;
           ++shown) {
      }
    }
    if (selected >= shown) {
      selected = shown == 0 ? 0 : shown - 1;
    }
    pages = (page->total + CART_LIST_PAGE - 1) / CART_LIST_PAGE;
    menuTitle();
    textAt(0, kHeaderRow);
    textPuts(label);
    menuPath(folder, kLineWidth - menuLength(label));
    textAt(0, kCountRow);
    textPrintf("%s found: %lu. Page %lu of %lu",
               foldersOnly ? "Entries" : "Files", page->total,
               (first / CART_LIST_PAGE) + 1, pages == 0 ? 1 : pages);
    for (unsigned long i = 0; i < shown; ++i) {
      menuEntry(page, i, i == selected);
    }
    textAt(0, kFooterRow);
    textPuts("[UP] and [DOWN] to select. [LEFT] and [RIGHT] to paginate.\n");
    textPuts(foldersOnly ? "[RETURN] to open a folder. [SPACE] to choose it "
                           "('..': this one). [ESC] to exit."
                         : "[RETURN] to open a folder. [SPACE] to confirm. "
                           "[ESC] to exit.");
    if (notice != 0) {
      menuStatus(notice);
      notice = 0;
    }
    for (;;) {
      const int pressed = kbdPoll();
      if (pressed < 0) {
        continue;
      }
      code = pressed & KBD_CODE_MASK;
      if (code == KBD_UP && selected > 0) {
        menuEntry(page, selected, 0);
        menuEntry(page, --selected, 1);
      } else if (code == KBD_DOWN && selected + 1 < shown) {
        menuEntry(page, selected, 0);
        menuEntry(page, ++selected, 1);
      } else if (code == KBD_LEFT || code == KBD_RIGHT || code == KBD_ESC ||
                 code == KBD_RETURN || code == KBD_ENTER || code == KBD_SPACE) {
        break;
      }
    }
    if (code == KBD_ESC) {
      return;
    }
    if (code == KBD_LEFT || code == KBD_RIGHT) {
      if (code == KBD_LEFT && first >= CART_LIST_PAGE) {
        first -= CART_LIST_PAGE;
      } else if (code == KBD_RIGHT && first + CART_LIST_PAGE < page->total) {
        first += CART_LIST_PAGE;
      }
      selected = 0;
      continue;
    }
    if (shown == 0) {
      continue;
    }
    if (code == KBD_RETURN || code == KBD_ENTER) {
      if (page->flags[selected] & CART_ENTRY_FOLDER) {
        if (menuIsDotDot(page->names[selected])) {
          menuParent(folder);
        } else if (!menuJoin(folder, folder, page->names[selected])) {
          notice = "The path is too long.";
          continue;
        }
        first = selected = 0;
      }
      continue;
    }
    /* SPACE: save the entry. */
    {
      char path[CART_PATH_SIZE];
      const int isFolder = (page->flags[selected] & CART_ENTRY_FOLDER) != 0;
      long result;
      if (isFolder != foldersOnly) {
        notice = (foldersOnly ? "Select a folder, not a file."
                              : "Select a file, not a folder.");
        continue;
      }
      if (menuIsDotDot(page->names[selected])) {
        /* On "..": the folder shown, the one the user went into (Diego,
           2026-10-19: saving its parent, as md-drives-emulator does, read
           as SPACE not working). */
        for (unsigned i = 0; i < CART_PATH_SIZE; ++i) {
          path[i] = folder[i];
        }
      } else if ((page->flags[selected] & CART_ENTRY_TOO_LONG) ||
                 !menuJoin(path, folder, page->names[selected])) {
        notice = "The path is too long.";
        continue;
      }
      if (!foldersOnly && menuInUse(key, path) != 0) {
        static char line[sizeof("That image is already in FD0.")];
        menuFormat(line, sizeof(line), "That image is already in %s.",
                   menuInUse(key, path));
        notice = line;
        continue;
      }
      result = menuSet(key, path);
      if (result == CART_RESULT_OK) {
        return;
      }
      notice = "The Multi-device refused it.";
    }
  }
}

/* md-drives-emulator's "Configure multiple images" (emul.c:1138-1174,
   1819-1845) for FD0. */
static void menuSlots(void) {
  for (;;) {
    int code;
    int pressed;
    int digit;
    int slot;
    menuTitle();
    textAt(0, kHeaderRow);
    textPuts("Configure multiple images\n\n");
    textPuts("Slot 1 uses the main FD0 image and cannot be edited here.\n\n");
    textPuts("  Slot 1 main: ");
    menuPath(menuSettings[1], kPathShown);
    for (int slot = kSlotFirstEditable; slot <= CART_FD0_SLOTS; ++slot) {
      textPrintf("\n  [%c] Slot %d: ",
                 slot == CART_FD0_SLOTS ? '0' : (char)('0' + slot), slot);
      menuPath(menuSettings[slot], kPathShown);
    }
    textAt(0, kFooterRow - 1);
    textPuts("[M] or SPACE for main menu\n");
    textPuts("Press 2..9 or 0 to choose slots 2..10,\n");
    textPuts("or SHIFT + 2..9 or 0 to unmount an image");
    /* The digits by the physical key and Shift as held, not through the
       layout: on an AZERTY keyboard the digits are the shifted layer, and
       the 5 key's "(" is US Shift+9 (EPIC-16 INPUT-02). */
    do { /* another key does nothing (Diego, 2026-10-20) */
      pressed = kbdPoll();
      code = pressed & KBD_CODE_MASK;
      digit = KBD_PHYS_CODE(pressed);
    } while (pressed < 0 ||
             !(code == KBD_M || code == KBD_SPACE || code == KBD_ESC ||
               (digit >= kScanDigitTwo && digit <= kScanDigitZero)));
    if (code == KBD_M || code == KBD_SPACE || code == KBD_ESC) {
      return;
    }
    slot = digit - kScanDigitTwo + kSlotFirstEditable;
    if (pressed & KBD_PHYS_SHIFT) {
      (void)menuSet(slot, "");
    } else {
      char label[sizeof("FD0 slot 10 file: ")];
      menuFormat(label, sizeof(label), "FD0 slot %d file: ", slot);
      menuBrowse(slot, label, 0);
    }
  }
}

/* A drive's line: its setting, and what md-jackintosh's unit table says
   the drive holds, so the menu shows the truth (Diego, 2026-10-19: "we are
   not showing truly what the configuration settings stores"). */
static void menuDrive(const char *label, int unit, int key) {
  const unsigned long flags = *(
      const volatile unsigned long *)(glueCartStatus + CART_UNIT_TABLE_OFFSET +
                                      (unit * CART_UNIT_ENTRY_BYTES) +
                                      CART_UNIT_FLAGS);
  const int inserted = (flags & CART_UNIT_FLAG_INSERTED) != 0;
  textPuts(label);
  menuPath(menuSettings[key], kPathShown);
  if (menuSettings[key][0] != '\0' && !inserted) {
    textPuts(" (not mounted)");
  } else if (menuSettings[key][0] == '\0' && inserted) {
    textPuts(" (a disk is mounted)");
  } else if (inserted && (flags & CART_UNIT_FLAG_READONLY)) {
    textPuts(" (read-only)");
  }
}

/* The keys of md-drives-emulator's text fields, by scancode (US places,
   as the menu's keys): digits, letters, '.', '-' and the keypad's. */
static int menuAscii(int code) {
  static const char kDigits[] = "1234567890-="; /* $02-$0D */
  static const char kTop[] = "qwertyuiop";      /* $10-$19 */
  static const char kHome[] = "asdfghjkl";      /* $1E-$26 */
  static const char kBottom[] = "zxcvbnm,./";   /* $2C-$35 */
  static const char kKeypad[] = "789456123";    /* $67-$6F */
  enum {
    kScanDigits = 0x02,
    kScanTop = 0x10,
    kScanHome = 0x1E,
    kScanBottom = 0x2C,
    kScanKeypadMinus = 0x4A,
    kScanKeypadPlus = 0x4E,
    kScanKeypad = 0x67,
    kScanKeypadZero = 0x70,
    kScanKeypadDot = 0x71
  };
  if (code >= kScanDigits && code < kScanDigits + (int)sizeof(kDigits) - 1) {
    return kDigits[code - kScanDigits];
  }
  if (code >= kScanTop && code < kScanTop + (int)sizeof(kTop) - 1) {
    return kTop[code - kScanTop];
  }
  if (code >= kScanHome && code < kScanHome + (int)sizeof(kHome) - 1) {
    return kHome[code - kScanHome];
  }
  if (code >= kScanBottom && code < kScanBottom + (int)sizeof(kBottom) - 1) {
    return kBottom[code - kScanBottom];
  }
  if (code >= kScanKeypad && code < kScanKeypad + (int)sizeof(kKeypad) - 1) {
    return kKeypad[code - kScanKeypad];
  }
  switch (code) {
    case kScanKeypadZero:
      return '0';
    case kScanKeypadDot:
      return '.';
    case kScanKeypadMinus:
      return '-';
    case kScanKeypadPlus:
      return '+';
    default:
      return 0;
  }
}

/* md-drives-emulator's text input (term.c:388-417, 497-510): the prompt,
   then the text echoed, Backspace, Return to take it, Esc to give up.
   Returns 1 with out filled, 0 on Esc. */
static int menuInput(const char *prompt, char *out, unsigned size) {
  unsigned length = 0;
  out[0] = '\0';
  textClearRow(kFooterRow - 1);
  textPuts(prompt);
  textPuts(" ");
  for (;;) {
    const int pressed = kbdPoll();
    int code;
    int character;
    if (pressed < 0) {
      continue;
    }
    code = pressed & KBD_CODE_MASK;
    if (code == KBD_ESC) {
      return 0;
    }
    if (code == KBD_RETURN || code == KBD_ENTER) {
      return 1;
    }
    if (code == KBD_BACKSPACE && length > 0) {
      out[--length] = '\0';
    } else {
      character = menuAscii(code);
      if (character == '=' && (pressed & KBD_SHIFT)) {
        character = '+';
      }
      if (character == 0 || length + 1 >= size) {
        continue;
      }
      out[length++] = (char)character;
      out[length] = '\0';
    }
    textClearRow(kFooterRow - 1);
    textPuts(prompt);
    textPuts(" ");
    textPuts(out);
  }
}

/* md-drives-emulator's isValidDomain (emul.c:397-437): labels of
   letters, digits and hyphens, none starting or ending with one. */
static int menuValidHost(const char *host) {
  enum { kLabelMax = 63, kHostMax = 95 };
  unsigned label = 0;
  unsigned length = 0;
  char last = '.';
  for (; host[length] != '\0'; ++length) {
    const char character = host[length];
    if (character == '.') {
      if (label == 0 || last == '-') {
        return 0;
      }
      label = 0;
    } else if ((character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               (character == '-' && label != 0)) {
      if (++label > kLabelMax) {
        return 0;
      }
    } else {
      return 0;
    }
    last = character;
  }
  return length != 0 && length <= kHostMax && last != '.' && last != '-';
}

/* A decimal number with an optional sign, or 0 when text is not one. */
static int menuNumber(const char *text, long *value) {
  const int negative = text[0] == '-';
  unsigned long result = 0;
  unsigned digits = 0;
  enum { kMaxDigits = 5 };
  if (text[0] == '-' || text[0] == '+') {
    ++text;
  }
  for (; *text >= '0' && *text <= '9' && digits < kMaxDigits;
       ++text, ++digits) {
    result = (result << 3) + (result << 1) + (unsigned long)(*text - '0');
  }
  if (*text != '\0' || digits == 0) {
    return 0;
  }
  *value = negative ? -(long)result : (long)result;
  return 1;
}

/* The Mac's time zone in hours and minutes (gmtDelta, mac.c). */
static void menuOffset(void) {
  enum { kSecondsPerHour = 3600, kSecondsPerMinute = 60 };
  const long delta = glueRtcGmtDelta();
  unsigned short rest = 0;
  const unsigned long hours = divU32U16(
      (unsigned long)(delta < 0 ? -delta : delta), kSecondsPerHour, &rest);
  textPrintf("%c%lu", delta < 0 ? '-' : '+', hours);
  if (rest != 0) {
    unsigned short seconds = 0;
    textPrintf(":%02lu", divU32U16(rest, kSecondsPerMinute, &seconds));
  }
}

/* md-drives-emulator's RTC lines (emul.c:917-953). [R], [H], [P] are
   md-jackintosh's (shown when it has them), [U] the Mac's own zone. */
static void menuRtcLines(void) {
  if (!menuHasRtc) {
    textPuts("\n\n[U] Offset:"); /* an older md-jackintosh: the Mac's zone */
    menuOffset();
    return;
  }
  if (!menuSame(menuSettings[CART_KEY_RTC_ENABLED], "true")) {
    textPuts("\n\n[R]TC Enabled? No"); /* nothing below, as md-drives */
    return;
  }
  textPuts("\n\n[R]TC Enabled? Yes\n  [H]ost NTP: ");
  textPuts(menuSettings[CART_KEY_NTP_HOST][0] != '\0'
               ? menuSettings[CART_KEY_NTP_HOST]
               : "Not set");
  textPrintf("\n  [P]ort:%s [U] Offset:", menuSettings[CART_KEY_NTP_PORT]);
  menuOffset();
}

/* Whether [R], [H], [P] or [U] is on the main menu (menuRtcLines). */
static int menuRtcShown(int code) {
  const int enabled =
      !menuHasRtc || menuSame(menuSettings[CART_KEY_RTC_ENABLED], "true");
  return (code == KBD_U && enabled) || (code == KBD_R && menuHasRtc) ||
         ((code == KBD_H || code == KBD_P) && menuHasRtc && enabled);
}

/* [R], [H], [P], [U]: md-drives-emulator's prompts, checks and messages.
   Returns the status line to show, or 0. */
static const char *menuRtcKey(int code) {
  enum {
    kSecondsPerHour = 3600,
    kOffsetMin = -12,
    kOffsetMax = 14,
    kPortMax = 65535
  };
  /* md-jackintosh reads them only when it starts: the computer's reset
     button restarts the ST, not the Multi-device (its session, 2026-10-20). */
  static const char kApplies[] =
      "Saved: applies after a power cycle or Multi-device reset, not the reset "
      "button.";
  char text[CART_PATH_SIZE];
  long value = 0;
  const int enabled =
      !menuHasRtc || menuSame(menuSettings[CART_KEY_RTC_ENABLED], "true");
  if (code == KBD_U && enabled) {
    if (!menuInput("Enter the UTC offset:", text, sizeof(text))) {
      return 0;
    }
    if (!menuNumber(text, &value) || value < kOffsetMin || value > kOffsetMax) {
      return "Invalid UTC offset.";
    }
    glueRtcSetGmtDelta(value * kSecondsPerHour);
    TRACE("menu offset %ld", value);
    return 0;
  }
  if (!menuHasRtc) {
    return 0;
  }
  if (code == KBD_R) {
    return menuSet(CART_KEY_RTC_ENABLED, enabled ? "false" : "true") ==
                   CART_RESULT_OK
               ? kApplies
               : "The Multi-device refused it.";
  }
  if (!enabled) {
    return 0; /* md-drives-emulator prompts only with the RTC enabled */
  }
  if (code == KBD_H) {
    if (!menuInput("Enter the NTP server host:", text, sizeof(text))) {
      return 0;
    }
    if (!menuValidHost(text) ||
        menuSet(CART_KEY_NTP_HOST, text) != CART_RESULT_OK) {
      return "Invalid NTP server host.";
    }
    return kApplies;
  }
  if (code == KBD_P) {
    if (!menuInput("Enter the NTP server port:", text, sizeof(text))) {
      return 0;
    }
    if (!menuNumber(text, &value) || value < 1 || value > kPortMax ||
        text[0] == '-' || text[0] == '+' ||
        menuSet(CART_KEY_NTP_PORT, text) != CART_RESULT_OK) {
      return "Invalid NTP server port.";
    }
    return kApplies;
  }
  return 0;
}

static void menuMain(void) {
  menuTitle();
  textAt(0, kHeaderRow);
  textPuts("Fo[l]der: ");
  menuPath(menuSettings[CART_KEY_FOLDER], kPathShown);
  textPuts("\n\n");
  if (menuSlot != 1) {
    char label[sizeof("[A] FD0 (slot 10): ")];
    menuFormat(label, sizeof(label), "[A] FD0 (slot %d): ", menuSlot);
    menuDrive(label, 0, menuSlot);
  } else {
    menuDrive("[A] FD0: ", 0, menuSlot);
  }
  textPuts("\n[CTRL+A] Configure multiple images\n");
  menuDrive("[B] FD1: ", 1, CART_KEY_FD1);
  textPuts("\n");
  menuDrive("[I] HD0: ", CART_UNIT_HD0, CART_KEY_HD0);
  menuRtcLines();
  textPuts("\n\nSHIFT+A, SHIFT+B or SHIFT+I empties a drive.");
  if (menuHasOptions) {
    textPuts(
        "\n\n[O]ptions: countdown, Mac RAM, right button, screen, keyboard, "
        "mouse");
    if (screenIsSte()) {
      textPuts(", audio");
    }
    if (glueMegaSte) {
      textPuts(", CPU");
    }
  }
  textAt(0, kFooterRow);
  textPuts("[E]xit to the Mac    Select an option:");
}

/* The countdown's values, in the order [C] steps them: no 0, as Diego
   asked ("minimum value is 5. Maximum value 30"); "" is the default 20. */
static const char *const kCountdowns[] = {"30", "20", "10", "5", 0};

/* The keyboard's values, in GLUE_LAYOUT_* order ("" is US), and its names. */
static const char *const kKeyboards[] = {"", "fr", "de", "es", "uk", 0};
static const char *const kKeyboardNames[] = {"US", "French", "German",
                                             "Spanish", "UK"};

/* The Mega STE's speed and cache (EPIC-14), the control panel's three: ""
   is 8 MHz, as a reset leaves it. The hardware keeps the cache off at 8 MHz
   (md-drives-emulator's bench, its tests/atarist/src/test_runner.c). */
static const char *const kMegaSte[] = {"", "16", "16c", 0};
static const char *const kMegaSteNames[] = {"8 MHz", "16 MHz, cache off",
                                            "16 MHz, cache on"};
static const unsigned char kMegaSteCtrl[] = {0, HW_MSTE_16MHZ,
                                             HW_MSTE_16MHZ | HW_MSTE_CACHE};

/* The ROM's options from md-jackintosh into glueOptions, and the
   countdown's seconds (EPIC-12). */
static int menuApplyOptions(void) {
  enum { kRamMinKilobytes = 512 };
  const char *countdown = menuOption(CART_OPT_COUNTDOWN);
  long value = 0;
  glueOptions.ramKilobytes = 0;
  if (menuNumber(menuOption(CART_OPT_RAM), &value) &&
      value >= kRamMinKilobytes) {
    glueOptions.ramKilobytes = (unsigned long)value;
  }
  glueOptions.plusScreen = menuSame(menuOption(CART_OPT_SCREEN), "512x342");
  glueOptions.keyboard = GLUE_LAYOUT_US;
  for (int i = 1; kKeyboards[i] != 0; ++i) {
    if (menuSame(menuOption(CART_OPT_KEYBOARD), kKeyboards[i])) {
      glueOptions.keyboard = i;
    }
  }
  glueOptions.megaSteCtrl = 0;
  glueOptions.megaSteIndex = 0;
  for (int i = 1; kMegaSte[i] != 0; ++i) {
    if (menuSame(menuOption(CART_OPT_MEGASTE), kMegaSte[i])) {
      glueOptions.megaSteCtrl = kMegaSteCtrl[i];
      glueOptions.megaSteIndex = (unsigned char)i;
    }
  }
  glueOptions.sound = GLUE_SOUND_FAST;
  if (menuSame(menuOption(CART_OPT_SOUND), "off")) {
    glueOptions.sound = GLUE_SOUND_OFF;
  } else if (menuSame(menuOption(CART_OPT_SOUND), "smooth")) {
    glueOptions.sound = GLUE_SOUND_SMOOTH;
  }
  glueOptions.mouseSpeed = GLUE_MOUSE_SPEED_DEFAULT;
  if (menuNumber(menuOption(CART_OPT_MOUSE), &value) && value >= 1 &&
      value <= GLUE_MOUSE_SPEED_MAX) {
    glueOptions.mouseSpeed = (int)value;
  }
  glueOptions.rightButton = GLUE_RIGHT_OFF;
  if (menuSame(menuOption(CART_OPT_RIGHT), "cmd")) {
    glueOptions.rightButton = GLUE_RIGHT_COMMAND;
  } else if (menuSame(menuOption(CART_OPT_RIGHT), "double")) {
    glueOptions.rightButton = GLUE_RIGHT_DOUBLE;
  }
  for (int i = 0; kCountdowns[i] != 0; ++i) {
    if (menuSame(countdown, kCountdowns[i]) && menuNumber(countdown, &value)) {
      return (int)value; /* 30, 20, 10 or 5 (Diego, 2026-10-20) */
    }
  }
  return kCountdownSeconds;
}

/* The next of values after current (values ends with 0), wrapping. */
static const char *menuNext(const char *const *values, const char *current) {
  int index = 0;
  while (values[index] != 0 && !menuSame(values[index], current)) {
    ++index;
  }
  return values[index] != 0 && values[index + 1] != 0 ? values[index + 1]
                                                      : values[0];
}

/* The options page: each key steps its option through its values. */
static void menuOptionsPage(void) {
  static const char *const kRams[] = {"", "1024", "2048", "2560", 0};
  static const char *const kRights[] = {"", "cmd", "double", 0};
  static const char *const kScreens[] = {"", "512x342", 0};
  enum { kScanC = 0x2E, kScanK = 0x25, kScanT = 0x14 };
  const int megaSteKey = glueMegaSte ? KBD_P : -1;
  const int soundKey = screenIsSte() ? KBD_A : -1; /* DMA sound */
  for (;;) {
    const char *right = menuOption(CART_OPT_RIGHT);
    int pressed;
    int code;
    menuTitle();
    textAt(0, kHeaderRow);
    textPuts("Options\n\n[C]ountdown at boot: ");
    textPuts(menuOption(CART_OPT_COUNTDOWN)[0] != '\0'
                 ? menuOption(CART_OPT_COUNTDOWN)
                 : "20");
    textPuts(" seconds\n[M]ac RAM: ");
    if (menuOption(CART_OPT_RAM)[0] != '\0') {
      textPrintf("%s KB", menuOption(CART_OPT_RAM));
    } else {
      textPuts("all of it");
    }
    textPuts("\n[R]ight mouse button: ");
    if (menuSame(right, "cmd")) {
      textPuts("Command-click");
    } else if (menuSame(right, "double")) {
      textPuts("double-click");
    } else {
      textPuts("off");
    }
    textPuts("\n[S]creen: ");
    textPuts(glueOptions.plusScreen ? "512x342 (the Plus's), centred"
                                    : "640x400");
    textPuts("\n[K]eyboard (EXPERIMENTAL): ");
    textPuts(kKeyboardNames[glueOptions.keyboard]);
    textPrintf("\nMouse [T]racking: %d (1 slowest, 10 fastest; SHIFT+T down)",
               glueOptions.mouseSpeed);
    if (screenIsSte()) {
      textPuts("\n[A]udio (EXPERIMENTAL): ");
      if (glueOptions.sound == GLUE_SOUND_SMOOTH) {
        textPuts("smooth (more CPU)");
      } else {
        textPuts(glueOptions.sound == GLUE_SOUND_FAST ? "fast" : "off");
      }
    }
    if (glueMegaSte) {
      textPuts("\nMega STE [P]rocessor: ");
      textPuts(kMegaSteNames[glueOptions.megaSteIndex]);
    }
    textAt(0, kFooterRow);
    textPuts("Each key steps its option. They apply from this boot on.\n");
    textPuts("[SPACE] or [ESC] for the main menu");
    do { /* another key does nothing */
      pressed = kbdPoll();
      code = pressed & KBD_CODE_MASK;
    } while (pressed < 0 ||
             !(code == KBD_SPACE || code == KBD_ESC || code == kScanC ||
               code == KBD_M || code == KBD_R || code == KBD_S ||
               code == kScanK || code == kScanT || code == megaSteKey ||
               code == soundKey));
    if (code == KBD_SPACE || code == KBD_ESC) {
      return;
    }
    if (code == kScanC) {
      const char *countdown = menuOption(CART_OPT_COUNTDOWN);
      (void)menuSet(
          CART_OPT_COUNTDOWN,
          menuNext(kCountdowns, countdown[0] != '\0' ? countdown : "20"));
    } else if (code == KBD_M) {
      (void)menuSet(CART_OPT_RAM, menuNext(kRams, menuOption(CART_OPT_RAM)));
    } else if (code == KBD_R) {
      (void)menuSet(CART_OPT_RIGHT, menuNext(kRights, right));
    } else if (code == KBD_S) {
      (void)menuSet(CART_OPT_SCREEN,
                    menuNext(kScreens, menuOption(CART_OPT_SCREEN)));
    } else if (code == soundKey) {
      static const char *const kSounds[] = {"", "smooth", "off", 0};
      (void)menuSet(CART_OPT_SOUND,
                    menuNext(kSounds, menuOption(CART_OPT_SOUND)));
    } else if (code == megaSteKey) {
      (void)menuSet(CART_OPT_MEGASTE,
                    menuNext(kMegaSte, menuOption(CART_OPT_MEGASTE)));
    } else if (code == kScanT) {
      /* 1-10, wrapping: T up, SHIFT+T down. */
      char speed[sizeof("10")];
      int next = glueOptions.mouseSpeed + ((pressed & KBD_SHIFT) ? -1 : 1);
      if (next < 1) {
        next = GLUE_MOUSE_SPEED_MAX;
      } else if (next > GLUE_MOUSE_SPEED_MAX) {
        next = 1;
      }
      menuFormat(speed, sizeof(speed), "%d", next);
      (void)menuSet(CART_OPT_MOUSE, speed);
    } else if (code == kScanK) {
      (void)menuSet(CART_OPT_KEYBOARD,
                    menuNext(kKeyboards, menuOption(CART_OPT_KEYBOARD)));
    }
    (void)menuApplyOptions();
  }
}

void glueMenu(void) {
  int seconds = kCountdownSeconds;
  int frames = 0;
  int counting = 1;
  const char *notice = 0;
  if (!menuGetSettings()) {
    TRACE("menu none"); /* a firmware without the chooser's commands */
    return;
  }
  if (menuHasRtc && !menuSame(menuSettings[CART_KEY_RTC_ENABLED], "true")) {
    glueCartNoTime(); /* md-jackintosh's TIME stays not valid */
  }
  kbdInit();
  seconds = menuApplyOptions();
  TRACE("menu");
  TRACE("menu countdown %d", seconds);
  menuMain();
  if (counting) {
    char line[sizeof("Boot will continue in 20 seconds...")];
    menuFormat(line, sizeof(line), "Boot will continue in %d seconds...",
               seconds);
    menuStatus(line);
  } else {
    menuStatus("Countdown stopped. Press [E] to continue.");
  }
  for (;;) {
    const int pressed = kbdPoll();
    int code;
    if (pressed < 0) {
      if (counting && screenNewFrame() && ++frames >= kFramesPerSecond) {
        char line[sizeof("Boot will continue in 20 seconds...")];
        frames = 0;
        if (--seconds == 0) {
          break;
        }
        menuFormat(line, sizeof(line), "Boot will continue in %d seconds...",
                   seconds);
        menuStatus(line);
      }
      continue;
    }
    code = pressed & KBD_CODE_MASK;
    TRACE("menu key %x", (unsigned)pressed);
    if (code == KBD_E && !(pressed & (KBD_SHIFT | KBD_CONTROL))) {
      break;
    }
    if (!(code == KBD_A || code == KBD_B || code == KBD_I || code == KBD_L ||
          menuRtcShown(code) || (code == KBD_O && menuHasOptions))) {
      /* Not a menu key: it only stops the countdown (Diego, 2026-10-20:
         the menu redrawn on any key was annoying). */
      if (counting) {
        counting = 0;
        menuStatus("Countdown stopped. Press [E] to continue.");
      }
      continue;
    }
    counting = 0;
    if (code == KBD_A && (pressed & KBD_CONTROL)) {
      menuSlots();
    } else if (code == KBD_A) {
      if (pressed & KBD_SHIFT) {
        (void)menuSet(menuSlot, "");
      } else {
        menuBrowse(menuSlot, "FD0 file: ", 0);
      }
    } else if (code == KBD_B) {
      if (pressed & KBD_SHIFT) {
        (void)menuSet(CART_KEY_FD1, "");
      } else {
        menuBrowse(CART_KEY_FD1, "FD1 file: ", 0);
      }
    } else if (code == KBD_I) {
      if (pressed & KBD_SHIFT) {
        (void)menuSet(CART_KEY_HD0, "");
      } else {
        menuBrowse(CART_KEY_HD0, "HD0 file: ", 0);
      }
    } else if (code == KBD_L) {
      menuBrowse(CART_KEY_FOLDER, "Folder: ", 1);
    } else if (code == KBD_O) {
      menuOptionsPage();
    } else {
      notice = menuRtcKey(code);
    }
    menuMain();
    menuStatus(notice != 0 ? notice
                           : "Countdown stopped. Press [E] to continue.");
    notice = 0;
  }
  menuStatus("Booting... Please wait...");
  TRACE("menu boot");
}
