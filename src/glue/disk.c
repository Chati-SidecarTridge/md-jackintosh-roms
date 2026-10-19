/**
 * File: src/glue/disk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's Mac disk driver (EPIC-03 STORY-01): installed at
 * unit 4 (.Sony's slot, refNum -5) with the backend's drives, 1 and up
 * (EPIC-09 STORY-01: a second floppy), on a backend (disk.h). The
 * contract it meets is docs/macplus/boot-map.md 5.4: Prime moves whole
 * 512-byte blocks at dCtlPosition and ends through IODone, Status 8 returns
 * the drive status record (_MountVol reads its write-protect bit), Control 7
 * ejects. A medium with an Apple partition map is served as its first
 * Apple_HFS partition. Each call names its drive in ioVRefNum. A disk in a
 * drive other than the startup one is announced with a disk-inserted event,
 * as .Sony does, so the System mounts it. The DRVR header and the entry
 * stubs are in macglue.s.
 */

#include "disk.h"

#include "cart.h"
#include "macrom.h"
#include "trace.h"

extern long glueDrvrInstall(long refNum);
extern void glueAddDrive(long driveAndRefNum, void *dqe);
extern unsigned short glueDriver[];
extern void *glueNewPtrSysClear(unsigned long size);
extern void glueInsTime(void *task);
extern void glueRts(void);
extern long gluePostDiskInserted(long drive);

enum {
  kDceOpened = 0x20, /* dCtlFlags+1 bit 5 */
  kDcePosition = 16,
  kDriveNumberShift = 16,
  kTypeDiskInPlace = 1, /* a floppy with a disk */
  kTypeFixed = 8,       /* a fixed disk: offline only, never ejected */
  kFlagWriteProtect = 0x80,
  kFlagInstalled = 1,
  kFlagDoubleSided = 0xFF,
  kSizeHighShift = 16,
  kSizeLowMask = 0xFFFF,
  /* Parameter-block fields (IOParam, CntrlParam). */
  kIoTrap = 6,
  kIoVRefNum = 0x16, /* the drive number, for a driver call */
  kIoBuffer = 0x20,
  kIoReqCount = 0x24,
  kIoActCount = 0x28,
  kIoCsCode = 0x1A,
  kIoCsParam = 0x1C,
  kTrapWrite = 3, /* the low byte of _Write ($A003) */
  kTrapLowMask = 0xFF,
  kCsKillIo = 1,
  kCsVerify = 5,
  kCsFormat = 6,
  kCsEject = 7,
  kCsTagBuffer = 8,
  kCsTrackCache = 9,
  kCsDriveStatus = 8,
  kBlockMask = DISK_BLOCK_BYTES - 1,
  kBlockShift = 9,
  kTracedTransfers = 32,
  kDrvStsFlags = 2,
  kDrvStsFlagsAndQueue =
      16, /* the flags, qLink, qType, dQDrive, dQRefNum, dQFSID */
  kDrvStsTwoSided = 18,
  kUnsetLong = 0xFFFFFFFFUL /* the boot's fill */
};

#define FIELD_LONG(pb, off) (*(unsigned long *)((pb) + (off)))
#define LOWMEM_LONG(addr) (*(volatile unsigned long *)(unsigned long)(addr))
#define FIELD_SHORT(pb, off) (*(short *)((pb) + (off)))

/* The drive queue element and the four bytes before it: write-protected
   (bit 7), drive type / disk in place, installed, sides. A struct keeps them
   together; _AddDrive gets the address of qLink. One per drive. */
static struct {
  unsigned char flags[4];
  unsigned long qLink;
  unsigned short qType;
  unsigned short dQDrive;
  short dQRefNum;
  unsigned short dQFSID;
  unsigned short dQDrvSz;
  unsigned short dQDrvSz2;
} glueDrives[DISK_DRIVES];

static int glueDriveCount;
/* The backend chosen at glueInstallDisk (EPIC-05 STORY-07). */
static const struct DiskBackend *glueBackend = &diskNoBackend;
/* Disks to announce once the event queue exists (glueDiskTick). */
static unsigned char glueInsertPending[DISK_DRIVES];
static int glueTransfersTraced;
static int glueOfflineTraced;

/* The volume inside the medium: the whole medium, or the first Apple_HFS
   partition of a disk with an Apple partition map (a driver descriptor
   "ER" in block 0, "PM" entries from block 1), as md-jackintosh serves an
   HD0 image raw (EPIC-06). */
static unsigned long glueVolumeStart[DISK_DRIVES];
static unsigned long glueVolumeBlocks[DISK_DRIVES];
static unsigned char glueBlockBuffer[DISK_BLOCK_BYTES];

enum {
  kDdmSignature = 0x4552, /* "ER" */
  kPmSignature = 0x504D,  /* "PM" */
  kPmMapBlocks = 4,       /* the number of map entries */
  kPmStart = 8,
  kPmBlocks = 12,
  kPmType = 48,
  kPmMaxEntries = 64,
  kByteShift = 8,
  kWordShiftBits = 16,
  kByte3Shift = 24
};

static unsigned long glueLong(const unsigned char *bytes) {
  return ((unsigned long)bytes[0] << kByte3Shift) |
         ((unsigned long)bytes[1] << kWordShiftBits) |
         ((unsigned long)bytes[2] << kByteShift) | bytes[3];
}

static unsigned glueWord(const unsigned char *bytes) {
  return ((unsigned)bytes[0] << kByteShift) | bytes[1];
}

static int glueIsAppleHfs(const unsigned char *type) {
  static const char kHfs[] = "Apple_HFS";
  for (unsigned i = 0; i < sizeof(kHfs); ++i) {
    if (type[i] != (unsigned char)kHfs[i]) {
      return 0;
    }
  }
  return 1;
}

static void glueVolumeLocate(int drive) {
  struct DiskInfo info;
  unsigned long entries = 1;
  glueBackend->info(drive, &info);
  glueVolumeStart[drive] = 0;
  glueVolumeBlocks[drive] = info.blocks;
  if (!info.present ||
      glueBackend->read(drive, 0, 1, glueBlockBuffer) != MAC_NOERR ||
      glueWord(glueBlockBuffer) != kDdmSignature) {
    return;
  }
  for (unsigned long i = 1; i <= entries && i <= kPmMaxEntries; ++i) {
    if (glueBackend->read(drive, i, 1, glueBlockBuffer) != MAC_NOERR ||
        glueWord(glueBlockBuffer) != kPmSignature) {
      break;
    }
    entries = glueLong(glueBlockBuffer + kPmMapBlocks);
    if (glueIsAppleHfs(glueBlockBuffer + kPmType)) {
      glueVolumeStart[drive] = glueLong(glueBlockBuffer + kPmStart);
      glueVolumeBlocks[drive] = glueLong(glueBlockBuffer + kPmBlocks);
      TRACE("disk partition %lu blocks %lu", glueVolumeStart[drive],
            glueVolumeBlocks[drive]);
      return;
    }
  }
  TRACE("disk partition map without Apple_HFS");
}

static void glueDriveRefresh(int drive) {
  struct DiskInfo info;
  glueBackend->info(drive, &info);
  glueDrives[drive].flags[0] = info.writeProtected ? kFlagWriteProtect : 0;
  if (!info.present) {
    glueDrives[drive].flags[1] = 0;
  } else {
    glueDrives[drive].flags[1] = info.fixed ? kTypeFixed : kTypeDiskInPlace;
  }
  glueDrives[drive].flags[2] = kFlagInstalled;
  glueDrives[drive].flags[3] = kFlagDoubleSided;
  glueDrives[drive].qType = 1; /* dQDrvSz2 holds the size's high word */
  glueDrives[drive].dQDrvSz =
      (unsigned short)(glueVolumeBlocks[drive] & kSizeLowMask);
  glueDrives[drive].dQDrvSz2 =
      (unsigned short)(glueVolumeBlocks[drive] >> kSizeHighShift);
}

/* The backend's index of the drive a call names (ioVRefNum, 1 and up), or
   -1 for a drive the driver does not have. */
static int glueCallDrive(const unsigned char *block) {
  const int number = FIELD_SHORT(block, kIoVRefNum);
  return (number >= 1 && number <= glueDriveCount) ? number - 1 : -1;
}

void glueInstallDisk(void) {
  const unsigned long *table =
      *(unsigned long *const *)(unsigned long)MAC_UTABLEBASE;
  unsigned char *dce;
  unsigned long *sonyVars;
  long err = glueDrvrInstall(MAC_SONY_REFNUM);

  /* _DrvrInstall made a DCE in the system heap; its handle is in the unit
     table. Point it at our driver directly (not RAM-based) and mark it
     open, as _Open does for a driver in ROM
     (plus/os/device_mgr.s:480-500). */
  dce = (unsigned char *)*(unsigned long *)table[MAC_SONY_UNIT];
  *(unsigned long *)dce = (unsigned long)glueDriver;
  *(unsigned short *)(dce + MAC_DCE_FLAGS) = glueDriver[0];
  dce[MAC_DCE_FLAGS + 1] = kDceOpened;

  /* What .Sony's open leaves for the System (res_drvr_sony.s:99-104):
     SonyVars, a cleared block with the DCE at offset 0 (System 6's PTCH 117
     writes its hooks into it: EPIC-03 STORY-04), and unit 1 aliased to
     unit 4 (refNum -2). */
  sonyVars = (unsigned long *)glueNewPtrSysClear(MAC_SONYVARS_BYTES);
  sonyVars[0] = (unsigned long)dce;
  *(unsigned long **)(unsigned long)MAC_SONYVARS = sonyVars;
  ((unsigned long *)table)[1] = table[MAC_SONY_UNIT];
  /* .Sony's Time Manager task, queued but never primed: System 6's PTCH 117
     moves the queued tasks into its own Time Manager and takes the first
     one without checking for an empty queue (res_drvr_sony.s:131-133). */
  *(unsigned long *)((unsigned char *)sonyVars + MAC_SONY_TIMEQEL +
                     MAC_TMADDR) = (unsigned long)glueRts;
  glueInsTime((unsigned char *)sonyVars + MAC_SONY_TIMEQEL);

  glueTransfersTraced = 0;
  glueOfflineTraced = 0;
  TRACE("mac sony %ld dce %06lx", err, (unsigned long)dce);
  (void)err; /* traced only in debug builds */
  if (glueCartState == CART_STATE_OK) {
    glueBackend = &diskCartBackend;
  } else {
#if defined(_TEST) && (_TEST > 0)
    glueBackend = &diskRamBackend;
#else
    glueBackend = &diskNoBackend;
#endif
  }
  TRACE("disk backend %s", glueBackend->name);
  glueDriveCount = glueBackend->drives();
  for (int drive = 0; drive < glueDriveCount; ++drive) {
    glueVolumeLocate(drive);
    glueDriveRefresh(drive);
    glueDrives[drive].dQFSID = 0;
    glueAddDrive(((long)(drive + 1) << kDriveNumberShift) |
                     (unsigned short)MAC_SONY_REFNUM,
                 &glueDrives[drive].qLink);
    TRACE("disk drive %d type %d blocks %lu wp %d", drive + 1,
          glueDrives[drive].flags[1],
          ((unsigned long)glueDrives[drive].dQDrvSz2 << kSizeHighShift) |
              glueDrives[drive].dQDrvSz,
          glueDrives[drive].flags[0] != 0);
    /* The startup disk is mounted by the boot; any other disk is announced
       as .Sony announces an inserted disk, once there is an event queue. */
    glueInsertPending[drive] = drive > 0 && glueDrives[drive].flags[1] != 0;
  }
}

/* Every tick, from the glue's level-1 work (macglue.s), as .Sony polls its
   drives (res_drvr_sony.s:1948-1980): a pending disk is announced with a
   disk-inserted event once _InitEvents, which runs after InitIOMgr
   (plus/boot/boot_load.s:142), has made the event queue. */
void glueDiskTick(void) {
  int changed;
  if (LOWMEM_LONG(MAC_EVTBUFPTR) == kUnsetLong) {
    return;
  }
  /* A medium the backend has put in a drive since (a swap in test builds,
     md-jackintosh's media change later): announced like the others. */
  changed = glueBackend->changed();
  for (int drive = 0; drive < glueDriveCount; ++drive) {
    if (changed & (1 << drive)) {
      glueVolumeLocate(drive);
      glueDriveRefresh(drive);
      glueInsertPending[drive] = 1;
    }
  }
  for (int drive = 0; drive < glueDriveCount; ++drive) {
    if (glueInsertPending[drive] &&
        gluePostDiskInserted(drive + 1) == MAC_NOERR) {
      glueInsertPending[drive] = 0;
      TRACE("disk%d inserted", drive + 1);
    }
  }
}

/* Prime: ioReqCount bytes at dCtlPosition into or out of ioBuffer, whole
   blocks only. */
short glueDriverPrime(unsigned char *block, unsigned char *dce) {
  const int write = (FIELD_SHORT(block, kIoTrap) & kTrapLowMask) == kTrapWrite;
  const unsigned long position = FIELD_LONG(dce, kDcePosition);
  const unsigned long bytes = FIELD_LONG(block, kIoReqCount);
  void *buffer = (void *)FIELD_LONG(block, kIoBuffer);
  const int drive = glueCallDrive(block);
  struct DiskInfo info;
  short result;

  FIELD_LONG(block, kIoActCount) = 0;
  if (drive < 0) {
    return MAC_NSDRVERR;
  }
  glueBackend->info(drive, &info);
  if (!info.present) {
    if (!glueOfflineTraced) {
      glueOfflineTraced = 1;
      TRACE("mac read offline");
    }
    return MAC_OFFLINERR;
  }
  if ((position & kBlockMask) != 0 || (bytes & kBlockMask) != 0) {
    TRACE("disk unaligned %s %lu at %lu", write ? "write" : "read", bytes,
          position);
    return MAC_PARAMERR;
  }
  if ((position >> kBlockShift) + (bytes >> kBlockShift) >
      glueVolumeBlocks[drive]) {
    return MAC_PARAMERR;
  }
  if (write) {
    result =
        info.writeProtected
            ? MAC_WPRERR
            : glueBackend->write(
                  drive, glueVolumeStart[drive] + (position >> kBlockShift),
                  bytes >> kBlockShift, buffer);
  } else {
    result = glueBackend->read(
        drive, glueVolumeStart[drive] + (position >> kBlockShift),
        bytes >> kBlockShift, buffer);
  }
  if (glueTransfersTraced < kTracedTransfers) {
    ++glueTransfersTraced;
    if (drive == 0) {
      TRACE("disk %s %lu+%lu %d", write ? "write" : "read",
            position >> kBlockShift, bytes >> kBlockShift, result);
    } else {
      TRACE("disk%d %s %lu+%lu %d", drive + 1, write ? "write" : "read",
            position >> kBlockShift, bytes >> kBlockShift, result);
    }
  }
  if (result == MAC_NOERR) {
    FIELD_LONG(block, kIoActCount) = bytes;
    FIELD_LONG(dce, kDcePosition) = position + bytes;
  }
  return result;
}

short glueDriverControl(const unsigned char *block, const unsigned char *dce) {
  const short csCode = *(const short *)(block + kIoCsCode);
  const int drive = glueCallDrive(block);
  struct DiskInfo info;
  (void)dce;
  if (drive < 0) {
    return MAC_NSDRVERR;
  }
  switch (csCode) {
    case kCsKillIo:
    case kCsVerify:
    case kCsFormat:
    case kCsTagBuffer:
    case kCsTrackCache:
      return MAC_NOERR;
    case kCsEject:
      glueBackend->info(drive, &info);
      if (info.fixed) {
        return MAC_CONTROLERR;
      }
      glueBackend->eject(drive);
      glueDriveRefresh(drive);
      if (drive == 0) {
        TRACE("disk eject");
      } else {
        TRACE("disk%d eject", drive + 1);
      }
      return MAC_NOERR;
    default:
      return MAC_CONTROLERR;
  }
}

/* Status 8: the drive status record (DrvSts): the track (a word), the 4
   flag bytes (write-protected at csParam+2, which _MountVol reads), the
   queue element from qLink to dQFSID, then two-sided format, needs flush
   and disk errors. */
short glueDriverStatus(unsigned char *block, const unsigned char *dce) {
  unsigned char *dst = block + kIoCsParam;
  const int drive = glueCallDrive(block);
  const unsigned char *src;
  (void)dce;
  if (drive < 0) {
    return MAC_NSDRVERR;
  }
  if (FIELD_SHORT(block, kIoCsCode) != kCsDriveStatus) {
    return MAC_STATUSERR;
  }
  glueDriveRefresh(drive);
  src = glueDrives[drive].flags;
  dst[0] = 0; /* track */
  dst[1] = 0;
  for (int i = 0; i < kDrvStsFlagsAndQueue; ++i) {
    dst[kDrvStsFlags + i] = src[i];
  }
  dst[kDrvStsTwoSided] = kFlagDoubleSided;
  dst[kDrvStsTwoSided + 1] = 0; /* needs flush */
  dst[kDrvStsTwoSided + 2] = 0; /* disk errors */
  dst[kDrvStsTwoSided + 3] = 0;
  return MAC_NOERR;
}
