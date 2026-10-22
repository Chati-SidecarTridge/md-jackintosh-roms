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
 * The Plus's ROM space, $400000-$57FFFF, likewise (EPIC-09): on a Plus a
 * write there does nothing and a read returns ROM bytes; programs probe it
 * for cards (Excel 1.03's floating point looks for a coprocessor at
 * $500000). On an ST it is unmapped. A write there, or to our copy of the
 * Mac ROM, is skipped; a read through an address register is run again
 * against our Mac ROM: the register is moved by the distance to the same
 * offset there, and the instruction re-executed with the trace bit set;
 * the trace exception (glueBusTrace) moves the register back.
 *
 * EPIC-17: a read at an absolute address there ((xxx).L, as programs read
 * the ROM's version at $400008) runs once from a copy of the instruction in
 * RAM with the address moved to our Mac ROM, traced, and the trace exception
 * resumes after the original. An instruction fetch there (a JMP or JSR to
 * $40xxxx; SmoothTalker restarts through ROMBase+$0A) resumes at the same
 * offset of our Mac ROM, but for a JSR, which fetches its target before it
 * pushes the return address; a VBL through a $70 a program pointed there
 * (SmoothTalker) runs the glue's VBL handler. The diagnostic-ROM socket,
 * $F80000-$F9FFFF, which a Plus without a test ROM leaves empty and an ST
 * leaves unmapped, reads as the Plus ROM space does (Dark Castle looks there
 * for a test ROM's signature, which our ROM's bytes never match).
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

#include "macrom.h"
#include "trace.h"

enum {
  kSswRead = 0x10,
  kLowLimit = 8,
  kPlusRomLow = 0x400000UL,  /* the Plus's ROM space, with its mirrors */
  kPlusRomHigh = 0x580000UL, /* the SCSI chip from here */
  kPlusRomMask = 0x1FFFFUL,  /* 128 KB */
  kDiagRomLow = 0xF80000UL,  /* the diagnostic-ROM socket */
  kDiagRomHigh = 0xFA0000UL, /* the ST's cartridge from here */
  kSswFcProgram = 2,         /* the function code's program bit */
  kSswFcMask = 3,
  kJsrMask = 0xFFC0,
  kJsr = 0x4E80,
  kSrTrace = 0x8000,
  kFrameSr = 4, /* words into the bus error frame */
  kVectorTrace = 0x24,
  kVectorVbl = 0x70,
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
  kRegA6 = 14,
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
  kLineAdd = 0xD,
  kLineCmp = 0xB,
  kLineShift = 0xE,
  kTstMask = 0xFF00,
  kTst = 0x4A00,
  kShiftMemMask = 0xF8C0, /* ASd/LSd/ROXd/ROd <ea>: size field 3 */
  kShiftMem = 0xE0C0
};

unsigned long glueBusFixes;
/* The handler the bus error vector held before the glue's (macglue.s). */
unsigned long glueBusPrevious;
extern void glueBusTrace(void);
extern void glueVbl(void);

/* A read being re-run against our Mac ROM: the register moved, by how much,
   and the trace vector to put back. */
static int glueRemapRegister = -1;
static unsigned long glueRemapDelta;
static unsigned long glueTracePrevious;
/* An absolute read being run from glueAbsCopy: where the original ends. */
static unsigned long glueAbsReturn;
static unsigned short glueAbsCopy[6];

/* Whether a read at addr gets Plus behaviour: the Plus's ROM space and the
   diagnostic-ROM socket; the distance to the same offset of our Mac ROM. */
static int glueRomLike(unsigned long addr) {
  return (addr >= kPlusRomLow && addr < kPlusRomHigh) ||
         (addr >= kDiagRomLow && addr < kDiagRomHigh);
}
static unsigned long glueRomDelta(unsigned long addr) {
  return MAC_ROM_BASE_ADDR + ((addr - kPlusRomLow) & kPlusRomMask) - addr;
}

#define VECTOR(addr) (*(volatile unsigned long *)(unsigned long)(addr))

/* Where a write may be skipped: $0-$7, the Plus's ROM space, our Mac ROM. */
static int glueSkippable(unsigned long addr) {
  addr &= kAddressMask;
  return addr < kLowLimit || (addr >= kPlusRomLow && addr < kPlusRomHigh) ||
         (addr >= MAC_ROM_BASE_ADDR &&
          addr < MAC_ROM_BASE_ADDR + MAC_ROM_SIZE_BYTES);
}

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
   extension words are at ext, could have gone to an address glueSkippable
   takes, with the registers regs (D0-D7, A0-A7). For (An)+ and -(An) both
   the address before and after the update count: the 68000 may stop either
   side of it. */
static int glueEaSkippable(unsigned mode, unsigned reg,
                           const unsigned short *ext, int size,
                           const unsigned long *regs) {
  const unsigned long areg = regs[kRegA0 + reg];
  unsigned long addr;
  switch (mode) {
    case 2:
      addr = areg;
      break;
    case kModeAnPost:
    case kModeAnPre:
      return glueSkippable(areg) || glueSkippable(areg - size);
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
  return glueSkippable(addr);
}

/* The length of the instruction at code if it writes memory at an
   address glueSkippable takes, with these registers; 0 otherwise. */
static int glueWriteSkippable(const unsigned short *code,
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
          !glueEaSkippable(dmode, dreg, code + 1 + (extra / kWordBytes), size,
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
      !glueEaSkippable(mode, reg, code + (lead / kWordBytes), size, regs)) {
    return 0;
  }
  return lead + extra;
}

/* The address an effective address of mode (An), (An)+, d16(An) or d8(An,Xn)
   reaches with the registers regs, its extension words at ext; 0 for any
   other mode. *areg gets the register's number (0-7). */
static unsigned long glueEaThroughAn(unsigned mode, unsigned reg,
                                     const unsigned short *ext,
                                     const unsigned long *regs, int *areg) {
  const unsigned long base = regs[kRegA0 + reg];
  *areg = (int)reg;
  switch (mode) {
    case 2:
    case kModeAnPost: /* the 68000 adds after the read (Dark Castle) */
      return base;
    case kModeAnDisp:
      return base + (long)(short)ext[0];
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
      return base + index + disp;
    }
    default:
      return 0;
  }
}

/* The length of the instruction at code if it reads memory through an
   address register, at an effective address within a long of access with
   these registers; 0 otherwise. *areg gets the register. The forms: MOVE's
   source, the immediate and bit operations, TST and the unary operations
   (which read first on a 68000), ADDQ/SUBQ/Scc, the <ea>,Dn and Dn,<ea>
   arithmetic of lines 8, 9, B, C, D, and the memory shifts. */
static int glueReadForm(const unsigned short *code, int *srcExt) {
  const unsigned opcode = code[0];
  const unsigned mode = (opcode >> kModeShift) & kSevenBits;
  const unsigned reg = opcode & kSevenBits;
  const unsigned line = opcode >> kOpLineShift;
  int size = glueSize(opcode);
  int lead = kWordBytes;
  int extra;
  int tail = 0; /* MOVE's destination extension words */
  switch (line) {
    case 1:
    case 2:
    case 3: {
      static const int kMoveSize[4] = {0, 1, kLongBytes, kWordBytes};
      const unsigned dmode = (opcode >> kDstModeShift) & kSevenBits;
      const unsigned dreg = (opcode >> kDstRegShift) & kSevenBits;
      size = kMoveSize[line];
      tail = glueEaExtra(dmode, dreg, size);
      if (tail < 0) {
        return 0;
      }
      break;
    }
    case 0:
      if ((opcode & kDynamicBit) && mode != kModeAn) {
        size = 1;
      } else if ((opcode & kStaticBitMask) == kStaticBit) {
        size = 1;
        lead += kWordBytes;
      } else if (size != 0) {
        lead += size == kLongBytes ? kLongBytes : kWordBytes;
      } else {
        return 0;
      }
      break;
    case 4:
      if (((opcode & kUnaryMask) == kUnary || (opcode & kTstMask) == kTst) &&
          size != 0) {
        break;
      }
      if ((opcode & kTasMask) == kTas) {
        size = 1;
        break;
      }
      return 0;
    case kLineQuick:
      if (size == 0) {
        if (mode == kModeAn) {
          return 0;
        }
        size = 1;
      }
      break;
    case kLineOr:
    case kLineSub:
    case kLineCmp:
    case kLineAnd:
    case kLineAdd:
      if (size == 0) { /* DIVU/S, SUBA, CMPA, MULU/S, ADDA */
        size = (line == kLineOr || line == kLineAnd || !(opcode & kToMemory))
                   ? kWordBytes
                   : kLongBytes;
      } else if (line == kLineCmp && (opcode & kToMemory) && mode == kModeAn) {
        return 0; /* CMPM */
      }
      break;
    case kLineShift:
      if ((opcode & kShiftMemMask) != kShiftMem) {
        return 0;
      }
      size = kWordBytes;
      break;
    default:
      return 0;
  }
  extra = glueEaExtra(mode, reg, size);
  if (extra < 0) {
    return 0;
  }
  *srcExt = lead / kWordBytes;
  return lead + extra + tail;
}

static int glueReadThroughAn(const unsigned short *code,
                             const unsigned long *regs, unsigned long access,
                             int *areg) {
  int srcExt;
  const int length = glueReadForm(code, &srcExt);
  const unsigned long address =
      length ? glueEaThroughAn((code[0] >> kModeShift) & kSevenBits,
                               code[0] & kSevenBits, code + srcExt, regs, areg)
             : 0;
  if (address == 0 || *areg > kRegA6 - kRegA0 ||
      ((access - address) & kAddressMask) >= kLongBytes) {
    return 0;
  }
  return length;
}

/* The same for a read at an absolute long address ((xxx).L); *srcExt gets
   the address's word index in the instruction. */
static int glueReadAbsolute(const unsigned short *code, unsigned long access,
                            int *srcExt) {
  const int length = glueReadForm(code, srcExt);
  unsigned long address;
  if (length == 0 || ((code[0] >> kModeShift) & kSevenBits) != kModeOther ||
      (code[0] & kSevenBits) != kRegAbsL) {
    return 0;
  }
  address = (((unsigned long)code[*srcExt] << kWordShift) | code[*srcExt + 1]) &
            kAddressMask;
  return ((access - address) & kAddressMask) < kLongBytes ? length : 0;
}

/* Puts a remapped register back: from the trace exception after the
   re-run instruction, or from a write skipped during it. */
static void glueRemapUndo(unsigned short *frame, unsigned long *regs) {
  regs[kRegA0 + glueRemapRegister] -= glueRemapDelta;
  frame[0] &= (unsigned short)~kSrTrace;
  VECTOR(kVectorTrace) = glueTracePrevious;
  glueRemapRegister = -1;
}

/* Called by glueBusTrace (the trace exception after a re-run read) with
   its frame (SR, PC) and the registers D0-D7/A0-A6. */
void glueBusTraceFix(unsigned short *frame, unsigned long *regs) {
  if (glueRemapRegister >= 0) {
    glueRemapUndo(frame, regs);
  }
  if (glueAbsReturn != 0) {
    frame[1] = (unsigned short)(glueAbsReturn >> kWordShift);
    frame[2] = (unsigned short)glueAbsReturn;
    frame[0] &= (unsigned short)~kSrTrace;
    VECTOR(kVectorTrace) = glueTracePrevious;
    glueAbsReturn = 0;
  }
}

/* Called by glueBusError with the bus error frame and the registers
   D0-D7/A0-A6 as they were; returns where to resume, or 0 for "not an
   access this can handle". */
unsigned long glueBusFix(unsigned short *frame, unsigned long *regs) {
  const unsigned long access =
      (((unsigned long)frame[1] << kWordShift) | frame[2]) & kAddressMask;
  const unsigned long stackedPc =
      ((unsigned long)frame[5] << kWordShift) | frame[6];
  const int read = (frame[0] & kSswRead) != 0;
  regs[kRegA7] = (unsigned long)frame + kFrameBytes;
  if (read && (frame[0] & kSswFcMask) == kSswFcProgram &&
      access >= kPlusRomLow && access < kPlusRomHigh &&
      access == (VECTOR(kVectorVbl) & kAddressMask)) {
    /* The Atari's VBL through a level-4 vector a program pointed into the
       ROM space: never taken on a Plus (the interrupt switch). */
    return (unsigned long)glueVbl;
  }
  if (read && (frame[0] & kSswFcMask) == kSswFcProgram &&
      access >= kPlusRomLow && access < kPlusRomHigh &&
      (frame[3] & kJsrMask) != kJsr) {
    /* Not a JSR: it fetches its target before it pushes the return
       address, which nothing in the frame gives. */
    if (glueBusFixes++ < kTracedFixes) {
      TRACE("buserr fetch %06lx from rom ir %04x", access, (unsigned)frame[3]);
    }
    return access + glueRomDelta(access);
  }
  if (read && glueRomLike(access) && glueRemapRegister < 0 &&
      glueAbsReturn == 0) {
    for (int back = 1; back <= kMaxBack; ++back) {
      const unsigned short *start =
          (const unsigned short *)(stackedPc -
                                   ((unsigned long)back * kWordBytes));
      int areg;
      const int length = glueReadThroughAn(start, regs, access, &areg);
      if (length > 0 &&
          (unsigned long)start + length + kWordBytes >= stackedPc) {
        glueRemapDelta = glueRomDelta(access);
        glueRemapRegister = areg;
        regs[kRegA0 + areg] += glueRemapDelta;
        glueTracePrevious = VECTOR(kVectorTrace);
        VECTOR(kVectorTrace) = (unsigned long)glueBusTrace;
        frame[kFrameSr] |= kSrTrace;
        if (glueBusFixes++ < kTracedFixes) {
          TRACE("buserr read %06lx from rom at %06lx op %04x", access,
                (unsigned long)start, (unsigned)start[0]);
        }
        return (unsigned long)start;
      }
      int srcExt;
      const int absLength = glueReadAbsolute(start, access, &srcExt);
      if (absLength > 0 &&
          (unsigned long)start + absLength + kWordBytes >= stackedPc) {
        const unsigned long moved =
            (((unsigned long)start[srcExt] << kWordShift) | start[srcExt + 1]) +
            glueRomDelta(access);
        for (int i = 0; i < absLength / kWordBytes; ++i) {
          glueAbsCopy[i] = start[i];
        }
        glueAbsCopy[srcExt] = (unsigned short)(moved >> kWordShift);
        glueAbsCopy[srcExt + 1] = (unsigned short)moved;
        glueAbsReturn = (unsigned long)start + absLength;
        glueTracePrevious = VECTOR(kVectorTrace);
        VECTOR(kVectorTrace) = (unsigned long)glueBusTrace;
        frame[kFrameSr] |= kSrTrace;
        if (glueBusFixes++ < kTracedFixes) {
          TRACE("buserr read %06lx absolute at %06lx op %04x", access,
                (unsigned long)start, (unsigned)start[0]);
        }
        return (unsigned long)glueAbsCopy;
      }
    }
  }
  if (read || !glueSkippable(access)) {
    return 0;
  }
  /* First an instruction that ends at the stacked PC or a word before it
     (a one-word read-modify-write stacks its end + 2); only if none, one
     that ends past it (MOVE <mem>,(xxx).L prefetches after its write).
     The first candidate tried can be the next instruction, which a single
     pass took and skipped unexecuted (EPIC-16 PATCH-02). */
  for (int back = 1; back <= 2 * kMaxBack; ++back) {
    const int exact = back <= kMaxBack;
    const unsigned short *start =
        (const unsigned short *)(stackedPc -
                                 ((unsigned long)(exact ? back
                                                        : back - kMaxBack) *
                                  kWordBytes));
    const int length = glueWriteSkippable(start, regs);
    const unsigned long end = (unsigned long)start + (unsigned long)length;
    if (length > 0 && end + kWordBytes >= stackedPc &&
        (!exact || end <= stackedPc)) {
      if (glueBusFixes++ < kTracedFixes) {
        TRACE("buserr write %06lx skipped at %06lx op %04x", access,
              (unsigned long)start, (unsigned)start[0]);
      }
      if (glueRemapRegister >= 0) {
        /* The write of a re-run read-modify-write: done with the remap. */
        glueRemapUndo(frame + kFrameSr, regs);
      }
      return (unsigned long)start + length;
    }
  }
  TRACE("buserr %s %06lx not handled stackedPc %06lx ir %04x",
        read ? "read" : "write", access, stackedPc, (unsigned)frame[3]);
  return 0;
}
