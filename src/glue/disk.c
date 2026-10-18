/**
 * File: src/glue/disk.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's Mac disk driver (EPIC-03 STORY-01): installed at
 * unit 4 (.Sony's slot, refNum -5) with drive 1, on a backend (disk.h). The
 * contract it meets is docs/macplus/boot-map.md 5.4: Prime moves whole
 * 512-byte blocks at dCtlPosition and ends through IODone, Status 8 returns
 * the drive status record (_MountVol reads its write-protect bit), Control 7
 * ejects. A medium with an Apple partition map is served as its first
 * Apple_HFS partition. The DRVR header and the entry stubs are in
 * macglue.s.
 */

#include "disk.h"

#include "macrom.h"
#include "trace.h"

extern long glueDrvrInstall(long refNum);
extern void glueAddDrive(long driveAndRefNum, void *dqe);
extern unsigned short glueDriver[];
extern void *glueNewPtrSysClear(unsigned long size);
extern void glueInsTime(void *task);
extern void glueRts(void);

enum {
  kDceOpened = 0x20, /* dCtlFlags+1 bit 5 */
  kDcePosition = 16,
  kDriveNumber = 1,
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
  kDrvStsTwoSided = 18
};

#define FIELD_LONG(pb, off) (*(unsigned long *)((pb) + (off)))
#define FIELD_SHORT(pb, off) (*(short *)((pb) + (off)))

/* The drive queue element and the four bytes before it: write-protected
   (bit 7), drive type / disk in place, installed, sides. A struct keeps them
   together; _AddDrive gets the address of qLink. */
static struct {
  unsigned char flags[4];
  unsigned long qLink;
  unsigned short qType;
  unsigned short dQDrive;
  short dQRefNum;
  unsigned short dQFSID;
  unsigned short dQDrvSz;
  unsigned short dQDrvSz2;
} glueDrive;

static int glueTransfersTraced;
static int glueOfflineTraced;

/* The volume inside the medium: the whole medium, or the first Apple_HFS
   partition of a disk with an Apple partition map (a driver descriptor
   "ER" in block 0, "PM" entries from block 1), as md-jackintosh serves an
   HD0 image raw (EPIC-06). */
static unsigned long glueVolumeStart;
static unsigned long glueVolumeBlocks;
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

static void glueVolumeLocate(void) {
  struct DiskInfo info;
  unsigned long entries = 1;
  diskBackendInfo(&info);
  glueVolumeStart = 0;
  glueVolumeBlocks = info.blocks;
  if (!info.present || diskBackendRead(0, 1, glueBlockBuffer) != MAC_NOERR ||
      glueWord(glueBlockBuffer) != kDdmSignature) {
    return;
  }
  for (unsigned long i = 1; i <= entries && i <= kPmMaxEntries; ++i) {
    if (diskBackendRead(i, 1, glueBlockBuffer) != MAC_NOERR ||
        glueWord(glueBlockBuffer) != kPmSignature) {
      break;
    }
    entries = glueLong(glueBlockBuffer + kPmMapBlocks);
    if (glueIsAppleHfs(glueBlockBuffer + kPmType)) {
      glueVolumeStart = glueLong(glueBlockBuffer + kPmStart);
      glueVolumeBlocks = glueLong(glueBlockBuffer + kPmBlocks);
      TRACE("disk partition %lu blocks %lu", glueVolumeStart, glueVolumeBlocks);
      return;
    }
  }
  TRACE("disk partition map without Apple_HFS");
}

static void glueDriveRefresh(void) {
  struct DiskInfo info;
  diskBackendInfo(&info);
  glueDrive.flags[0] = info.writeProtected ? kFlagWriteProtect : 0;
  if (!info.present) {
    glueDrive.flags[1] = 0;
  } else {
    glueDrive.flags[1] = info.fixed ? kTypeFixed : kTypeDiskInPlace;
  }
  glueDrive.flags[2] = kFlagInstalled;
  glueDrive.flags[3] = kFlagDoubleSided;
  glueDrive.qType = 1; /* dQDrvSz2 holds the size's high word */
  glueDrive.dQDrvSz = (unsigned short)(glueVolumeBlocks & kSizeLowMask);
  glueDrive.dQDrvSz2 = (unsigned short)(glueVolumeBlocks >> kSizeHighShift);
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

  glueVolumeLocate();
  glueDriveRefresh();
  glueDrive.dQFSID = 0;
  glueAddDrive((1L << kDriveNumberShift) | (unsigned short)MAC_SONY_REFNUM,
               &glueDrive.qLink);
  glueTransfersTraced = 0;
  glueOfflineTraced = 0;
  TRACE("mac sony %ld dce %06lx", err, (unsigned long)dce);
  TRACE(
      "disk drive %d type %d blocks %lu wp %d", kDriveNumber,
      glueDrive.flags[1],
      ((unsigned long)glueDrive.dQDrvSz2 << kSizeHighShift) | glueDrive.dQDrvSz,
      glueDrive.flags[0] != 0);
  (void)err; /* traced only in debug builds */
}

/* Prime: ioReqCount bytes at dCtlPosition into or out of ioBuffer, whole
   blocks only. */
short glueDriverPrime(unsigned char *block, unsigned char *dce) {
  const int write = (FIELD_SHORT(block, kIoTrap) & kTrapLowMask) == kTrapWrite;
  const unsigned long position = FIELD_LONG(dce, kDcePosition);
  const unsigned long bytes = FIELD_LONG(block, kIoReqCount);
  void *buffer = (void *)FIELD_LONG(block, kIoBuffer);
  struct DiskInfo info;
  short result;

  FIELD_LONG(block, kIoActCount) = 0;
  diskBackendInfo(&info);
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
  if ((position >> kBlockShift) + (bytes >> kBlockShift) > glueVolumeBlocks) {
    return MAC_PARAMERR;
  }
  if (write) {
    result = info.writeProtected
                 ? MAC_WPRERR
                 : diskBackendWrite(glueVolumeStart + (position >> kBlockShift),
                                    bytes >> kBlockShift, buffer);
  } else {
    result = diskBackendRead(glueVolumeStart + (position >> kBlockShift),
                             bytes >> kBlockShift, buffer);
  }
  if (glueTransfersTraced < kTracedTransfers) {
    ++glueTransfersTraced;
    TRACE("disk %s %lu+%lu %d", write ? "write" : "read",
          position >> kBlockShift, bytes >> kBlockShift, result);
  }
  if (result == MAC_NOERR) {
    FIELD_LONG(block, kIoActCount) = bytes;
    FIELD_LONG(dce, kDcePosition) = position + bytes;
  }
  return result;
}

short glueDriverControl(const unsigned char *block, const unsigned char *dce) {
  const short csCode = *(const short *)(block + kIoCsCode);
  struct DiskInfo info;
  (void)dce;
  switch (csCode) {
    case kCsKillIo:
    case kCsVerify:
    case kCsFormat:
    case kCsTagBuffer:
    case kCsTrackCache:
      return MAC_NOERR;
    case kCsEject:
      diskBackendInfo(&info);
      if (info.fixed) {
        return MAC_CONTROLERR;
      }
      diskBackendEject();
      glueDriveRefresh();
      TRACE("disk eject");
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
  const unsigned char *src = glueDrive.flags;
  (void)dce;
  if (FIELD_SHORT(block, kIoCsCode) != kCsDriveStatus) {
    return MAC_STATUSERR;
  }
  glueDriveRefresh();
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
