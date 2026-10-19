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
enum {
  GLUE_RIGHT_OFF = 0,     /* the right mouse button does nothing */
  GLUE_RIGHT_COMMAND = 1, /* Command-click */
  GLUE_RIGHT_DOUBLE = 2   /* a double-click */
};
struct GlueOptions {
  unsigned long ramKilobytes; /* the Mac's RAM, 0 for all of it */
  int rightButton;            /* GLUE_RIGHT_* */
  int plusScreen;             /* the Plus's 512x342, centred */
};
extern struct GlueOptions glueOptions;
