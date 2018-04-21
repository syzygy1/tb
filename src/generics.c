/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static uint64_t mask_a1h8;
static uint64_t idx_mask1[8], idx_mask2[8];

static const int16_t KK_init[10][64] = {
  { -1, -1, -1,  0,  1,  2,  3,  4,
    -1, -1, -1,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57 },
  { 58, -1, -1, -1, 59, 60, 61, 62,
    63, -1, -1, -1, 64, 65, 66, 67,
    68, 69, 70, 71, 72, 73, 74, 75,
    76, 77, 78, 79, 80, 81, 82, 83,
    84, 85, 86, 87, 88, 89, 90, 91,
    92, 93, 94, 95, 96, 97, 98, 99,
   100,101,102,103,104,105,106,107,
   108,109,110,111,112,113,114,115 },
  {116,117, -1, -1, -1,118,119,120,
   121,122, -1, -1, -1,123,124,125,
   126,127,128,129,130,131,132,133,
   134,135,136,137,138,139,140,141,
   142,143,144,145,146,147,148,149,
   150,151,152,153,154,155,156,157,
   158,159,160,161,162,163,164,165,
   166,167,168,169,170,171,172,173 },
  {174, -1, -1, -1,175,176,177,178,
   179, -1, -1, -1,180,181,182,183,
   184, -1, -1, -1,185,186,187,188,
   189,190,191,192,193,194,195,196,
   197,198,199,200,201,202,203,204,
   205,206,207,208,209,210,211,212,
   213,214,215,216,217,218,219,220,
   221,222,223,224,225,226,227,228 },
  {229,230, -1, -1, -1,231,232,233,
   234,235, -1, -1, -1,236,237,238,
   239,240, -1, -1, -1,241,242,243,
   244,245,246,247,248,249,250,251,
   252,253,254,255,256,257,258,259,
   260,261,262,263,264,265,266,267,
   268,269,270,271,272,273,274,275,
   276,277,278,279,280,281,282,283 },
  {284,285,286,287,288,289,290,291,
   292,293, -1, -1, -1,294,295,296,
   297,298, -1, -1, -1,299,300,301,
   302,303, -1, -1, -1,304,305,306,
   307,308,309,310,311,312,313,314,
   315,316,317,318,319,320,321,322,
   323,324,325,326,327,328,329,330,
   331,332,333,334,335,336,337,338 },
  { -1, -1,339,340,341,342,343,344,
    -1, -1,345,346,347,348,349,350,
    -1, -1,441,351,352,353,354,355,
    -1, -1, -1,442,356,357,358,359,
    -1, -1, -1, -1,443,360,361,362,
    -1, -1, -1, -1, -1,444,363,364,
    -1, -1, -1, -1, -1, -1,445,365,
    -1, -1, -1, -1, -1, -1, -1,446 },
  { -1, -1, -1,366,367,368,369,370,
    -1, -1, -1,371,372,373,374,375,
    -1, -1, -1,376,377,378,379,380,
    -1, -1, -1,447,381,382,383,384,
    -1, -1, -1, -1,448,385,386,387,
    -1, -1, -1, -1, -1,449,388,389,
    -1, -1, -1, -1, -1, -1,450,390,
    -1, -1, -1, -1, -1, -1, -1,451 },
  {452,391,392,393,394,395,396,397,
    -1, -1, -1, -1,398,399,400,401,
    -1, -1, -1, -1,402,403,404,405,
    -1, -1, -1, -1,406,407,408,409,
    -1, -1, -1, -1,453,410,411,412,
    -1, -1, -1, -1, -1,454,413,414,
    -1, -1, -1, -1, -1, -1,455,415,
    -1, -1, -1, -1, -1, -1, -1,456 },
  {457,416,417,418,419,420,421,422,
    -1,458,423,424,425,426,427,428,
    -1, -1, -1, -1, -1,429,430,431,
    -1, -1, -1, -1, -1,432,433,434,
    -1, -1, -1, -1, -1,435,436,437,
    -1, -1, -1, -1, -1,459,438,439,
    -1, -1, -1, -1, -1, -1,460,440,
    -1, -1, -1, -1, -1, -1, -1,461 }
};

int16_t KK_map[64][64];
static uint8_t KK_inv[462][2];

int8_t mirror[64][64];

static const uint8_t in_triangle[64] = {
  1, 1, 1, 1, 0, 0, 0, 0,
  0, 1, 1, 1, 0, 0, 0, 0,
  0, 0, 1, 1, 0, 0, 0, 0,
  0, 0, 0, 1, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0
};

static const int inv_tri0x40[] = {
  1, 2, 3, 10, 11, 19, 0, 9, 18, 27
};

static uint64_t mask[MAX_PIECES];
int shift[MAX_PIECES];

uint64_t sq_mask[64];

static const uint8_t tri0x40_init[32] = {
  6, 0, 1, 2, 0, 0, 0, 0,
  0, 7, 3, 4, 0, 0, 0, 0,
  0, 0, 8, 5, 0, 0, 0, 0,
  0, 0, 0, 9, 0, 0, 0, 0
};

uint64_t diagonal;

void init_tables(void)
{
  int i, j, sq;

  for (i = 0; i < 64; i++)
    for (j = 0; j < 64; j++) {
      int sq1 = i;
      int sq2 = j;
      if (sq1 & 0x04) {
        sq1 ^= 0x07;
        sq2 ^= 0x07;
      }
      if (sq1 & 0x20) {
        sq1 ^= 0x38;
        sq2 ^= 0x38;
      }
      mirror[i][j] = 1;
      if ((sq1 >> 3) > (sq1 & 7)) {
        sq1 = ((sq1 & 7) << 3) | (sq1 >> 3);
        sq2 = ((sq2 & 7) << 3) | (sq2 >> 3);
        mirror[i][j] = -1;
      } else if ((sq1 >> 3) == (sq1 & 7)) {
        if ((sq2 >> 3) > (sq2 & 7)) {
          sq2 = ((sq2 & 7) << 3) | (sq2 >> 3);
          mirror[i][j] = -1;
        } else if ((sq2 >> 3) == (sq2 & 7))
          mirror[i][j] = 0;
      }
      KK_map[i][j] = KK_init[tri0x40_init[sq1]][sq2];
    }

  for (i = 0; i < 10; i++)
    for (j = 0; j < 64; j++)
      if (KK_init[i][j] >= 0) {
        KK_inv[KK_init[i][j]][0] = inv_tri0x40[i];
        KK_inv[KK_init[i][j]][1] = j;
      }

  uint64_t mask_a1h1, mask_a1a8;
  mask_a1h1 = mask_a1a8 = mask_a1h8 = 0;
  for (i = 2; i < numpcs; i++) {
    mask_a1h1 = (mask_a1h1 << 6) | 0x07;
    mask_a1a8 = (mask_a1a8 << 6) | 0x38;
    mask_a1h8 = (mask_a1h8 << 6) | 0x07;
  }

  for (sq = 0; sq < 64; sq++) {
    uint64_t mask = 0;
    if (sq & 0x04) mask ^= mask_a1h1;
    if (sq & 0x20) mask ^= mask_a1a8;
    sq_mask[sq] = mask;
  }

  diagonal = 441ULL << shift[1];

  set_up_tables();
}

#define MIRROR_A1H8(x) ((((x) & mask_a1h8) << 3) | (((x) >> 3) & mask_a1h8))

static uint64_t __inline__ MakeMove0(uint64_t idx, int wk)
{
  uint64_t idx2 = idx >> shift[1];
  int bk = KK_inv[idx2][1];
  idx &= ~mask[0];
  idx ^= sq_mask[wk];
  if (mirror[wk][bk] < 0) idx = MIRROR_A1H8(idx);
  return idx | ((uint64_t)KK_map[wk][bk] << shift[1]);
}

static uint64_t __inline__ MakeMove1(uint64_t idx, int bk)
{
  int wk = KK_inv[idx >> shift[1]][0];
  idx &= ~mask[0];
  if (mirror[wk][bk] < 0) idx = MIRROR_A1H8(idx);
  return idx | ((uint64_t)KK_map[wk][bk] << shift[1]);
}

static uint64_t __inline__ MakeMove2(uint64_t idx, int k, int sq)
{
  return idx | ((uint64_t)sq << shift[k]);
}

#define CHECK_DIAG int flag = idx < diagonal
#define PIVOT_ON_DIAG(idx2) (flag && idx2 >= diagonal)
#define PIVOT_MIRROR(idx) (MIRROR_A1H8(idx) | (idx & mask[0]))

#define bit_set(x,y) { uint64_t dummy = y; __asm__("bts %1,%0" : "+r" (x) : "r" (dummy));}
#define bit_set(x,y) { uint64_t dummy = y; __asm__("bts %1,%0" : "+r" (x) : "r" (dummy));}

#define jump_bit_set(x,y,lab) \
  __asm__ goto ("bt %1, %0; jc %l[lab]" : : "r" (x), "r" ((uint64_t)(y)) : : lab);

#define jump_bit_clear(x,y,lab) \
  __asm__ goto ("bt %1, %0; jnc %l[lab]" : : "r" (x), "r" ((uint64_t)(y)) : : lab);

#ifndef USE_POPCNT
#define bit_set_test(x,y,v) \
  __asm__("bts %2, %0\n\tadcl $0, %1\n" : "+r" (x), "+r" (v) : "r" ((uint64_t)(y)) :);
#endif

#ifdef USE_POPCNT
#define FILL_OCC64_cheap \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 1; i--, idx2 >>= 6) \
    bit_set(occ, idx2 & 0x3f); \
  bit_set(occ, KK_inv[idx2][0]); \
  bit_set(occ, KK_inv[idx2][1]); \
  if (PopCount(occ) == n - 1)

#define FILL_OCC64 \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 1; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[0] = KK_inv[idx2][0]); \
  bit_set(occ, p[1] = KK_inv[idx2][1]); \
  if (PopCount(occ) == n - 1)
#else
#define FILL_OCC64_cheap \
  int c = 0; \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 1; i--, idx2 >>= 6) \
    bit_set_test(occ, idx2 & 0x3f, c); \
  bit_set_test(occ, KK_inv[idx2][0], c); \
  bit_set_test(occ, KK_inv[idx2][1], c); \
  if (!c)

#define FILL_OCC64 \
  int c = 0; \
  occ = 0; \
  for (i = n - 2, idx2 = idx >> 6; i > 1; i--, idx2 >>= 6) \
    bit_set_test(occ, p[i] = idx2 & 0x3f, c); \
  bit_set_test(occ, p[0] = KK_inv[idx2][0], c); \
  bit_set_test(occ, p[1] = KK_inv[idx2][1], c); \
  if (!c)
#endif

#define FILL_OCC \
  occ = 0; \
  for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6) \
    bit_set(occ, p[i] = idx2 & 0x3f); \
  bit_set(occ, p[0] = KK_inv[idx2][0]); \
  bit_set(occ, p[1] = KK_inv[idx2][1]);

#ifdef USE_POPCNT
#define FILL_OCC_CAPTS \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 1; k--) \
    if (k != i) { \
      bit_set(occ, p[k] = idx2 & 0x3f); \
      idx2 >>= 6; \
    } \
  bit_set(occ, p[0] = KK_inv[idx2][0]); \
  bit_set(occ, p[1] = KK_inv[idx2][1]); \
  if (PopCount(occ) == n - 1)
#else
#define FILL_OCC_CAPTS \
  int c = 0; \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 1; k--) \
    if (k != i) { \
      bit_set_test(occ, p[k] = idx2 & 0x3f, c); \
      idx2 >>= 6; \
    } \
  bit_set_test(occ, p[0] = KK_inv[idx2][0], c); \
  bit_set_test(occ, p[1] = KK_inv[idx2][1], c); \
  if (!c)
#endif

#define MAKE_IDX2 \
  idx2 = ((idx << 6) & idx_mask1[i]) | (idx & idx_mask2[i])

#define MARK(func, ...) \
static void func(int k, uint8_t *restrict table, uint64_t idx, bitboard occ, int *restrict p, ##__VA_ARGS__)

#define MARK_PIVOT0(func, ...) \
static void func##_pivot0(uint8_t *restrict table, uint64_t idx, bitboard occ, int *restrict p, ##__VA_ARGS__)

#define MARK_PIVOT1(func, ...) \
static void func##_pivot1(uint8_t *restrict table, uint64_t idx, bitboard occ, int *restrict p, ##__VA_ARGS__)

#define WhiteKingMoves (KingRange(p[0]) & ~(KingRange(p[1]) | occ))
#define BlackKingMoves (KingRange(p[1]) & ~(KingRange(p[0]) | occ))

#define MARK_BEGIN_PIVOT0 \
  int sq; \
  uint64_t idx2; \
  bitboard bb; \
  CHECK_DIAG; \
  bb = WhiteKingMoves; \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove0(idx, sq)

#define MARK_BEGIN_PIVOT1 \
  int sq; \
  uint64_t idx2; \
  bitboard bb; \
  CHECK_DIAG; \
  bb = BlackKingMoves; \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove1(idx, sq)

#define MARK_BEGIN \
  int sq; \
  uint64_t idx2; \
  bitboard bb; \
  bb = PieceMoves2(p[k], pt[k], occ); \
  while (bb) { \
    sq = FirstOne(bb); \
    idx2 = MakeMove2(idx, k, sq)

#define MARK_END \
    ClearFirst(bb); \
  }

#define BEGIN_CAPTS \
  uint64_t idx; \
  bitboard occ; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int pt2[MAX_PIECES]; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  uint64_t end = thread->end >> 6; \
  for (k = 0; k < n; k++) \
    pt2[k] = pt[k]; \
  pt2[i] = 0

#define BEGIN_CAPTS_NOPROBE \
  uint64_t idx; \
  bitboard occ; \
  int i = captured_piece; \
  int j, k; \
  int p[MAX_PIECES]; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  uint64_t end = thread->end >> 6

#define LOOP_CAPTS \
  for (idx = thread->begin >> 6; idx < end; idx++)

#define LOOP_WHITE_PIECES(func, ...) \
  do { \
    uint64_t idx3 = idx2 | ((uint64_t)p[0] << shift[i]); \
    func##_pivot0(table_w, idx3, occ, p, ##__VA_ARGS__); \
    for (j = 1; white_pcs[j] >= 0; j++) { \
      k = white_pcs[j]; \
      uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]); \
      func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)

#define LOOP_BLACK_PIECES(func, ...) \
  do { \
    uint64_t idx3 = idx2 | ((uint64_t)p[1] << shift[i]); \
    func##_pivot1(table_b, idx3, occ, p, ##__VA_ARGS__); \
    for (j = 1; black_pcs[j] >= 0; j++) { \
      k = black_pcs[j]; \
      uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]); \
      func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
    } \
  } while (0)

#define BEGIN_CAPTS_PIVOT_NOPROBE \
  uint64_t idx; \
  bitboard occ; \
  int j, k; \
  int p[MAX_PIECES]; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  uint64_t end = thread->end

#define LOOP_CAPTS_PIVOT1 \
  for (idx = thread->begin; idx < end; idx++)

#ifdef USE_POPCNT
#define FILL_OCC_CAPTS_PIVOT1 \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 1; k--, idx2 >>= 6) \
    bit_set(occ, p[k] = idx2 & 0x3f); \
  bit_set(occ, p[0] = inv_tri0x40[idx2]); \
  if (PopCount(occ) == n - 1)
#else
#define FILL_OCC_CAPTS_PIVOT1 \
  int c = 0; \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 1; k--, idx2 >>= 6) \
    bit_set_test(occ, p[k] = idx2 & 0x3f, c); \
  bit_set_test(occ, p[0] = inv_tri0x40[idx2], c); \
  if (!c)
#endif

#define MAKE_IDX2_PIVOT1 \
  idx2 = idx & ~mask[0]

#define LOOP_WHITE_PIECES_PIVOT1(func, ...) \
  do { for (j = 1; white_pcs[j] >= 0; j++) { \
    k = white_pcs[j]; \
    if (KK_map[p[0]][p[k]] < 0 || mirror[p[0]][p[k]] < 0) continue; \
    uint64_t idx3 = idx2 | ((uint64_t)KK_map[p[0]][p[k]] << shift[1]); \
    func(k, table_w, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)

#define LOOP_CAPTS_PIVOT0 \
  for (idx = thread->begin; idx < end; idx++)

#ifdef USE_POPCNT
#define FILL_OCC_CAPTS_PIVOT0 \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--, idx2 >>= 6) \
    bit_set(occ, p[k] = idx2 & 0x3f); \
  if (PopCount(occ) == n - 1)
#else
#define FILL_OCC_CAPTS_PIVOT0 \
  int c = 0; \
  uint64_t idx2 = idx; \
  occ = 0; \
  for (k = n - 1; k > 0; k--, idx2 >>= 6) \
    bit_set_test(occ, p[k] = idx2 & 0x3f, c); \
  if (!c)
#endif

#define MAKE_IDX2_PIVOT0 \
  idx2 = idx & ~mask[0]

#define LOOP_BLACK_PIECES_PIVOT0(func, ...) \
  do { for (j = 1; black_pcs[j] >= 0; j++) { \
    k = black_pcs[j]; \
    if ((p[k] & 0x24) || KK_map[p[k]][p[1]] < 0 || mirror[p[k]][p[1]] < 0) \
      continue; \
    uint64_t idx3 = idx2 | ((uint64_t)KK_map[p[k]][p[1]] << shift[1]); \
    func(k, table_b, idx3 & ~mask[k], occ, p, ##__VA_ARGS__); \
  } } while (0)

#define BEGIN_ITER \
  uint64_t idx, idx2; \
  bitboard occ; \
  int i; \
  int n = numpcs; \
  assume(n >= 3 && n <= TBPIECES); \
  int p[MAX_PIECES]; \
  uint64_t end = thread->end;

#define LOOP_ITER \
  for (idx = thread->begin; idx < end; idx++)

#define RETRO(func, ...) \
  do { int j; \
    if (pcs_opp[0] == 0) \
      func##_pivot0(table_opp, idx, occ, p, ##__VA_ARGS__); \
    else \
      func##_pivot1(table_opp, idx, occ, p, ##__VA_ARGS__); \
    for (j = 1; pcs_opp[j] >= 0; j++) { \
      int k = pcs_opp[j]; \
      func(k, table_opp, idx & ~mask[k], occ, p , ##__VA_ARGS__); \
    } \
  } while (0)

