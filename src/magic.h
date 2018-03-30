/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef MAGIC_H
#define MAGIC_H

#ifdef MAGIC

extern bitboard attack_table[97264];

struct Magic {
  bitboard *data;
  bitboard mask;
  uint64_t magic;
};

extern struct Magic bishop_magic[64];
extern struct Magic rook_magic[64];

static __inline__ bitboard BishopRange(int sq, bitboard occ)
{
  struct Magic *mag = &bishop_magic[sq];
  return mag->data[((occ & mag->mask) * mag->magic) >> (64-9)];
}

static __inline__ bitboard RookRange(int sq, bitboard occ)
{
  struct Magic *mag = &rook_magic[sq];
  return mag->data[((occ & mag->mask) * mag->magic) >> (64-12)];
}

#define QueenRange(sq,occ) (BishopRange(sq,occ)|RookRange(sq,occ))

#endif

#endif
