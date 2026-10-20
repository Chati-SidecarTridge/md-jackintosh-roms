/**
 * File: src/glue/layout.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The ST keyboard's layout (EPIC-12): lookups in layouts.c's
 * tables for the Mac's keys (ikbd.c) and the menu's (kbd.c).
 */

#include "layout.h"

const unsigned char *glueLayoutStrokes(int layout, unsigned scancode,
                                       int layer) {
  unsigned key;
  const unsigned char *strokes;
  if (layout <= GLUE_LAYOUT_US || layout >= GLUE_LAYOUTS) {
    return 0;
  }
  if (scancode == GLUE_LAYOUT_ISO) {
    key = GLUE_LAYOUT_KEYS - 1;
  } else if (scancode >= GLUE_LAYOUT_FIRST && scancode <= GLUE_LAYOUT_LAST) {
    key = scancode - GLUE_LAYOUT_FIRST;
  } else {
    return 0;
  }
  strokes = glueLayoutTables[layout - 1][key][layer];
  return strokes[0] != GLUE_STROKE_NONE ? strokes : 0;
}

unsigned glueLayoutMenuKey(int layout, unsigned scancode, int *shift) {
  const unsigned char *strokes = glueLayoutStrokes(
      layout, scancode, *shift ? GLUE_LAYER_SHIFT : GLUE_LAYER_NORMAL);
  unsigned key;
  /* One stroke without Option: a character the US keyboard has. */
  if (strokes == 0 || strokes[1] != GLUE_STROKE_NONE ||
      (strokes[0] & GLUE_STROKE_OPTION)) {
    return scancode;
  }
  key = strokes[0] & GLUE_STROKE_KEY;
  if (key >= GLUE_LAYOUT_MAC_KEYS || glueLayoutUsScan[key] == 0) {
    return scancode;
  }
  *shift = (strokes[0] & GLUE_STROKE_SHIFT) != 0;
  return glueLayoutUsScan[key];
}
