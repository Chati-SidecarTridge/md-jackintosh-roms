/**
 * File: src/glue/trace.h
 * Author: Diego Parrilla Santamaría
 * Date: 2026-10-18
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: Debug-build trace lines for the Hatari harness.
 */

#pragma once

/*
 * TRACE("ram %lu", n) sends "@@ ram 1048576\n" to Hatari's native features
 * (NF_STDERR). tools/dev/hatari_harness.py waits for these lines, so change
 * both together. Release builds compile every TRACE() to nothing. Debug
 * builds never go on hardware: the native-feature opcodes trap on a real ST.
 */
#if defined(_DEBUG) && (_DEBUG > 0)
void traceInit(void);
void traceLine(const char *fmt, ...);
#define TRACE_INIT() traceInit()
#define TRACE(...) traceLine(__VA_ARGS__)
#else
#define TRACE_INIT() ((void)0)
#define TRACE(...) ((void)0)
#endif
