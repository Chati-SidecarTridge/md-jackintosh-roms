/**
 * File: src/glue/text.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Text on the glue's own screens (text.h). The glyphs are
 * romemul-switcher-rom's 8x8 font; the plotting follows its src/st/glyph.c.
 */

#include "text.h"

#include "font8x8.h"
#include "lib.h"

enum {
  kMonoLineBytes = 80,
  kMediumLineBytes = 160,
  kMonoScreenBytes = 32000,
  kGlyphRows = 8,
  kDecimalBase = 10,
  kHexBase = 16,
  kHexDigitMask = 0xF,
  kHexDigitBits = 4,
  kNumberDigits = 12,
  kLineBufferSize = 128,
  kAsciiMask = 0x7F,
  kFirstPrintable = 0x20,
  kLastPrintable = 0x7E,
  kInverseMask = 0xFF
};

static volatile unsigned char *textScreen;
static int textMono;
static unsigned short textColumn;
static unsigned short textRow;
static unsigned char textInvert; /* kInverseMask while in inverse video */

void textInit(volatile unsigned char *screen, int mono) {
  textScreen = screen;
  textMono = mono;
  textColumn = 0;
  textRow = 0;
}

void textClear(void) {
  for (unsigned long i = 0; i < kMonoScreenBytes; ++i) {
    textScreen[i] = 0;
  }
}

void textAt(unsigned short column, unsigned short row) {
  textColumn = column;
  textRow = row;
}

/* Mono: one plane, each glyph row twice (16 lines a cell). Medium: plane 0
   only (colour 1), in the interleaved word of its 16-pixel group. */
static void textGlyph(unsigned short column, unsigned short row,
                      unsigned char character) {
  const unsigned char *glyph;
  volatile unsigned char *cell;
  if (character < kFirstPrintable || character > kLastPrintable) {
    character = '?'; /* a file name's code page 437 byte, a control code */
  }
  glyph = font8x8_basic[character & kAsciiMask];
  if (textMono) {
    cell = textScreen +
           ((unsigned long)row * (kMonoLineBytes * 2 * kGlyphRows)) + column;
    for (int i = 0; i < kGlyphRows; ++i) {
      cell[0] = (unsigned char)(glyph[i] ^ textInvert);
      cell[kMonoLineBytes] = (unsigned char)(glyph[i] ^ textInvert);
      cell += 2 * kMonoLineBytes;
    }
  } else {
    cell = textScreen + ((unsigned long)row * (kMediumLineBytes * kGlyphRows)) +
           ((unsigned long)(column & ~1U) << 1) + (column & 1U);
    for (int i = 0; i < kGlyphRows; ++i) {
      cell[0] = (unsigned char)(glyph[i] ^ textInvert);
      cell += kMediumLineBytes;
    }
  }
}

void textPuts(const char *text) {
  for (; *text != '\0'; ++text) {
    if (*text == '\n') {
      textColumn = 0;
      ++textRow;
      continue;
    }
    if (textColumn < TEXT_COLUMNS && textRow < TEXT_ROWS) {
      textGlyph(textColumn, textRow, (unsigned char)*text);
    }
    ++textColumn;
  }
}

void textInverse(int inverse) { textInvert = inverse ? kInverseMask : 0; }

void textClearRow(unsigned short row) {
  for (unsigned short column = 0; column < TEXT_COLUMNS; ++column) {
    textGlyph(column, row, ' ');
  }
  textAt(0, row);
}

void textPrintf(const char *fmt, ...) {
  char line[kLineBufferSize];
  __builtin_va_list args;
  __builtin_va_start(args, fmt);
  (void)textFormat(line, sizeof(line), fmt, args);
  __builtin_va_end(args);
  textPuts(line);
}

/* The digits of value in base, most significant first, into digits;
   returns their count. */
static int textDigits(unsigned long value, unsigned short base, int upper,
                      char *digits) {
  char reversed[kNumberDigits];
  int count = 0;
  do {
    unsigned short digit;
    if (base == kHexBase) {
      digit = (unsigned short)(value & kHexDigitMask);
      value >>= kHexDigitBits;
    } else {
      value = divU32U16(value, base, &digit);
    }
    reversed[count++] = (char)(digit < kDecimalBase ? '0' + digit
                                                    : (upper ? 'A' : 'a') +
                                                          digit - kDecimalBase);
  } while (value != 0 && count < kNumberDigits);
  for (int i = 0; i < count; ++i) {
    digits[i] = reversed[count - 1 - i];
  }
  return count;
}

int textFormat(char *out, unsigned long size, const char *fmt,
               __builtin_va_list args) {
  unsigned long len = 0;
  if (size == 0) {
    return 0;
  }
#define TEXT_EMIT(c)          \
  do {                        \
    if (len + 1 < size) {     \
      out[len++] = (char)(c); \
    }                         \
  } while (0)
  for (; *fmt != '\0'; ++fmt) {
    char digits[kNumberDigits];
    char pad = ' ';
    int width = 0;
    int isLong = 0;
    int count;
    int negative = 0;
    unsigned long value;
    if (*fmt != '%') {
      TEXT_EMIT(*fmt);
      continue;
    }
    ++fmt;
    if (*fmt == '0') {
      pad = '0';
      ++fmt;
    }
    while (*fmt >= '0' && *fmt <= '9') {
      width = (width * kDecimalBase) + (*fmt - '0');
      ++fmt;
    }
    if (*fmt == 'l') {
      isLong = 1;
      ++fmt;
    }
    switch (*fmt) {
      case 's': {
        const char *text = __builtin_va_arg(args, const char *);
        for (; *text != '\0'; ++text) {
          TEXT_EMIT(*text);
        }
        continue;
      }
      case 'c':
        TEXT_EMIT(__builtin_va_arg(args, int));
        continue;
      case 'd': {
        long signedValue =
            isLong ? __builtin_va_arg(args, long) : __builtin_va_arg(args, int);
        negative = signedValue < 0;
        value = negative ? (unsigned long)(-signedValue)
                         : (unsigned long)signedValue;
        count = textDigits(value, kDecimalBase, 0, digits);
        break;
      }
      case 'u':
      case 'x':
      case 'X':
        value = isLong ? __builtin_va_arg(args, unsigned long)
                       : __builtin_va_arg(args, unsigned int);
        count = textDigits(value, *fmt == 'u' ? kDecimalBase : kHexBase,
                           *fmt == 'X', digits);
        break;
      case '\0':
        --fmt;
        continue;
      default:
        TEXT_EMIT(*fmt);
        continue;
    }
    if (negative) {
      TEXT_EMIT('-');
      --width;
    }
    for (int i = count; i < width; ++i) {
      TEXT_EMIT(pad);
    }
    for (int i = 0; i < count; ++i) {
      TEXT_EMIT(digits[i]);
    }
  }
#undef TEXT_EMIT
  out[len] = '\0';
  return (int)len;
}
