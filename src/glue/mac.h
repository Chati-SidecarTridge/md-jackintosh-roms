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
