/**
 * File: src/glue/sound.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The Mac's sound on the STE's DMA sound (sound.c, EPIC-15).
 */

#pragma once

/* The DMA ring below the screen: mac.c lowers BufPtr by this much on an
   STE with the sound on, and passes the base to glueSoundInit. The
   allocation leaves room for one tick written past the ring's end. */
#define GLUE_SOUND_RING_BYTES 2048
#define GLUE_SOUND_TABLE_BYTES 4096 /* 256 rows of 16 (sound.s) */
#define GLUE_SOUND_RING_ALLOC \
  (GLUE_SOUND_RING_BYTES + 512 + GLUE_SOUND_TABLE_BYTES)

/* The [A]udio option (EPIC-15): how SoundBase becomes DMA samples. */
enum { GLUE_SOUND_OFF = 0, GLUE_SOUND_FAST = 1, GLUE_SOUND_SMOOTH = 2 };

/* The ring's base (0: no sound: the ST, or the option off) and the way,
   on every boot (mac.c), the DMA stopped. */
void glueSoundInit(unsigned char *ring, int mode);

/* _SysBeep's beep (sound-bootbeep, macglue.s glueBootBeep): BootBeep's
   sound for ticks ticks, in SoundBase; silent on the ST and with
   interrupts masked (ipl). */
void glueBeep(long ticks, long ipl);

/* Each tick, in level-1 context after the VBL work (macglue.s). */
void glueSoundTick(void);

/* sound.s: one tick of SoundBase (370 words, the samples in the high bytes,
   unsigned) as 414 signed bytes at dst, 416 with extra (the last two
   samples too). Fast: averages at the added samples, dst even; smooth:
   interpolated through glueSoundTable, doubled. Returns the bytes. */
extern signed char *glueSoundTable;
long glueSoundConvertFast(const unsigned char *src, unsigned char *dst,
                          long extra);
long glueSoundConvertSmooth(const unsigned char *src, unsigned char *dst,
                            long extra);
