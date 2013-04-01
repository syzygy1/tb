/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static long64 mask_a1h1, mask_a1a8, mask_a1h8;
static long64 idx_mask1[8], idx_mask2[8];

static const char mirror[] = {
  0, 1, 1, 1, 1, 1, 1, 0,
 -1, 0, 1, 1, 1, 1, 0,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1, 0, 1, 1, 1, 1, 0,-1,
  0, 1, 1, 1, 1, 1, 1, 0
};

static const int inv_tri0x40[] = {
  1, 2, 3, 10, 11, 19, 0, 9, 18, 27
};

static long64 mask[MAX_PIECES];
static int shift[MAX_PIECES];

long64 tri0x40[64];
long64 sq_mask[64];

static const ubyte tri0x40_init[32] = {
  6, 0, 1, 2, 0, 0, 0, 0,
  0, 7, 3, 4, 0, 0, 0, 0,
  0, 0, 8, 5, 0, 0, 0, 0,
  0, 0, 0, 9, 0, 0, 0, 0
};

void init_tables(void)
{
  int i, sq;

  mask_a1h1 = mask_a1a8 = mask_a1h8 = 0;
  for (i = 1; i < numpcs; i++) {
    mask_a1h1 = (mask_a1h1 << 6) | 0x07;
    mask_a1a8 = (mask_a1a8 << 6) | 0x38;
    mask_a1h8 = (mask_a1h8 << 6) | 0x07;
  }

  for (i = 0; i < 64; i++) {
    sq = i;
    if (sq & 0x04)
      sq ^= 0x07;
    if (sq & 0x20)
      sq ^= 0x38;
    if ((sq >> 3) > (sq & 7))
      sq = ((sq & 7) << 3) | (sq >> 3);
    tri0x40[i] = ((long64)tri0x40_init[sq]) << shift[0];
  }

  for (sq = 0; sq < 64; sq++) {
    long64 mask = 0;
    if (sq & 0x04) mask ^= mask_a1h1;
    if (sq & 0x20) mask ^= mask_a1a8;
    sq_mask[sq] = mask;
  }

  set_up_tables();
}

#define MIRROR_A1H1(x) ((x) ^ mask_a1h1)
#define MIRROR_A1A8(x) ((x) ^ mask_a1a8)
#define MIRROR_A1H8(x) ((((x) & mask_a1h8) << 3) | (((x) >> 3) & mask_a1h8))

static long64 __inline__ MakeMove0(long64 idx, int sq)
{
  idx ^= sq_mask[sq];
  if (mirror[sq] < 0) idx = MIRROR_A1H8(idx);
  return idx | tri0x40[sq];
}

static long64 __inline__ MakeMove1(long64 idx, int k, int sq)
{
  return idx | (sq << shift[k]);
}

static long64 __inline__ MakeMove(long64 idx, int k, int sq)
{
  if (k) return idx | (sq << shift[k]);
  idx ^= sq_mask[sq];
  if (mirror[sq] < 0) idx = MIRROR_A1H8(idx);
  return idx | tri0x40[sq];
}

#define CHECK_DIAG int flag = mirror[p[0]]
#define PIVOT_ON_DIAG(idx) (flag && idx >= tri0x40[0])
#define PIVOT_MIRROR(idx) (MIRROR_A1H8(idx) | (idx & mask[0]))

// use bit_set
#if 1

#define bit_set(x,y) { long64 dummy = y; asm("bts %1,%0" : "+r" (x) : "r" (dummy));}
#define bit_set(x,y) { long64 dummy = y; asm("bts %1,%0" : "+r" (x) : "r" (dummy));}

#define jump_bit_set(x,y,lab) \
  asm goto ("bt %1, %0; jc %l[lab]" : : "r" (x), "r" ((long64)(y)) : : lab);

#define jump_bit_clear(x,y,lab) \
  asm goto ("bt %1, %0; jnc %l[lab]" : : "r" (x), "r" ((long64)(y)) : : lab);

#define bit_set_jump_set(x,y,lab) \
  asm goto ("bts %1, %0; jc %l[lab]" : "+r" (x) : "r" ((long64)(y)) : : lab);

#define FILL_OCC64_cheap \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 0; i--, idx2 >>= 6) \
    bit_set(occ, idx2 & 0x3f); \
  bit_set(occ, inv_tri0x40[idx2]); \
  if (PopCount(occ) == n - 1)

#define FILL_OCC64 \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 0; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[i] = inv_tri0x40[idx2]); \
  if (PopCount(occ) == n - 1)

#define FILL_OCC64_asmgoto \
  occ = 0; \
  i = n - 2; \
  idx2 = idx >> 6; \
  do { \
    bit_set_jump_set(occ, p[i] = idx2 & 0x3f, lab); \
    idx2 >>= 6; \
    i--; \
  } while (i > 0); \
  bit_set_jump_set(occ, p[0] = inv_tri0x40[idx2], lab)

#define FILL_OCC \
  occ = 0; \
  for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[0] = inv_tri0x40[idx2])

#define FILL_OCC_CAPTS \
  long64 idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--) \
    if (k != i) { \
      bit_set(occ, p[k] = idx2 & 0x3f); \
      idx2 >>= 6; \
    } \
  bit_set(occ, p[0] = inv_tri0x40[idx2]); \
  if (PopCount(occ) == n - 1)

#define FILL_OCC_CAPTS_PIVOT \
  long64 idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--, idx2 >>= 6) \
    bit_set(occ, p[k] = idx2 & 0x3f); \
  if (PopCount(occ) == n - 1)

#else

#define FILL_OCC64 \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 0; i--, idx2 >>= 6) \
    occ |= bit[p[i] = idx2 & 0x3f]; \
  occ |= bit[p[0] = inv_tri0x40[idx2]]; \
  if (PopCount(occ) == n - 1)

#define FILL_OCC \
  occ = 0; \
  for (i = n - 1, idx2 = idx; i > 0; i--, idx2 >>= 6) \
    occ |= bit[p[i] = idx2 & 0x3f]; \
  occ |= bit[p[0] = inv_tri0x40[idx2]]

#define FILL_OCC_CAPTS \
  long64 idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--) \
    if (k != i) { \
      occ |= bit[p[k] = idx2 & 0x3f]; \
      idx2 >>= 6; \
    } \
  occ |= bit[p[k] = inv_tri0x40[idx2]]; \
  if (PopCount(occ) == n - 1)

#define FILL_OCC_CAPTS_PIVOT \
  long64 idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--, idx2 >>= 6) \
    occ |= bit[p[k] = idx2 & 0x3f]; \
  if (PopCount(occ) == n - 1)

#endif

#define MAKE_IDX2 \
  idx2 = ((idx << 6) & idx_mask1[i]) | (idx & idx_mask2[i])

#define MAKE_IDX2_PIVOT \
  idx2 = idx

#define MARK(func, ...) \
static void func(int k, ubyte *table, long64 idx, bitboard occ, int *p, ##__VA_ARGS__)

#define MARK_PIVOT(func, ...) \
static void func##_pivot(ubyte *table, long64 idx, bitboard occ, int *p, ##__VA_ARGS__)

#define MARK_BEGIN_PIVOT \
  int sq; \
  long64 idx2; \
  bitboard bb; \
  CHECK_DIAG; \
  bb = PieceMoves(p[0], pt[0], occ); \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove0(idx, sq)

#define MARK_BEGIN \
  int sq; \
  long64 idx2; \
  bitboard bb; \
  bb = PieceMoves(p[k], pt[k], occ); \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove1(idx, k, sq)

#define MARK_END \
    ClearFirst(bb); \
  }

#ifndef ATOMIC
#define BEGIN_CAPTS \
  long64 idx; \
  bitboard occ; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  int n = numpcs; \
  long64 end = thread->end >> 6; \
  for (k = 0; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[i] = 0
#else
#define BEGIN_CAPTS \
  long64 idx; \
  bitboard occ; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  int n = numpcs; \
  long64 end = thread->end >> 6
#endif

#define BEGIN_CAPTS_NOPROBE \
  long64 idx; \
  bitboard occ; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int n = numpcs; \
  long64 end = thread->end >> 6

#define BEGIN_CAPTS_PIVOT \
  long64 idx; \
  bitboard occ; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  int n = numpcs; \
  long64 end = thread->end; \
  for (k = 1; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[0] = 0

#define BEGIN_CAPTS_PIVOT_NOPROBE \
  long64 idx; \
  bitboard occ; \
  int j, k; \
  int p[MAX_PIECES]; \
  int n = numpcs; \
  long64 end = thread->end

#define LOOP_CAPTS \
  for (idx = thread->begin >> 6; idx < end; idx++)

#define LOOP_CAPTS_PIVOT \
  for (idx = thread->begin; idx < end; idx++)

#ifndef ATOMIC
#define LOOP_WHITE_PIECES(func, ...) \
  do { \
    long64 idx3 = idx2 | (p[0] << shift[i]); \
    func##_pivot(table_w, idx3 & ~mask[0], occ, p, ##__VA_ARGS__); \
    for (j = 1; white_pcs[j] >= 0; j++) { \
      k = white_pcs[j]; \
      long64 idx3 = idx2 | (p[k] << shift[i]); \
      func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)
#else
#define LOOP_WHITE_PIECES(func, ...) \
  do { \
    bitboard bits = king_range[p[0]]; \
    for (j = 1; white_pcs[j] >= 0; j++) { \
      k = white_pcs[j]; \
      if (bit[p[k]] & bits) continue; \
      long64 idx3 = idx2 | (p[k] << shift[i]); \
      func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)
#endif

#if 0
#define LOOP_WHITE_PIECES(func, ...) \
  do { for (j = 0; white_pcs[j] >= 0; j++) { \
    k = white_pcs[j]; \
    long64 idx3 = idx2 | (p[k] << shift[i]); \
    func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#endif

#ifndef ATOMIC
#define LOOP_BLACK_PIECES(func, ...) \
  do { for (j = 0; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    long64 idx3 = idx2 | (p[k] << shift[i]); \
    func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#else
#define LOOP_BLACK_PIECES(func, ...) \
  do { \
    bitboard bits = king_range[p[black_king]]; \
    for (j = 1; black_pcs[j] >= 0; j++) { \
      k = black_pcs[j]; \
      if (bit[p[k]] & bits) continue; \
      long64 idx3 = idx2 | (p[k] << shift[i]); \
      func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#endif

#define CHECK_BLACK_PIECES_PIVOT \
  for (j = 0; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    if (!(p[k] & 0x24) && mirror[p[k]] >= 0) break; \
  } \
  if (black_pcs[j] < 0) continue

#ifndef ATOMIC
#define LOOP_BLACK_PIECES_PIVOT(func, ...) \
  do { for (j = 0; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    if ((p[k] & 0x24) || mirror[p[k]] < 0) continue; \
    long64 idx3 = idx2 | tri0x40[p[k]]; \
    func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#else
#define LOOP_BLACK_PIECES_PIVOT(func, ...) \
  do { \
    bitboard bits = king_range[p[black_king]]; \
    for (j = 1; black_pcs[j] >= 0; j++) { \
      k = black_pcs[j]; \
      if (bit[p[k]] & bits) continue; \
      if ((p[k] & 0x24) || mirror[p[k]] < 0) continue; \
      long64 idx3 = idx2 | tri0x40[p[k]]; \
      func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)
#endif

#define BEGIN_ITER \
  long64 idx, idx2; \
  bitboard occ; \
  int i; \
  int n = numpcs; \
  int p[MAX_PIECES]; \
  long64 end = thread->end;

#define LOOP_ITER \
  for (idx = thread->begin; idx < end; idx++)

#define RETRO(func, ...) \
  do { int j; \
    j = 0; \
    if (pcs_opp[0] == 0) { \
      func##_pivot(table_opp, idx & ~mask[0], occ, p, ##__VA_ARGS__); \
      j = 1; \
    } \
    for (; pcs_opp[j] >= 0; j++) { \
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
