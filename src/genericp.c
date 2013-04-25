/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static long64 mask[MAX_PIECES];
int shift[MAX_PIECES];

int piv_sq[24];
long64 piv_idx[64];
ubyte piv_valid[64];
long64 sq_mask[0];

#ifdef SMALL
long64 diagonal;
short KK_map[64][0];
char mirror[64][0];
#endif

static long64 pw_mask, pw_pawnmask;
static long64 pw_capt_mask[8];
static long64 idx_mask1[8], idx_mask2[8];

void init_tables(void)
{
  set_up_tables();
}

static long64 __inline__ MakeMove(long64 idx, int k, int sq)
{
  return idx | (sq << shift[k]);
}

#define PAWN_MASK 0xff000000000000ffULL

// use bit_set
#if 1

#define bit_set(x,y) { long64 dummy = y; __asm__("bts %1,%0" : "+r" (x) : "r" (dummy));}

#define bit_set_test(x,y,v) \
  asm("bts %2, %0\n\tadcl $0, %1\n" : "+r" (x), "+r" (v) : "r" ((long64)(y)) :);

#ifdef USE_POPCNT
#define FILL_OCC64_cheap \
  occ = bb = 0; \
  for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6) \
    bit_set(occ, idx2 & 0x3f); \
  for (; i > 0; i--, idx2 >>= 6) \
    bit_set(bb, idx2 & 0x3f); \
  bit_set(bb, piv_sq[idx2]); \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC64 \
  occ = bb = 0; \
  for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  for (; i > 0; i--, idx2 >>= 6) \
    bit_set(bb, p[i] = idx2 & 0x3f); \
  bit_set(bb, p[0] = piv_sq[idx2]); \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC_CAPTS \
  long64 idx2 = idx ^ pw_capt_mask[i]; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--) \
    if (k != i) { \
      bit_set(occ, p[k] = idx2 & 0x3f); \
      idx2 >>= 6; \
    } \
  for (; k > 0; k--) \
    if (k != i) { \
      bit_set(bb, p[k] = idx2 & 0x3f); \
      idx2 >>= 6; \
    } \
  bit_set(bb, p[0] = piv_sq[idx2]); \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC_CAPTS_PIVOT \
  long64 idx2 = idx ^ pw_mask; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--, idx2 >>= 6) \
    bit_set(occ, p[k] = idx2 & 0x3f); \
  for (; k > 0; k--, idx2 >>= 6) \
    bit_set(bb, p[k] = idx2 & 0x3f); \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))
#else
#define FILL_OCC64_cheap \
  int c = 0; \
  occ = bb = 0; \
  for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6) \
    bit_set_test(occ, idx2 & 0x3f, c); \
  for (; i > 0; i--, idx2 >>= 6) \
    bit_set_test(bb, idx2 & 0x3f, c); \
  bit_set_test(bb, piv_sq[idx2], c); \
  if (!c && !(bb & (occ | PAWN_MASK)) && (occ |= bb, 1))

#define FILL_OCC64 \
  int c = 0; \
  occ = bb = 0; \
  for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6) \
    bit_set_test(occ, p[i] = idx2 & 0x3f, c); \
  for (; i > 0; i--, idx2 >>= 6) \
    bit_set_test(bb, p[i] = idx2 & 0x3f, c); \
  bit_set_test(bb, p[0] = piv_sq[idx2], c); \
  if (!c && !(bb & (occ | PAWN_MASK)) && (occ |= bb, 1))

#define FILL_OCC_CAPTS \
  int c = 0; \
  long64 idx2 = idx ^ pw_capt_mask[i]; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--) \
    if (k != i) { \
      bit_set_test(occ, p[k] = idx2 & 0x3f, c); \
      idx2 >>= 6; \
    } \
  for (; k > 0; k--) \
    if (k != i) { \
      bit_set_test(bb, p[k] = idx2 & 0x3f, c); \
      idx2 >>= 6; \
    } \
  bit_set_test(bb, p[0] = piv_sq[idx2], c); \
  if (!c && !(bb & (occ | PAWN_MASK)) && (occ |= bb, 1))

#define FILL_OCC_CAPTS_PIVOT \
  int c = 0; \
  long64 idx2 = idx ^ pw_mask; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--, idx2 >>= 6) \
    bit_set_test(occ, p[k] = idx2 & 0x3f, c); \
  for (; k > 0; k--, idx2 >>= 6) \
    bit_set_test(bb, p[k] = idx2 & 0x3f, c); \
  if (!c && !(bb & (occ | PAWN_MASK)) && (occ |= bb, 1))
#endif

#define FILL_OCC_PIECES \
  occ = bb; \
  for (i = n - 1, idx2 = idx; i >= numpawns; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f)

#ifdef USE_POPCNT
#define FILL_OCC_PAWNS \
  occ = 0; \
  for (idx2 = idx ^ pw_pawnmask, i = numpawns - 1; i > 0; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[0] = piv_sq[idx2]); \
  if (PopCount(occ) == numpawns && !(occ & PAWN_MASK))
#else
#define FILL_OCC_PAWNS \
  int c = 0; \
  occ = 0; \
  for (idx2 = idx ^ pw_pawnmask, i = numpawns - 1; i > 0; i--, idx2 >>= 6) \
    bit_set_test(occ, p[i] = idx2 & 0x3f, c); \
  bit_set_test(occ, p[0] = piv_sq[idx2], c); \
  if (!c && !(occ & PAWN_MASK))
#endif

#define FILL_OCC_PAWNS_PIECES \
  occ = 0; \
  for (idx2 = idx ^ pw_mask, i = n - 1; i >= numpawns; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  for (pawns = 0; i > 0; i--, idx2 >>= 6) \
    bit_set(pawns, p[i] = idx2 & 0x3f); \
  bit_set(pawns, p[0] = piv_sq[idx2]); \
  occ |= pawns

#define FILL_OCC \
  occ = 0; \
  for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[0] = piv_sq[idx2])

#define MAKE_IDX2 \
  idx2 = ((idx << 6) & idx_mask1[i]) | (idx & idx_mask2[i])

#define MAKE_IDX2_PIVOT \
  idx2 = idx

#else

#define FILL_OCC64 \
  occ = bb = 0; \
  for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6) \
    occ |= bit[p[i] = idx2 & 0x3f]; \
  for (; i > 0; i--, idx2 >>= 6) \
    bb |= bit[p[i] = idx2 & 0x3f]; \
  bb |= bit[p[0] = piv_sq[idx2]]; \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC_CAPTS \
  long64 idx2 = idx ^ pw_capt_mask[i]; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--) \
    if (k != i) { \
      occ |= bit[p[k] = idx2 & 0x3f]; \
      idx2 >>= 6; \
    } \
  for (; k > 0; k--) \
    if (k != i) { \
      bb |= bit[p[k] = idx2 & 0x3f]; \
      idx2 >>= 6; \
    } \
  bb |= bit[p[0] = piv_sq[idx2]]; \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC_CAPTS_PIVOT \
  long64 idx2 = idx ^ pw_mask; \
  occ = bb = 0; \
  for (k = n - 1; k >= numpawns; k--, idx2 >>= 6) \
    occ |= bit[p[k] = idx2 & 0x3f]; \
  for (; k > 0; k--, idx2 >>= 6) \
    bb |= bit[p[k] = idx2 & 0x3f]; \
  occ |= bb; \
  if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK))

#define FILL_OCC_PIECES \
  occ = bb; \
  for (i = n - 1, idx2 = idx; i >= numpawns; i--, idx2 >>= 6) \
    occ |= bit[p[i] = idx2 & 0x3f]

#define FILL_OCC_PAWNS \
  occ = 0; \
  for (idx2 = idx ^ pw_pawnmask, i = numpawns - 1; i > 0; i--, idx2 >>= 6) \
    occ |= bit[p[i] = idx2 & 0x3f]; \
  occ |= bit[p[0] = piv_sq[idx2]]; \
  if (PopCount(occ) == numpawns && !(occ & PAWN_MASK))

#define FILL_OCC \
  for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6) \
    bd[p[i] = CONV0x88(idx2 & 0x3f)] = i + 1; \
  bd[p[0] = piv_sq[idx2]] = 1

#define MAKE_IDX2 \
  idx2 = ((idx << 6) & idx_mask1[i]) | (idx & idx_mask2[i])

#define MAKE_IDX2_PIVOT \
  idx2 = idx

#endif

#define MARK(func, ...) \
static void func(int k, ubyte *table, long64 idx, bitboard occ, int *p, ##__VA_ARGS__)

#define MARK_BEGIN \
  int sq; \
  long64 idx2; \
  bitboard bb = PieceMoves(p[k], pt[k], occ); \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove(idx, k, sq)

#define MARK_END \
    ClearFirst(bb); \
  }

#define BEGIN_CAPTS \
  long64 idx; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  long64 end = thread->end >> 6; \
  for (k = 0; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[i] = 0

#define BEGIN_CAPTS_NOPROBE \
  long64 idx; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  long64 end = thread->end >> 6

#ifndef SUICIDE
#ifndef ATOMIC
#define BEGIN_CAPTS_PIVOT \
  long64 idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  int king, wtm; \
  ubyte *table; \
  int *pcs; \
  long64 end = thread->end; \
  for (k = 1; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[0] = 0; \
  if (pt[0] == WPAWN) { \
    king = black_king; \
    table = table_b; \
    pcs = black_pcs; \
    wtm = 1; \
  } else { \
    king = white_king; \
    table = table_w; \
    pcs = white_pcs; \
    wtm = 0; \
  }
#else /* ATOMIC */
#define BEGIN_CAPTS_PIVOT \
  long64 idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  int king, opp_king, wtm; \
  ubyte *table; \
  int *pcs; \
  long64 end = thread->end; \
  if (pt[0] == WPAWN) { \
    king = black_king; \
    opp_king = white_king; \
    table = table_b; \
    pcs = black_pcs; \
    wtm = 1; \
  } else { \
    king = white_king; \
    opp_king = black_king; \
    table = table_w; \
    pcs = white_pcs; \
    wtm = 0; \
  }
#endif

#define BEGIN_CAPTS_PIVOT_NOPROBE \
  long64 idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  int king; \
  ubyte *table; \
  int *pcs; \
  long64 end = thread->end; \
  if (pt[0] == WPAWN) { \
    king = black_king; \
    table = table_b; \
    pcs = black_pcs; \
  } else { \
    king = white_king; \
    table = table_w; \
    pcs = white_pcs; \
  }
#else
#define BEGIN_CAPTS_PIVOT \
  long64 idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  int wtm; \
  ubyte *table; \
  int *pcs; \
  long64 end = thread->end; \
  for (k = 1; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[0] = 0; \
  if (pt[0] == WPAWN) { \
    table = table_b; \
    pcs = black_pcs; \
    wtm = 1; \
  } else { \
    table = table_w; \
    pcs = white_pcs; \
    wtm = 0; \
  }

#define BEGIN_CAPTS_PIVOT_NOPROBE \
  long64 idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  ubyte *table; \
  int *pcs; \
  long64 end = thread->end; \
  if (pt[0] == WPAWN) { \
    table = table_b; \
    pcs = black_pcs; \
  } else { \
    table = table_w; \
    pcs = white_pcs; \
  }
#endif

#define LOOP_CAPTS \
  for (idx = thread->begin >> 6; idx < end; idx++)

#define LOOP_CAPTS_PIVOT \
  for (idx = thread->begin; idx < end; idx++)

#define CHECK_WHITE_PIECES \
  if (i < numpawns) { \
    for (j = 0; white_pcs[j] >= 0; j++) { \
      k = white_pcs[j]; \
      if (p[k] >= 0x08 && p[k] < 0x38) break; \
    } \
    if (white_pcs[j] < 0) continue; \
  }

#define CHECK_BLACK_PIECES \
  if (i < numpawns) { \
    for (j = 0; black_pcs[j] >= 0; j++) { \
      k = black_pcs[j]; \
      if (p[k] >= 0x08 && p[k] < 0x38) break; \
    } \
    if (black_pcs[j] < 0) continue; \
  }

#define CHECK_PIECES_PIVOT \
  for (j = 0; pcs[j] >= 0; j++) { \
    k = pcs[j]; \
    if (piv_valid[p[k]]) break; \
  } \
  if (pcs[j] < 0) continue

#ifndef ATOMIC
#define LOOP_WHITE_PIECES(func, ...) \
  do { for (j = 0; white_pcs[j] >= 0; j++) { \
    k = white_pcs[j]; \
    if (i < numpawns && (p[k] < 0x08 || p[k] >= 0x38)) continue; \
    long64 idx3 = idx2 | (p[k] << shift[i]); \
    func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#else
#define LOOP_WHITE_PIECES(func, ...) \
  do { \
    bitboard bits = king_range[p[white_king]]; \
    if (i < numpawns) bits |= 0xff000000000000ffULL; \
    for (j = 1; white_pcs[j] >= 0; j++) { \
      k = white_pcs[j]; \
      if (bit[p[k]] & bits) continue; \
      long64 idx3 = idx2 | (p[k] << shift[i]); \
      func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)
#endif

#ifndef ATOMIC
#define LOOP_BLACK_PIECES(func, ...) \
  do { for (j = 0; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    if (i < numpawns && (p[k] < 0x08 || p[k] >= 0x38)) continue; \
    long64 idx3 = idx2 | ((p[k] ^ pw[i]) << shift[i]); \
    func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#else
#define LOOP_BLACK_PIECES(func, ...) \
  do { \
    bitboard bits = king_range[p[black_king]]; \
    if (i < numpawns) bits |= 0xff000000000000ffULL; \
    for (j = 1; black_pcs[j] >= 0; j++) { \
      k = black_pcs[j]; \
      if (bit[p[k]] & bits) continue; \
      long64 idx3 = idx2 | ((p[k] ^ pw[i]) << shift[i]); \
      func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#endif

#define LOOP_PIECES_PIVOT(func, ...) \
  do { for (j = 0; pcs[j] >= 0; j++) { \
    k = pcs[j]; \
    if (!piv_valid[p[k]]) continue; \
    long64 idx3 = idx2 | piv_idx[p[k]]; \
    func(k, table, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)

#define BEGIN_ITER \
  long64 idx, idx2; \
  int i; \
  int n = numpcs; \
  bitboard occ, bb = thread->occ; \
  int *p = thread->p; \
  long64 end = begin + thread->end

#define LOOP_ITER \
  for (idx = begin + thread->begin; idx < end; idx++)

#define BEGIN_ITER_ALL \
  long64 idx, idx2; \
  int i; \
  int n = numpcs; \
  bitboard occ; \
  int p[MAX_PIECES]; \
  long64 end = thread->end

#define LOOP_ITER_ALL \
  for (idx = thread->begin; idx < end; idx++)

#define RETRO(func, ...) \
  do { int j; \
    for (j = 0; pcs_opp[j] >= 0; j++) { \
      int k = pcs_opp[j]; \
      func(k, table_opp, idx & ~mask[k], occ, p , ##__VA_ARGS__); \
    } \
  } while (0)

#if 0
#define RETRO_BLACK(func, ...) \
  do { int j; \
    for (j = 0; black_pcs[j] >= 0; j++) { \
      int k = black_pcs[j]; \
      func(k, table_b, idx & ~mask[k], occ, p , ##__VA_ARGS__); \
    } \
  } while (0)

#define RETRO_WHITE(func, ...) \
  do { int j; \
    for (j = 0; white_pcs[j] >= 0; j++) { \
      int k = white_pcs[j]; \
      func(k, table_w, idx & ~mask[k], occ, p , ##__VA_ARGS__); \
    } \
  } while (0)
#endif

