#ifndef BMI2_H
#define BMI2_H

#ifdef BMI2

#include <x86intrin.h>

// implementations of _pext_u64 and _pdep_u64 for testing
#if 0
static uint64_t _pext_u64(uint64_t val, uint64_t mask)
{
  uint64_t res = 0;
  int i = 0;

  while (mask) {
    if (val & mask & -mask)
      res |= bit[i];
    mask &= mask - 1;
    i++;
  }

  return res;
}

static uint64_t _pdep_u64(uint64_t val, uint64_t mask)
{
  uint64_t res = 0;
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
  uint16_t *data;
  bitboard mask1;
  bitboard mask2;
};

extern uint16_t attack_table[107648];
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
