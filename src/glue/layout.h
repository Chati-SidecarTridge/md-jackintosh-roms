/**
 * File: src/glue/layout.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The ST keyboard's layout (EPIC-12): the tables of
 * layouts.c, which type each key's character on the Mac's US System.
 */

#pragma once

/* glueOptions.keyboard: US keeps every key where it is. */
enum {
  GLUE_LAYOUT_US = 0,
  GLUE_LAYOUT_FR = 1,
  GLUE_LAYOUT_DE = 2,
  GLUE_LAYOUT_ES = 3,
  GLUE_LAYOUT_UK = 4,
  GLUE_LAYOUTS = 5
};

/* The keys in the tables: scancodes $02-$35, then $60 (the ISO key). */
#define GLUE_LAYOUT_FIRST 0x02
#define GLUE_LAYOUT_LAST 0x35
#define GLUE_LAYOUT_ISO 0x60
#define GLUE_LAYOUT_KEYS (GLUE_LAYOUT_LAST - GLUE_LAYOUT_FIRST + 2)
#define GLUE_LAYOUT_MAC_KEYS 0x33

/* A stroke: the Mac key code, with Shift and Option held or not. */
#define GLUE_STROKE_KEY 0x3F
#define GLUE_STROKE_SHIFT 0x40
#define GLUE_STROKE_OPTION 0x80
#define GLUE_STROKE_NONE 0xFF

/* The layers: Shift and Alt held. */
enum {
  GLUE_LAYER_NORMAL = 0,
  GLUE_LAYER_SHIFT = 1,
  GLUE_LAYER_ALT = 2,
  GLUE_LAYER_ALT_SHIFT = 3
};

/* [layout - 1][key][layer]: one or two strokes, GLUE_STROKE_NONE after the
   last; a key that types nothing has none at all. */
extern const unsigned char glueLayoutTables[GLUE_LAYOUTS - 1][GLUE_LAYOUT_KEYS]
                                           [4][2];
extern const unsigned char glueLayoutUsScan[GLUE_LAYOUT_MAC_KEYS];

/* The strokes of an ST key in the chosen layout, or 0 when it is not
   translated (US, or a key the tables leave where it is). */
const unsigned char *glueLayoutStrokes(int layout, unsigned scancode,
                                       int layer);

/* The menu's keys (kbd.c): an ST scancode and Shift as the US keyboard's
   key for the same character, so that [A] is the key showing A. */
unsigned glueLayoutMenuKey(int layout, unsigned scancode, int *shift);
