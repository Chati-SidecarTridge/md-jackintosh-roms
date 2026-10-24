/**
 * File: src/glue/menu.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-19
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The setup menu at boot (menu.c, EPIC-06).
 */

#pragma once

/* md-drives-emulator's setup menu for the Mac's disks, until [E] or the
   20 s countdown. Returns at once when the Multi-device has no chooser
   (GET_SETTINGS not answered). Interrupts masked, before the Mac. */
void glueMenu(void);

/* The options the menu read from md-jackintosh (EPIC-12), applied by the
   glue: its defaults when the Multi-device has none. */
enum { GLUE_MOUSE_SPEED_DEFAULT = 5, GLUE_MOUSE_SPEED_MAX = 10 };
enum {
  GLUE_RIGHT_OFF = 0,     /* the right mouse button does nothing */
  GLUE_RIGHT_COMMAND = 1, /* Command-click */
  GLUE_RIGHT_DOUBLE = 2   /* a double-click */
};
struct GlueOptions {
  unsigned long ramKilobytes; /* the Mac's RAM, 0 for all of it */
  int rightButton;            /* GLUE_RIGHT_* */
  int plusScreen;             /* the Plus's 512x342, at the top left */
  int keyboard;               /* GLUE_LAYOUT_* (layout.h) */
  int mouseSpeed;             /* 1 slowest, 5 the ST's own, 10 fastest */
  unsigned char megaSteCtrl;  /* $FF8E21's value on a Mega STE (hw.h) */
  unsigned char megaSteIndex; /* its line on the options page */
  int sound;                  /* GLUE_SOUND_* (sound.h): the STE's DMA sound */
};
extern struct GlueOptions glueOptions;

/* A Mega STE (EPIC-14): main.c found its cache and processor control. */
extern int glueMegaSte;
