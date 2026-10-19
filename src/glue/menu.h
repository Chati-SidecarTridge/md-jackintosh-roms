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
