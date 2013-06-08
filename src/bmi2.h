#ifndef BMI2_H
#define BMI2_H

#ifdef BMI2

#include <x86intrin.h>

// implementations of _pext_u64 and _pdep_u64 for testing
#if 0
static long64 _pext_u64(long64 val, long64 mask)
{
  long64 res = 0;
  int i = 0;

  while (mask) {
    if (val & mask & -mask)
      res |= bit[i];
    mask &= mask - 1;
    i++;
  }

  return res;
}

static long64 _pdep_u64(long64 val, long64 mask)
{
  long64 res = 0;
  int i = 0;

  while (mask) {
    if (val & bit[i++])
      res |= mask & -mask;
    mask &= mask - 1;
  }

  return res;
}
#endif

struct BMI2Info {
  ushort *data;
  bitboard mask1;
  bitboard mask2;
};

extern ushort attack_table[107648];
extern struct BMI2Info bishop_bmi2[64];
extern struct BMI2Info rook_bmi2[64];

static __inline__ bitboard BishopRange(int sq, bitboard occ)
{
  struct BMI2Info *info = &bishop_bmi2[sq];
  return _pdep_u64(info->data[_pext_u64(occ, info->mask1)], info->mask2);
}

static __inline__ bitboard RookRange(int sq, bitboard occ)
{
  struct BMI2Info *info = &rook_bmi2[sq];
  return _pdep_u64(info->data[_pext_u64(occ, info->mask1)], info->mask2);
}

#define QueenRange(sq,occ) (BishopRange(sq,occ)|RookRange(sq,occ))

#endif

#endif
