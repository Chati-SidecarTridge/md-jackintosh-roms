/**
 * File: src/glue/mac.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The glue's side of the Mac (mac.c, macglue.s).
 */

#pragma once

/* Prepares what E_SoftReset expects and enters the Mac ROM there (D-16).
   Never returns. memTop is the top of the Mac's RAM, screen the 640x400
   screen's base (MemTop - $8100, D-19). */
void glueMacBoot(unsigned long memTop, unsigned long screen);

/* The RTC's PRAM and clock (mac.c, EPIC-08), for the Multi-device's copy
   of them (cart.c, EPIC-05). */
void glueRtcLoadPram(const volatile unsigned char *bytes);
const unsigned char *glueRtcPramBytes(void);
extern volatile int glueRtcPramChanged;
extern volatile unsigned long glueRtcPramChangedTick;
long glueRtcGmtDelta(void);
void glueRtcSetSeconds(unsigned long seconds);
/* The Mac's time zone (gmtDelta, seconds east of UTC) changed, the clock
   moved by the difference, the PRAM marked for md-jackintosh (EPIC-13). */
void glueRtcSetGmtDelta(long seconds);
