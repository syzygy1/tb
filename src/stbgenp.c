/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define REDUCE_PLY 108
#define REDUCE_PLY_RED 116
//#define REDUCE_PLY 30
//#define REDUCE_PLY_RED 20

#define STAT_MATE (MAX_STATS - 1)
#define STAT_DRAW (MAX_STATS/2)
#define STAT_THREAT_CWIN1 (STAT_DRAW - 6)
#define STAT_THREAT_CWIN2 (STAT_DRAW - 5)
#define STAT_THREAT_WIN1 (STAT_DRAW - 4)
#define STAT_THREAT_WIN2 (STAT_DRAW - 3)
#define STAT_CAPT_CWIN (STAT_DRAW - 2)
#define STAT_CAPT_WIN (STAT_DRAW - 1)
#define STAT_CAPT_DRAW (STAT_DRAW + 1)
#define STAT_CAPT_LOSS (STAT_DRAW + 2)
#define STAT_CAPT_CLOSS (STAT_DRAW + 3)
#define STAT_THREAT_DRAW (STAT_DRAW + 4)

#define MAX_PLY (STAT_DRAW - 7)
#define MIN_PLY_WIN 3
#define MIN_PLY_LOSS 2

#define BROKEN 0xff
#define UNKNOWN 0xfe
#define CHANGED 0xfd

// CAPT_val: best forced capture leads to val (--> in 1)
// THREAT_val: best non-capture move forces a capture leading to val (--> in 2)

#define CAPT_WIN 0
#define CAPT_CWIN 1
#define CAPT_DRAW 2
#define CAPT_CLOSS 3
#define CAPT_LOSS 4
#define STALE_WIN 5
#define THREAT_WIN1 7
#define THREAT_WIN2 8
#define BASE_WIN 7 // BASE_WIN + 2 == 9
#define THREAT_CWIN1 (BASE_WIN + DRAW_RULE + 2)
#define THREAT_CWIN2 (BASE_WIN + DRAW_RULE + 3)

#define THREAT_WIN_RED 5
#define BASE_WIN_RED 6
#define THREAT_CWIN_RED 7
#define BASE_CWIN_RED 8

//#define THREAT_CLOSS 0xfd
#define PAWN_CLOSS 0xfc
#define PAWN_DRAW 0xfb
#define THREAT_DRAW 0xfa
#define BASE_LOSS 0xf9

#define BASE_LOSS_RED 0xf9
#define BASE_CLOSS_RED 0xf8

#define SET_CHANGED(x) \
{ uint8_t dummy = CHANGED; \
__asm__( \
"movb %2, %%al\n\t" \
"lock cmpxchgb %1, %0" \
: "+m" (x), "+r" (dummy) : "i" (UNKNOWN) : "eax"); }

#define SET_CAPT_VALUE(x,v) \
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

// table[idx] > THREAD_WIN2, then table[idx] = THREAT_WIN2
// table[idx] == STALE_WIN + 1, then table[idx] = THREAT_WIN1
#define SET_THREAT_WIN(x) \
{ uint8_t dummy = THREAT_WIN2; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"ja 1f\n\t" \
"cmpb %2, %%al\n\t" \
"jnz 2f\n\t" \
"decb %1\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jmp 2f\n" \
"1:\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"2:" \
: "+m" (x), "+r" (dummy) : "i" (STALE_WIN + 1) : "eax"); }

// table[idx] > THREAT_WIN2, then table[idx] = STALE_WIN + 1
// table[idx] == THREAT_WIN2, then table[idx] = THREAT_WIN1
#define SET_WIN_IN_1(x) \
{ uint8_t dummy = STALE_WIN + 1; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %2, %%al\n\t" \
"ja 1f\n\t" \
"jnz 2f\n\t" \
"incb %1\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jmp 2f\n" \
"1:\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"2:" \
: "+m" (x), "+r" (dummy) : "i" (THREAT_WIN2) : "eax"); }

// table[idx] > THREAT_CWIN2, then table[idx] = THREAT_CWIN2
// table[idx] == BASE_WIN + 101, then table[idx] = THREAT_CWIN1
#define SET_THREAT_CWIN(x) \
{ uint8_t dummy = THREAT_CWIN2; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"ja 1f\n\t" \
"cmpb %2, %%al\n\t" \
"jnz 2f\n\t" \
"decb %1\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jmp 2f\n" \
"1:\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"2:" \
: "+m" (x), "+r" (dummy) : "i" (BASE_WIN + DRAW_RULE + 1) : "eax"); }

// FIXME: analyse the case table[idx] = THREAT_CWIN1. test + loop needed?

// table[idx] > THREAT_CWIN2, then table[idx] = BASE_WIN + 101
// table[idx] == THREAT_CWIN2, then table[idx] = THREAT_CWIN1
#define SET_CWIN_IN_1(x) \
{ uint8_t dummy = BASE_WIN + DRAW_RULE + 1; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %2, %%al\n\t" \
"ja 1f\n\t" \
"jnz 2f\n\t" \
"incb %1\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jmp 2f\n" \
"1:\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"2:" \
: "+m" (x), "+r" (dummy) : "i" (THREAT_CWIN2) : "eax"); }

uint8_t win_loss[256];
uint8_t loss_win[256];
char tbl_to_wdl[256];
uint8_t wdl_to_tbl[8] = {
  0xff, 0xff, CAPT_LOSS, CAPT_CLOSS, CAPT_DRAW, CAPT_CWIN, CAPT_WIN, 0xff
};
uint8_t wdl_to_tbl_pawn[8] = {
  0xff, 0xff, CHANGED, PAWN_CLOSS,
  PAWN_DRAW, BASE_WIN + DRAW_RULE + 1, STALE_WIN + 1, 0xff
};

static void set_tbl_to_wdl(int saves)
{
  int i;

  for (i = 5; i < 256; i++)
    tbl_to_wdl[i] = 4;
  
  if (saves == 0) {
    tbl_to_wdl[CAPT_WIN] = -2;
    tbl_to_wdl[CAPT_CWIN] = -1;
    tbl_to_wdl[CAPT_DRAW] = 0;
    tbl_to_wdl[CAPT_CLOSS] = 1;
    tbl_to_wdl[CAPT_LOSS] = 2;
    tbl_to_wdl[STALE_WIN] = -2;
    tbl_to_wdl[STALE_WIN + 1] = -2;
    tbl_to_wdl[THREAT_WIN1] = -2;
    tbl_to_wdl[THREAT_WIN2] = -2;
    for (i = 2; i <= DRAW_RULE; i++)
      tbl_to_wdl[BASE_WIN + i] = -2;
    tbl_to_wdl[BASE_WIN + DRAW_RULE + 1] = -1;
    tbl_to_wdl[THREAT_CWIN1] = -1;
    tbl_to_wdl[THREAT_CWIN2] = -1;
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY; i++)
      tbl_to_wdl[BASE_WIN + i + 2] = -1;
    tbl_to_wdl[UNKNOWN] = 0;
    tbl_to_wdl[CAPT_DRAW] = 0;
    tbl_to_wdl[PAWN_DRAW] = 0;
    for (i = DRAW_RULE + 1; i <= REDUCE_PLY; i++)
      tbl_to_wdl[BASE_LOSS - i] = 1;
    for (i = 0; i <= DRAW_RULE; i++)
      tbl_to_wdl[BASE_LOSS - i] = 2;
  } else {
    tbl_to_wdl[THREAT_WIN_RED] = -2;
    tbl_to_wdl[BASE_WIN_RED] = -2;
    tbl_to_wdl[THREAT_CWIN_RED] = -1;
    tbl_to_wdl[BASE_CWIN_RED] = -1;
    for (i = 0; i <= REDUCE_PLY_RED; i++)
      tbl_to_wdl[BASE_CWIN_RED + i + 1] = -1;
    tbl_to_wdl[UNKNOWN] = 0;
    tbl_to_wdl[CAPT_DRAW] = 0;
    tbl_to_wdl[PAWN_DRAW] = 0;
    tbl_to_wdl[BASE_CLOSS_RED] = 1;
    for (i = 0; i <= REDUCE_PLY_RED; i++)
      tbl_to_wdl[BASE_CLOSS_RED - i - 1] = 1;
    tbl_to_wdl[BASE_LOSS_RED] = 2;
  }
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
      if (n == numpawns) {
        for (i = 0; i < 8; i++) {
          table_w[idx + i] = BROKEN;
          table_b[idx + i] = BROKEN;
        }
        for (bb = 1ULL << 8; i < 56; i++, bb <<= 1)
          if (occ & bit[i ^ pw[n - 1]]) {
            table_w[idx + i] = BROKEN;
            table_b[idx + i] = BROKEN;
          } else {
            table_w[idx + i] = table_b[idx + i] = UNKNOWN;
          }
        for (; i < 64; i++) {
          table_w[idx + i] = BROKEN;
          table_b[idx + i] = BROKEN;
        }
      } else {
        for (i = 0, bb = 1; i < 64; i++, bb <<= 1)
          if (occ & bit[i]) {
            table_w[idx + i] = BROKEN;
            table_b[idx + i] = BROKEN;
          } else {
            table_w[idx + i] = table_b[idx + i] = UNKNOWN;
          }
      }
    } else
      for (i = 0; i < 64; i++) {
        table_w[idx + i] = BROKEN;
        table_b[idx + i] = BROKEN;
      }
  }
}

static int check_loss(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
    int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = BASE_LOSS - 1;

  for (; *pcs >= 0; pcs++) {
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
  }

  return best;
}

MARK(mark_capt_wins)
{
  MARK_BEGIN;
  table[idx2] = CAPT_WIN;
  MARK_END;
}

MARK(mark_capt_cursed_wins)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], CAPT_CWIN);
  MARK_END;
}

MARK(mark_capt_draws)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], CAPT_DRAW);
  MARK_END;
}

MARK(mark_capt_cursed_losses)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], CAPT_CLOSS);
  MARK_END;
}

MARK(mark_capt_losses)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], CAPT_LOSS);
  MARK_END;
}

static int captured_piece;

static void probe_last_capture_w(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_capt_losses);
    }
  }
}

static void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      // FIXME: test whether this improves efficiency...
      CHECK_WHITE_PIECES;
      int v = probe_tb(pt2, p, 0, occ, -2, 2);
      MAKE_IDX2;
      switch (v) {
      case -2:
        LOOP_WHITE_PIECES(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_WHITE_PIECES(mark_capt_cursed_wins);
        break;
      case 0:
        LOOP_WHITE_PIECES(mark_capt_draws);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_WHITE_PIECES(mark_capt_cursed_losses);
        break;
      case 2:
        LOOP_WHITE_PIECES(mark_capt_losses);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void probe_last_capture_b(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_capt_losses);
    }
  }
}

static void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      // FIXME: test whether this improves efficiency...
      CHECK_BLACK_PIECES;
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2;
      switch (v) {
      case -2:
        LOOP_BLACK_PIECES(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_BLACK_PIECES(mark_capt_cursed_wins);
        break;
      case 0:
        LOOP_BLACK_PIECES(mark_capt_draws);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_BLACK_PIECES(mark_capt_cursed_losses);
        break;
      case 2:
        LOOP_BLACK_PIECES(mark_capt_losses);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void probe_last_pivot_capture(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      LOOP_PIECES_PIVOT(mark_capt_losses);
    }
  }
}

static void probe_pivot_captures(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;
  int has_cursed = 0;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      CHECK_PIECES_PIVOT;
      int v = probe_tb(pt2, p, wtm, occ, -2, 2);
      MAKE_IDX2_PIVOT;
      switch (v) {
      case -2:
        LOOP_PIECES_PIVOT(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_PIECES_PIVOT(mark_capt_cursed_wins);
        break;
      case 0:
        LOOP_PIECES_PIVOT(mark_capt_draws);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_PIECES_PIVOT(mark_capt_cursed_losses);
        break;
      case 2:
        LOOP_PIECES_PIVOT(mark_capt_losses);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[0] |= has_cursed;
}

static void calc_captures_w(void)
{
  int i;
  int n = numpcs;

  if (last_b >= 0) {
    if (last_b) {
      captured_piece = last_b;
      run_threaded(probe_last_capture_w, work_g, 1);
    } else
      run_threaded(probe_last_pivot_capture, work_piv, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over black pieces
      if (!(pt[i] & 0x08)) continue;
      if (i) {
        captured_piece = i;
        run_threaded(probe_captures_w, work_g, 1);
      } else
        run_threaded(probe_pivot_captures, work_piv, 1);
    }
  }
}

static void calc_captures_b(void)
{
  int i;
  int n = numpcs;

  if (last_w >= 0) {
    if (last_w) {
      captured_piece = last_w;
      run_threaded(probe_last_capture_b, work_g, 1);
    } else
      run_threaded(probe_last_pivot_capture, work_piv, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over white pieces
      if (pt[i] & 0x08) continue;
      if (i) {
        captured_piece = i;
        run_threaded(probe_captures_b, work_g, 1);
      } else
        run_threaded(probe_pivot_captures, work_piv, 1);
    }
  }
}

MARK(mark_threat_cwins)
{
  MARK_BEGIN;
  SET_THREAT_CWIN(table[idx2]);
  MARK_END;
}

MARK(mark_cwins_in_1)
{
  MARK_BEGIN;
  SET_CWIN_IN_1(table[idx2]);
  MARK_END;
}

MARK(mark_threat_wins)
{
  MARK_BEGIN;
  SET_THREAT_WIN(table[idx2]);
  MARK_END;
}

MARK(mark_wins_in_1)
{
  MARK_BEGIN;
  SET_WIN_IN_1(table[idx2]);
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
  SET_WIN_VALUE(table[idx2], v);
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
    case 3: /* CAPT_LOSS -> THREAT_WIN2/1 */
      RETRO(mark_threat_wins);
      break;
    case 4: /* BASE_LOSS -> STALE_WIN + 1 */
      RETRO(mark_wins_in_1);
      break;
    case 5: /* CAPT_CLOSS -> THREAT_CWIN2/1 */
      RETRO(mark_threat_cwins);
      break;
    case 6: /* CHANGED -> BASE_WIN + DRAW_RULE + 1 */
      v = check_loss(pcs, idx, table_opp, occ, p);
      if (v) {
        table[idx] = v;
        if (v == BASE_LOSS - DRAW_RULE)
          RETRO(mark_cwins_in_1);
        else
          RETRO(mark_wins, loss_win[v]);
      } else {
        table[idx] = UNKNOWN;
      }
      break;
    case 7: /* PAWN_CLOSS -> BASE_WIN + DRAW_RULE + 4 */
      v = check_loss(pcs, idx, table_opp, occ, p);
      if (v) {
        if (v > BASE_LOSS - DRAW_RULE - 1)
          v = BASE_LOSS - DRAW_RULE - 1;
        table[idx] = v;
        RETRO(mark_wins, loss_win[v]);
      } else {
        table[idx] = UNKNOWN;
      }
      break;
    default:
      assume(0);
      break;
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

static void find_draw_threats(struct thread_data *thread);

static void iterate()
{
  int i;
  iter_wtm = 1;
  int local_num_saves = 0;

  // FIXME: properly init cursed_w, cursed_b
  // FIXME: replace by something more incremental
  for (i = 0; i < 256; i++)
    tbl[i] = win_loss[i] = loss_win[i] = 0;

  // ply = n -> find all wins in n, n+1 and potential losses in n, n+1

  // pawns, so propagate win/loss by stalemate is possible
  ply = 0;
  tbl[STALE_WIN] = 2;
  tbl[BASE_LOSS] = 4; // BASE_LOSS -> STALE_WIN + 1 / THREAT_WIN1
  run_iter();

  // ply = 1
  ply++;
  tbl[CHANGED] = 1;
  tbl[STALE_WIN + 1] = 2;
  tbl[CAPT_WIN] = 2;
  tbl[CAPT_LOSS] = 3; // CAPT_LOSS -> THREAT_WIN2 / THREAT_WIN1
  win_loss[STALE_WIN] = BASE_LOSS - 1;
  loss_win[BASE_LOSS - 1] = BASE_WIN + 2;
  run_iter();

  // ply = 2
  ply++;
  tbl[STALE_WIN] = tbl[BASE_LOSS] = 0;
  tbl[THREAT_WIN1] = tbl[THREAT_WIN2] = 2;
  tbl[BASE_WIN + 2] = 2;
  win_loss[STALE_WIN + 1] = BASE_LOSS - 2;
  win_loss[THREAT_WIN1] = BASE_LOSS - 2;
  win_loss[CAPT_WIN] = BASE_LOSS - 2;
  loss_win[BASE_LOSS - 2] = BASE_WIN + 3;
  run_iter();

  // ply = 3
  ply++;
  tbl[CAPT_WIN] = tbl[CAPT_LOSS] = 0;
  tbl[STALE_WIN + 1] = tbl[THREAT_WIN1] = 0;
  tbl[BASE_WIN + 3] = 2;
  win_loss[THREAT_WIN2] = BASE_LOSS - 3;
  win_loss[BASE_WIN + 2] = BASE_LOSS - 3;
  loss_win[BASE_LOSS - 3] = BASE_WIN + 4;
  run_iter();

  tbl[THREAT_WIN2] = 0;
  finished = 0;
  while (!finished && ply < DRAW_RULE - 1) {
    finished = 1;
    ply++;
    tbl[BASE_WIN + ply - 2] = 0;
    tbl[BASE_WIN + ply] = 2;
    win_loss[BASE_WIN + ply - 1] = BASE_LOSS - ply;
    loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 1;
    run_iter();
  }
  tbl[BASE_WIN + ply - 1] = 0;
  tbl[BASE_WIN + ply] = 0;

// PAWN_CWIN? PAWN_CLOSS?
  if (!finished || has_cursed_capts || has_cursed_pawn_moves) {
    // ply = 101
    ply = DRAW_RULE + 1;
    tbl[BASE_WIN + ply - 1] = 2;
    tbl[BASE_WIN + ply] = 2;
    tbl[CAPT_CWIN] = 2;
    tbl[CAPT_CLOSS] = 5; // CAPT_CLOSS -> THREAT_CWIN2 / THREAT_CWIN1
    tbl[CHANGED] = 6;
    tbl[PAWN_CLOSS] = 7; // FIXME: check
    win_loss[BASE_WIN + ply - 1] = BASE_LOSS - ply;
    loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 3;
    run_iter();
    tbl[CHANGED] = 1;

    finished = 1;
    // ply = 102
    ply++;
    tbl[BASE_WIN + ply - 2] = 0;
    tbl[THREAT_CWIN1] = 2; // 101
    tbl[THREAT_CWIN2] = 2; // 102
    tbl[BASE_WIN + ply + 2] = 2; // 102
    win_loss[BASE_WIN + ply - 1] = BASE_LOSS - ply;
    win_loss[CAPT_CWIN] = BASE_LOSS - ply;
    win_loss[THREAT_CWIN1] = BASE_LOSS - ply;
    loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 3;
    run_iter();

    if (!finished) {
      finished = 1;
      // ply = 103
      ply++;
      tbl[CAPT_CWIN] = tbl[CAPT_CLOSS] = tbl[THREAT_CWIN1] = 0;
      tbl[BASE_WIN + ply - 2] = 0;
      tbl[BASE_WIN + ply + 2] = 2;
      win_loss[BASE_WIN + ply + 1] = BASE_LOSS - ply;
      win_loss[THREAT_CWIN2] = BASE_LOSS - ply;
      loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 3;
      run_iter();
    }

    tbl[THREAT_CWIN2] = 0;
    while (!finished && ply < REDUCE_PLY) {
      finished = 1;
      ply++;
      tbl[BASE_WIN + ply] = 0;
      tbl[BASE_WIN + ply + 2] = 2;
      win_loss[BASE_WIN + ply + 1] = BASE_LOSS - ply;
      loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 3;
      run_iter();
    }

    tbl[BASE_WIN + ply + 1] = 0;
    tbl[BASE_WIN + ply + 2] = 0;

    while (!finished) {
      if (num_saves == 0)
        set_tbl_to_wdl(1);
      reduce_tables(local_num_saves);
      local_num_saves++;

      for (i = 0; i < 256; i++)
        win_loss[i] = loss_win[i] = 0;
      win_loss[CAPT_WIN] = 0xff;
      win_loss[CAPT_CWIN] = 0xff;
      win_loss[THREAT_WIN_RED] = 0xff;
      win_loss[BASE_WIN_RED] = 0xff;
      win_loss[THREAT_CWIN_RED] = 0xff;
      win_loss[BASE_CWIN_RED] = 0xff;

      ply = 0;
      tbl[BASE_CWIN_RED + 2] = 2;
      win_loss[BASE_CWIN_RED + 1] = BASE_CLOSS_RED - 1;
      loss_win[BASE_CLOSS_RED - 1] = BASE_CWIN_RED + 3;

      while (ply < REDUCE_PLY_RED && !finished) {
        finished = 1;
        ply++;
        tbl[BASE_CWIN_RED + ply] = 0;
        tbl[BASE_CWIN_RED + ply + 2] = 2;
        win_loss[BASE_CWIN_RED + ply + 1] = BASE_CLOSS_RED - ply - 1;
        loss_win[BASE_CLOSS_RED - ply - 1] = BASE_CWIN_RED + ply + 3;
        run_iter();
      }

      tbl[BASE_CWIN_RED + ply + 1] = 0;
      tbl[BASE_CWIN_RED + ply + 2] = 0;
    }
  }

  while (local_num_saves < num_saves) {
    reduce_tables(local_num_saves);
    local_num_saves++;
  }
}

static void set_draw_threats(void)
{
  int i;

  for (i = 0; i < 256; i++)
    tbl[i] = 0;
  tbl[UNKNOWN] = tbl[PAWN_DRAW] = 1;

  iter_table = table_w;
  iter_table_opp = table_b;
  iter_pcs = white_pcs;
  iter_pcs_opp = black_pcs;
  run_threaded(find_draw_threats, work_g, 0);

  iter_table = table_b;
  iter_table_opp = table_w;
  iter_pcs = black_pcs;
  iter_pcs_opp = white_pcs;
  run_threaded(find_draw_threats, work_g, 0);

  // FIXME: take care of drawing pawn moves into CAPT_DRAW positions
}

MARK(mark_threat_draws)
{
  MARK_BEGIN;
  if (tbl[table[idx2]])
    table[idx2] = THREAT_DRAW;
  MARK_END;
}

static void find_draw_threats(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *table = iter_table;
  uint8_t *table_opp = iter_table_opp;
  int *pcs_opp = iter_pcs_opp;

  LOOP_ITER_ALL {
    if (table[idx] != CAPT_DRAW) continue;
    FILL_OCC;
    RETRO(mark_threat_draws);
  }
}

static void calc_last_pawn_capture_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_w[idx] == BROKEN || table_w[idx] == CAPT_LOSS) continue;
    FILL_OCC;
    bitboard bits = bit[p[last_b]];
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      if (sides_mask[p[i] + 0x08] & bits) break;
    }
    if (i < numpawns)
      table_w[idx] = CAPT_LOSS;
  }
}

static void calc_last_pawn_capture_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;

  LOOP_ITER_ALL {
    if (table_b[idx] == BROKEN || table_b[idx] == CAPT_LOSS) continue;
    FILL_OCC;
    bitboard bits = bit[p[last_w]];
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      if (sides_mask[p[i] - 0x08] & bits) break;
    }
    if (i < numpawns)
      table_b[idx] = CAPT_LOSS;
  }
}

static int probe_pawn_capt(int k, int sq, uint64_t idx, int clr, int wtm,
    bitboard occ, int *p)
{
  int i, m;
  int v;
  int best = -4;
  int pt2[MAX_PIECES];
  int pos[MAX_PIECES];
  bitboard bits;

  if (sq >= 0x08 && sq < 0x38) {
    for (bits = sides_mask[sq] & occ; bits; ClearFirst(bits)) {
      int sq2 = FirstOne(bits);
      int t;
      for (t = 0; p[t] != sq2; t++);
      if ((pt[t] & 0x08) == clr) continue;
      for (i = 0; i < numpcs; i++) {
        pos[i] = p[i];
        pt2[i] = pt[i];
      }
      pt2[t] = 0;
      pos[k] = sq2;
      v = -probe_tb(pt2, pos, wtm, occ, -2, 2);
      if (v > 1) return v;
      if (v > best) best = v;
    }
  } else { /* pawn promotion */
    for (bits = sides_mask[sq] & occ; bits; ClearFirst(bits)) {
      int sq2 = FirstOne(bits);
      int t;
      for (t = 0; p[t] != sq2; t++);
      if ((pt[t] & 0x08) == clr) continue;
      for (i = 0; i < numpcs; i++) {
        pos[i] = p[i];
        pt2[i] = pt[i];
      }
      pt2[t] = 0;
      pos[k] = sq2;
      if (best < -2) best = -2;
      for (m = KING; m >= KNIGHT; m--) {
        pt2[k] = m | (pt[k] & 0x08);
        v = -probe_tb(pt2, pos, wtm, occ, -2, -best);
        if (v > 1) return v;
        if (v > best) best = v;
      }
    }
  }

  return best;
}

static void calc_pawn_captures_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int has_cursed = 0;

  LOOP_ITER_ALL {
    if (table_w[idx] == BROKEN || table_w[idx] == CAPT_WIN) continue;
    int w = table_w[idx];
    FILL_OCC;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      int val = probe_pawn_capt(i, p[i] + 0x08, idx & ~mask[i], 0, 0, occ & ~bit[p[i]], p);
      if (val > v) v = val;
    }
    has_cursed |= v;
    v = wdl_to_tbl[v + 4];
    if (v < w) table_w[idx] = v;
  }

  if (has_cursed & 1)
    cursed_pawn_capt_w = 1;
}

static void calc_pawn_captures_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int has_cursed = 0;

  LOOP_ITER_ALL {
    if (table_b[idx] == BROKEN || table_b[idx] <= CAPT_WIN) continue;
    int w = table_b[idx];
    FILL_OCC;
    int v = -4;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      int val = probe_pawn_capt(i, p[i] - 0x08, idx & ~mask[i], 8, 1, occ & ~bit[p[i]], p);
      if (val > v) v = val;
    }
    has_cursed |= v;
    v = wdl_to_tbl[v + 4];
    if (v < w) table_b[idx] = v;
  }

  if (has_cursed & 1)
    cursed_pawn_capt_b = 1;
}

// now returns -2, -1, 0, 1, 2
static int eval_ep(int k, int l, int sq, int ep, int wtm, bitboard occ, int *p)
{
  int i, v;
  int pt2[MAX_PIECES];

  occ ^= bit[sq] | bit[ep] | bit[p[k]];
  p[l] = ep;

  for (i = 0; i < numpcs; i++)
    pt2[i] = pt[i];
  pt2[k] = 0;
  v = probe_tb(pt2, p, wtm, occ, -2, 2);
  p[l] = sq;

  return v;
}

static int has_moves(int *pcs, bitboard occ, int *p)
{
  int k;
  bitboard bb;

  for (; *pcs >= 0; pcs++) {
    k = *pcs;
    bb = PieceMoves(p[k], pt[k], occ);
    if (bb) return 1;
  }
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
    if (table_w[idx] != UNKNOWN) continue;
    FILL_OCC_PIECES;
    best = -4;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue;
      sq = p[i];
      if (bit[sq + 8] & occ) continue;
      if (sq >= 0x30) {
        bitboard bb = occ ^ bit[sq] ^ bit[sq + 8];
        for (k = 0; k < n; k++) {
          pos[k] = p[k];
          pt2[k] = pt[k];
        }
        pos[i] = sq + 8;
        if (best < -2) best = -2;
        for (k = WKING; k >= WKNIGHT; k--) {
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
          int v0, v1 = 3;
          bitboard bits = sides_mask[p[i] + 0x10] & occ;
          while (bits) {
            int sq2 = FirstOne(bits);
            for (k = 0; p[k] != sq2; k++);
            if (pt[k] == BPAWN) {
              v0 = eval_ep(i, k, sq2, p[i] + 0x08, 1, occ, p);
              if (v0 < v1) v1 = v0;
            }
            ClearFirst(bits);
          }
          int v = tbl_to_wdl[table_b[idx2]];
          if (v1 < 3) {
            if (v1 < v || table_b[idx2] >= 5)
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
    if (best > -4) {
      has_cursed |= best;
      table_w[idx] = wdl_to_tbl_pawn[best + 4];
    } else if (!has_moves(white_pcs, occ, p))
      table_w[idx] = stalemate_w;
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
    if (table_b[idx] != UNKNOWN) continue;
    FILL_OCC_PIECES;
    best = -4;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue;
      sq = p[i];
      if (bit[sq - 8] & occ) continue;
      if (sq < 0x10) {
        bitboard bb = occ ^ bit[sq] ^ bit[sq - 8];
        for (k = 0; k < n; k++) {
          pos[k] = p[k];
          pt2[k] = pt[k];
        }
        pos[i] = sq - 8;
        if (best < -2) best = -2;
        for (k = BKING; k >= BKNIGHT; k--) {
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
          int v0, v1 = 3;
          bitboard bits = sides_mask[p[i] - 0x10] & occ;
          while (bits) {
            int sq2 = FirstOne(bits);
            for (k = 0; p[k] != sq2; k++);
            if (pt[k] == WPAWN) {
              v0 = eval_ep(i, k, sq2, p[i] - 0x08, 0, occ, p);
              if (v0 < v1) v1 = v0;
            }
            ClearFirst(bits);
          }
          int v = tbl_to_wdl[table_w[idx2]];
          if (v1 < 3) {
            if (v1 < v || table_w[idx2] >= 5)
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
    if (best > -4) {
      has_cursed |= best;
      table_b[idx] = wdl_to_tbl_pawn[best + 4];
    } else if (!has_moves(black_pcs, occ, p))
      table_b[idx] = stalemate_b;
  }

  if (has_cursed & 1)
    has_cursed_pawn_moves = 1;
}

#if 0
MARK(reset_capts)
{
  MARK_BEGIN;
  table[idx2] = 0x81;
  MARK_END;
}

static void reset_captures_w(struct thread_data *thread)
{
  uint64_t idx;
  int i = captured_piece;
  int j, k;
  int p[MAX_PIECES];
  bitboard occ, bb = thread->occ;
  int n = numpcs;
  uint64_t end = thread->end >> 6;

  for (idx = thread->begin >> 6; idx < end; idx++) {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      for (j = 0; white_pcs[j] >= 0; j++) {
        k = white_pcs[j];
        if (i < numpawns && (p[k] < 0x08 || p[k] >= 0x38)) continue;
        uint64_t idx3 = idx2 | ((uint64_t)p[k] << shift[i]);
        reset_capts(k, table_w, idx3 & ~mask[k], occ, p);
      }
    }
  }
}

static void reset_captures_b(struct thread_data *thread)
{
  uint64_t idx;
  int i = captured_piece;
  int j, k;
  int p[MAX_PIECES];
  bitboard occ, bb = thread->occ;
  int n = numpcs;
  uint64_t end = thread->end >> 6;

  for (idx = thread->begin >> 6; idx < end; idx++) {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      for (j = 0; black_pcs[j] >= 0; j++) {
        k = black_pcs[j];
        if (i < numpawns && (p[k] < 0x08 || p[k] >= 0x38)) continue;
        uint64_t idx3 = idx2 | ((uint64_t)(p[k] ^ pw[i]) << shift[i]);
        reset_capts(k, table_b, idx3 & ~mask[k], occ, p);
      }
    }
  }
}

static void reset_pivot_captures(struct thread_data *thread)
{
  uint64_t idx;
  int j, k;
  int p[MAX_PIECES];
  bitboard occ, bb = thread->occ;
  int n = numpcs;
  uint8_t *table;
  int *pcs;

  if (pt[0] == WPAWN) {
    table = table_b;
    pcs = black_pcs;
  } else {
    table = table_w;
    pcs = white_pcs;
  }

  uint64_t end = thread->end;
  for (idx = thread->begin; idx < end; idx++) {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      for (j = 0; pcs[j] >= 0; j++) {
        k = pcs[j];
        if (!piv_valid[p[k]]) continue;
        uint64_t idx3 = idx2 | piv_idx[p[k]];
        reset_capts(k, table, idx3 & ~mask[k], occ, p);
      }
    }
  }
}

static void reset_piece_captures(void)
{
  int i;
  int n = numpcs;

  printf("Setting winning white captures to broken.\n");
  for (i = 0; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08)) continue;
    if (i) {
      captured_piece = i;
      run_threaded(reset_captures_w, work_g, 1);
    } else
      run_threaded(reset_pivot_captures, work_piv, 1);
  }

  printf("Setting winning black captures to broken.\n");
  for (i = 0; i < n; i++) { // loop over white pieces
    if (pt[i] & 0x08) continue;
    if (i) {
      captured_piece = i;
      run_threaded(reset_captures_b, work_g, 1);
    } else
      run_threaded(reset_pivot_captures, work_piv, 1);
  }
}

static void reset_pawn_captures_w(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, k;
  int n = numpcs;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;
  bitboard bits;

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (table_w[idx] == 0x81 || table_w[idx] == BROKEN) continue;
    FILL_OCC_PIECES;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      for (bits = sides_mask[p[i] + 0x08] & occ; bits; ClearFirst(bits)) {
        int sq = FirstOne(bits);
        for (k = 0; p[k] != sq; k++);
        if (pt[k] & 0x08) {
          table_w[idx] = 0x81;
          i = numpawns;
          break;
        }
      }
    }
  }
}

static void reset_pawn_captures_b(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, k;
  int n = numpcs;
  bitboard occ, bb = thread->occ;
  int *p = thread->p;
  uint64_t end = begin + thread->end;
  bitboard bits;

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (table_b[idx] == 0x81 || table_b[idx] == BROKEN) continue;
    FILL_OCC_PIECES;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      for (bits = sides_mask[p[i] - 0x08] & occ; bits; ClearFirst(bits)) {
        int sq = FirstOne(bits);
        for (k = 0; p[k] != sq; k++);
        if (!(pt[k] & 0x08)) {
          table_b[idx] = 0x81;
          i = numpawns;
          break;
        }
      }
    }
  }
}

static void reset_pawn_captures_unthreaded(void)
{
  uint64_t idx, idx2;
  uint64_t size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  int p[MAX_PIECES];
  bitboard occ;

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;
  thread_data[0].p = p;

  for (idx = 0; idx < pawnsize; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5-cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      thread_data[0].occ = occ;
      if (has_white_pawns)
        run_single(reset_pawn_captures_w, work_p, 0);
      if (has_black_pawns)
        run_single(reset_pawn_captures_b, work_p, 0);
    }
  }
  printf("\n\n");
}

static void reset_pawn_captures_threaded(void)
{
  uint64_t idx, idx2;
  uint64_t size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  int p[MAX_PIECES];
  bitboard occ;

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;

  for (idx = 0; idx < pawnsize; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5-cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      for (i = 0; i < numthreads; i++)
        thread_data[i].occ = occ;
      for (i = 0; i < numthreads; i++)
        memcpy(thread_data[i].p, p, MAX_PIECES * sizeof(int));
      if (has_white_pawns)
        run_threaded(reset_pawn_captures_w, work_p, 0);
      if (has_black_pawns)
        run_threaded(reset_pawn_captures_b, work_p, 0);
    }
  }
  printf("\n\n");
}
#endif
