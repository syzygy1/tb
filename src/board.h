/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef BOARD_BB_H
#define BOARD_BB_H

#include "defs.h"
#include "types.h"

extern bitboard bit[64];

static __inline__ int FirstOne(bitboard x)
{
  bitboard res;
  __asm__("bsfq %1, %0" : "=r" (res) : "g" (x));
  return (int)res;
}

#ifdef USE_POPCNT
static __inline__ int PopCount(bitboard x)
{
  bitboard res;
  __asm__("popcnt %1, %0" : "=r" (res) : "g" (x));
  return (int)res;
}
#endif

#define ClearFirst(x) ((x)&=(x)-1)

#include "magic.h"
#include "hyper.h"
#include "bmi2.h"

extern bitboard knight_range[64], king_range[64];
#ifdef HAS_PAWNS
extern bitboard pawn_range[2][64];
#define white_pawn_range pawn_range[0]
#define black_pawn_range pawn_range[1]
extern bitboard sides_mask[64];
extern bitboard double_push_mask[16];
#endif

#ifdef ATOMIC
extern bitboard atom_mask[64];
#endif

#define KnightRange(x) knight_range[x]
#define KingRange(x) king_range[x]

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

static __inline__ bitboard PieceRange(int sq, int type, bitboard occ)
{
  switch (type & 0x07) {
#ifdef HAS_PAWNS
  case PAWN:
    return pawn_range[(type & 0x08) >> 3][sq];
#endif
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  default:
    return KingRange(sq);
  }
}

static __inline__ bitboard PieceMoves(int sq, int type, bitboard occ)
{
  return PieceRange(sq, type, occ) & ~occ;
}

// only used in rtbgenp / rtbverp
static __inline__ bitboard PieceRange1(int sq, int type, bitboard occ)
{
  switch (type & 0x07) {
  case KING:
    return KingRange(sq);
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  default:
    assume(0);
  }
}

static __inline__ bitboard PieceMoves1(int sq, int type, bitboard occ)
{
  return PieceRange1(sq, type, occ) & ~occ;
}

// only used in rtbgen / rtbver
static __inline__ bitboard PieceRange2(int sq, int type, bitboard occ)
{
  switch (type & 0x07) {
  case KNIGHT:
    return KnightRange(sq);
  case BISHOP:
    return BishopRange(sq, occ);
  case ROOK:
    return RookRange(sq, occ);
  case QUEEN:
    return QueenRange(sq, occ);
  default:
    assume(0);
  }
}

static __inline__ bitboard PieceMoves2(int sq, int type, bitboard occ)
{
  return PieceRange2(sq, type, occ) & ~occ;
}

#endif

