/*
  Copyright (c) 2011-2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef DEFS_H
#define DEFS_H

#include <inttypes.h>

#ifdef REGULAR
#define SMALL
#endif

#define DRAW_RULE (2 * 50)

#if TBPIECES < 7
#define MAX_STATS 1536
#else
#define MAX_STATS 2560
#endif

#ifndef COMPRESSION_THREADS
#define COMPRESSION_THREADS 1
#endif

#define MAX_VALS (((MAX_STATS / 2) - DRAW_RULE) / 2)

enum { MAXSYMB = 4095 + 8 };

#define LOOKUP
#define LUBITS 12

// GIVEAWAY is a variation on SUICIDE
#ifdef GIVEAWAY
#define SUICIDE
#endif

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define assume(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#else
#define assume(x) do { } while (0)
#endif

#if 0
#define likely(x) (x)
#define unlikely(x) (x)
#else
#define likely(x) __builtin_expect(!!(x),1)
#define unlikely(x) __builtin_expect(!!(x),0)
#endif

#define PASTER(x,y) x##_##y
#define EVALUATOR(x,y) PASTER(x,y)

#endif
