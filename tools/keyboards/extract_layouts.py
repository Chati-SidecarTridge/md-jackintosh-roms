#!/usr/bin/env python3
"""tools/keyboards/extract_layouts.py: the Atari ST keyboard layouts as data
(EPIC-12), from Thorsten Otto's page https://tho-otto.m68k.eu/keyboards/ .

Each key there carries a title naming its layers ("<char> 0xNN U+XXXX NAME",
then "Shift: ...", "Alt: ...", "AltShift: ...", and so on), its scancode, and
a "dead" class on the labels of dead keys. This writes, per layout, each
scancode's layers: the character, its code in the Atari character set, its
Unicode code point and whether it is a dead key.

Usage: extract_layouts.py PAGE.html OUT.json
       (the page: curl -o PAGE.html https://tho-otto.m68k.eu/keyboards/)
"""

import html
import json
import re
import sys

LAYER = re.compile(r"^(?:(?P<layer>[A-Za-z]+): )?(?P<char>.*?) 0x(?P<atari>[0-9A-Fa-f]{2}) "
                   r"U\+(?P<unicode>[0-9A-Fa-f]{4,6})(?: (?P<name>.*))?$")
KEY = re.compile(r'<div class="kb"[^>]*?title="(?P<title>[^"]*)"(?P<body>.*?)'
                 r'<div class="kl scancode">\s*<div title="(?P<code>[0-9A-F]{2})"', re.S)
LAYOUT = re.compile(r">\s*([^<>]+?) layout\s*<")


def layers(title, body):
    """The key's layers from its title, each marked dead when its label is."""
    dead = {html.unescape(c) for c in re.findall(r'<span class="dead">([^<]*)</span>', body)}
    out = {}
    for line in html.unescape(title).split("\n"):
        match = LAYER.match(line.strip())
        if not match:
            continue
        name = (match.group("layer") or "normal").lower()
        out[name] = {"char": match.group("char"), "atari": f"0x{match.group('atari').upper()}",
                     "unicode": f"U+{match.group('unicode').upper()}",
                     "dead": match.group("char") in dead}
    return out


def main():
    page = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    starts = [(m.start(), m.group(1).strip()) for m in LAYOUT.finditer(page)]
    result = {"_source": "https://tho-otto.m68k.eu/keyboards/ (Thorsten Otto), extracted by "
                         "tools/keyboards/extract_layouts.py",
              "_layers": "normal, shift, alt, altshift (and caps where the page has it); "
                         "atari: the code in the Atari ST character set",
              "layouts": {}}
    for index, (start, name) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(page)
        keys = {}
        for key in KEY.finditer(page, start, end):
            found = layers(key.group("title"), key.group("body"))
            if found:
                keys[key.group("code")] = found
        if keys:
            result["layouts"][name] = dict(sorted(keys.items()))
    with open(sys.argv[2], "w", encoding="utf-8") as handle:
        json.dump(result, handle, ensure_ascii=False, indent=1, sort_keys=False)
        handle.write("\n")
    for name, keys in result["layouts"].items():
        print(f"{name}: {len(keys)} keys")


main()
