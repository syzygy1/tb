/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include "board.h"
#include "magic.c"
#include "hyper.c"
#include "bmi2.c"

#define MAX_PIECES 8

bitboard bit[64];
bitboard knight_range[64], king_range[64];
#ifdef HAS_PAWNS
bitboard pawn_range[2][64];
bitboard sides_mask[64];
#endif

#ifdef ATOMIC
bitboard atom_mask[64];
#endif

void set_up_tables(void)
{
  int i, sq;
  int x, y;

  bitboard bits;

  static int Nx[] = {1, 2, 2, 1, -1, -2, -2, -1},
	     Ny[] = {2, 1, -1, -2, -2, -1, 1, 2},
	     Kx[] = {-1, 0, 1, 1, 1, 0, -1, -1},
	     Ky[] = {1, 1, 1, 0, -1, -1, -1, 0};

  for (sq = 0; sq < 64; sq++)
    bit[sq] = 1ULL << sq;

  for (sq = 0; sq < 64; sq++) {
    x = sq & 7;
    y = sq >> 3;

#ifdef HAS_PAWNS
    bits = 0;
    if (y < 7) {
      if (x > 0) bits |= bit[sq + 7];
      if (x < 7) bits |= bit[sq + 9];
    }
    white_pawn_range[sq] = bits;

    bits = 0;
    if (y > 0) {
      if (x > 0) bits |= bit[sq - 9];
      if (x < 7) bits |= bit[sq - 7];
    }
    black_pawn_range[sq] = bits;

    bits = 0;
    if (x > 0) bits |= bit[sq - 1];
    if (x < 7) bits |= bit[sq + 1];
    sides_mask[sq] = bits;
#endif

    bits = 0;
    for (i = 0; i < 8; i++)
      if (x + Nx[i] >= 0 && x + Nx[i] < 8 && y + Ny[i] >= 0 && y + Ny[i] < 8)
	bits |= bit[x + Nx[i] + 8 * (y + Ny[i])];
    knight_range[sq] = bits;

    bits = 0;
    for (i = 0; i < 8; i++) 
      if (x + Kx[i] >= 0 && x + Kx[i] < 8 && y + Ky[i] >= 0 && y + Ky[i] < 8) 
	bits |= bit[x + Kx[i] + 8 * (y + Ky[i])];
    king_range[sq] = bits;

#ifdef ATOMIC
    atom_mask[sq] = bits | bit[sq];
#endif
  }

  set_up_move_gen();
}
