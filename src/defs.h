/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef DEFS_H
#define DEFS_H

#ifdef REGULAR
#define SMALL
#endif

typedef unsigned long long bitboard;
typedef unsigned long long long64;
typedef unsigned int uint32;
typedef unsigned char ubyte;
typedef unsigned short ushort;
typedef ushort Move;

#ifndef __WIN32__
// hack to avoid warnings on Linux that seem to be incorrect.
// inttypes.h makes PRIu64 "lu", which in itself is fine given that
// long on 64-bit Linux is 64 bits. However, gcc insists on llu...
#undef PRIu64
#define PRIu64 "llu"
#endif

#define PAWN 1
#define KNIGHT 2
#define BISHOP 3
#define ROOK 4
#define QUEEN 5
#define KING 6

#define WPAWN 1
#define WKNIGHT 2
#define WBISHOP 3
#define WROOK 4
#define WQUEEN 5
#define WKING 6

#define BPAWN 9
#define BKNIGHT 10
#define BBISHOP 11
#define BROOK 12
#define BQUEEN 13
#define BKING 14

#endif

