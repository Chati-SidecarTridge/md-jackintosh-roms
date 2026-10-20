#!/usr/bin/env python3
"""tools/keyboards/gen_layouts.py: the glue's keyboard layout tables
(src/glue/layouts.c, EPIC-12) from atari_st_layouts.json.

The Mac runs a US System, so for each key of a non-US Atari keyboard this
works out the Mac key strokes that type, on a US Mac, the character the
Atari key shows: the Mac key code with Shift and Option, and a dead key
before it for accented letters (e = Option-e, then e). Each layer (normal,
Shift, Alt, Alt+Shift) gets one or two strokes; a stroke is a byte, the Mac
key code in bits 0-5, Shift in bit 6, Option in bit 7, $FF for none.

Usage: gen_layouts.py [atari_st_layouts.json] [src/glue/layouts.c]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# The layouts the glue offers, in the order of GLUE_LAYOUT_* (layout.h),
# after US, which keeps every key where it is (ikbd.c's kKeyTable).
LAYOUTS = [("French", "fr"), ("German", "de"), ("Spanish", "es"), ("UK", "uk")]
LAYERS = ["normal", "shift", "alt", "altshift"]

# ST scancode -> Mac key code for the keys the tables cover: ikbd.c's
# kKeyTable for $02-$35, the US keyboard's places. $60, the ISO key left of
# Z, has no US place.
US_MAC_KEYS = {
    0x02: 0x12, 0x03: 0x13, 0x04: 0x14, 0x05: 0x15, 0x06: 0x17, 0x07: 0x16,
    0x08: 0x1A, 0x09: 0x1C, 0x0A: 0x19, 0x0B: 0x1D, 0x0C: 0x1B, 0x0D: 0x18,
    0x10: 0x0C, 0x11: 0x0D, 0x12: 0x0E, 0x13: 0x0F, 0x14: 0x11, 0x15: 0x10,
    0x16: 0x20, 0x17: 0x22, 0x18: 0x1F, 0x19: 0x23, 0x1A: 0x21, 0x1B: 0x1E,
    0x1E: 0x00, 0x1F: 0x01, 0x20: 0x02, 0x21: 0x03, 0x22: 0x05, 0x23: 0x04,
    0x24: 0x26, 0x25: 0x28, 0x26: 0x25, 0x27: 0x29, 0x28: 0x27, 0x29: 0x32,
    0x2B: 0x2A, 0x2C: 0x06, 0x2D: 0x07, 0x2E: 0x08, 0x2F: 0x09, 0x30: 0x0B,
    0x31: 0x2D, 0x32: 0x2E, 0x33: 0x2B, 0x34: 0x2F, 0x35: 0x2C,
}
SCANCODES = list(range(0x02, 0x36)) + [0x60]  # layout.h's GLUE_LAYOUT_KEYS
SHIFT, OPTION, NONE = 0x40, 0x80, 0xFF

# What a US System types beyond ASCII (System 6.0.8's KCHR 0): the strokes,
# by Mac key code and modifiers.
MAC_A, MAC_S, MAC_E, MAC_U, MAC_O, MAC_C, MAC_N, MAC_GRAVE = 0x00, 0x01, 0x0E, 0x20, 0x1F, 0x08, 0x2D, 0x32
MAC_1, MAC_3, MAC_6, MAC_8, MAC_I, MAC_COMMA, MAC_SLASH = 0x12, 0x14, 0x16, 0x1C, 0x22, 0x2B, 0x2C
ACUTE, GRAVE, CIRCUMFLEX, UMLAUT, TILDE = (MAC_E | OPTION, MAC_GRAVE | OPTION, MAC_I | OPTION,
                                           MAC_U | OPTION, MAC_N | OPTION)
EXTRA = {
    "¡": [MAC_1 | OPTION], "£": [MAC_3 | OPTION], "§": [MAC_6 | OPTION],
    "°": [MAC_8 | SHIFT | OPTION], "¯": [MAC_COMMA | SHIFT | OPTION],
    "¿": [MAC_SLASH | SHIFT | OPTION], "ß": [MAC_S | OPTION], "ç": [MAC_C | OPTION],
    "é": [ACUTE, MAC_E], "è": [GRAVE, MAC_E], "à": [GRAVE, MAC_A], "ù": [GRAVE, MAC_U],
    "ä": [UMLAUT, MAC_A], "ö": [UMLAUT, MAC_O], "ü": [UMLAUT, MAC_U],
    "Ä": [UMLAUT, MAC_A | SHIFT], "Ö": [UMLAUT, MAC_O | SHIFT], "Ü": [UMLAUT, MAC_U | SHIFT],
    "ñ": [TILDE, MAC_N], "Ñ": [TILDE, MAC_N | SHIFT],
}
# The Atari's dead keys as the US System's: Option and a key.
DEAD = {"´": ACUTE, "`": GRAVE, "^": CIRCUMFLEX, "¨": UMLAUT, "~": TILDE}


def ascii_strokes(us_layout):
    """The printable ASCII characters at their US places."""
    out = {}
    for code, mac in US_MAC_KEYS.items():
        for layer, modifier in (("normal", 0), ("shift", SHIFT)):
            entry = us_layout.get(f"{code:02X}", {}).get(layer)
            if entry and " " < entry["char"] < "\x7f":
                out.setdefault(entry["char"], [mac | modifier])
    return out


def strokes_for(entry, typed):
    if entry is None:
        return None
    if entry["dead"]:
        return [DEAD[entry["char"]]] if entry["char"] in DEAD else None
    return typed.get(entry["char"])


def key_layers(code, keys, typed):
    """The four layers' strokes of a key, or None when it types nothing."""
    found = {layer: strokes_for(keys.get(layer), typed) for layer in LAYERS}
    if found["normal"] is None:
        return None
    place = US_MAC_KEYS.get(code)
    if found["shift"] is None:
        found["shift"] = [place | SHIFT] if place is not None else found["normal"]
    for alt, plain in (("alt", "normal"), ("altshift", "shift")):
        if found[alt] is not None:
            continue
        # No Alt character: Alt is the Mac's Option on the key's own letter
        # (Alt-e is Option-e), or does nothing on a key that needs more.
        base = found["normal"]
        if len(base) == 1 and not base[0] & (SHIFT | OPTION):
            found[alt] = [base[0] | OPTION | (SHIFT if alt == "altshift" else 0)]
        else:
            found[alt] = found[plain]
    return [found[layer] for layer in LAYERS]


def main():
    source = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "atari_st_layouts.json")
    target = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "src", "glue", "layouts.c")
    layouts = json.load(open(source, encoding="utf-8"))["layouts"]
    typed = ascii_strokes(layouts["US"])
    typed.update(EXTRA)
    lines = []
    for name, tag in LAYOUTS:
        lines.append(f"    /* {name} ({tag}) */\n    {{")
        for code in SCANCODES:
            keys = layouts[name].get(f"{code:02X}", {})
            found = key_layers(code, keys, typed)
            if found is None:
                cells = ["{0xFF, 0xFF}"] * len(LAYERS)
                label = "-"
            else:
                cells = ["{" + ", ".join(f"0x{s:02X}" for s in (strokes + [NONE])[:2]) + "}"
                         for strokes in found]
                label = " ".join(keys[layer]["char"] + ("(dead)" if keys[layer]["dead"] else "")
                                 for layer in LAYERS if layer in keys)
            lines.append(f"        /* {code:02X} {label} */ {{{', '.join(cells)}}},")
        lines.append("    },")
    usscan = {mac: code for code, mac in US_MAC_KEYS.items()}
    inverse = ", ".join(f"0x{usscan.get(mac, 0):02X}" for mac in range(0x33))
    with open(target, "w", encoding="utf-8") as out:
        out.write(f"""/**
 * File: src/glue/layouts.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-20
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: The keyboard layouts' tables (EPIC-12), generated by
 * tools/keyboards/gen_layouts.py from tools/keyboards/atari_st_layouts.json
 * (Thorsten Otto's https://tho-otto.m68k.eu/keyboards/). Do not edit: run
 * the script. Each ST key's normal, Shift, Alt and Alt+Shift layers, as up
 * to two Mac key strokes on a US System (layout.h).
 */

#include "layout.h"

/* clang-format off */
const unsigned char glueLayoutTables[GLUE_LAYOUTS - 1][GLUE_LAYOUT_KEYS][4][2] = {{
{chr(10).join(lines)}
}};

/* The Mac key codes $00-$32 back to the US keyboard's ST scancodes. */
const unsigned char glueLayoutUsScan[GLUE_LAYOUT_MAC_KEYS] = {{
    {inverse}}};
/* clang-format on */
""")
    print(f"{target}: {len(LAYOUTS)} layouts, {len(SCANCODES)} keys")


main()
