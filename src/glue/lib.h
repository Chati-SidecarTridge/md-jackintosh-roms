/**
 * File: src/glue/lib.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The few helpers a freestanding glue needs (lib.s): no libc,
 * no libgcc (C-03).
 */

#pragma once

/* n / d and n % d for a 16-bit divisor, with two divu.w: GCC calls libgcc
   for a 32-bit division it cannot fold. */
unsigned long divU32U16(unsigned long n, unsigned short d,
                        unsigned short *remainder);

/* 1 when a byte read at address completes, 0 when it is a bus error. */
int probeRead(unsigned long address);
