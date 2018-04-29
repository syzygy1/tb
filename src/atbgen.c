/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define REDUCE_PLY 122
#define REDUCE_PLY_RED 119

#define STAT_DRAW (MAX_STATS/2)
#define STAT_CAPT_CWIN (STAT_DRAW - 2)
#define STAT_CAPT_WIN (STAT_DRAW - 1)
#define STAT_CAPT_DRAW (STAT_DRAW + 1)
#define STAT_MATE (MAX_STATS - 1)

#define MAX_PLY (STAT_DRAW - 3)
#define MIN_PLY_WIN 1
#define MIN_PLY_LOSS 0

#define ILLEGAL 0
#define BROKEN 0xff
#define UNKNOWN 0xfe
#define CHANGED 0xfd
#define CAPT_CLOSS 0xfc
#define CAPT_DRAW 0xfb
#define MATE 0xfa
#define LOSS_IN_ONE 0xf9
#define CAPT_WIN 1
#define WIN_IN_ONE 2
#define CAPT_CWIN (WIN_IN_ONE + DRAW_RULE)
#define CAPT_CWIN_RED (WIN_IN_ONE + 1)

#define SET_CHANGED(x) \
do { uint8_t dummy = CHANGED; \
__asm__( \
"movb %2, %%al\n\t" \
"lock cmpxchgb %1, %0" \
: "+m" (x), "+r" (dummy) : "i" (UNKNOWN) : "eax"); } while (0)

#define SET_CAPT_VALUE(x,v) \
do { uint8_t dummy = v; __asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); } while (0)

#define SET_WIN_VALUE(x,v) \
do { uint8_t dummy = v; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); } while (0)

uint8_t win_loss[256];
uint8_t loss_win[256];

/*
uint8_t capt_val[6] = {
  UNKNOWN, CHANGED, CAPT_CLOSS, CAPT_DRAW, CAPT_CWIN, CAPT_WIN
};
*/

// check whether all moves end up in wins for the opponent
// if we are here, all captures are losing
static int check_loss(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
    int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = LOSS_IN_ONE;

  int k = *pcs;
  if (k == 0) {
    bb = PieceMoves(p[0], pt[0], occ);
    idx = idx0 & ~mask[0];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove0(idx, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
    k = *(++pcs);
  }
  while (k >= 0) {
    bb = PieceMoves(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove1(idx, k, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
    k = *(++pcs);
  }

  return best;
}

#if 0
#define ASM_GOTO
static int is_attacked(int sq, int *pcs, bitboard occ, int *p)
{
  int k;

  if (bit[sq] & KingRange(p[*pcs])) return 0;
  while (*(++pcs) >= 0) {
    k = *pcs;
    bitboard bb = PieceRange(p[k], pt[k], occ);
#ifdef ASM_GOTO
    jump_bit_set(bb, sq, lab);
#else
    if (bb & bit[sq]) return 1;
#endif
  }
  return 0;
#ifdef ASM_GOTO
lab:
  return 1;
#endif
}
#endif

static int check_mate(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
    int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;

  do {
    int k = *pcs;
    bb = PieceMoves(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove(idx, k, sq);
      if (table[idx2] != ILLEGAL) return 0;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return 1;
}

static void calc_broken(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    FILL_OCC64_cheap {
      for (i = 0, bb = 1; i < 64; i++, bb <<= 1) {
        if (occ & bb)
          table_w[idx + i] = table_b[idx + i] = BROKEN;
        else
          table_w[idx + i] = table_b[idx + i] = UNKNOWN;
      }
    } else {
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = BROKEN;
    }
  }
}

static void calc_mates(struct thread_data *thread)
{
  uint64_t idx, idx2;
  bitboard occ, bb;
  int i;
  int n = numpcs;
  int p[MAX_PIECES];
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    FILL_OCC64 {
      for (i = 0, bb = 1; i < 64; i++, bb <<= 1) {
        if (occ & bb) continue;
        int chk_b = (table_w[idx + i] == ILLEGAL);
        int chk_w = (table_b[idx + i] == ILLEGAL);
        if (chk_w == chk_b) continue;
        p[n - 1] = i;
        if (chk_w) {
          if (table_w[idx + i] == UNKNOWN && check_mate(white_pcs, idx + i, table_b, occ | bb, p))
            table_w[idx + i] = MATE;
        } else {
          if (table_b[idx + i] == UNKNOWN && check_mate(black_pcs, idx + i, table_w, occ | bb, p))
            table_b[idx + i] = MATE;
        }
      }
    }
  }
}

MARK(mark_illegal)
{
  MARK_BEGIN;
  table[idx2] = ILLEGAL;
  MARK_END;
}

MARK(mark_capt_wins)
{
  MARK_BEGIN;
  if (table[idx2] != ILLEGAL)
    table[idx2] = CAPT_WIN;
  MARK_END;
}

MARK(mark_capt_value, uint8_t v)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], v);
  MARK_END;
}

MARK_PIVOT(mark_changed)
{
  MARK_BEGIN_PIVOT;
  if (table[idx2] == UNKNOWN)
    SET_CHANGED(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    if (table[idx3] == UNKNOWN)
      SET_CHANGED(table[idx3]);
  }
  MARK_END;
}

MARK(mark_changed)
{
  MARK_BEGIN;
  if (table[idx2] == UNKNOWN)
    SET_CHANGED(table[idx2]);
  MARK_END;
}

MARK_PIVOT(mark_wins, int v)
{
  MARK_BEGIN_PIVOT;
  if (table[idx2]) {
    SET_WIN_VALUE(table[idx2], v);
    if (PIVOT_ON_DIAG(idx2)) {
      uint64_t idx3 = PIVOT_MIRROR(idx2);
      SET_WIN_VALUE(table[idx3], v);
    }
  }
  MARK_END;
}

MARK(mark_wins, int v)
{
  MARK_BEGIN;
  if (table[idx2])
    SET_WIN_VALUE(table[idx2], v);
  MARK_END;
}

static void calc_illegal_w(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_illegal);
    }
  }
}

static void calc_illegal_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      LOOP_BLACK_PIECES_PIVOT(mark_illegal);
    }
  }
}

static void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[0]);
      for (j = 1; white_pcs[j] >= 0; j++) {
        k = white_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[black_king])) {
          uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
          mark_capt_wins(k, table_w, idx3 & ~mask[k], occ, p);
          continue;
        }
        /* perform capture */
        bitboard bb = occ & ~atom_mask[sq];
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & bb) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. white king not in check */
        if (!(bits & bit[p[black_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!(s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, bb);
            if (bits2 & bit[p[0]]) break;
          }
          if (l < n) continue;
        }
        uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
        int v = probe_tb(pt2, p, 0, bb, -2, 2);
        switch (v) {
        case -2:
          mark_capt_wins(k, table_w, idx3 & ~mask[k], occ, p);
          break;
        case -1:
          has_cursed |= 1;
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          has_cursed |= 2;
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_changed(k, table_w, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[black_king]);
      for (j = 1; black_pcs[j] >= 0; j++) {
        k = black_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[0])) {
          uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
          mark_capt_wins(k, table_b, idx3 & ~mask[k], occ, p);
          continue;
        }
        /* perform capture */
        bitboard bb = occ & ~atom_mask[sq];
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & bb) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. black king not in check */
        if (!(bits & bit[p[white_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!s || (s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, bb);
            if (bits2 & bit[p[black_king]]) break;
          }
          if (l < n) continue;
        }
        uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
        int v = probe_tb(pt2, p, 1, bb, -2, 2);
        switch (v) {
        case -2:
          mark_capt_wins(k, table_b, idx3 & ~mask[k], occ, p);
          break;
        case -1:
          has_cursed |= 1;
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          has_cursed |= 2;
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_changed(k, table_b, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void calc_captures_w(void)
{
  int i, j, k;
  int n = numpcs;

  captured_piece = black_king;
  run_threaded(calc_illegal_w, work_g, 1);

  for (i = 0; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08) || i == black_king) continue;
    for (k = 0, j = 0; black_pcs[k] >= 0; k++)
      if (black_pcs[k] != i)
        pcs2[j++] = black_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(probe_captures_w, work_g, 1);
  }
}

static void calc_captures_b(void)
{
  int i, j, k;
  int n = numpcs;

  run_threaded(calc_illegal_b, work_piv, 1);

  for (i = 0; i < n; i++) { // loop over white pieces
    if ((pt[i] & 0x08) || i == white_king) continue;
    for (k = 0, j = 0; white_pcs[k] >= 0; k++)
      if (white_pcs[k] != i)
        pcs2[j++] = white_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(probe_captures_b, work_g, 1);
  }
}

MARK_PIVOT(mark_win_in_1)
{
  MARK_BEGIN_PIVOT;
  if (table[idx2] != ILLEGAL && table[idx2] != CAPT_WIN) {
    table[idx2] = WIN_IN_ONE;
    if (PIVOT_ON_DIAG(idx2)) {
      uint64_t idx3 = PIVOT_MIRROR(idx2);
      table[idx3] = WIN_IN_ONE;
    }
  }
  MARK_END;
}

MARK(mark_win_in_1)
{
  MARK_BEGIN;
  if (table[idx2] != ILLEGAL && table[idx2] != CAPT_WIN)
    table[idx2] = WIN_IN_ONE;
  MARK_END;
}

uint8_t *iter_table, *iter_table_opp;
int *iter_pcs;
int *iter_pcs_opp;
uint8_t tbl[256];

static void iter(struct thread_data *thread)
{
  BEGIN_ITER;
  int not_fin = 0;
  uint8_t *table = iter_table;
  uint8_t *table_opp = iter_table_opp;
  int *pcs = iter_pcs;
  int *pcs_opp = iter_pcs_opp;

  LOOP_ITER {
    int v = table[idx];
    int w = tbl[v];
    if (!w) continue;
    FILL_OCC;
    not_fin = 1;
    switch (w) {
    case 1: /* CHANGE */
      v = check_loss(pcs, idx, table_opp, occ, p);
      if (v) {
        table[idx] = v;
        RETRO(mark_wins, loss_win[v]);
      } else {
        table[idx] = UNKNOWN;
      }
      break;
    case 2: /* normal WIN, including CAPT_WIN, WIN_IN_ONE */
      RETRO(mark_changed);
      break;
    case 3: /* MATE */
      RETRO(mark_win_in_1);
      break;
    case 4: /* CAPT_CLOSS */
      v  = check_loss(pcs, idx, table_opp, occ, p);
      if (v) {
        if (v > LOSS_IN_ONE - DRAW_RULE)
          v = LOSS_IN_ONE - DRAW_RULE;
        table[idx] = v;
        RETRO(mark_wins, loss_win[v]);
      } else {
        table[idx] = UNKNOWN;
      }
    }
  }

  if (not_fin)
    finished = 0;
}

static int iter_cnt;
static int iter_wtm;

static void run_iter(void)
{
  if (iter_wtm) {
    iter_table = table_w;
    iter_table_opp = table_b;
    iter_pcs = white_pcs;
    iter_pcs_opp = black_pcs;
    printf("Iteration %d ... ", ++iter_cnt);
    fflush(stdout);
  } else {
    iter_table = table_b;
    iter_table_opp = table_w;
    iter_pcs = black_pcs;
    iter_pcs_opp = white_pcs;
  }
  run_threaded(iter, work_g, 0);
  if (!iter_wtm)
    printf("done.\n");
  iter_wtm ^= 1;
}

static void iterate()
{
  int i;
  iter_cnt = 0;
  iter_wtm = 1;

  for (i = 0; i < 256; i++)
    tbl[i] = win_loss[i] = loss_win[i] = 0;

  // ply = n -> find all wins in n, n+1 and potential losses in n, n+1

  ply = 0;
  tbl[MATE] = 3;
  run_iter();

  // ply = 1
  ply++;
  tbl[CHANGED] = 1;
  tbl[CAPT_WIN] = tbl[WIN_IN_ONE] = 2;
  win_loss[ILLEGAL] = 0xff;
  loss_win[LOSS_IN_ONE] = WIN_IN_ONE + 1;
  run_iter();

  // ply = 2
  ply++;
  tbl[MATE] = 0;
  tbl[WIN_IN_ONE + 1] = 2;
  win_loss[CAPT_WIN] = LOSS_IN_ONE - 1;
  win_loss[WIN_IN_ONE] = LOSS_IN_ONE - 1;
  loss_win[LOSS_IN_ONE - 1] = WIN_IN_ONE + 2;
  run_iter();

  tbl[CAPT_WIN] = 0;
  finished = 0;
  while (!finished && ply < DRAW_RULE - 1) {
    finished = 1;
    ply++;
    tbl[WIN_IN_ONE + ply - 3] = 0;
    tbl[WIN_IN_ONE + ply - 1] = 2;
    win_loss[WIN_IN_ONE + ply - 2] = LOSS_IN_ONE - ply + 1;
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply;
    run_iter();
  }

  tbl[WIN_IN_ONE + ply - 2] = 0;
  if (!finished) {
    finished = 1;
    // ply = 100
    ply++;
    tbl[WIN_IN_ONE + ply - 1] = 2;
    win_loss[WIN_IN_ONE + ply - 2] = LOSS_IN_ONE - ply + 1;
    // skip WIN_IN_ONE + 100, which is CAPT_CWIN
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 1;
    run_iter();
    tbl[WIN_IN_ONE + ply - 2] = 0;
  }
  tbl[WIN_IN_ONE + ply - 1] = 0;

  if (!finished || has_cursed_capts) {
    finished = 1;
    ply = DRAW_RULE + 1;
    tbl[WIN_IN_ONE + ply - 2] = 2;
    tbl[CAPT_CWIN] = 2;
    tbl[WIN_IN_ONE + ply] = 2;
    win_loss[WIN_IN_ONE + ply - 2] = LOSS_IN_ONE - ply + 1;
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 1;
    tbl[CAPT_CLOSS] = 4;
    run_iter();

    // ply = 102
    ply++;
    tbl[WIN_IN_ONE + ply - 3] = 0;
    tbl[WIN_IN_ONE + ply] = 2;
    win_loss[CAPT_CWIN] = LOSS_IN_ONE - ply + 1;
    win_loss[WIN_IN_ONE + ply - 1] = LOSS_IN_ONE - ply + 1;
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 1;
    run_iter();

    tbl[CAPT_CWIN] = 0;

    while (!finished && ply < REDUCE_PLY) {
      finished = 1;
      ply++;
      tbl[WIN_IN_ONE + ply - 2] = 0;
      tbl[WIN_IN_ONE + ply] = 2;
      win_loss[WIN_IN_ONE + ply - 1] = LOSS_IN_ONE - ply + 1;
      loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 1;
      run_iter();
    }

    tbl[WIN_IN_ONE + ply - 1] = 0;
    tbl[WIN_IN_ONE + ply] = 0;

    while (!finished) {
      reduce_tables();
      num_saves++;

      for (i = 0; i < 256; i++)
        win_loss[i] = loss_win[i] = 0;
      for (i = 0; i <= CAPT_CWIN_RED + 1; i++)
        win_loss[i] = 0xff;

      ply = 0;
      tbl[CAPT_CWIN_RED + ply + 3] = 2;
      win_loss[CAPT_CWIN_RED + ply + 2] = LOSS_IN_ONE - ply - 1;
      loss_win[LOSS_IN_ONE - ply - 1] = CAPT_CWIN_RED + ply + 4;

      while (ply < REDUCE_PLY_RED && !finished) {
        finished = 1;
        ply++;
        tbl[CAPT_CWIN_RED + ply + 1] = 0;
        tbl[CAPT_CWIN_RED + ply + 3] = 2;
        win_loss[CAPT_CWIN_RED + ply + 2] = LOSS_IN_ONE - ply - 1;
        loss_win[LOSS_IN_ONE - ply - 1] = CAPT_CWIN_RED + ply + 4;
        run_iter();
      }

      tbl[CAPT_CWIN_RED + ply + 2] = 0;
      tbl[CAPT_CWIN_RED + ply + 3] = 0;
    }
  }
}

static uint8_t *reset_v;

MARK(reset_capt_closs)
{
  uint8_t *v = reset_v;

  MARK_BEGIN;
  if (v[table[idx2]]) table[idx2] = CAPT_CLOSS;
  MARK_END;
}

static void reset_captures_worker_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[0]);
      for (j = 1; white_pcs[j] >= 0; j++) {
        k = white_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[black_king])) continue;
        /* perform capture */
        bitboard bb = occ & ~atom_mask[sq];
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & bb) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. white king not in check */
        if (!(bits & bit[p[black_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!(s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, bb);
            if (bits2 & bit[p[0]]) break;
          }
          if (l < n) continue;
        }
        int v = probe_tb(pt2, p, 0, bb, 0, 2);
        if (v == 1) {
          uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
          reset_capt_closs(k, table_w, idx3 & ~mask[k], occ, p);
        }
      }
    }
  }
}

static void reset_captures_worker_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[black_king]);
      for (j = 1; black_pcs[j] >= 0; j++) {
        k = black_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[0])) continue;
        /* perform capture */
        bitboard bb = occ & ~atom_mask[sq];
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & bb) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. black king not in check */
        if (!(bits & bit[p[0]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!s || (s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, bb);
            if (bits2 & bit[p[black_king]]) break;
          }
          if (l < n) continue;
        }
        int v = probe_tb(pt2, p, 1, bb, 0, 2);
        if (v == 1) {
          uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
          reset_capt_closs(k, table_b, idx3 & ~mask[k], occ, p);
        }
      }
    }
  }
}

static void reset_captures_w(void)
{
  int i, j, k;
  int n = numpcs;
  uint8_t v[256];

  reset_v = v;

  for (i = 0; i < 256; i++)
    v[i] = 0;

  if (num_saves == 0)
    for (i = DRAW_RULE; i < REDUCE_PLY; i++)
      v[LOSS_IN_ONE - i] = 1;
  else
    for (i = 0; i <= REDUCE_PLY_RED + 1; i++)
      v[LOSS_IN_ONE - i] = 1;

  to_fix_w = 0;

  for (i = 0; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08) || i == black_king) continue;
    if (!(cursed_capt[i] & 2)) continue;
    if (!to_fix_w) {
      printf("Resetting white cursed losses.\n");
      to_fix_w = 1;
    }
    for (k = 0, j = 0; black_pcs[k] >= 0; k++)
      if (black_pcs[k] != i)
        pcs2[j++] = black_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(reset_captures_worker_w, work_g, 1);
  }
}

static void reset_captures_b(void)
{
  int i, j, k;
  int n = numpcs;
  uint8_t v[256];

  reset_v = v;

  for (i = 0; i < 256; i++)
    v[i] = 0;

  if (num_saves == 0)
    for (i = DRAW_RULE; i < REDUCE_PLY; i++)
      v[LOSS_IN_ONE - i] = 1;
  else
    for (i = 0; i <= REDUCE_PLY_RED + 1; i++)
      v[LOSS_IN_ONE - i] = 1;

  to_fix_b = 0;

  for (i = 0; i < n; i++) { // loop over white pieces
    if ((pt[i] & 0x08) || i == white_king) continue;
    if (!(cursed_capt[i] & 2)) continue;
    if (!to_fix_b) {
      printf("Resetting black cursed losses.\n");
      to_fix_b = 1;
    }
    for (k = 0, j = 0; white_pcs[k] >= 0; k++)
      if (white_pcs[k] != i)
        pcs2[j++] = white_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(reset_captures_worker_b, work_g, 1);
  }
}

// CAPT_CLOSS means there is a capture into a cursed win, preventing a loss
// we need to determine if there are regular moves into a slower cursed loss
static int compute_capt_closs(int *pcs, uint64_t idx0, uint8_t *table,
    bitboard occ, int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = 0;

  do {
    int k = *pcs;
    bb = PieceMoves(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove(idx, k, sq);
      int v = table[idx2];
      if (v > best) best = v;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return best;
}

static void fix_closs_worker_w(struct thread_data *thread)
{
  BEGIN_ITER;

  LOOP_ITER {
    if (table_w[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(white_pcs, idx, table_b, occ, p);
    table_w[idx] = win_loss[v];
  }
}

static void fix_closs_worker_b(struct thread_data *thread)
{
  BEGIN_ITER;

  LOOP_ITER {
    if (table_b[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(black_pcs, idx, table_w, occ, p);
    table_b[idx] = win_loss[v];
  }
}

static void fix_closs_w(void)
{
  int i;

  if (!to_fix_w) return;

  for (i = 0; i < 256; i++)
    win_loss[i] = 0;
  if (num_saves == 0) {
    // if no legal moves or all moves lose, then CLOSS capture was best
    for (i = 0; i < CAPT_CWIN; i++)
      win_loss[i] = LOSS_IN_ONE - DRAW_RULE;
    win_loss[CAPT_CWIN] = LOSS_IN_ONE - DRAW_RULE - 1;
    for (i = DRAW_RULE; i < REDUCE_PLY - 1; i++)
      win_loss[WIN_IN_ONE + i + 1] = LOSS_IN_ONE - i - 1;
  } else {
    // CAPT_CLOSS will be set to 0, then overridden by what was saved before
    for (i = 0; i < CAPT_CWIN_RED + 2; i++)
      win_loss[i] = CAPT_CLOSS;
    for (i = 0; i < REDUCE_PLY_RED; i++)
      win_loss[CAPT_CWIN_RED + i + 2] = LOSS_IN_ONE - i - 1;
  }

  if (to_fix_w) {
    printf("fixing cursed white losses.\n");
    run_threaded(fix_closs_worker_w, work_g, 1);
  }
}

static void fix_closs_b(void)
{
  int i;

  if (!to_fix_b) return;

  for (i = 0; i < 256; i++)
    win_loss[i] = 0;
  if (num_saves == 0) {
    // if no legal moves or all moves lose, then CLOSS capture was best
    for (i = 0; i < CAPT_CWIN; i++)
      win_loss[i] = LOSS_IN_ONE - DRAW_RULE;
    win_loss[CAPT_CWIN] = LOSS_IN_ONE - DRAW_RULE - 1;
    for (i = DRAW_RULE; i < REDUCE_PLY - 1; i++)
      win_loss[WIN_IN_ONE + i + 1] = LOSS_IN_ONE - i - 1;
  } else {
    // CAPT_CLOSS will be set to 0, then overridden by what was saved before
    for (i = 0; i < CAPT_CWIN_RED + 2; i++)
      win_loss[i] = CAPT_CLOSS;
    for (i = 0; i < REDUCE_PLY_RED; i++)
      win_loss[CAPT_CWIN_RED + i + 2] = LOSS_IN_ONE - i - 1;
  }

  if (to_fix_b) {
    printf("fixing cursed black losses.\n");
    run_threaded(fix_closs_worker_b, work_g, 1);
  }
}
