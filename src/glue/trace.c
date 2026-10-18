/**
 * File: src/glue/trace.c
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Debug-build trace lines through Hatari's NF_STDERR, as
 * romemul-switcher-rom's src/common/trace.c and src/st/htrace.c do.
 */

#include "trace.h"

#if defined(_DEBUG) && (_DEBUG > 0)

#include "text.h"

extern long nfGetId(const char *featureName); /* natfeat.s */
extern long nfCall(long id, ...);             /* natfeat.s */

enum { kTraceLineSize = 128, kTracePrefixLength = 3 };

static long traceStderrId;

void traceInit(void) { traceStderrId = nfGetId("NF_STDERR"); }

void traceLine(const char *fmt, ...) {
  char line[kTraceLineSize];
  __builtin_va_list args;
  int len;

  if (traceStderrId == 0) {
    return;
  }
  /* Byte by byte: an initialised local array may become a memcpy call. */
  line[0] = '@';
  line[1] = '@';
  line[2] = ' ';
  __builtin_va_start(args, fmt);
  len = textFormat(line + kTracePrefixLength,
                   kTraceLineSize - kTracePrefixLength - 1, fmt, args);
  __builtin_va_end(args);
  line[kTracePrefixLength + len] = '\n';
  line[kTracePrefixLength + len + 1] = '\0';
  (void)nfCall(traceStderrId, line);
}

#endif
