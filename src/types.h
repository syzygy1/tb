#ifndef TYPES_H
#define TYPES_H

#include <inttypes.h>
#include "defs.h"

typedef uint64_t bitboard;
typedef uint16_t Move;

typedef uint8_t u8;
typedef uint16_t u16;

enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };

enum {
  WPAWN = 1, WKNIGHT, WBISHOP, WROOK, WQUEEN, WKING,
  BPAWN = 9, BKNIGHT, BBISHOP, BROOK, BQUEEN, BKING,
};

struct dtz_map {
  uint16_t map[4][MAX_VALS];
  uint16_t inv_map[4][MAX_VALS];
  uint16_t num[4];
  uint16_t max_num;
  uint8_t side;
  uint8_t ply_accurate_win;
  uint8_t ply_accurate_loss;
  uint8_t wide;
  uint8_t high_freq_max;
};

#endif
