/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define REDUCE_PLY 110
#define REDUCE_PLY_RED 105
//#define REDUCE_PLY 30
//#define REDUCE_PLY_RED 20

#define STAT_DRAW (MAX_STATS/2)            // 512
#define STAT_CAPT_WIN 1
#define STAT_THREAT_WIN 2
#define STAT_CAPT_CWIN (STAT_DRAW - 1)     // 511
#define STAT_THREAT_CWIN1 (STAT_DRAW - 2)  // 510
#define STAT_THREAT_CWIN2 (STAT_DRAW - 3)  // 509
#define STAT_CAPT_DRAW (STAT_DRAW + 1)     // 513
#define STAT_THREAT_DRAW (STAT_DRAW + 2)   // 514
#define STAT_CAPT_CLOSS (STAT_DRAW + 3)    // 515
#define STAT_CAPT_LOSS (MAX_STATS - 2)     // 1022
#define STAT_MATE (MAX_STATS - 1)          // 1023

#define MAX_PLY 507
#define MIN_PLY_WIN 3
#define MIN_PLY_LOSS 2

// for pawns, STALEMATE. probably not necessary to rank correctly

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

#define BASE_WIN 3
#define THREAT_WIN 5

#define THREAT_CWIN1 (BASE_WIN + DRAW_RULE + 2)
#define THREAT_CWIN2 (BASE_WIN + DRAW_RULE + 3)
//#define THREAT_CLOSS 0xfd
#define THREAT_DRAW 0xfc
#define BASE_LOSS (0xfb + 2)

// for clang:
//#define VOLATILE volatile
#define VOLATILE

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

// check whether all moves end up in wins for the opponent
static int check_loss(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ,
    int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = BASE_LOSS - 2;

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

MARK_PIVOT(mark_capt_wins)
{
  MARK_BEGIN_PIVOT;
  table[idx2] = CAPT_WIN;
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    table[idx3] = CAPT_WIN;
  }
  MARK_END;
}

MARK(mark_capt_wins)
{
  MARK_BEGIN;
  table[idx2] = CAPT_WIN;
  MARK_END;
}

MARK_PIVOT(mark_capt_value, uint8_t v)
{
  MARK_BEGIN_PIVOT;
  SET_CAPT_VALUE(table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    SET_CAPT_VALUE(table[idx3], v);
  }
  MARK_END;
}

MARK(mark_capt_value, uint8_t v)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], v);
  MARK_END;
}

static int captured_piece;

static void probe_last_capture_w(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_capt_value, CAPT_LOSS);
    }
  }
}

static void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      int v = probe_tb(pt2, p, 0, occ, -2, 2);
      MAKE_IDX2;
      switch (v) {
      case -2:
        LOOP_WHITE_PIECES(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_WHITE_PIECES(mark_capt_value, CAPT_CWIN);
        break;
      case 0:
        LOOP_WHITE_PIECES(mark_capt_value, CAPT_DRAW);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_WHITE_PIECES(mark_capt_value, CAPT_CLOSS);
        break;
      case 2:
        LOOP_WHITE_PIECES(mark_capt_value, CAPT_LOSS);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void probe_last_pivot_capture_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      MAKE_IDX2_PIVOT;
      LOOP_BLACK_PIECES_PIVOT(mark_capt_value, CAPT_LOSS);
    }
  }
}

static void probe_pivot_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT;
  int has_cursed = 0;

  LOOP_CAPTS_PIVOT {
    FILL_OCC_CAPTS_PIVOT {
      CHECK_BLACK_PIECES_PIVOT;
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2_PIVOT;
      switch (v) {
      case -2:
        LOOP_BLACK_PIECES_PIVOT(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_BLACK_PIECES_PIVOT(mark_capt_value, CAPT_CWIN);
        break;
      case 0:
        LOOP_BLACK_PIECES_PIVOT(mark_capt_value, CAPT_DRAW);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_BLACK_PIECES_PIVOT(mark_capt_value, CAPT_CLOSS);
        break;
      case 2:
        LOOP_BLACK_PIECES_PIVOT(mark_capt_value, CAPT_LOSS);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[0] |= has_cursed;
}

static void probe_last_capture_b(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_capt_value, CAPT_LOSS);
    }
  }
}

static void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      int v = probe_tb(pt2, p, 1, occ, -2, 2);
      MAKE_IDX2;
      switch (v) {
      case -2:
        LOOP_BLACK_PIECES(mark_capt_wins);
        break;
      case -1:
        has_cursed |= 1;
        LOOP_BLACK_PIECES(mark_capt_value, CAPT_CWIN);
        break;
      case 0:
        LOOP_BLACK_PIECES(mark_capt_value, CAPT_DRAW);
        break;
      case 1:
        has_cursed |= 2;
        LOOP_BLACK_PIECES(mark_capt_value, CAPT_CLOSS);
        break;
      case 2:
        LOOP_BLACK_PIECES(mark_capt_value, CAPT_LOSS);
        break;
      default:
        assume(0);
        break;
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void calc_captures_w(void)
{
  int i, k;
  int n = numpcs;

  k = 0;
  for (i = 0; i < n; i++)
    if (pt[i] & 0x08) k++;

  if (k == 1) {
    for (i = 0; i < n; i++)
      if (pt[i] & 0x08) break;
    captured_piece = i;
    run_threaded(probe_last_capture_w, work_g, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over black pieces
      if (!(pt[i] & 0x08)) continue;
      captured_piece = i;
      run_threaded(probe_captures_w, work_g, 1);
    }
  }
}

static void calc_captures_b(void)
{
  int i, k;
  int n = numpcs;

  k = 0;
  for (i = 0; i < n; i++)
    if (!(pt[i] & 0x08)) k++;

  if (k == 1) {
    for (i = 0; i < n; i++)
      if (!(pt[i] & 0x08)) break;
    captured_piece = i;
    if (i == 0)
      run_threaded(probe_last_pivot_capture_b, work_piv, 1);
    else
      run_threaded(probe_last_capture_b, work_g, 1);
  } else {
    for (i = 0; i < n; i++) { // loop over white pieces
      if (pt[i] & 0x08) continue;
      captured_piece = i;
      if (i == 0)
        run_threaded(probe_pivot_captures_b, work_piv, 1);
      else
        run_threaded(probe_captures_b, work_g, 1);
    }
  }
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
  SET_WIN_VALUE(table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    SET_WIN_VALUE(table[idx3], v);
  }
  MARK_END;
}

MARK(mark_wins, int v)
{
  MARK_BEGIN;
  SET_WIN_VALUE(table[idx2], v);
  MARK_END;
}

MARK_PIVOT(mark_threat_cwins)
{
  MARK_BEGIN_PIVOT;
  SET_THREAT_CWIN(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    SET_THREAT_CWIN(table[idx3]);
  }
  MARK_END;
}

MARK(mark_threat_cwins)
{
  MARK_BEGIN;
  SET_THREAT_CWIN(table[idx2]);
  MARK_END;
}

MARK_PIVOT(mark_cwins_in_1)
{
  MARK_BEGIN_PIVOT;
  SET_CWIN_IN_1(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    uint64_t idx3 = PIVOT_MIRROR(idx2);
    SET_CWIN_IN_1(table[idx3]);
  }
  MARK_END;
}

MARK(mark_cwins_in_1)
{
  MARK_BEGIN;
  SET_CWIN_IN_1(table[idx2]);
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
    case 3: /* CAPT_LOSS */
      RETRO(mark_wins, THREAT_WIN);
      break;
    case 4: /* CAPT_CLOSS */
      RETRO(mark_threat_cwins);
      break;
    case 5: /* CHANGED -> BASE_WIN + DRAW_RULE + 1 */
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
    default:
      assume(0);
      break;
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
  if (!iter_wtm && ply)
    printf("done.\n");
  iter_wtm ^= 1;
}

static void find_draw_threats(struct thread_data *thread);

static void iterate()
{
  int i;
  iter_cnt = 0;
  iter_wtm = 1;

  for (i = 0; i < 256; i++)
    tbl[i] = win_loss[i] = loss_win[i] = 0;

  // ply = n -> determine all losses in n-1, n for current side;
  // find all wins in n, n+1 and potential losses in n, n+1 for other side

  // ply = 1
  // CAPT_LOSS -> THREAT_WIN, and CAPT_WIN -> potential loss in 2
  ply = 1;
  tbl[CAPT_WIN] = 2; // win in 1
  tbl[CAPT_LOSS] = 3;
  run_iter();

  // ply = 2
  ply++;
  tbl[CHANGED] = 1;
  tbl[THREAT_WIN] = 2; // win in 2
  win_loss[CAPT_WIN] = BASE_LOSS - 2;
  loss_win[BASE_LOSS - 2] = BASE_WIN + 3;
  run_iter();

  finished = 1;
  // ply = 3
  ply++;
  tbl[CAPT_WIN] = tbl[CAPT_LOSS] = 0;
  tbl[THREAT_WIN + 1] = 2;
  win_loss[THREAT_WIN] = BASE_LOSS - 3;
  loss_win[BASE_LOSS - 3] = BASE_WIN + 4;
  run_iter();

  while (!finished && ply < DRAW_RULE) {
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

  if (!finished || has_cursed_capts) {
    // ply = 101 -> we have win in 100, 101 and pot loss in 100, 101
    // -> find wins in 101, 102 for other side
    ply = DRAW_RULE + 1;
    tbl[BASE_WIN + ply - 1] = 2; // 100
    tbl[BASE_WIN + ply] = 2; // 101
    tbl[CAPT_CWIN] = 2; // 101
    tbl[CAPT_CLOSS] = 4; // loss in 101 -> threat_cwin
    win_loss[BASE_WIN + ply - 1] = BASE_LOSS - ply;
    // skip BASE_WIN + 102 / 103, which is THREAT_CWIN
    loss_win[BASE_LOSS - ply] = BASE_WIN + ply + 3;
    tbl[CHANGED] = 5; // when marking win in 101, test for thread_cwin
    run_iter();
    tbl[CHANGED] = 1;

    finished = 1;
    // ply = 102 -> we have win in 101, 102 and pot loss in 101, 102
    // -> find wins in 102, 103 for other side
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
      // ply = 104
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
      reduce_tables();
      num_saves++;

      for (i = 0; i < 256; i++)
        win_loss[i] = loss_win[i] = 0;
      win_loss[CAPT_WIN] = 0xff;
      win_loss[CAPT_CWIN] = 0xff;
      win_loss[THREAT_WIN] = 0xff;
      win_loss[BASE_WIN + 3] = 0xff;
      win_loss[BASE_WIN + 4] = 0xff;
      win_loss[BASE_WIN + 5] = 0xff;

      ply = 0;
      tbl[BASE_WIN + ply + 7] = 2;
      win_loss[BASE_WIN + ply + 6] = BASE_LOSS - ply - 4;
      loss_win[BASE_LOSS - ply - 4] = BASE_WIN + ply + 8;

      while (ply < REDUCE_PLY_RED && !finished) {
        finished = 1;
        ply++;
        tbl[BASE_WIN + ply + 5] = 0;
        tbl[BASE_WIN + ply + 7] = 2;
        win_loss[BASE_WIN + ply + 6] = BASE_LOSS - ply - 4;
        loss_win[BASE_LOSS - ply - 4] = BASE_WIN + ply + 8;
        run_iter();
      }

      tbl[BASE_WIN + ply + 6] = 0;
      tbl[BASE_WIN + ply + 7] = 0;
    }
  }

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
}

MARK_PIVOT(mark_threat_draws)
{
  MARK_BEGIN_PIVOT;
  if (table[idx2] == UNKNOWN) {
    table[idx2] = THREAT_DRAW;
    if (PIVOT_ON_DIAG(idx2)) {
      uint64_t idx3 = PIVOT_MIRROR(idx2);
      table[idx3] = THREAT_DRAW;
    }
  }
  MARK_END;
}

MARK(mark_threat_draws)
{
  MARK_BEGIN;
  if (table[idx2] == UNKNOWN)
    table[idx2] = THREAT_DRAW;
  MARK_END;
}

static void find_draw_threats(struct thread_data *thread)
{
  BEGIN_ITER;
  uint8_t *table = iter_table;
  uint8_t *table_opp = iter_table_opp;
  int *pcs_opp = iter_pcs_opp;

  LOOP_ITER {
    if (table[idx] != CAPT_DRAW) continue;
    FILL_OCC;
    RETRO(mark_threat_draws);
  }
}
