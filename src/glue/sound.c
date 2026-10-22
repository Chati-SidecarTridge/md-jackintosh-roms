/**
 * File: src/glue/sound.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The Mac's sound on the STE's and Mega STE's DMA sound
 * (EPIC-15). The Plus plays its sound buffer whenever VIA port B bit 7 is
 * clear; programs write it directly (Tetris) or through the .Sound driver
 * (plus/resources/res_drvr_sound.s). While it is on and its samples vary,
 * each tick converts it (sound.s) into a looping DMA ring, written ahead of
 * the play position; after half a second of a constant buffer (silence),
 * or with the sound off, the DMA is stopped and costs nothing.
 */

#include "sound.h"

#include "hw.h"
#include "lib.h"
#include "trace.h"

enum {
  kSoundActive = 0x027E, /* the .Sound driver's VBL task is installed */
  kSoundBase = 0x0266,
  kTicks = 0x016A,
  kBeepRun = 20,       /* BootBeep: each of its four levels 20 samples, */
  kBeepSilence = 0x80, /* then smoothed each tick (vectors.s:533-604) */
  kBeepSmoothFrom = 73,
  kBeepSmoothCount = 74,
  kBeepRepeat = 37,
  kSamples = 370,
  kIplGlue = 0x0200, /* the level the glue runs the Mac at (D-17) */
  kDmaPlayLoop = 0x03,
  kDmaMono25k = 0x82, /* mono, 25,033 Hz */
  kDmaStart = 0x03,   /* the frame's start, end and counter: three bytes */
  kDmaCounter = 0x09,
  kDmaEnd = 0x0F,
  kHighShift = 16,
  kMidShift = 8,
  kLead = 640,       /* the bytes written ahead of the play position */
  kLeadMin = 128,    /* closer than this, or farther than kLeadMax: */
  kLeadMax = 1400,   /* start again kLead ahead */
  kSilentTicks = 30, /* a constant buffer this long: the DMA stopped */
  kPortBSoundOff = 0x80,
  kProbeStep = 92, /* the bytes between the samples checked for a wave */
  kBufferBytes = 740,
  kPortAVolume = 0x07, /* VIA port A bits 0-2: 7 the loudest */
  kViaPortA = 0x1E00,
  /* The LMC1992 through the microwire: 11-bit commands, all bits sent. */
  kMicrowireMask = 0x07FF,
  kLmcMixing = 0x0401,     /* DMA and YM2149, as TOS sets it */
  kLmcLeft = 0x0554,       /* left 0 dB */
  kLmcRight = 0x0514,      /* right 0 dB */
  kLmcBass = 0x0446,       /* bass flat (6 = 0 dB): no reset, so set it */
  kLmcTreble = 0x0486,     /* treble flat */
  kLmcMaster = 0x04C0,     /* master: plus 0-40, 40 = 0 dB, 2 dB a step */
  kMicrowireSpins = 0x1000 /* a command takes 16 us */
};

#define MICROWIRE_DATA HW_REG16(0x00FF8922UL)
#define MICROWIRE_MASK HW_REG16(0x00FF8924UL)

#define DMA_REG(offset) HW_REG8(0x00FF8900UL + (offset))
#define DMA_CONTROL DMA_REG(0x01)
#define DMA_MODE DMA_REG(0x21)
#define LOWMEM_BYTE(addr) (*(volatile unsigned char *)(unsigned long)(addr))
#define LOWMEM_LONG(addr) (*(volatile unsigned long *)(unsigned long)(addr))

extern unsigned char glueViaShadow[]; /* port B at 0 (shadows.s) */

static unsigned char *soundRing;
static int soundMode;
signed char *glueSoundTable; /* sound.s's weights */
static int soundPlaying;
static unsigned long soundWrite;  /* the next byte to write, in the ring */
static unsigned soundSilent;      /* ticks of a constant buffer */
static unsigned char soundVolume; /* the port A volume last sent */

/* One LMC1992 command, then its shifting out waited for (the mask register
   rotates while it goes). */
static void soundMicrowire(unsigned short command) {
  unsigned spins = kMicrowireSpins;
  MICROWIRE_MASK = kMicrowireMask;
  MICROWIRE_DATA = command;
  /* The mask rotates while the command shifts out: wait for it to start,
     then to end, or the next write cuts it short. */
  while (spins != 0 && MICROWIRE_MASK == kMicrowireMask) {
    --spins;
  }
  while (spins != 0 && MICROWIRE_MASK != kMicrowireMask) {
    --spins;
  }
}

/* The Mac's speaker volume (port A bits 0-2) as the master volume, in the
   LMC1992's 2 dB steps (40 = 0 dB): 7 0 dB, 3 -8 dB, 0 -24 dB. */
static void soundSetVolume(unsigned char volume) {
  static const unsigned char kMaster[] = {30, 34, 37, 40, 40, 40, 40, 40};
  soundVolume = volume;
  soundMicrowire((unsigned short)(kLmcMaster + kMaster[volume]));
}

/* A 24-bit address into three DMA byte registers, high first. */
static void soundSetAddress(unsigned offset, unsigned long address) {
  DMA_REG(offset) = (unsigned char)(address >> kHighShift);
  DMA_REG(offset + 2) = (unsigned char)(address >> kMidShift);
  DMA_REG(offset + 4) = (unsigned char)address;
}

static unsigned long soundCounter(void) {
  return ((unsigned long)DMA_REG(kDmaCounter) << kHighShift) |
         ((unsigned long)DMA_REG(kDmaCounter + 2) << kMidShift) |
         DMA_REG(kDmaCounter + 4);
}

/* The play position in the ring, read until two reads agree. */
static unsigned long soundPlayOffset(void) {
  unsigned long address;
  unsigned long again;
  do {
    address = soundCounter();
    again = soundCounter();
  } while (address != again);
  return (address - (unsigned long)soundRing) & (GLUE_SOUND_RING_BYTES - 1);
}

/* Whether the buffer holds a wave: samples across it differ. */
static int soundLive(const unsigned char *buffer) {
  for (unsigned i = kProbeStep; i < kBufferBytes; i += kProbeStep) {
    if (buffer[i] != buffer[0]) {
      return 1;
    }
  }
  return 0;
}

/* BootBeep's 1:2:1 smoothing of the buffer, then the result repeated
   (vectors.s:567-584), samples at even bytes. */
static void soundBeepSmooth(unsigned char *buffer) {
  for (unsigned k = 0; k < kBeepSmoothCount; ++k) {
    const unsigned from = (kBeepSmoothFrom + k) * 2;
    buffer[k * 2] = (unsigned char)((buffer[from] + (2 * buffer[from + 2]) +
                                     buffer[from + 4] + 3) >>
                                    2);
  }
  for (unsigned k = kBeepSmoothCount; k < kSamples; ++k) {
    buffer[k * 2] = buffer[(k - kBeepRepeat) * 2];
  }
}

void glueBeep(long ticks, long ipl) {
  static const unsigned char kLevels[] = {0x40, 0xFA, 0x06, 0xC0};
  unsigned char *buffer = (unsigned char *)LOWMEM_LONG(kSoundBase);
  unsigned long now;
  if (soundRing == 0 || (ipl != 0 && ipl != kIplGlue)) {
    return; /* the ST, or interrupts masked: no ticks to time it */
  }
  for (unsigned k = 0, run = 0, level = 0; k < kSamples; ++k) {
    buffer[k * 2] = kLevels[level];
    if (++run == kBeepRun) { /* a counter: a division would need libgcc */
      run = 0;
      level = (level + 1) & 3;
    }
  }
  glueViaShadow[0] &= (unsigned char)~kPortBSoundOff;
  TRACE("sound beep %ld", ticks);
  for (long left = ticks; left >= 0; --left) {
    now = LOWMEM_LONG(kTicks);
    while (LOWMEM_LONG(kTicks) == now) {
    }
    soundBeepSmooth(buffer);
  }
  /* BootBeep's last wait: the last smoothed buffer plays for a tick
     (vectors.s:540-541, 591). */
  now = LOWMEM_LONG(kTicks);
  while (LOWMEM_LONG(kTicks) == now) {
  }
  for (unsigned k = 0; k < kSamples; ++k) {
    buffer[k * 2] = kBeepSilence;
  }
}

/* sound.s's table: for each raw sample s (unsigned, $80 the middle) and
   w 0-9, (s - 128) * w / 9 rounded, a row of 16 bytes. 1/9 as 7282/65536:
   no division (libgcc). */
static void soundBuildTable(signed char *table) {
  enum {
    kRows = 256,
    kRowBytes = 16,
    kWeights = 10,
    kMiddle = 128,
    kNinth = 7282,
    kHalf = 0x8000,
    kShift = 16,
    kTop = 127
  };
  for (int raw = 0; raw < kRows; ++raw) {
    /* Doubled and saturated: programs write quietly (Tetris 85-164). */
    int doubled = 2 * (raw - kMiddle);
    long sum = 0;
    if (doubled > kTop) {
      doubled = kTop;
    }
    if (doubled < -kMiddle) {
      doubled = -kMiddle;
    }
    for (int weight = 0; weight < kWeights; ++weight) {
      table[(raw * kRowBytes) + weight] =
          (signed char)((mulS16((short)sum, kNinth) + kHalf) >> kShift);
      sum += doubled;
    }
  }
}

void glueSoundInit(unsigned char *ring, int mode) {
  soundRing = ring;
  soundMode = mode;
  soundPlaying = 0;
  if (ring != 0 && mode == GLUE_SOUND_SMOOTH) {
    glueSoundTable =
        (signed char *)(ring + GLUE_SOUND_RING_ALLOC - GLUE_SOUND_TABLE_BYTES);
    soundBuildTable(glueSoundTable);
  }
  soundSilent = 0;
  if (ring != 0) {
    DMA_CONTROL = 0;
    /* The LMC1992 keeps whatever it had (no reset): TOS's settings. */
    soundMicrowire(kLmcMixing);
    soundMicrowire(kLmcLeft);
    soundMicrowire(kLmcRight);
    soundMicrowire(kLmcBass);
    soundMicrowire(kLmcTreble);
    soundSetVolume(glueViaShadow[kViaPortA] & kPortAVolume);
    TRACE("sound ring %06lx", (unsigned long)ring);
  }
}

static void soundStart(void) {
  for (unsigned i = 0; i < kLead; ++i) {
    soundRing[i] = 0; /* silence until the first tick plays */
  }
  soundWrite = kLead;
  soundSetAddress(kDmaStart, (unsigned long)soundRing);
  soundSetAddress(kDmaEnd, (unsigned long)soundRing + GLUE_SOUND_RING_BYTES);
  DMA_MODE = kDmaMono25k;
  DMA_CONTROL = kDmaPlayLoop;
  soundPlaying = 1;
  TRACE("sound start");
}

static void soundStop(void) {
  DMA_CONTROL = 0;
  soundPlaying = 0;
  TRACE("sound stop");
}

void glueSoundTick(void) {
  const unsigned char *buffer;
  unsigned long lead;
  long written;
  int playing;
  if (soundRing == 0) {
    return;
  }
  buffer = (const unsigned char *)LOWMEM_LONG(kSoundBase);
  playing = (glueViaShadow[0] & kPortBSoundOff) == 0;
  if (playing && (LOWMEM_BYTE(kSoundActive) != 0 || soundLive(buffer))) {
    soundSilent = 0;
  } else if (!playing || ++soundSilent >= kSilentTicks) {
    if (soundPlaying) {
      soundStop();
    }
    return;
  }
  if (!soundPlaying) {
    soundStart();
  }
  if ((glueViaShadow[kViaPortA] & kPortAVolume) != soundVolume) {
    soundSetVolume(glueViaShadow[kViaPortA] & kPortAVolume);
  }
  lead = (soundWrite - soundPlayOffset()) & (GLUE_SOUND_RING_BYTES - 1);
  if (lead < kLeadMin || lead > kLeadMax) {
    TRACE("sound resync lead %lu", lead);
    soundWrite = (soundPlayOffset() + kLead) & (GLUE_SOUND_RING_BYTES - 2);
    lead = kLead;
  }
  /* 416 a tick is 25,058 Hz at the Mac's 60.235: two fewer when ahead. */
  written =
      soundMode == GLUE_SOUND_SMOOTH
          ? glueSoundConvertSmooth(buffer, soundRing + soundWrite,
                                   lead <= kLead)
          : glueSoundConvertFast(buffer, soundRing + soundWrite, lead <= kLead);
  soundWrite += (unsigned long)written;
  if (soundWrite >= GLUE_SOUND_RING_BYTES) {
    /* The bytes past the ring's end go to its start. */
    soundWrite -= GLUE_SOUND_RING_BYTES;
    for (unsigned long i = 0; i < soundWrite; ++i) {
      soundRing[i] = soundRing[GLUE_SOUND_RING_BYTES + i];
    }
  }
}
