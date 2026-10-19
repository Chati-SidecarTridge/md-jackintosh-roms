/**
 * File: src/glue/kbd.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The keyboard before the Mac starts (kbd.c): key presses as
 * ST scancodes with the Shift and Control state.
 */

#pragma once

#define KBD_SHIFT 0x100
#define KBD_CONTROL 0x200
#define KBD_CODE_MASK 0xFF

/* ST scancodes (the same key on every layout) */
#define KBD_ESC 0x01
#define KBD_1 0x02
#define KBD_0 0x0B
#define KBD_BACKSPACE 0x0E
#define KBD_E 0x12
#define KBD_R 0x13
#define KBD_U 0x16
#define KBD_P 0x19
#define KBD_H 0x23
#define KBD_I 0x17
#define KBD_O 0x18
#define KBD_A 0x1E
#define KBD_L 0x26
#define KBD_RETURN 0x1C
#define KBD_B 0x30
#define KBD_M 0x32
#define KBD_S 0x1F
#define KBD_SPACE 0x39
#define KBD_UP 0x48
#define KBD_LEFT 0x4B
#define KBD_RIGHT 0x4D
#define KBD_DOWN 0x50
#define KBD_ENTER 0x72

/* Takes the ACIA for polling and turns the IKBD's mouse off. */
void kbdInit(void);
/* The next key pressed (its scancode, KBD_SHIFT and KBD_CONTROL), or -1
   when none is waiting. Never waits. */
int kbdPoll(void);
