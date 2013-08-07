/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define DRAW_RULE (2 * 50)
#define REDUCE_PLY 122
#define REDUCE_PLY_RED 119

#define MAX_PLY 509
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

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);
void reduce_tables(void);

#define SET_CHANGED(x) \
do { ubyte dummy = CHANGED; \
__asm__( \
"movb %2, %%al\n\t" \
"lock cmpxchgb %1, %0" \
: "+m" (x), "+r" (dummy) : "i" (UNKNOWN) : "eax"); } while (0)

#define SET_CAPT_VALUE(x,v) \
do { ubyte dummy = v; __asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); } while (0)

#define SET_WIN_VALUE(x,v) \
do { ubyte dummy = v; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jbe 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); } while (0)

ubyte win_loss[256];
ubyte loss_win[256];

// check whether all moves end up in wins for the opponent
// if we are here, all captures are losing
static int check_loss(int *restrict pcs, long64 idx0, ubyte *restrict table,
	bitboard occ, int *restrict p)
{
  int sq;
  long64 idx, idx2;
  bitboard bb;
  int best = LOSS_IN_ONE;

  int k = *pcs++;
  if (k == 0) { // white king
    bb = WhiteKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove0(idx0, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
  } else { // otherwise k == 1, i.e. black king
    bb = BlackKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove1(idx0, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
  }
  while ((k = *pcs++) >= 0) {
    bb = PieceMoves2(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove2(idx, k, sq);
      int v = win_loss[table[idx2]];
      if (!v) return 0;
      if (v < best) best = v;
      ClearFirst(bb);
    }
  }

  return best;
}

#define ASM_GOTO
static int is_attacked(int sq, const int *restrict pcs, bitboard occ,
			const int *restrict p)
{
  int k;

  do {
    k = *pcs;
    bitboard bb = PieceRange(p[k], pt[k], occ);
#ifdef ASM_GOTO
    jump_bit_set(bb, sq, lab);
#else
    if (bb & bit[sq]) return 1;
#endif
  } while (*(++pcs) >= 0);
  return 0;
#ifdef ASM_GOTO
lab:
  return 1;
#endif
}

static int check_mate(int *restrict pcs, long64 idx0, ubyte *restrict table,
	bitboard occ, int *restrict p)
{
  int sq;
  long64 idx, idx2;
  bitboard bb;

  int k = *pcs++;
  if (k == 0) { // white king
    bb = WhiteKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove0(idx0, sq);
      if (table[idx2] != ILLEGAL) return 0;
      ClearFirst(bb);
    }
  } else { // otherwise k == 1, i.e. black king
    bb = BlackKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove1(idx0, sq);
      if (table[idx2] != ILLEGAL) return 0;
      ClearFirst(bb);
    }
  }
  while ((k = *pcs++) >= 0) {
    bb = PieceMoves2(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove2(idx, k, sq);
      if (table[idx2] != ILLEGAL) return 0;
      ClearFirst(bb);
    }
  }

  return 1;
}

static void calc_broken(struct thread_data *thread)
{
  long64 idx, idx2;
  int i;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  bitboard occ, bb;
  long64 end = thread->end;

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
  long64 idx, idx2;
  bitboard occ, bb;
  int i;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  int p[MAX_PIECES];
  long64 end = thread->end;

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

MARK_PIVOT0(mark_capt_wins)
{
  MARK_BEGIN_PIVOT0;
  if (table[idx2] != ILLEGAL) {
    table[idx2] = CAPT_WIN;
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
      table[idx3] = CAPT_WIN;
    }
  }
  MARK_END;
}

MARK_PIVOT1(mark_capt_wins)
{
  MARK_BEGIN_PIVOT1;
  if (table[idx2] != ILLEGAL) {
    table[idx2] = CAPT_WIN;
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
      table[idx3] = CAPT_WIN;
    }
  }
  MARK_END;
}

MARK(mark_capt_wins)
{
  MARK_BEGIN;
  if (table[idx2] != ILLEGAL)
    table[idx2] = CAPT_WIN;
  MARK_END;
}

MARK_PIVOT0(mark_capt_value, ubyte v)
{
  MARK_BEGIN_PIVOT0;
  SET_CAPT_VALUE(table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    SET_CAPT_VALUE(table[idx3], v);
  }
  MARK_END;
}

MARK_PIVOT1(mark_capt_value, ubyte v)
{
  MARK_BEGIN_PIVOT1;
  SET_CAPT_VALUE(table[idx2], v);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    SET_CAPT_VALUE(table[idx3], v);
  }
  MARK_END;
}

MARK(mark_capt_value, ubyte v)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], v);
  MARK_END;
}

MARK_PIVOT0(mark_changed)
{
  MARK_BEGIN_PIVOT0;
  if (table[idx2] == UNKNOWN)
    SET_CHANGED(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    if (table[idx3] == UNKNOWN)
      SET_CHANGED(table[idx3]);
  }
  MARK_END;
}

MARK_PIVOT1(mark_changed)
{
  MARK_BEGIN_PIVOT1;
  if (table[idx2] == UNKNOWN)
    SET_CHANGED(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
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

MARK_PIVOT0(mark_wins, int v)
{
  MARK_BEGIN_PIVOT0;
  if (table[idx2]) {
    SET_WIN_VALUE(table[idx2], v);
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
      SET_WIN_VALUE(table[idx3], v);
    }
  }
  MARK_END;
}

MARK_PIVOT1(mark_wins, int v)
{
  MARK_BEGIN_PIVOT1;
  if (table[idx2]) {
    SET_WIN_VALUE(table[idx2], v);
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
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
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT1 {
    FILL_OCC_CAPTS_PIVOT1 {
      MAKE_IDX2_PIVOT1;
      LOOP_WHITE_PIECES_PIVOT1(mark_illegal);
    }
  }
}

static void calc_illegal_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT0 {
    FILL_OCC_CAPTS_PIVOT0 {
      MAKE_IDX2_PIVOT0;
      LOOP_BLACK_PIECES_PIVOT0(mark_illegal);
    }
  }
}

static void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      if (is_attacked(p[white_king], pcs2, occ, p)) continue;
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
	LOOP_WHITE_PIECES(mark_changed);
	break;
      default:
	assume(0);
	break;
      }
    }
  }

  if (has_cursed) cursed_capt[i] |= has_cursed;
}

static void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;
  int has_cursed = 0;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      if (is_attacked(p[black_king], pcs2, occ, p)) continue;
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
	LOOP_BLACK_PIECES(mark_changed);
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
  int i, j, k;
  int n = numpcs;

  run_threaded(calc_illegal_w, work_piv1, 1);

  for (i = 2; i < n; i++) { // loop over non-king black pieces
    if (!(pt[i] & 0x08)) continue;
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

  run_threaded(calc_illegal_b, work_piv0, 1);

  for (i = 2; i < n; i++) { // loop over non-king white pieces
    if (pt[i] & 0x08) continue;
    for (k = 0, j = 0; white_pcs[k] >= 0; k++)
      if (white_pcs[k] != i)
        pcs2[j++] = white_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(probe_captures_b, work_g, 1);
  }
}

MARK_PIVOT0(mark_win_in_1)
{
  MARK_BEGIN_PIVOT0;
  if (table[idx2] != ILLEGAL && table[idx2] != CAPT_WIN) {
    table[idx2] = WIN_IN_ONE;
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
      table[idx3] = WIN_IN_ONE;
    }
  }
  MARK_END;
}

MARK_PIVOT1(mark_win_in_1)
{
  MARK_BEGIN_PIVOT1;
  if (table[idx2] != ILLEGAL && table[idx2] != CAPT_WIN) {
    table[idx2] = WIN_IN_ONE;
    if (PIVOT_ON_DIAG(idx2)) {
      long64 idx3 = PIVOT_MIRROR(idx2);
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

ubyte *iter_table, *iter_table_opp;
int *iter_pcs;
int *iter_pcs_opp;
ubyte tbl[256];

void iter(struct thread_data *thread)
{
  BEGIN_ITER;
  int not_fin = 0;
  ubyte *restrict table = iter_table;
  ubyte *restrict table_opp = iter_table_opp;
  int *restrict pcs = iter_pcs;
  int *restrict pcs_opp = iter_pcs_opp;

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

void run_iter(void)
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

void iterate()
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

static ubyte *reset_v;

MARK_PIVOT0(reset_capt_closs)
{
  ubyte *v = reset_v;

  MARK_BEGIN_PIVOT0;
  if (v[table[idx2]]) table[idx2] = CAPT_CLOSS;
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    if (v[table[idx3]]) table[idx3] = CAPT_CLOSS;
  }
  MARK_END;
}

MARK_PIVOT1(reset_capt_closs)
{
  ubyte *v = reset_v;

  MARK_BEGIN_PIVOT1;
  if (v[table[idx2]]) table[idx2] = CAPT_CLOSS;
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    if (v[table[idx3]]) table[idx3] = CAPT_CLOSS;
  }
  MARK_END;
}

MARK(reset_capt_closs)
{
  ubyte *v = reset_v;

  MARK_BEGIN;
  if (v[table[idx2]]) table[idx2] = CAPT_CLOSS;
  MARK_END;
}

void reset_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      if (is_attacked(p[white_king], pcs2, occ, p)) continue;
      int v = probe_tb(pt2, p, 0, occ, 0, 2);
      if (v == 1) {
	MAKE_IDX2;
	LOOP_WHITE_PIECES(reset_capt_closs);
      }
    }
  }
}

void reset_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      if (is_attacked(p[black_king], pcs2, occ, p)) continue;
      int v = probe_tb(pt2, p, 1, occ, 0, 2);
      if (v == 1) {
	MAKE_IDX2;
	LOOP_BLACK_PIECES(reset_capt_closs);
      }
    }
  }
}

void reset_captures(void)
{
  int i, j, k;
  int n = numpcs;
  ubyte v[256];

  reset_v = v;

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
    for (k = 0, j = 0; black_pcs[k] >= 0; k++)
      if (black_pcs[k] != i)
	pcs2[j++] = black_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(reset_captures_w, work_g, 1);
  }

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
    run_threaded(reset_captures_b, work_g, 1);
  }
}

// CAPT_CLOSS means there is a capture into a cursed win, preventing a loss
// we need to determine if there are regular moves into a slower cursed loss
int compute_capt_closs(int *restrict pcs, long64 idx0, ubyte *restrict table,
	bitboard occ, int *restrict p)
{
  int sq;
  long64 idx, idx2;
  bitboard bb;
  int best = 0;

  int k = *pcs++;
  if (k == 0) { // white king
    bb = WhiteKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove0(idx0, sq);
      int v = table[idx2];
      if (v > best) best = v;
      ClearFirst(bb);
    }
  } else { // otherwise k == 1, i.e. black king
    bb = BlackKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove1(idx0, sq);
      int v = table[idx2];
      if (v > best) best = v;
      ClearFirst(bb);
    }
  }
  while ((k = *pcs++) >= 0) {
    bb = PieceMoves2(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove2(idx, k, sq);
      int v = table[idx2];
      if (v > best) best = v;
      ClearFirst(bb);
    }
  }

  return best;
}

void fix_closs_w(struct thread_data *thread)
{
  BEGIN_ITER;

  LOOP_ITER {
    if (table_w[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(white_pcs, idx, table_b, occ, p);
    table_w[idx] = win_loss[v];
  }
}

void fix_closs_b(struct thread_data *thread)
{
  BEGIN_ITER;

  LOOP_ITER {
    if (table_b[idx] != CAPT_CLOSS) continue;
    FILL_OCC;
    int v = compute_capt_closs(black_pcs, idx, table_w, occ, p);
    table_b[idx] = win_loss[v];
  }
}

void fix_closs(void)
{
  int i;

  if (!to_fix_w && !to_fix_b) return;

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
    run_threaded(fix_closs_w, work_g, 1);
  }
  if (to_fix_b) {
    printf("fixing cursed black losses.\n");
    run_threaded(fix_closs_b, work_g, 1);
  }
}

