/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

/* to be included in board.c */

#ifdef HYPER

#ifndef HYPER_SSE3
struct Hyper hyper_table[64];
#else
bitboard hyper_rook_mask[64];
#endif
uint8_t hyper_rank[512];

#ifdef HYPER_SSE3
#include <tmmintrin.h>
__m128i hyper_diagmask_xmm[64];
__m128i hyper_bitmask_xmm[64];
__m128i hyper_swapmask_xmm;
#endif

#ifndef HYPER_INLINE
#ifdef HYPER_SSE3
bitboard BishopRange(int sq, bitboard occ) {
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
bitboard BishopRange(int sq, bitboard occ)
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
bitboard RookAttacks(int sq, bitboard occ)
{
  struct Hyper *hyper = &hyper_table[sq];
  bitboard file, reverse;
  bitboard rank;

  file = occ & hyper->filemask;
  reverse = __builtin_bswap64(file);
  file -= hyper->bitmask;
//  reverse -= __builtin_bswap64(hyper->bitmask);
  reverse -= bit[sq ^ 0x38];
  file ^= __builtin_bswap64(reverse);
  file &= hyper->filemask;

  uint32 shift = sq & 0x38;
  rank = ((bitboard)hyper_rank[4 * ((occ >> shift) & 0x7e) + (sq & 0x07)]) << shift;

  return file | rank;
}
#else
bitboard RookAttacks(int sq, bitboard occ)
{
  bitboard file, reverse, rank;

  file = occ & hyper_rook_mask[sq];
  reverse = __builtin_bswap64(file);
  file -= bit[sq];
  reverse -= bit[sq ^ 0x38];
  file ^= __builtin_bswap64(reverse);
  file &= hyper_rook_mask[sq];

  uint32 shift = sq & 0x38;
  rank = ((bitboard)hyper_rank[4 * ((occ >> shift) & 0x7e) + (sq & 0x07)]) << shift;

  return file | rank;
}
#endif
#endif

static void set_up_move_gen(void)
{
  int sq, sq88, d;
  int i, j;
  uint8_t b;
  bitboard bb, bb2;
#ifdef HYPER_SSE3
  __m128i xmm1, xmm2;
#endif

  for (sq = 0; sq < 64; sq++) {
    sq88 = sq + (sq & ~7);

    bb = 0;
    for (d = 1; !((sq88 + 17 * d) & 0x88); d++)
      bb |= bit[sq + 9 * d];
    for (d = 1; !((sq88 - 17 * d) & 0x88); d++)
      bb |= bit[sq - 9 * d];

    bb2 = 0;
    for (d = 1; !((sq88 + 15 * d) & 0x88); d++)
      bb2 |= bit[sq + 7 * d];
    for (d = 1; !((sq88 - 15 * d) & 0x88); d++)
      bb2 |= bit[sq - 7 * d];

#ifdef HYPER_SSE3
    xmm1 = _mm_cvtsi64x_si128(bb);
    xmm2 = _mm_cvtsi64x_si128(bb2);
    hyper_diagmask_xmm[sq] = _mm_unpacklo_epi64(xmm1, xmm2);
    xmm1 = _mm_cvtsi64x_si128(bit[sq]);
    hyper_bitmask_xmm[sq] = _mm_unpacklo_epi64(xmm1, xmm1);
#else
    hyper_table[sq].diagmask135 = bb;
    hyper_table[sq].diagmask45 = bb2;
    hyper_table[sq].bitmask = bit[sq];
#endif
    
    bb = 0;
    for (d = 1; !((sq88 + 16 * d) & 0x88); d++)
      bb |= bit[sq + 8 * d];
    for (d = 1; !((sq88 - 16 * d) & 0x88); d++)
      bb |= bit[sq - 8 * d];
#ifdef HYPER_SSE3
    hyper_rook_mask[sq] = bb;
#else
    hyper_table[sq].filemask = bb;
#endif
  }

#ifdef HYPER_SSE3
  xmm1 = _mm_cvtsi64x_si128(0x0001020304050607);
  xmm2 = _mm_cvtsi64x_si128(0x08090a0b0c0d0e0f);
  hyper_swapmask_xmm = _mm_unpacklo_epi64(xmm1, xmm2);
#endif

  for (i = 0; i < 128; i += 2) {
    for (j = 0; j < 8; j++) {
      b = 0;
      for (d = j + 1; d < 8; d++) {
        b |= 1 << d;
        if (i & (1 << d)) break;
      }
      for (d = j - 1; d >= 0; d--) {
        b |= 1 << d;
        if (i & (1 << d)) break;
      }
      hyper_rank[4 * i + j] = b;
    }
  }
}

#endif
