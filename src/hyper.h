/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef HYPER_H
#define HYPER_H

#ifdef HYPER

#define HYPER_INLINE
#define HYPER_SSE3

#ifndef HYPER_SSE3
struct Hyper {
  bitboard diagmask135;
  bitboard diagmask45;
  bitboard bitmask;
  bitboard filemask;
};

extern struct Hyper hyper_table[64];
#else
extern bitboard hyper_rook_mask[64];
#endif

extern ubyte hyper_rank[512];

#ifdef HYPER_INLINE

#ifdef HYPER_SSE3
#include <tmmintrin.h>

// from http://chessprogramming.wikispaces.com/SSSE3#SSSE3Version

__m128i hyper_diagmask_xmm[64];
__m128i hyper_bitmask_xmm[64];
__m128i hyper_swapmask_xmm;
 
static __inline__ bitboard BishopRange(int sq, bitboard occ) {
  __m128i o, r, m, b, s;

  m = hyper_diagmask_xmm[sq];
  b = hyper_bitmask_xmm[sq];
  s = hyper_swapmask_xmm;
  o = _mm_cvtsi64x_si128(occ);
  o = _mm_unpacklo_epi64(o, o);
  o = _mm_and_si128(o, m);
  r = _mm_shuffle_epi8(o, s);
  o = _mm_sub_epi64(o, b);
  b = _mm_shuffle_epi8(b, s);
  r = _mm_sub_epi64(r, b);
  r = _mm_shuffle_epi8(r, s);
  o = _mm_xor_si128(o, r);
  o = _mm_and_si128(o, m);
  r = _mm_unpackhi_epi64(o, o);
  o = _mm_add_epi64(o, r);
  return _mm_cvtsi128_si64(o);
}
#else
static __inline__ bitboard BishopRange(int sq, bitboard occ)
{
  struct Hyper *hyper = &hyper_table[sq];
  bitboard diag135, diag45, reverse;

  diag135 = occ & hyper->diagmask135;
  reverse = __builtin_bswap64(diag135);
  diag135 -= hyper->bitmask;
//  reverse -= __builtin_bswap64(hyper->bitmask);
  reverse -= bit[sq ^ 0x38];
  diag135 ^= __builtin_bswap64(reverse);
  diag135 &= hyper->diagmask135;

  diag45 = occ & hyper->diagmask45;
  reverse = __builtin_bswap64(diag45);
  diag45 -= hyper->bitmask;
//  reverse -= __builtin_bswap64(hyper->bitmask);
  reverse -= bit[sq ^ 0x38];
  diag45 ^= __builtin_bswap64(reverse);
  diag45 &= hyper->diagmask45;

  return diag135 | diag45;
}
#endif

#ifndef HYPER_SSE3
static __inline__ bitboard RookRange(int sq, bitboard occ)
{
  struct Hyper *hyper = &hyper_table[sq];
  bitboard file, reverse;
  bitboard rank;

  file = occ & hyper->filemask;
  reverse = __builtin_bswap64(file);
  file -= hyper->bitmask;
  reverse -= __builtin_bswap64(hyper->bitmask);
  file ^= __builtin_bswap64(reverse);
  file &= hyper->filemask;

  uint32_t shift = sq & 0x38;
  rank = ((bitboard)hyper_rank[4 * ((occ >> shift) & 0x7e) + (sq & 0x07)]) << shift;

  return file | rank;
}
#else
static __inline__ bitboard RookRange(int sq, bitboard occ)
{
  bitboard file, reverse, rank;

  file = occ & hyper_rook_mask[sq];
  reverse = __builtin_bswap64(file);
  file -= bit[sq];
  reverse -= bit[sq ^ 0x38];
  file ^= __builtin_bswap64(reverse);
  file &= hyper_rook_mask[sq];

  uint32_t shift = sq & 0x38;
  rank = ((bitboard)hyper_rank[4 * ((occ >> shift) & 0x7e) + (sq & 0x07)]) << shift;

  return file | rank;
}
#endif

#else
bitboard BishopRange(int sq, bitboard occ) __attribute__ ((pure));
bitboard RookRange(int sq, bitboard occ) __attribute__ ((pure));
#endif

#define QueenRange(sq,occ) (BishopRange(sq,occ)|RookRange(sq,occ))

#endif

#endif
