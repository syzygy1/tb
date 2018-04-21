/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static uint64_t mask[MAX_PIECES];
int shift[MAX_PIECES];

int piv_sq[24];
uint64_t piv_idx[64];
uint8_t piv_valid[64];
uint64_t sq_mask[64];

#ifdef SMALL
uint64_t diagonal;
int16_t KK_map[64][64];
char mirror[64][64];
#endif

static uint64_t pw_mask, pw_pawnmask;
static uint64_t pw_capt_mask[8];
static uint64_t idx_mask1[8], idx_mask2[8];

void init_tables(void)
{
  set_up_tables();
}

static uint64_t __inline__ MakeMove(uint64_t idx, int k, int sq)
{
  return idx | ((uint64_t)sq << shift[k]);
}

#define PAWN_MASK 0xff000000000000ffULL

// use bit_set
#if 1

#define bit_set(x,y) { uint64_t dummy = y; __asm__("bts %1,%0" : "+r" (x) : "r" (dummy));}

#define bit_set_test(x,y,v) \
  __asm__("bts %2, %0\n\tadcl $0, %1\n" : "+r" (x), "+r" (v) : "r" ((uint64_t)(y)) :);

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
  uint64_t idx2 = idx ^ pw_capt_mask[i]; \
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
  uint64_t idx2 = idx ^ pw_mask; \
  occ = bb = 0; \
  assume(numpawns >= 0); \
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
  uint64_t idx2 = idx ^ pw_capt_mask[i]; \
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
  uint64_t idx2 = idx ^ pw_mask; \
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
  uint64_t idx2 = idx ^ pw_capt_mask[i]; \
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
  uint64_t idx2 = idx ^ pw_mask; \
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
static void func(int k, uint8_t *restrict table, uint64_t idx, bitboard occ, int *restrict p, ##__VA_ARGS__)

#define MARK_BEGIN \
  int sq; \
  uint64_t idx2; \
  bitboard bb = PieceMoves1(p[k], pt[k], occ); \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove(idx, k, sq)

#define MARK_END \
    ClearFirst(bb); \
  }

#define BEGIN_CAPTS \
  uint64_t idx; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  uint64_t end = thread->end >> 6; \
  for (k = 0; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[i] = 0

#define BEGIN_CAPTS_NOPROBE \
  uint64_t idx; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  uint64_t end = thread->end >> 6

#ifndef SUICIDE
#ifndef ATOMIC
#define BEGIN_CAPTS_PIVOT \
  uint64_t idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  assume(numpawns > 0); \
  int king, wtm; \
  uint8_t *restrict table; \
  int *restrict pcs; \
  uint64_t end = thread->end; \
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
  uint64_t idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  int king, opp_king, wtm; \
  uint8_t *restrict table; \
  int *restrict pcs; \
  uint64_t end = thread->end; \
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
  uint64_t idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  int king; \
  uint8_t *restrict table; \
  int *restrict pcs; \
  uint64_t end = thread->end; \
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
  uint64_t idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  int wtm; \
  uint8_t *restrict table; \
  int *restrict pcs; \
  uint64_t end = thread->end; \
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
  uint64_t idx; \
  int j, k; \
  int p[MAX_PIECES]; \
  bitboard occ, bb; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  uint8_t *restrict table; \
  int *restrict pcs; \
  uint64_t end = thread->end; \
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
    uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]); \
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
      uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]); \
      func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)
#endif

#ifndef ATOMIC
#define LOOP_BLACK_PIECES(func, ...) \
  do { for (j = 0; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    if (i < numpawns && (p[k] < 0x08 || p[k] >= 0x38)) continue; \
    uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]); \
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
      uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]); \
      func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#endif

#define LOOP_PIECES_PIVOT(func, ...) \
  do { for (j = 0; pcs[j] >= 0; j++) { \
    k = pcs[j]; \
    if (!piv_valid[p[k]]) continue; \
    uint64_t idx3 = idx2 | piv_idx[p[k]]; \
    func(k, table, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)

#define BEGIN_ITER \
  uint64_t idx, idx2; \
  int i; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  bitboard occ, bb = thread->occ; \
  int *p = thread->p; \
  uint64_t end = begin + thread->end

#define LOOP_ITER \
  for (idx = begin + thread->begin; idx < end; idx++)

#define BEGIN_ITER_ALL \
  uint64_t idx, idx2; \
  int i; \
  int n = numpcs; \
  assume(n >= 2 && n <= TBPIECES); \
  bitboard occ; \
  int p[MAX_PIECES]; \
  uint64_t end = thread->end

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

