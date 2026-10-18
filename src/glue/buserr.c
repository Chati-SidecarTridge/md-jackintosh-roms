/**
 * File: src/glue/buserr.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Writes to $0-$7, skipped (EPIC-04). Mac code writes through
 * nil handles (a handle's flag byte at address 0, a master pointer); a Plus
 * has RAM there, an ST has ROM, and the write is a bus error. The glue's
 * bus-error handler (glueBusError, macglue.s) asks glueBusFix whether the
 * fault is such a write and where the faulting instruction ends; the
 * handler then resumes there, the write not done, as on a Plus where the
 * value would land in RAM nobody reads. Anything else goes to the handler
 * the vector held before (the Mac's, or glueCrash in debug builds).
 * Spectre did the same (docs/macplus/spectre-notes.md, recommendation 1).
 *
 * The 68000's bus error frame: special status word (bit 4 set for a
 * read), access address, instruction register, SR, PC. The PC is a few
 * bytes past the faulting instruction, and the instruction register is not
 * always its first word (Hatari gives the next instruction's for a
 * read-modify-write such as BSET). So the instruction is found by
 * walking back from the PC: the first candidate that decodes as a form
 * that writes memory, ends near the PC, and whose destination, computed
 * from the saved registers, is in $0-$7.
 */

#include "trace.h"

enum {
  kSswRead = 0x10,
  kLowLimit = 8,
  kAddressMask = 0x00FFFFFFUL,
  kMaxBack = 6, /* words before the stacked PC */
  kWordBytes = 2,
  kLongBytes = 4,
  kSevenBits = 7,
  kModeShift = 3,
  kSizeShift = 6,
  kDstModeShift = 6,
  kDstRegShift = 9,
  kOpLineShift = 12,
  kWordShift = 16,
  kFrameBytes = 14, /* the bus error frame */
  kRegA0 = 8,
  kRegA7 = 15,
  kTracedFixes = 16,
  /* Effective address modes */
  kModeAn = 1,
  kModeAnPost = 3,
  kModeAnPre = 4,
  kModeAnDisp = 5,
  kModeAnIndex = 6,
  kModeOther = 7,
  kRegAbsW = 0,
  kRegAbsL = 1,
  kRegPcDisp = 2,
  kRegPcIndex = 3,
  kRegImm = 4,
  kIndexRegShift = 12,
  kIndexLong = 0x0800,
  kIndexAddrReg = 0x8000,
  kByteMask = 0xFF,
  kByteSign = 0x80,
  kByteRange = 0x100,
  /* Opcode classes */
  kDynamicBit = 0x0100,
  kStaticBitMask = 0xFF00,
  kStaticBit = 0x0800,
  kUnaryMask = 0xF900,
  kUnary = 0x4000, /* NEGX, CLR, NEG, NOT */
  kMovemMask = 0xFB80,
  kMovemToMem = 0x4880,
  kTasMask = 0xFFC0,
  kTas = 0x4AC0,
  kToMemory = 0x0100, /* OR, SUB, EOR, AND, ADD: Dn,<ea> */
  kLineQuick = 5,
  kLineOr = 8,
  kLineSub = 9,
  kLineEor = 0xB,
  kLineAnd = 0xC,
  kLineAdd = 0xD
};

unsigned long glueBusFixes;
/* The handler the bus error vector held before the glue's (macglue.s). */
unsigned long glueBusPrevious;

/* The extension words of an effective address, in bytes; -1 for one this
   decoder does not know. size: the operand's bytes (for #imm). */
static int glueEaExtra(unsigned mode, unsigned reg, int size) {
  if (mode < kModeAnDisp) {
    return 0;
  }
  if (mode == kModeAnDisp || mode == kModeAnIndex) {
    return kWordBytes;
  }
  switch (reg) {
    case kRegAbsW:
    case kRegPcDisp:
    case kRegPcIndex:
      return kWordBytes;
    case kRegAbsL:
      return kLongBytes;
    case kRegImm:
      return size == kLongBytes ? kLongBytes : kWordBytes;
    default:
      return -1;
  }
}

/* The size in bytes of the standard size field (bits 7-6); 0 for 3. */
static int glueSize(unsigned opcode) {
  static const int kSizes[4] = {1, kWordBytes, kLongBytes, 0};
  return kSizes[(opcode >> kSizeShift) & 3];
}

/* Whether a write of size bytes to the effective address mode/reg, whose
   extension words are at ext, could have gone to $0-$7 with the registers
   regs (D0-D7, A0-A7). For (An)+ and -(An) both the address before and
   after the update count: the 68000 may stop either side of it. */
static int glueEaLow(unsigned mode, unsigned reg, const unsigned short *ext,
                     int size, const unsigned long *regs) {
  const unsigned long areg = regs[kRegA0 + reg];
  unsigned long addr;
  switch (mode) {
    case 2:
      addr = areg;
      break;
    case kModeAnPost:
      return ((areg & kAddressMask) < kLowLimit) ||
             (((areg - size) & kAddressMask) < kLowLimit);
    case kModeAnPre:
      return ((areg & kAddressMask) < kLowLimit) ||
             (((areg - size) & kAddressMask) < kLowLimit);
    case kModeAnDisp:
      addr = areg + (long)(short)ext[0];
      break;
    case kModeAnIndex: {
      const unsigned xreg = (ext[0] >> kIndexRegShift) & kSevenBits;
      long index = (long)regs[((ext[0] & kIndexAddrReg) ? kRegA0 : 0) + xreg];
      long disp = ext[0] & kByteMask;
      if (!(ext[0] & kIndexLong)) {
        index = (short)index;
      }
      if (disp & kByteSign) {
        disp -= kByteRange;
      }
      addr = areg + index + disp;
      break;
    }
    case kModeOther:
      if (reg == kRegAbsW) {
        addr = (unsigned long)(long)(short)ext[0];
      } else if (reg == kRegAbsL) {
        addr = ((unsigned long)ext[0] << kWordShift) | ext[1];
      } else {
        return 0;
      }
      break;
    default:
      return 0; /* a register, not memory */
  }
  return (addr & kAddressMask) < kLowLimit;
}

/* The length of the instruction at code if it writes memory at an
   address in $0-$7 with these registers; 0 otherwise. */
static int glueWriteToLow(const unsigned short *code,
                          const unsigned long *regs) {
  const unsigned opcode = code[0];
  const unsigned mode = (opcode >> kModeShift) & kSevenBits;
  const unsigned reg = opcode & kSevenBits;
  int size = glueSize(opcode);
  int extra;
  int lead = kWordBytes; /* the opcode and any words before the EA's */
  switch (opcode >> kOpLineShift) {
    case 1:
    case 2:
    case 3: { /* MOVE.B, MOVE.L, MOVE.W: the destination's EA */
      static const int kMoveSize[4] = {0, 1, kLongBytes, kWordBytes};
      const unsigned dmode = (opcode >> kDstModeShift) & kSevenBits;
      const unsigned dreg = (opcode >> kDstRegShift) & kSevenBits;
      size = kMoveSize[opcode >> kOpLineShift];
      extra = glueEaExtra(mode, reg, size);
      if (extra < 0 || glueEaExtra(dmode, dreg, size) < 0 ||
          !glueEaLow(dmode, dreg, code + 1 + (extra / kWordBytes), size,
                     regs)) {
        return 0;
      }
      return kWordBytes + extra + glueEaExtra(dmode, dreg, size);
    }
    case 0:
      if ((opcode & kDynamicBit) && mode != kModeAn) {
        size = 1; /* BCHG/BCLR/BSET Dn,<ea> */
      } else if ((opcode & kStaticBitMask) == kStaticBit) {
        size = 1; /* the same, #imm */
        lead += kWordBytes;
      } else if (size != 0) {
        lead += size == kLongBytes ? kLongBytes : kWordBytes; /* ORI … #imm */
      } else {
        return 0;
      }
      break;
    case 4:
      if ((opcode & kUnaryMask) == kUnary && size != 0) {
        break;
      }
      if ((opcode & kMovemMask) == kMovemToMem) {
        lead += kWordBytes;
        size = kWordBytes;
        break;
      }
      if ((opcode & kTasMask) == kTas) {
        size = 1;
        break;
      }
      return 0;
    case kLineQuick: /* ADDQ, SUBQ; Scc (size 3, not DBcc) */
      if (size == 0) {
        if (mode == kModeAn) {
          return 0;
        }
        size = 1;
      }
      break;
    case kLineOr:
    case kLineSub:
    case kLineEor:
    case kLineAnd:
    case kLineAdd: /* OR, SUB, EOR, AND, ADD Dn,<ea>: the EA is written */
      if (!(opcode & kToMemory) || size == 0) {
        return 0;
      }
      break;
    default:
      return 0;
  }
  extra = glueEaExtra(mode, reg, size);
  if (extra < 0 ||
      !glueEaLow(mode, reg, code + (lead / kWordBytes), size, regs)) {
    return 0;
  }
  return lead + extra;
}

/* Called by glueBusError with the bus error frame and the registers
   D0-D7/A0-A6 as they were; returns where to resume, or 0 for "not a write
   to $0-$7 this can skip". */
unsigned long glueBusFix(const unsigned short *frame, unsigned long *regs) {
  const unsigned long access =
      (((unsigned long)frame[1] << kWordShift) | frame[2]) & kAddressMask;
  const unsigned long stackedPc =
      ((unsigned long)frame[5] << kWordShift) | frame[6];
  if ((frame[0] & kSswRead) || access >= kLowLimit) {
    return 0;
  }
  regs[kRegA7] = (unsigned long)frame + kFrameBytes;
  for (int back = 1; back <= kMaxBack; ++back) {
    const unsigned short *start =
        (const unsigned short *)(stackedPc -
                                 ((unsigned long)back * kWordBytes));
    const int length = glueWriteToLow(start, regs);
    if (length > 0 && (unsigned long)start + length + kWordBytes >= stackedPc) {
      if (glueBusFixes++ < kTracedFixes) {
        TRACE("buserr write %06lx skipped at %06lx op %04x", access,
              (unsigned long)start, (unsigned)start[0]);
      }
      return (unsigned long)start + length;
    }
  }
  TRACE("buserr write %06lx not skipped stackedPc %06lx ir %04x", access,
        stackedPc, (unsigned)frame[3]);
  return 0;
}
