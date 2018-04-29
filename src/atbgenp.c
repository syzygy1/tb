/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define REDUCE_PLY 121 // checked
#define REDUCE_PLY_RED 119 // checked

#define STAT_DRAW (MAX_STATS/2)
#define STAT_PAWN_WIN (STAT_DRAW - 3)
#define STAT_CAPT_CWIN (STAT_DRAW - 2)
#define STAT_CAPT_WIN (STAT_DRAW - 1)
#define STAT_CAPT_DRAW (STAT_DRAW + 1)
#define STAT_PAWN_CWIN (STAT_DRAW + 2)
#define STAT_MATE (MAX_STATS - 1)

#define MAX_PLY (STAT_DRAW - 4)
#define MIN_PLY_WIN 1
#define MIN_PLY_LOSS 1

#define ILLEGAL 0
#define BROKEN 0xff
#define UNKNOWN 0xfe
#define CHANGED 0xfd
#define CAPT_CLOSS 0xfc
#define PAWN_DRAW 0xfb
#define CAPT_DRAW 0xfa
#define MATE 0xf9
#define LOSS_IN_ONE 0xf8
#define CAPT_WIN 1
#define PAWN_WIN 2
#define WIN_IN_ONE 3
#define CAPT_CWIN (WIN_IN_ONE + DRAW_RULE)
#define PAWN_CWIN (CAPT_CWIN + 1)
#define WIN_RED 2
#define CAPT_CWIN_RED (WIN_RED + 1)

#define SET_CHANGED(x) \
{ uint8_t dummy = CHANGED; \
__asm__( \
"movb %2, %%al\n\t" \
"lock cmpxchgb %1, %0" \
: "+m" (x), "+r" (dummy) : "i" (UNKNOWN) : "eax"); }

#define SET_CAPT_VALUE(x,v) \
{ uint8_t dummy = v; __asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); }

#define SET_WIN_VALUE(x,v) \
{ uint8_t dummy = v; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); }

uint8_t win_loss[256];
uint8_t loss_win[256];
char tbl_to_wdl[256];
uint8_t wdl_to_tbl[8] = {
  0xff, 0xff, CHANGED, CAPT_CLOSS, CAPT_DRAW, CAPT_CWIN, CAPT_WIN, 0xff
};
uint8_t wdl_to_tbl_pawn[8] = {
  0xff, 0xff, CHANGED, CAPT_CLOSS, PAWN_DRAW, PAWN_CWIN, PAWN_WIN, 0xff
};

static void set_tbl_to_wdl(int saves)
{
  int i;

  for (i = 0; i < 256; i++)
    tbl_to_wdl[i] = 0;
  tbl_to_wdl[ILLEGAL] = -4;
  if (saves == 0) {
    for (i = CAPT_WIN; i < CAPT_CWIN; i++)
      tbl_to_wdl[i] = -2;
    for (; i <= WIN_IN_ONE + REDUCE_PLY + 1; i++)
      tbl_to_wdl[i] = -1;
    for (i = MATE; i > LOSS_IN_ONE - DRAW_RULE; i--)
      tbl_to_wdl[i] = 2;
    for (; i >= LOSS_IN_ONE - REDUCE_PLY + 1; i--)
      tbl_to_wdl[i] = 1;
  } else {
    for (i = CAPT_WIN; i < CAPT_CWIN_RED; i++)
      tbl_to_wdl[i] = -2;
    for (; i <= CAPT_CWIN_RED + REDUCE_PLY_RED + 3; i++)
      tbl_to_wdl[i] = -1;
    tbl_to_wdl[MATE] = 2;
    for (i = LOSS_IN_ONE; i >= LOSS_IN_ONE - REDUCE_PLY_RED - 1; i--)
      tbl_to_wdl[i] = 1;
  }
}

// check whether all moves end up in wins for the opponent
// we already know there are no legal captures
static int check_loss(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
    int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = LOSS_IN_ONE;

  do {
    int k = *pcs;
    bb = PieceMoves(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove(idx, k, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return best;
}

static int is_attacked(int sq, int *pcs, bitboard occ, int *p)
{
  int k;

  if (bit[sq] & KingRange(p[*pcs])) return 0;
  while (*(++pcs) >= 0) {
    k = *pcs;
    bitboard bb = PieceRange(p[k], pt[k], occ);
    if (bb & bit[sq]) return 1;
  }
  return 0;
}

// pawn moves and captures should be taken care of already
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
      for (i = 0, bb = 1ULL; i < 64; i++, bb <<= 1)
        if (occ & bb)
          table_w[idx + i] = table_b[idx + i] = BROKEN;
        else
          table_w[idx + i] = table_b[idx + i] = UNKNOWN;
    } else {
      // two or more on one square or a pawn on rank 1 or 8
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = BROKEN;
    }
  }
}

static void calc_mates(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (table_w[idx] == BROKEN) continue;
    int chk_b = (table_w[idx] == ILLEGAL);
    int chk_w = (table_b[idx] == ILLEGAL);
    if (chk_w == chk_b) continue;
    FILL_OCC_PIECES;
    if (chk_w) {
      if (table_w[idx] == UNKNOWN && check_mate(white_pcs, idx, table_b, occ, p))
        table_w[idx] = MATE;
    } else {
      if (table_b[idx] == UNKNOWN && check_mate(black_pcs, idx, table_w, occ, p))
        table_b[idx] = MATE;
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

MARK(mark_changed)
{
  MARK_BEGIN;
  if (table[idx2] == UNKNOWN)
    SET_CHANGED(table[idx2]);
  MARK_END;
}

MARK(mark_wins, int v)
{
  MARK_BEGIN;
  if (table[idx2])
    SET_WIN_VALUE(table[idx2], v);
  MARK_END;
}

MARK(mark_win_in_1)
{
  MARK_BEGIN;
  if (table[idx2] != ILLEGAL && table[idx2] != CAPT_WIN)
    table[idx2] = WIN_IN_ONE;
  MARK_END;
}

static int captured_piece;

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
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_illegal);
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
      bitboard bits = KingRange(p[white_king]);
      if (i < numpawns) bits |= 0xff000000000000ffULL;
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
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. white king not in check */
        if (!(KingRange(p[white_king]) & bit[p[black_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!(s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[white_king]]) break;
          }
          if (l < n) continue;
        }
        uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
        int v = probe_tb(pt2, p, 0, occ2, -2, 2);
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
      if (i < numpawns) bits |= 0xff000000000000ffULL;
      for (j = 1; black_pcs[j] >= 0; j++) {
        k = black_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[white_king])) {
          uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]);
          mark_capt_wins(k, table_b, idx3 & ~mask[k], occ, p);
          continue;
        }
        /* perform capture */
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. black king not in check */
        if (!(KingRange(p[black_king]) & bit[p[white_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!s || (s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[black_king]]) break;
          }
          if (l < n) continue;
        }
        uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]);
        int v = probe_tb(pt2, p, 1, occ2, -2, 2);
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

static void probe_pivot_captures(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;
  int has_cursed = 0;
  p[0] = 0;
  pt2[0] = 0;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      bitboard bits = KingRange(p[king]);
      for (j = 1; pcs[j] >= 0; j++) {
        k = pcs[j];
        int sq = p[k];
        if (!piv_valid[sq]) continue;
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[opp_king])) {
          uint64_t idx3 = idx2 | piv_idx[p[k]];
          mark_capt_wins(k, table, idx3 & ~mask[k], occ, p);
          continue;
        }
        /* perform capture */
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 1; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        /* check whether capture is legal, i.e. own king not in check */
        if (!(bits & bit[p[opp_king]])) {
          for (l = 1; l < n; l++) {
            int s = pt2[l];
            if (!s || (s >> 3) == wtm) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[king]]) break;
          }
          if (l < n) continue;
        }
        uint64_t idx3 = idx2 | piv_idx[p[k]];
        int v = probe_tb(pt2, p, wtm, occ2, -2, 2);
        switch (v) {
        case -2:
          mark_capt_wins(k, table, idx3 & ~mask[k], occ, p);
          break;
        case -1:
          has_cursed |= 1;
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          has_cursed |= 2;
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_changed(k, table, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }

  if (has_cursed) cursed_capt[0] |= has_cursed;
}

static int capturing_pawn;

static void calc_pawn_illegal_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int k = capturing_pawn;
  int l = black_king;
  int m = white_king;

  LOOP_ITER_ALL {
    if (table_w[idx] != UNKNOWN) continue;
    FILL_OCC;
    if (white_pawn_range[p[k]] & bit[p[l]] & ~king_range[p[m]])
      table_w[idx] = ILLEGAL;
  }
}

static void calc_pawn_illegal_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int k = capturing_pawn;
  int l = white_king;
  int m = black_king;

  LOOP_ITER_ALL {
    if (table_b[idx] != UNKNOWN) continue;
    FILL_OCC;
    if (black_pawn_range[p[k]] & bit[p[l]] & ~king_range[p[m]])
      table_b[idx] = ILLEGAL;
  }
}

static void calc_captures_w(void)
{
  int i, j, k;
  int n = numpcs;

  captured_piece = black_king;
  run_threaded(calc_illegal_w, work_g, 1);

  for (i = 0; i < numpawns; i++) {
    if (!pw[i]) continue;
    capturing_pawn = i;
    run_threaded(calc_pawn_illegal_w, work_g, 1);
  }

  for (i = 0; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08) || i == black_king) continue;
    for (k = 0, j = 0; black_all[k] >= 0; k++)
      if (black_all[k] != i)
        pcs2[j++] = black_all[k];
    pcs2[j] = -1;
    if (i) {
      captured_piece = i;
      run_threaded(probe_captures_w, work_g, 1);
    } else
      run_threaded(probe_pivot_captures, work_piv, 1);
  }
}

static void calc_captures_b(void)
{
  int i, j, k;
  int n = numpcs;

  captured_piece = white_king;
  run_threaded(calc_illegal_b, work_g, 1);

  for (i = 0; i < numpawns; i++) {
    if (pw[i]) continue;
    capturing_pawn = i;
    run_threaded(calc_pawn_illegal_b, work_g, 1);
  }

  for (i = 0; i < n; i++) { // loop over white pieces
    if ((pt[i] & 0x08) || i == white_king) continue;
    for (k = 0, j = 0; white_all[k] >= 0; k++)
      if (white_all[k] != i)
        pcs2[j++] = white_all[k];
    pcs2[j] = -1;
    if (i) {
      captured_piece = i;
      run_threaded(probe_captures_b, work_g, 1);
    } else
      run_threaded(probe_pivot_captures, work_piv, 1);
  }
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
    FILL_OCC_PIECES;
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

static int iter_wtm;

static void run_iter(void)
{
  if (iter_wtm) {
    iter_table = table_w;
    iter_table_opp = table_b;
    iter_pcs = white_pcs;
    iter_pcs_opp = black_pcs;
  } else {
    iter_table = table_b;
    iter_table_opp = table_w;
    iter_pcs = black_pcs;
    iter_pcs_opp = white_pcs;
  }
  if (slice_threading_high)
    run_threaded(iter, work_p, 0);
  else
    run_single(iter, work_p, 0);
  iter_wtm ^= 1;
}

static void iterate()
{
  int i;
  iter_wtm = 0;
  int local_num_saves = 0;

  // FIXME: replace by something more incremental
  for (i = 0; i < 256; i++)
    tbl[i] = win_loss[i] = loss_win[i] = 0;

  // ply = n -> find all wins in n, n+1 and potential losses in n, n+1

  ply = 0;
  tbl[MATE] = 3;
  run_iter();

  // ply = 1
  ply++;
  tbl[CHANGED] = 1;
  tbl[CAPT_WIN] = tbl[PAWN_WIN] = tbl[WIN_IN_ONE] = 2;
  win_loss[ILLEGAL] = 0xff;
  loss_win[LOSS_IN_ONE] = WIN_IN_ONE + 1;
  run_iter();

  // ply = 2
  ply++;
  tbl[MATE] = 0;
  tbl[WIN_IN_ONE + 1] = 2;
  win_loss[CAPT_WIN] = LOSS_IN_ONE - 1;
  win_loss[PAWN_WIN] = LOSS_IN_ONE - 1;
  win_loss[WIN_IN_ONE] = LOSS_IN_ONE - 1;
  loss_win[LOSS_IN_ONE - 1] = WIN_IN_ONE + 2;
  run_iter();

  tbl[CAPT_WIN] = tbl[PAWN_WIN] = 0;
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
    // and skip WIN_IN_ONE + 101, which is PAWN_CWIN
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 2;
    run_iter();
    tbl[WIN_IN_ONE + ply - 2] = 0;
  }
  tbl[WIN_IN_ONE + ply - 1] = 0;

  if (!finished || has_cursed_capts || has_cursed_pawn_moves) {
    finished = 1;
    ply = DRAW_RULE + 1;
    tbl[WIN_IN_ONE + ply - 2] = 2;
    tbl[CAPT_CWIN] = tbl[PAWN_CWIN] = 2;
    tbl[WIN_IN_ONE + ply + 1] = 2;
    win_loss[WIN_IN_ONE + ply - 2] = LOSS_IN_ONE - ply + 1;
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 2;
    tbl[CAPT_CLOSS] = 4;
    run_iter();

    // ply = 102
    ply++;
    tbl[WIN_IN_ONE + ply - 3] = 0;
    tbl[WIN_IN_ONE + ply + 1] = 2;
    win_loss[CAPT_CWIN] = win_loss[PAWN_CWIN] = LOSS_IN_ONE - ply + 1;
    win_loss[WIN_IN_ONE + ply] = LOSS_IN_ONE - ply + 1;
    loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 2;
    run_iter();

    tbl[CAPT_CWIN] = tbl[PAWN_CWIN] = 0;

    while (!finished && ply < REDUCE_PLY) {
      finished = 1;
      ply++;
      tbl[WIN_IN_ONE + ply - 1] = 0;
      tbl[WIN_IN_ONE + ply + 1] = 2;
      win_loss[WIN_IN_ONE + ply] = LOSS_IN_ONE - ply + 1;
      loss_win[LOSS_IN_ONE - ply + 1] = WIN_IN_ONE + ply + 2;
      run_iter();
    }

    tbl[WIN_IN_ONE + ply] = 0;
    tbl[WIN_IN_ONE + ply + 1] = 0;

    while (!finished) {
      if (num_saves == 0)
        set_tbl_to_wdl(1);
      reduce_tables(local_num_saves);
      local_num_saves++;

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

  while (local_num_saves < num_saves) {
    reduce_tables(local_num_saves);
    local_num_saves++;
  }
}

// returns -4, -2, -1, 0, 1, 2
static int probe_pawn_capt(int k, int sq, uint64_t idx, int king, int opp_king,
    int wtm, bitboard occ, bitboard pawns, int *p)
{
  int i;
  int best = -4;
  int pt2[MAX_PIECES];
  bitboard bits;

  for (bits = sides_mask[sq] & occ; bits; ClearFirst(bits)) {
    int sq2 = FirstOne(bits);
    if (bit[sq2] & KingRange(p[king])) continue;
    int t;
    for (t = 0; p[t] != sq2; t++);
    if ((pt[t] >> 3) == wtm) continue;
    if (bit[sq2] & KingRange(p[opp_king])) return 2;
    bitboard bb = occ & ~(bit[sq2] | (king_range[sq2] & ~pawns));
    for (i = 0; i < numpcs; i++)
      pt2[i] = (bit[p[i]] & bb) ? pt[i] : 0;
    if (!(bit[p[king]] & KingRange(p[opp_king]))) {
      for (i = 0; i < numpcs; i++) {
        int s = pt2[i];
        if (!s || (s >> 3) == wtm) continue;
        bitboard bits = PieceRange(p[i], s, bb);
        if (bits & bit[p[king]]) break;
      }
      if (i < numpcs) continue;
    }
    int v = -probe_tb(pt2, p, wtm, occ, -2, 2);
    if (v > 1) return v;
    if (v > best) best = v;
  }

  return best;
}

static uint8_t wdl_to_cursed[8] = {
  0, 0, 0, 1, 0, 2, 0, 0
};

// TODO: consider updating 'alpha'
static void calc_pawn_captures_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;
  uint8_t has_cursed = 0;

  LOOP_ITER_ALL {
    if (table_w[idx] == BROKEN || table_w[idx] <= CAPT_WIN) continue;
    int w = table_w[idx];
    FILL_OCC_PAWNS_PIECES;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      int val = probe_pawn_capt(i, p[i] + 0x08, idx & ~mask[i], white_king, black_king, 0, occ & ~bit[p[i]], pawns, p);
      if (val > v) v = val;
    }
    has_cursed |= wdl_to_cursed[v + 4];
    v = wdl_to_tbl[v + 4];
    if (v < w) table_w[idx] = v;
  }

  if (has_cursed) cursed_pawn_capt_w |= has_cursed;
}

static void calc_pawn_captures_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;
  uint8_t has_cursed = 0;

  LOOP_ITER_ALL {
    if (table_b[idx] == BROKEN || table_b[idx] <= CAPT_WIN) continue;
    int w = table_b[idx];
    FILL_OCC_PAWNS_PIECES;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      int val = probe_pawn_capt(i, p[i] - 0x08, idx & ~mask[i], black_king, white_king, 1, occ & ~bit[p[i]], pawns, p);
      if (val > v) v = val;
    }
    has_cursed |= wdl_to_cursed[v + 4];
    v = wdl_to_tbl[v + 4];
    if (v < w) table_b[idx] = v;
  }

  if (has_cursed) cursed_pawn_capt_b |= has_cursed;
}

// now returns -2, -1, 0, 1, 2, 3
static int eval_ep(int k, int l, int sq, int ep, int king, int opp_king,
    int wtm, bitboard occ, bitboard pawns, int *p)
{
  int i;
  int pt2[MAX_PIECES];

  if (king_range[ep] & bit[p[king]]) return 3;
  if (king_range[ep] & bit[p[opp_king]]) return -2;
  occ &= ~(bit[sq] | bit[p[k]] | (king_range[ep] & ~pawns));
  for (i = 0; i < numpcs; i++)
    pt2[i] = (bit[p[i]] & occ) ? pt[i] : 0;

  bitboard king_bb = bit[p[king]];
  if (!(king_bb & KingRange(p[opp_king]))) {
    for (i = 0; i < numpcs; i++) {
      int s = pt2[i];
      if (!s || (s >> 3) == wtm) continue;
      if (PieceRange(p[i], s, occ) & king_bb) break;
    }
    if (i < numpcs)
      return 3;
  }
  return probe_tb(pt2, p, wtm, occ, -2, 2);
}

static int has_moves(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
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
      if (table[idx2] != ILLEGAL) return 1;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return 0;
}

static void calc_pawn_moves_w(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, k;
  int n = numpcs;
  int best;
  int sq;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;
  int pos[MAX_PIECES];
  int pt2[MAX_PIECES];
  int has_cursed = 0;

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (table_w[idx] == ILLEGAL || table_w[idx] == BROKEN || table_w[idx] == CAPT_WIN) continue;
    FILL_OCC_PIECES;
    best = -4;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue;
      sq = p[i];
      if (bit[sq + 8] & occ) continue;
      if (sq >= 0x30) {
        bitboard bb = occ ^ bit[sq] ^ bit[sq + 8];
        if (is_attacked(p[white_king], black_all, bb, p))
          continue;
        for (k = 0; k < n; k++) {
          pos[k] = p[k];
          pt2[k] = pt[k];
        }
        pos[i] = sq + 8;
        if (best < -2) best = -2;
        for (k = WQUEEN; k >= WKNIGHT; k--) {
          pt2[i] = k;
          int v = -probe_tb(pt2, pos, 0, bb, -2, -best);
          if (v > best) {
            best = v;
            if (best == 2) goto lab;
          }
        }
      } else {
        uint64_t idx0 = idx & ~mask[i];
        if (i) idx2 = idx0 | ((uint64_t)((p[i] + 0x08) ^ 0x38) << shift[i]);
        else idx2 = idx0 | piv_idx[p[i] + 0x08];
        if (tbl_to_wdl[table_b[idx2]] > best) best = tbl_to_wdl[table_b[idx2]];
        if (sq < 0x10 && !(bit[sq + 0x10] & occ)) {
          if (i) idx2 = idx0 | ((uint64_t)((p[i] + 0x10) ^ 0x38) << shift[i]);
          else idx2 = idx0 | piv_idx[p[i] + 0x10];
          if (table_b[idx2] == ILLEGAL) continue;
          int v0, v1 = 3;
          bitboard bits = sides_mask[p[i] + 0x10] & bb;
          while (bits) {
            int sq2 = FirstOne(bits);
            for (k = 0; p[k] != sq2; k++);
            if (pt[k] == BPAWN) {
              v0 = eval_ep(i, k, sq2, p[i] + 0x08, black_king, white_king, 1, occ, bb, p);
              if (v0 < v1) v1 = v0;
            }
            ClearFirst(bits);
          }
          int v = tbl_to_wdl[table_b[idx2]];
          if (v1 < 3) {
            if (v1 < v || (table_b[idx2] == UNKNOWN && !has_moves(black_pcs, idx2, table_w, occ ^ bit[p[i]] ^ bit[p[i] + 0x10], p)))
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
    if (wdl_to_tbl_pawn[best + 4] < table_w[idx]) {
      has_cursed |= best;
      table_w[idx] = wdl_to_tbl_pawn[best + 4];
    }
  }

  if (has_cursed & 1)
    has_cursed_pawn_moves = 1;
}

static void calc_pawn_moves_b(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, k;
  int n = numpcs;
  int best;
  int sq;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;
  int pos[MAX_PIECES];
  int pt2[MAX_PIECES];
  int has_cursed = 0;

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (table_b[idx] == ILLEGAL || table_b[idx] == BROKEN || table_b[idx] == CAPT_WIN) continue;
    FILL_OCC_PIECES;
    best = -4;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue;
      sq = p[i];
      if (bit[sq - 8] & occ) continue;
      if (sq < 0x10) {
        bitboard bb = occ ^ bit[sq] ^ bit[sq - 8];
        if (is_attacked(p[black_king], white_all, bb, p))
          continue;
        for (k = 0; k < numpcs; k++) {
          pos[k] = p[k];
          pt2[k] = pt[k];
        }
        pos[i] = sq - 8;
        if (best < -2) best = -2;
        for (k = BQUEEN; k >= BKNIGHT; k--) {
          pt2[i] = k;
          int v = -probe_tb(pt2, pos, 1, bb, -2, -best);
          if (v > best) {
            best = v;
            if (best == 2) goto lab;
          }
        }
      } else {
        uint64_t idx0 = idx & ~mask[i];
        if (i) idx2 = idx0 | ((uint64_t)(p[i] - 0x08) << shift[i]);
        else idx2 = idx0 | piv_idx[p[i] - 0x08];
        if (tbl_to_wdl[table_w[idx2]] > best) best = tbl_to_wdl[table_w[idx2]];
        if (sq >= 0x30 && !(bit[sq - 0x10] & occ)) {
          if (i) idx2 = idx0 | ((uint64_t)(p[i] - 0x10) << shift[i]);
          else idx2 = idx0 | piv_idx[p[i] - 0x10];
          if (table_w[idx2] == ILLEGAL) continue;
          int v0, v1 = 3;
          bitboard bits = sides_mask[p[i] - 0x10] & bb;
          while (bits) {
            int sq2 = FirstOne(bits);
            for (k = 0; p[k] != sq2; k++);
            if (pt[k] == WPAWN) {
              v0 = eval_ep(i, k, sq2, p[i] - 0x08, white_king, black_king, 0, occ, bb, p);
              if (v0 < v1) v1 = v0;
            }
            ClearFirst(bits);
          }
          int v = tbl_to_wdl[table_w[idx2]];
          if (v1 < 3) {
            if (v1 < v || (table_w[idx2] == UNKNOWN && !has_moves(white_pcs, idx2, table_b, occ ^ bit[p[i]] ^ bit[p[i] - 0x10], p)))
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
    if (wdl_to_tbl_pawn[best + 4] < table_b[idx]) {
      has_cursed |= best;
      table_b[idx] = wdl_to_tbl_pawn[best + 4];
    }
  }

  if (has_cursed & 1)
    has_cursed_pawn_moves = 1;
}

static uint8_t reset_v[256];

MARK(reset_capt_closs)
{
  uint8_t *v = reset_v;

  MARK_BEGIN;
  if (v[table[idx2]]) table[idx2] = CAPT_CLOSS;
  MARK_END;
}

static void reset_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[white_king]);
      if (i < numpawns) bits |= 0xff000000000000ffULL;
      for (j = 1; white_pcs[j] >= 0; j++) {
        k = white_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[black_king])) continue;
        /* perform capture */
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. white king not in check */
        if (!(KingRange(p[white_king]) & bit[p[black_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!(s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[white_king]]) break;
          }
          if (l < n) continue;
        }
        int v = probe_tb(pt2, p, 0, occ2, 0, 2);
        if (v == 1) {
          uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
          reset_capt_closs(k, table_w, idx3 & ~mask[k], occ, p);
        }
      }
    }
  }
}

static void reset_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  p[i] = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      bitboard bits = KingRange(p[black_king]);
      if (i < numpawns) bits |= 0xff000000000000ffULL;
      for (j = 1; black_pcs[j] >= 0; j++) {
        k = black_pcs[j];
        int sq = p[k];
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[white_king])) continue;
        /* perform capture */
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 0; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        pt2[i] = 0;
        /* check whether capture is legal, i.e. black king not in check */
        if (!(KingRange(p[black_king]) & bit[p[white_king]])) {
          for (l = 0; l < n; l++) {
            int s = pt2[l];
            if (!s || (s & 0x08)) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[black_king]]) break;
          }
          if (l < n) continue;
        }
        int v = probe_tb(pt2, p, 1, occ2, 0, 2);
        if (v == 1) {
          uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]);
          reset_capt_closs(k, table_b, idx3 & ~mask[k], occ, p);
        }
      }
    }
  }
}

static void reset_pivot_captures(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;
  pt2[0] = 0;
  p[0] = 0;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      bitboard bits = KingRange(p[king]);
      for (j = 1; pcs[j] >= 0; j++) {
        k = pcs[j];
        int sq = p[k];
        if (!piv_valid[sq]) continue;
        if (bit[sq] & bits) continue;
        if (bit[sq] & KingRange(p[opp_king])) continue;
        /* perform capture */
        bitboard occ2 = occ & ~(bit[sq] | (king_range[sq] & ~bb));
        int l;
        for (l = 1; l < n; l++)
          pt2[l] = (bit[p[l]] & occ2) ? pt[l] : 0;
        /* check whether capture is legal, i.e. own king not in check */
        if (!(bits & bit[p[opp_king]])) {
          for (l = 1; l < n; l++) {
            int s = pt2[l];
            if (!s || (s >> 3) == wtm) continue;
            bitboard bits2 = PieceRange(p[l], s, occ2);
            if (bits2 & bit[p[king]]) break;
          }
          if (l < n) continue;
        }
        int v = probe_tb(pt2, p, wtm, occ2, 0, 2);
        if (v == 1) {
          uint64_t idx3 = idx2 | piv_idx[p[k]];
          reset_capt_closs(k, table, idx3 & ~mask[k], occ, p);
        }
      }
    }
  }
}

static void reset_piece_captures(void)
{
  int i, j, k;
  int n = numpcs;
  uint8_t *v = reset_v;

  for (i = 0; i < 256; i++)
    v[i] = 0;

  if (num_saves == 0)
    for (i = DRAW_RULE; i < REDUCE_PLY; i++)
      v[LOSS_IN_ONE - i] = 1;
  else
    for (i = 0; i <= REDUCE_PLY_RED + 1; i++)
      v[LOSS_IN_ONE - i] = 1;

  to_fix_w = to_fix_b = 0;

  for (i = 0; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08) || i == black_king) continue;
    if (!(cursed_capt[i] & 2)) continue;
    if (!to_fix_w) {
      printf("Resetting white cursed losses.\n");
      to_fix_w = 1;
    }
    for (k = 0, j = 0; black_all[k] >= 0; k++)
      if (black_all[k] != i)
        pcs2[j++] = black_all[k];
    pcs2[j] = -1;
    if (i) {
      captured_piece = i;
      run_threaded(reset_captures_w, work_g, 1);
    } else
      run_threaded(reset_pivot_captures, work_piv, 1);
  }

  for (i = 0; i < n; i++) { // loop over white pieces
    if ((pt[i] & 0x08) || i == white_king) continue;
    if (!(cursed_capt[i] & 2)) continue;
    if (!to_fix_b) {
      printf("Resetting black cursed losses.\n");
      to_fix_b = 1;
    }
    for (k = 0, j = 0; white_all[k] >= 0; k++)
      if (white_all[k] != i)
        pcs2[j++] = white_all[k];
    pcs2[j] = -1;
    if (i) {
      captured_piece = i;
      run_threaded(reset_captures_b, work_g, 1);
    } else
      run_threaded(reset_pivot_captures, work_piv, 1);
  }
}

// return 1 if a non-losing capture exists, otherwise 0
static int test_pawn_capt(int k, int sq, uint64_t idx, int king, int opp_king,
    int wtm, bitboard occ, bitboard pawns, int *p)
{
  int i;
  int pt2[MAX_PIECES];
  bitboard bits;

  for (bits = sides_mask[sq] & occ; bits; ClearFirst(bits)) {
    int sq2 = FirstOne(bits);
    if (bit[sq2] & KingRange(p[king])) continue;
    int t;
    for (t = 0; p[t] != sq2; t++);
    if ((pt[t] >> 3) == wtm) continue;
    if (bit[sq2] & KingRange(p[opp_king])) return 1;
    bitboard bb = occ & ~(bit[sq2] | (king_range[sq2] & ~pawns));
    for (i = 0; i < numpcs; i++)
      pt2[i] = (bit[p[i]] & bb) ? pt[i] : 0;
    if (!(bit[p[king]] & KingRange(p[opp_king]))) {
      for (i = 0; i < numpcs; i++) {
        int s = pt2[i];
        if (!s || (s >> 3) == wtm) continue;
        bitboard bits = PieceRange(p[i], s, bb);
        if (bits & bit[p[king]]) break;
      }
      if (i < numpcs) continue;
    }
    if (probe_tb(pt2, p, wtm, occ, 1, 2) <= 1)
      return 1;
  }

  return 0;
}

static void reset_pawn_captures_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;
  uint8_t *v = reset_v;

  LOOP_ITER_ALL {
    if (!v[table_w[idx]]) continue;
    FILL_OCC_PAWNS_PIECES;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      if (test_pawn_capt(i, p[i] + 0x08, idx & ~mask[i], white_king, black_king, 0, occ & ~bit[p[i]], pawns, p)) {
        table_w[idx] = CAPT_CLOSS;
        break;
      }
    }
  }
}

static void reset_pawn_captures_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;
  uint8_t *v = reset_v;

  LOOP_ITER_ALL {
    if (!v[table_b[idx]]) continue;
    FILL_OCC_PAWNS_PIECES;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      if (test_pawn_capt(i, p[i] - 0x08, idx & ~mask[i], black_king, white_king, 1, occ & ~bit[p[i]], pawns, p)) {
        table_b[idx] = CAPT_CLOSS;
        break;
      }
    }
  }
}

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

static void fix_closs_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_w[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(white_pcs, idx, table_b, occ, p);
    table_w[idx] = win_loss[v];
  }
}

static void fix_closs_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_b[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(black_pcs, idx, table_w, occ, p);
    table_b[idx] = win_loss[v];
  }
}

static void fix_closs(void)
{
  int i;

  if (!to_fix_w && !to_fix_b && !cursed_pawn_capt_w && !cursed_pawn_capt_b) return;

  for (i = 0; i < 256; i++)
    win_loss[i] = 0;
  if (num_saves == 0) {
    // if no legal moves or all moves lose, then CLOSS capture was best
    for (i = 0; i < CAPT_CWIN; i++)
      win_loss[i] = LOSS_IN_ONE - DRAW_RULE;
    win_loss[CAPT_CWIN] = LOSS_IN_ONE - DRAW_RULE - 1;
    win_loss[PAWN_CWIN] = LOSS_IN_ONE - DRAW_RULE - 1;
    for (i = DRAW_RULE; i < REDUCE_PLY - 1; i++)
      win_loss[WIN_IN_ONE + i + 2] = LOSS_IN_ONE - i - 1;
  } else {
    // CAPT_CLOSS will be set to 0, then overridden by what was saved before
    for (i = 0; i < CAPT_CWIN_RED + 2; i++)
      win_loss[i] = CAPT_CLOSS;
    for (i = 0; i < REDUCE_PLY_RED; i++)
      win_loss[CAPT_CWIN_RED + i + 2] = LOSS_IN_ONE - i - 1;
  }

  if (to_fix_w || cursed_pawn_capt_w) {
    printf("fixing cursed white losses.\n");
    run_threaded(fix_closs_w, work_g, 1);
  }
  if (to_fix_b || cursed_pawn_capt_b) {
    printf("fixing cursed black losses.\n");
    run_threaded(fix_closs_b, work_g, 1);
  }
}
