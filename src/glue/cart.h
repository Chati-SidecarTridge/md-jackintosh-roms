/**
 * File: src/glue/cart.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The SidecarTridge Multi-device's command protocol as the glue
 * speaks it with no TOS (EPIC-05): the cartridge window's layout (it must
 * match md-jackintosh's rp/src/include/chandler.h and
 * target/atarist/src/inc/sidecart_layout.s) and the senders in cart.s, a port
 * of the template's send_sync and send_write_sync. Shared by C and assembly.
 */

#pragma once

/* The command channel: each 16-bit word w goes out as a read of
   CART_ROM3_MIDDLE + w sign-extended, i.e. $FB0000 + (w XOR $8000). */
#define CART_ROM3_ADDR 0x00FB0000
#define CART_ROM3_MIDDLE (CART_ROM3_ADDR + 0x8000)
#define CART_MAGIC 0xABCD

/* The shared window (ROM4, read-only to the ST). */
#define CART_ROM4_ADDR 0x00FA0000
#define CART_SHARED_ADDR (CART_ROM4_ADDR + 0x2000)
#define CART_TOKEN_ADDR (CART_SHARED_ADDR + 4)      /* the token echoed */
#define CART_TOKEN_SEED_ADDR (CART_SHARED_ADDR + 8) /* the next token */
#define CART_SHARED_VARIABLES_ADDR (CART_SHARED_ADDR + 0x10)
#define CART_APP_FREE_ADDR (CART_SHARED_ADDR + 0x300) /* $FA2300 */

/* Commands chandler answers itself (app $FF). */
#define CART_CMD_SET_SHARED_VAR 0xFF00
#define CART_CMD_ST_HELLO 0xFF01

/* md-jackintosh's app $07 (its rp/src/include/macdisk.h, agreed for
   EPIC-05 with its session on 2026-10-19). The status block at APP_FREE:
   the last command's result, the time, the identity; the buffer after it,
   byte-swapped by the RP so the 68000 reads it in order. */
#define CART_CMD_TIME 0x0703       /* MACDISK_CMD_TIME */
#define CART_CMD_PRAM_READ 0x0704  /* MACDISK_CMD_PRAM_READ */
#define CART_CMD_PRAM_WRITE 0x0705 /* MACDISK_CMD_PRAM_WRITE */
#define CART_RESULT_OFFSET 0x00
#define CART_TIME_SECONDS_OFFSET 0x04 /* UTC, seconds since 1904-01-01 */
#define CART_TIME_FLAGS_OFFSET 0x08
#define CART_TIME_FLAG_VALID 0x1
#define CART_ID_OFFSET 0x0C
#define CART_ID_MAGIC 0x4A4B /* "JK", the high word */
#define CART_ID_VERSION 1    /* the contract's version, the low word */
#define CART_BUFFER_OFFSET 0x100
#define CART_PRAM_BYTES 256

/* The microSD card's state (md-jackintosh's MACDISK_CARD_*, added
   2026-10-19): PENDING until the RP's attempt is over, then OK or the
   reason it stopped (no disks, no PRAM, no NTP; its LED blinks). An older
   md-jackintosh leaves the long at 0. */
#define CART_CARD_OFFSET 0x40
#define CART_CARD_OK 0
#define CART_CARD_NONE 1       /* no card, or one that does not answer */
#define CART_CARD_UNREADABLE 2 /* no FAT or exFAT file system */
#define CART_CARD_NO_FOLDER 3  /* /jackintosh cannot be created */
#define CART_CARD_PENDING 4

/* The disk chooser's commands (md-jackintosh's MACDISK_CMD_LIST,
   _GET_SETTINGS, _SET_SETTING, agreed 2026-10-19 for EPIC-06). Paths are
   NUL-terminated in CART_PATH_SIZE bytes, names in code page 437. LIST
   (send_write_sync: d3 = the first entry, data = the folder) answers in the
   buffer: total, returned, flags, then CART_LIST_PAGE entries of
   CART_LIST_ENTRY_SIZE: the name, its flags, its size. GET_SETTINGS
   answers the settings' paths in key order, then FD0's current slot.
   SET_SETTING (send_write_sync: d3 = the key, data = the path, "" clears)
   saves it and, for a unit's image, mounts it at once. */
#define CART_CMD_LIST 0x0706
#define CART_CMD_GET_SETTINGS 0x0707
#define CART_CMD_SET_SETTING 0x0708
#define CART_PATH_SIZE 96
#define CART_LIST_MAX 256
#define CART_LIST_PAGE 16
#define CART_LIST_ENTRY_SIZE 104
#define CART_LIST_TOTAL 0x00 /* in the buffer */
#define CART_LIST_RETURNED 0x04
#define CART_LIST_FLAGS 0x08
#define CART_LIST_ENTRIES 0x0C
#define CART_LIST_MORE 0x1       /* the folder has more than CART_LIST_MAX */
#define CART_LIST_WHOLE_CARD 0x1 /* LIST's d4: "/" as the top, for FOLDER */
#define CART_ENTRY_FLAGS 96      /* in an entry, after the name */
#define CART_ENTRY_SIZE 100
#define CART_ENTRY_FOLDER 0x1
#define CART_ENTRY_RW 0x2       /* the name ends in .rw */
#define CART_ENTRY_TOO_LONG 0x4 /* its path does not fit CART_PATH_SIZE */
#define CART_FD0_SLOTS 10
#define CART_KEY_FOLDER 0
#define CART_KEY_FD1 11
#define CART_KEY_HD0 12
#define CART_SETTINGS 13 /* paths in GET_SETTINGS' answer */
#define CART_SETTINGS_SLOT (CART_SETTINGS * CART_PATH_SIZE) /* FD0's slot */
/* The RTC settings (EPIC-13, md-drives-emulator's RTC_ENABLED, RTC_NTP_HOST,
   RTC_NTP_PORT): strings after FD0's slot in GET_SETTINGS' answer. */
#define CART_KEY_RTC_ENABLED 13 /* "true" or "false" */
#define CART_KEY_NTP_HOST 14
#define CART_KEY_NTP_PORT 15 /* decimal */
#define CART_SETTINGS_RTC (CART_SETTINGS_SLOT + 4)
#define CART_SETTINGS_ALL 16
/* The ROM's options (EPIC-12, agreed 2026-10-20): keys 16-25, short
   strings md-jackintosh keeps without reading them ("" means the ROM's
   default), in GET_SETTINGS' answer after the RTC strings, behind a marker
   (an older firmware leaves stale bytes there). */
#define CART_KEY_OPTION 16
#define CART_OPTIONS 10
#define CART_OPTION_SIZE 16
#define CART_OPTIONS_MAGIC 0x4F505431 /* "OPT1" */
#define CART_SETTINGS_OPTIONS 1540    /* the marker */
#define CART_SETTINGS_OPTION_FIELDS 1544
#define CART_OPT_COUNTDOWN (CART_KEY_OPTION + 0) /* seconds: 0, 5, 10, 20 */
#define CART_OPT_RAM (CART_KEY_OPTION + 1)       /* the Mac's KB, "" all */
#define CART_OPT_RIGHT (CART_KEY_OPTION + 2)     /* off, cmd, double */
#define CART_OPT_SCREEN (CART_KEY_OPTION + 3)    /* 640x400, 512x342 */

/* The disks (macdisk.h's MACDISK_CMD_READ/WRITE/EJECT): d3 = unit | count
   << 16, d4 = the first sector; READ fills the buffer, WRITE carries the
   data. The unit table, kept current by the RP: four longs per unit. */
#define CART_CMD_DISK_READ 0x0700
#define CART_CMD_DISK_WRITE 0x0701
#define CART_CMD_DISK_EJECT 0x0702
#define CART_UNITS 3 /* FD0, FD1, HD0: the Mac's drives 1, 2, 3 */
#define CART_UNIT_HD0 2
#define CART_UNIT_TABLE_OFFSET 0x10
#define CART_UNIT_ENTRY_BYTES 0x10
#define CART_UNIT_FLAGS 0x0
#define CART_UNIT_SECTORS 0x4
#define CART_UNIT_CHANGES 0x8 /* moves on every insert and eject */
#define CART_UNIT_FLAG_INSERTED 0x1
#define CART_UNIT_FLAG_READONLY 0x2
#define CART_MAX_READ_SECTORS 32
#define CART_MAX_WRITE_SECTORS 2
#define CART_SECTOR_BYTES 512
#define CART_COUNT_SHIFT 16

/* Results in the status block (macdisk.h's MACDISK_OK, MACDISK_ERR_*). */
#define CART_RESULT_OK 0
#define CART_RESULT_PARAM (-1)
#define CART_RESULT_NOMEDIUM (-2)
#define CART_RESULT_RANGE (-3)
#define CART_RESULT_READONLY (-4)
#define CART_RESULT_IO (-5)

/* The wait for the token: a spin count around the check, about 1.5 s on an
   8 MHz 68000 (md-jackintosh: a write can keep the card busy up to 500 ms;
   to resize on its bench numbers). */
#define CART_TIMEOUT_SPINS 0x00040000

/* Results of the senders. */
#define CART_OK 0
#define CART_TIMEOUT (-1) /* no answer: no Multi-device, or it is busy */
#define CART_BUSY (-2)    /* another command is under way: not sent */

#ifndef __ASSEMBLER__
/* A command with up to 16 bytes of payload after the token: the template's
   d3, d4, d5, d6 (payloadBytes counts them: 0, 2, 4 ... 16). */
long glueCartSend(long command, long payloadBytes, unsigned long first,
                  unsigned long second, unsigned long third,
                  unsigned long fourth);
/* What the boot found (cart.c). */
enum CartState {
  CART_STATE_NONE = 0,  /* no answer: no Multi-device */
  CART_STATE_OTHER = 1, /* answered, but not md-jackintosh's contract */
  CART_STATE_OK = 2     /* md-jackintosh, a contract version we know */
};
extern int glueCartState;
/* The card's state when glueCartState is CART_STATE_OK: CART_CARD_OK, or
   NONE, UNREADABLE, NO_FOLDER. */
extern unsigned long glueCartCard;
/* The boot-time handshake (CMD_ST_HELLO, then the identity), the PRAM and
   the time, before the Mac starts; returns glueCartState. */
int glueCartBoot(void);
/* Every tick, from the glue's level-1 work: the time until it is known,
   the PRAM written back after the Mac changed it. */
void glueCartTick(void);
/* md-jackintosh's RTC is off (EPIC-13): its TIME never becomes valid, so
   glueCartTick stops asking. */
void glueCartNoTime(void);

/* A command and its answer, from md-jackintosh or, in test builds, the
   fake one (cart.c); the status block they answer in, and the most sectors
   a READ may ask for (fewer for the fake). */
long glueCartCommand(long command, long payloadBytes, unsigned long first,
                     unsigned long second, unsigned long third,
                     unsigned long fourth);
long glueCartCommandWrite(long command, unsigned long first,
                          unsigned long second, unsigned long third,
                          const void *data, long bytes);
extern volatile unsigned char *glueCartStatus;
extern int glueCartMaxReadSectors;
/* Test builds: the fake Multi-device takes a --swap image into its units
   (cart.c); called before the unit table is read. */
void glueCartFakePoll(void);

/* Whole sectors from the window into a Mac buffer (cart.s, the loops of
   md-drives-emulator's acsi.s). */
void glueCartCopy(void *dst, const volatile void *src, long bytes);

/* A command with the template's d3, d4, d5 and then bytes of data
   (send_write_sync). */
long glueCartSendWrite(long command, unsigned long first, unsigned long second,
                       unsigned long third, const void *data, long bytes);
#endif
