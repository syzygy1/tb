/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define DRAW_RULE (2 * 50)

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

#define SET_CAPT_VALUE(x,v) \
{ ubyte dummy = v; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"cmpb %1, %%al\n\t" \
"jge 1f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"jnz 0b\n" \
"1:" \
: "+m" (x), "+r" (dummy) : : "eax"); }

#define SET_CAPT_LOSS(x) \
{ ubyte dummy = CAPT_LOSS; \
__asm__( \
"movb %0, %%al\n" \
"0:\n\t" \
"testb %%al, %%al\n" \
"jnz 0f\n\t" \
"lock cmpxchgb %1, %0\n\t" \
"0:" \
: "+m" (x), "+r" (dummy) : : "eax"); }

#define CAPT_MATE 1
#define CAPT_LOSS 2
#define CAPT_CLOSS 3
#define CAPT_DRAW 4
#define CAPT_CWIN 5
#define CAPT_WIN 6

// for WDL only:
#define W_ILLEGAL 0
#define W_CAPT_WIN 1
#define W_WIN 2
#define W_CAPT_CWIN 3
#define W_CWIN 4
#define W_DRAW 5
#define W_CAPT_DRAW 6
#define W_CLOSS 7
#define W_CAPT_CLOSS 8
#define W_LOSS 9
#define W_CAPT_LOSS 10
#define W_MATE 11
#define W_BROKEN 12
#define W_ERROR 13

static ubyte w_wdl_matrix[5][8] = {
  { W_LOSS, W_MATE, W_CAPT_LOSS, W_CAPT_CLOSS, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_CLOSS, W_ERROR, W_CLOSS, W_CAPT_CLOSS, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_DRAW, W_ERROR, W_DRAW, W_DRAW, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_CWIN, W_ERROR, W_CWIN, W_CWIN, W_CWIN, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_WIN, W_ERROR, W_WIN, W_WIN, W_WIN, W_WIN, W_CAPT_WIN, 0 }
};

static ubyte w_ilbrok[2] = {
  W_ILLEGAL, W_BROKEN
};

static ubyte w_skip[14];

// no DTZ value required for:
// WDL_ERROR, WDL_DRAW, WDL_DRAW_C, WDL_CWIN_C, WDL_WIN_C, WDL_ILLEGAL, WDL_BROKEN
#define WDL_ERROR 7
#define WDL_MATE 0
#define WDL_LOSS 1
#define WDL_CAPT_LOSS 2
#define WDL_CLOSS 3
#define WDL_CAPT_CLOSS 4
#define WDL_DRAW 8
#define WDL_CAPT_DRAW 9
#define WDL_CWIN 5
#define WDL_CAPT_CWIN 10
#define WDL_WIN 6
#define WDL_CAPT_WIN 11
#define WDL_ILLEGAL 12
#define WDL_BROKEN 13

// really a 5x7 matrix
static ubyte wdl_matrix[5][8] = {
  { WDL_LOSS, WDL_MATE, WDL_CAPT_LOSS, WDL_CAPT_CLOSS, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_CLOSS, WDL_ERROR, WDL_CLOSS, WDL_CAPT_CLOSS, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_DRAW, WDL_ERROR, WDL_DRAW, WDL_DRAW, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_CWIN, WDL_ERROR, WDL_CWIN, WDL_CWIN, WDL_CWIN, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_WIN, WDL_ERROR, WDL_WIN, WDL_WIN, WDL_WIN, WDL_WIN, WDL_CAPT_WIN, 0 }
};

static int wdl_tbl_to_wdl[] = {
  1, 1, 1, 3, 3, 2, 0, 0, 0, 0, 0, 0
};

#define DTZ_BROKEN 255
#define DTZ_ERROR 254 // CHECK!
#define DTZ_ILLEGAL 0
#define DTZ_CAPT_WIN 1
#define DTZ_BASE_WIN 2
int dtz_capt_cwin;
int dtz_base_cwin;
int dtz_draw;
int dtz_capt_draw;
#define DTZ_MATE 253
#define DTZ_CAPT_LOSS 252
#define DTZ_BASE_LOSS 252
int dtz_capt_closs;
int dtz_base_closs;
int max_dtz_cursed;
int opp_capt_cwin, opp_base_cwin, opp_draw, opp_capt_draw;
int opp_capt_closs, opp_base_closs;

// mapping for WDL_ERROR  - WDL_BROKEN (7-13)
static ubyte wdl_to_dtz_c[7];

// mapping for WDL_LOSS - WDL_WIN (0-6)
static ubyte wdl_to_dtz[7][256];
static ubyte dtz_to_opp[12][256];
static ubyte dtz_matrix[256][256];

static ubyte w_matrix[16][16];

static void init_wdl_dtz(void)
{
  int i, j;
  int win_num, loss_num;

  win_num = ply_accurate_win ? DRAW_RULE : (DRAW_RULE / 2);
  loss_num = ply_accurate_loss ? DRAW_RULE : (DRAW_RULE / 2);
  dtz_capt_cwin = DTZ_BASE_WIN + 1 + win_num;
  dtz_capt_closs = DTZ_BASE_LOSS - 1 - loss_num;
  opp_capt_cwin = DTZ_BASE_WIN + 1 + loss_num;
  opp_capt_closs = DTZ_BASE_LOSS - 1 - win_num;
  dtz_base_cwin = dtz_capt_cwin + 1;
  dtz_base_closs = dtz_capt_closs - 1;
  opp_base_cwin = opp_capt_cwin + 1;
  opp_base_closs = opp_capt_closs - 1;
  max_dtz_cursed = (dtz_base_closs - dtz_base_cwin - 1) / 2 - 1;
  dtz_draw = dtz_base_cwin + max_dtz_cursed + 1;
  dtz_capt_draw = dtz_draw + 1;
  opp_draw = opp_base_cwin + max_dtz_cursed + 1;
  opp_capt_draw = opp_draw + 1;
#if 0
  printf("max_dtz_cursed = %d\n", max_dtz_cursed);
  printf("dtz_base_cwin = %d\n", dtz_base_cwin);
  printf("dtz_draw = %d\n", dtz_draw);
  printf("dtz_capt_draw = %d\n", dtz_capt_draw);
  printf("dtz_base_closs = %d\n", dtz_base_closs);
  printf("opp_base_cwin = %d\n", opp_base_cwin);
  printf("opp_draw = %d\n", opp_draw);
  printf("opp_capt_draw = %d\n", opp_capt_draw);
  printf("opp_base_closs = %d\n", opp_base_closs);
#endif

  wdl_to_dtz_c[WDL_ERROR - WDL_ERROR] = DTZ_ERROR;
  wdl_to_dtz_c[WDL_DRAW - WDL_ERROR] = dtz_draw;
  wdl_to_dtz_c[WDL_CAPT_DRAW - WDL_ERROR] = dtz_capt_draw;
  wdl_to_dtz_c[WDL_CAPT_CWIN - WDL_ERROR] = dtz_capt_cwin;
  wdl_to_dtz_c[WDL_CAPT_WIN - WDL_ERROR] = DTZ_CAPT_WIN;
  wdl_to_dtz_c[WDL_ILLEGAL - WDL_ERROR] = DTZ_ILLEGAL;
  wdl_to_dtz_c[WDL_BROKEN - WDL_ERROR] = DTZ_BROKEN;

  for (i = 0; i < 7; i++)
    for (j = 0; j < 256; j++)
      wdl_to_dtz[i][j] = DTZ_ERROR;

  wdl_to_dtz[WDL_MATE][0] = DTZ_MATE;

  for (i = 0; i < win_num; i++)
    wdl_to_dtz[WDL_WIN][i] = DTZ_BASE_WIN + 1 + i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_WIN][i] = DTZ_ERROR;

  for (i = 0; i < max_dtz_cursed; i++)
    wdl_to_dtz[WDL_CWIN][i] = dtz_base_cwin + i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_CWIN][i] = dtz_base_cwin + max_dtz_cursed;

  for (i = 0; i < loss_num; i++)
    wdl_to_dtz[WDL_LOSS][i] = DTZ_BASE_LOSS - 1 - i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_LOSS][i] = DTZ_ERROR;

  wdl_to_dtz[WDL_CAPT_LOSS][0] = DTZ_CAPT_LOSS;
  for (i = 1; i < loss_num; i++)
    wdl_to_dtz[WDL_CAPT_LOSS][i] = DTZ_BASE_LOSS - 1 - i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_CAPT_LOSS][i] = DTZ_ERROR;

  for (i = 0; i < max_dtz_cursed; i++)
    wdl_to_dtz[WDL_CLOSS][i] = dtz_base_closs - i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_CLOSS][i] = dtz_base_closs - max_dtz_cursed;

  wdl_to_dtz[WDL_CAPT_CLOSS][0] = dtz_capt_closs;
  for (i = 1; i < max_dtz_cursed; i++)
    wdl_to_dtz[WDL_CAPT_CLOSS][i] = dtz_base_closs - i;
  for (; i < 256; i++)
    wdl_to_dtz[WDL_CAPT_CLOSS][i] = dtz_base_closs - max_dtz_cursed;

  for (i = 0; i < 12; i++)
    for (j = 0; j < 256; j++)
      dtz_to_opp[i][j] = DTZ_ERROR;

  // WDL_MATE
  dtz_to_opp[WDL_MATE][DTZ_ILLEGAL] = DTZ_MATE;

  // WDL_DRAW
  dtz_to_opp[WDL_DRAW][DTZ_ILLEGAL] = opp_draw;
  dtz_to_opp[WDL_DRAW][dtz_draw] = opp_draw;
  dtz_to_opp[WDL_DRAW][dtz_capt_draw] = opp_draw;

  // WDL_CAPT_DRAW
  dtz_to_opp[WDL_CAPT_DRAW][DTZ_ILLEGAL] = opp_capt_draw;
  for (i = DTZ_CAPT_WIN; i <= dtz_capt_draw; i++)
    dtz_to_opp[WDL_CAPT_DRAW][i] = opp_capt_draw;

  // WDL_WIN
  dtz_to_opp[WDL_WIN][DTZ_MATE] = DTZ_BASE_WIN; // special case
  if (ply_accurate_loss) {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 2;
    for (i = 0; i < loss_num - 1; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 2;
  } else {
    dtz_to_opp[WDL_WIN][DTZ_CAPT_LOSS] = DTZ_BASE_WIN + 1;
    for (i = 0; i < loss_num; i++)
      dtz_to_opp[WDL_WIN][DTZ_BASE_LOSS - i - 1] = DTZ_BASE_WIN + i + 1;
  }

  // WDL_CAPT_WIN
  dtz_to_opp[WDL_CAPT_WIN][DTZ_ILLEGAL] = DTZ_CAPT_WIN;
  for (i = DTZ_CAPT_WIN; i <= DTZ_MATE; i++)
    dtz_to_opp[WDL_CAPT_WIN][i] = DTZ_CAPT_WIN;

  // WDL_CWIN
  if (ply_accurate_loss)
    dtz_to_opp[WDL_CWIN][DTZ_BASE_LOSS - loss_num] = opp_base_cwin;
  dtz_to_opp[WDL_CWIN][dtz_capt_closs] = opp_base_cwin;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CWIN][dtz_base_closs - i] = opp_base_cwin + i;

  // WDL_CAPT_CWIN
  dtz_to_opp[WDL_CAPT_CWIN][DTZ_ILLEGAL] = opp_capt_cwin;
  if (ply_accurate_loss)
    for (i = DTZ_CAPT_WIN; i <= DTZ_BASE_LOSS - loss_num; i++)
      dtz_to_opp[WDL_CAPT_CWIN][i] = opp_capt_cwin;
  else
    for (i = DTZ_CAPT_WIN; i <= dtz_capt_closs; i++)
      dtz_to_opp[WDL_CAPT_CWIN][i] = opp_capt_cwin;

  // WDL_LOSS
  if (ply_accurate_win) {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 2;
    for (i = 0; i < win_num - 1; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 2;
  } else {
    dtz_to_opp[WDL_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 1;
    for (i = 0; i < win_num; i++)
      dtz_to_opp[WDL_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 1;
  }

  // WDL_CAPT_LOSS
  dtz_to_opp[WDL_CAPT_LOSS][DTZ_ILLEGAL] = DTZ_CAPT_LOSS;
  if (ply_accurate_win) {
    dtz_to_opp[WDL_CAPT_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 2;
    for (i = 0; i < win_num - 1; i++)
      dtz_to_opp[WDL_CAPT_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 2;
  } else {
    dtz_to_opp[WDL_CAPT_LOSS][DTZ_CAPT_WIN] = DTZ_BASE_LOSS - 1;
    for (i = 0; i < win_num; i++)
      dtz_to_opp[WDL_CAPT_LOSS][DTZ_BASE_WIN + i + 1] = DTZ_BASE_LOSS - i - 1;
  }

  // WDL_CLOSS
  if (ply_accurate_win)
    dtz_to_opp[WDL_CLOSS][DTZ_BASE_WIN + win_num] = opp_base_closs;
  dtz_to_opp[WDL_CLOSS][dtz_capt_cwin] = opp_base_closs;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CLOSS][dtz_base_cwin + i] = opp_base_closs - i;

  // WDL_CAPT_CLOSS
  dtz_to_opp[WDL_CAPT_CLOSS][DTZ_ILLEGAL] = opp_capt_closs;
  for (i = DTZ_CAPT_WIN; i < dtz_capt_cwin; i++)
    dtz_to_opp[WDL_CAPT_CLOSS][i] = opp_capt_closs;
  dtz_to_opp[WDL_CAPT_CLOSS][dtz_capt_cwin] = opp_base_closs;
  for (i = 0; i <= max_dtz_cursed; i++)
    dtz_to_opp[WDL_CAPT_CLOSS][dtz_base_cwin + i] = opp_base_closs - i;

  for (i = 0; i < 256; i++)
    for (j = 0; j < 256; j++)
      dtz_matrix[i][j] = 0;

  // DRAW
  dtz_matrix[dtz_draw][DTZ_ILLEGAL] = 1;
  dtz_matrix[dtz_draw][opp_draw] = 1;
  dtz_matrix[dtz_draw][opp_capt_draw] = 1;

  // CAPT_DRAW
  dtz_matrix[dtz_capt_draw][DTZ_ILLEGAL] = 1;
  for (i = DTZ_CAPT_WIN; i <= opp_capt_draw; i++)
    dtz_matrix[dtz_capt_draw][i] = 1;

  // MATE
  dtz_matrix[DTZ_MATE][DTZ_ILLEGAL] = 1;

  // CAPT_WIN
  dtz_matrix[DTZ_CAPT_WIN][DTZ_ILLEGAL] = 1;
  for (i = DTZ_CAPT_WIN; i <= DTZ_MATE; i++)
    dtz_matrix[DTZ_CAPT_WIN][i] = 1;

  // BASE_WIN
  dtz_matrix[DTZ_BASE_WIN + 1][DTZ_MATE] = 1;
  if (ply_accurate_win) {
    dtz_matrix[DTZ_BASE_WIN + 2][DTZ_CAPT_LOSS] = 1;
    for (i = 1; i < win_num; i++)
      dtz_matrix[DTZ_BASE_WIN + i + 1][DTZ_BASE_LOSS - i] = 1;
  } else {
    dtz_matrix[DTZ_BASE_WIN + 1][DTZ_CAPT_LOSS] = 1;
    for (i = 1; i < win_num; i++) {
      dtz_matrix[DTZ_BASE_WIN + i + 1][DTZ_BASE_LOSS - i] = 1;
    }
  }

  // CAPT_LOSS
  dtz_matrix[DTZ_CAPT_LOSS][DTZ_ILLEGAL] = 1;
  if (!ply_accurate_loss) {
    dtz_matrix[DTZ_CAPT_LOSS][DTZ_CAPT_WIN] = 1;
    dtz_matrix[DTZ_CAPT_LOSS][DTZ_BASE_WIN] = 1;
  }

  // BASE_LOSS
  if (ply_accurate_loss) {
    dtz_matrix[DTZ_BASE_LOSS - 2][DTZ_BASE_WIN] = 1;
    dtz_matrix[DTZ_BASE_LOSS - 2][DTZ_CAPT_WIN] = 1;
    for (i = 1; i < loss_num; i++)
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
  } else {
    dtz_matrix[DTZ_BASE_LOSS - 1][DTZ_BASE_WIN] = 1;
    dtz_matrix[DTZ_BASE_LOSS - 1][DTZ_CAPT_WIN] = 1;
    for (i = 1; i < loss_num; i++) {
      dtz_matrix[DTZ_BASE_LOSS - 1 - i][DTZ_BASE_WIN + i] = 1;
    }
  }

  if (ply_accurate_win) {
    // CAPT_CWIN
    dtz_matrix[dtz_capt_cwin][DTZ_ILLEGAL] = 1;
    for (i = DTZ_CAPT_WIN; i <= DTZ_BASE_LOSS - win_num; i++)
      dtz_matrix[dtz_capt_cwin][i] = 1;

    // BASE_CWIN
    dtz_matrix[dtz_base_cwin][DTZ_BASE_LOSS - win_num] = 1;
    dtz_matrix[dtz_base_cwin][opp_capt_closs] = 1;
    dtz_matrix[dtz_base_cwin][opp_base_closs] = 1;
  } else {
    // CAPT_CWIN
    dtz_matrix[dtz_capt_cwin][DTZ_ILLEGAL] = 1;
    for (i = DTZ_CAPT_WIN; i <= opp_capt_closs; i++)
      dtz_matrix[dtz_capt_cwin][i] = 1;

    // BASE_CWIN
    dtz_matrix[dtz_base_cwin][opp_capt_closs] = 1;
  }
  for (i = 1; i <= max_dtz_cursed; i++)
    dtz_matrix[dtz_base_cwin + i][opp_base_closs - (i - 1)] = 1;
  dtz_matrix[dtz_base_cwin + max_dtz_cursed][opp_base_closs - max_dtz_cursed] = 1;

  if (ply_accurate_loss) {
    // CAPT_CLOSS
    dtz_matrix[dtz_capt_closs][DTZ_ILLEGAL] = 1;
    for (i = DTZ_CAPT_WIN; i <= opp_capt_cwin; i++)
      dtz_matrix[dtz_capt_closs][i] = 1;

    // BASE_CLOSS
    dtz_matrix[dtz_base_closs][DTZ_BASE_WIN + loss_num] = 1;
    dtz_matrix[dtz_base_closs][opp_capt_cwin] = 1;
    dtz_matrix[dtz_base_closs][opp_base_cwin] = 1;
  } else {
    // CAPT_CLOSS
    dtz_matrix[dtz_capt_closs][DTZ_ILLEGAL] = 1;
    for (i = DTZ_CAPT_WIN; i <= opp_capt_cwin; i++)
      dtz_matrix[dtz_capt_closs][i] = 1;

    // BASE_CLOSS
    dtz_matrix[dtz_base_closs][opp_capt_cwin] = 1;
  }
  for (i = 1; i <= max_dtz_cursed; i++)
    dtz_matrix[dtz_base_closs - i][opp_base_cwin + (i - 1)] = 1;
  dtz_matrix[dtz_base_closs - max_dtz_cursed][opp_base_cwin + max_dtz_cursed] = 1;
}

static void init_wdl(void)
{
  int i, j;

  for (i = 0; i < 14; i++)
    w_skip[i] = 0;
  w_skip[W_ILLEGAL] = 1;
  w_skip[W_BROKEN] = 1;

  for (i = 0; i < 16; i++)
    for (j = 0; j < 16; j++)
      w_matrix[i][j] = 0;

  w_matrix[W_CAPT_WIN][W_ILLEGAL] = 1;
  for (i = W_CAPT_WIN; i <= W_MATE; i++)
    w_matrix[W_CAPT_WIN][i] = 1;

  w_matrix[W_WIN][W_MATE] = 1;
  w_matrix[W_WIN][W_LOSS] = 1;
  w_matrix[W_WIN][W_CAPT_LOSS] = 1;

  w_matrix[W_CAPT_CWIN][W_ILLEGAL] = 1;
  for (i = W_CAPT_WIN; i < W_LOSS; i++)
    w_matrix[W_CAPT_CWIN][i] = 1;

  w_matrix[W_CWIN][W_CLOSS] = 1;
  w_matrix[W_CWIN][W_CAPT_LOSS] = 1;
  if (ply_accurate_win)
    w_matrix[W_CWIN][W_LOSS] = 1;

  w_matrix[W_DRAW][W_ILLEGAL] = 1;
  w_matrix[W_DRAW][W_DRAW] = 1;
  w_matrix[W_DRAW][W_CAPT_DRAW] = 1;

  w_matrix[W_CAPT_DRAW][W_ILLEGAL] = 1;
  for (i = W_CAPT_WIN; i <= W_CAPT_DRAW; i++)
    w_matrix[W_CAPT_DRAW][i] = 1;

  w_matrix[W_CLOSS][W_CWIN] = 1;
  w_matrix[W_CLOSS][W_CAPT_CWIN] = 1;
  if (ply_accurate_loss)
    w_matrix[W_CLOSS][W_WIN] = 1;

  w_matrix[W_CAPT_CLOSS][W_ILLEGAL] = 1;
  for (i = W_CAPT_WIN; i < W_DRAW; i++)
    w_matrix[W_CAPT_CLOSS][i] = 1;

  w_matrix[W_LOSS][W_CAPT_WIN] = 1;
  w_matrix[W_LOSS][W_WIN] = 1;

  w_matrix[W_CAPT_LOSS][W_ILLEGAL] = 1;
  w_matrix[W_CAPT_LOSS][W_CAPT_WIN] = 1;
  w_matrix[W_CAPT_LOSS][W_WIN] = 1;

  w_matrix[W_MATE][W_ILLEGAL] = 1;
}

static int is_attacked(int sq, int *pcs, bitboard occ, int *p)
{
  int k;

  do {
    k = *pcs;
    bitboard bb = PieceRange(p[k], pt[k], occ);
    if (bb & bit[sq]) return 1;
  } while (*(++pcs) >= 0);
  return 0;
}

static int check_mate(int *pcs, long64 idx0, ubyte *table, bitboard occ, int *p)
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
      if (table[idx2] < WDL_ILLEGAL) return 0;
      ClearFirst(bb);
    }
  } else { // otherwise k == 1, i.e. black king
    bb = BlackKingMoves;
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove1(idx0, sq);
      if (table[idx2] < WDL_ILLEGAL) return 0;
      ClearFirst(bb);
    }
  }
  while ((k = *pcs++) >= 0) {
    bb = PieceMoves2(p[k], pt[k], occ);
    idx = idx0 & ~mask[k];
    while (bb) {
      sq = FirstOne(bb);
      idx2 = MakeMove2(idx, k, sq);
      if (table[idx2] < WDL_ILLEGAL) return 0;
      ClearFirst(bb);
    }
  }

  return 1;
}

void calc_broken(struct thread_data *thread)
{
  long64 idx, idx2;
  int i;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  bitboard occ, bb;
  long64 end = thread->end;

  for (idx = thread->begin; idx < end; idx += 64) {
    FILL_OCC64_cheap {
      for (i = 0, bb = 1; i < 64; i++, bb <<= 1)
	table_w[idx + i] = table_b[idx + i] = (occ & bb) ? WDL_BROKEN : 0;
    } else {
      for (i = 0; i < 64; i++)
	table_w[idx + i] = table_b[idx + i] = WDL_BROKEN;
    }
  }
}

void calc_mates(struct thread_data *thread)
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
	int chk_b = (table_w[idx + i] == WDL_ILLEGAL);
	int chk_w = (table_b[idx + i] == WDL_ILLEGAL);
	if (chk_w == chk_b) continue;
	p[n - 1] = i;
	if (chk_w) {
	  if (!table_w[idx + i] && check_mate(white_pcs, idx + i, table_b, occ | bb, p))
	    table_w[idx + i] = CAPT_MATE;
	} else {
	  if (!table_b[idx + i] && check_mate(black_pcs, idx + i, table_w, occ | bb, p))
	    table_b[idx + i] = CAPT_MATE;
	}
      }
    }
  }
}

MARK(mark_illegal)
{
  MARK_BEGIN;
  table[idx2] = WDL_ILLEGAL;
  MARK_END;
}

MARK_PIVOT0(mark_capt_wins)
{
  MARK_BEGIN_PIVOT0;
  if (table[idx2] < WDL_ILLEGAL) {
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
  if (table[idx2] < WDL_ILLEGAL) {
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
  if (table[idx2] < WDL_ILLEGAL)
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

MARK_PIVOT0(mark_capt_losses)
{
  MARK_BEGIN_PIVOT0;
  SET_CAPT_LOSS(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    SET_CAPT_LOSS(table[idx3]);
  }
  MARK_END;
}

MARK_PIVOT1(mark_capt_losses)
{
  MARK_BEGIN_PIVOT1;
  SET_CAPT_LOSS(table[idx2]);
  if (PIVOT_ON_DIAG(idx2)) {
    long64 idx3 = PIVOT_MIRROR(idx2);
    SET_CAPT_LOSS(table[idx3]);
  }
  MARK_END;
}

MARK(mark_capt_losses)
{
  MARK_BEGIN;
  SET_CAPT_LOSS(table[idx2]);
  MARK_END;
}

static int captured_piece;

void calc_illegal_w(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT1 {
    FILL_OCC_CAPTS_PIVOT1 {
      MAKE_IDX2_PIVOT1;
      LOOP_WHITE_PIECES_PIVOT1(mark_illegal);
    }
  }
}

void calc_illegal_b(struct thread_data *thread)
{
  BEGIN_CAPTS_PIVOT_NOPROBE;

  LOOP_CAPTS_PIVOT0 {
    FILL_OCC_CAPTS_PIVOT0 {
      MAKE_IDX2_PIVOT0;
      LOOP_BLACK_PIECES_PIVOT0(mark_illegal);
    }
  }
}

void probe_captures_w(struct thread_data *thread)
{
  BEGIN_CAPTS;

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
	LOOP_WHITE_PIECES(mark_capt_value, CAPT_CWIN);
	break;
      case 0:
	LOOP_WHITE_PIECES(mark_capt_value, CAPT_DRAW);
	break;
      case 1:
	LOOP_WHITE_PIECES(mark_capt_value, CAPT_CLOSS);
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
}

void probe_captures_b(struct thread_data *thread)
{
  BEGIN_CAPTS;

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
	LOOP_BLACK_PIECES(mark_capt_value, CAPT_CWIN);
	break;
      case 0:
	LOOP_BLACK_PIECES(mark_capt_value, CAPT_DRAW);
	break;
      case 1:
	LOOP_BLACK_PIECES(mark_capt_value, CAPT_CLOSS);
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
}

void calc_captures_w(void)
{
  int i, j, k;
  int n = numpcs;

  captured_piece = black_king;
  run_threaded(calc_illegal_w, work_piv1, 1);

  for (i = 2; i < n; i++) { // loop over black pieces
    if (!(pt[i] & 0x08)) continue;
    for (k = 0, j = 0; black_pcs[k] >= 0; k++)
      if (black_pcs[k] != i)
	pcs2[j++] = black_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(probe_captures_w, work_g, 1);
  }
}

void calc_captures_b(void)
{
  int i, j, k;
  int n = numpcs;

  run_threaded(calc_illegal_b, work_piv0, 1);

  for (i = 2; i < n; i++) { // loop over white pieces
    if (pt[i] & 0x08) continue;
    for (k = 0, j = 0; white_pcs[k] >= 0; k++)
      if (white_pcs[k] != i)
        pcs2[j++] = white_pcs[k];
    pcs2[j] = -1;
    captured_piece = i;
    run_threaded(probe_captures_b, work_g, 1);
  }
}

// TODO: remove the if > 4
#if 1
void load_wdl(struct thread_data *thread)
{
  long64 idx, idx2, idx_p, idx2_p;
  int i, v1, v2, v1_p;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  ubyte *table = load_table;
  ubyte *src = tb_table;
  int *perm = tb_perm;
  long64 end = thread->end;
  int pos[MAX_PIECES];
  ubyte *norm = load_entry->norm[load_bside];
  int *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1_p = table[idx];
    if (v1_p < WDL_ILLEGAL) break;
  }
  if (idx == end) return;
  for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
    pos[perm[i]] = idx2 & 0x3f;
  pos[perm[0]] = KK_inv[idx2][0];
  pos[perm[1]] = KK_inv[idx2][1];
  idx2_p = encode_piece(entry, norm, pos, factor);
  __builtin_prefetch(&src[idx2_p], 0, 3);
  idx_p = idx;

  for (idx++; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) continue;
    for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = KK_inv[idx2][0];
    pos[perm[1]] = KK_inv[idx2][1];
    idx2 = encode_piece(entry, norm, pos, factor);
    __builtin_prefetch(&src[idx2], 0, 3);
    v2 = src[idx2_p];
    if (unlikely(v2 > 4)) table[idx_p] = WDL_ERROR;
    else table[idx_p] = wdl_matrix[v2][v1_p];
if(unlikely(table[idx_p]==WDL_ERROR))
error("WDL_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx_p, v2, v1_p);
    v1_p = v1;
    idx_p = idx;
    idx2_p = idx2;
  }

  v2 = src[idx2_p];
  if (unlikely(v2 > 4)) table[idx_p] = WDL_ERROR;
  else table[idx_p] = wdl_matrix[v2][v1_p];
if(unlikely(table[idx_p]==WDL_ERROR))
error("WDL_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx_p, v2, v1_p);
}
#else
void load_wdl(struct thread_data *thread)
{
  long64 idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  ubyte *table = load_table;
  ubyte *src = tb_table;
  int *perm = tb_perm;
  long64 end = thread->end;
  int pos[MAX_PIECES];
  ubyte *norm = load_entry->norm[load_bside];
  int *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) continue;
    for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = KK_inv[idx2][0];
    pos[perm[1]] = KK_inv[idx2][1];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = src[idx2];
    if (unlikely(v2 > 4)) table[idx] = WDL_ERROR;
    else table[idx] = wdl_matrix[v2][v1];
if(unlikely(table[idx]==WDL_ERROR))
printf("WDL_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx, v2, v1);
  }
}
#endif

void load_dtz(struct thread_data *thread)
{
  long64 idx, idx2, idx_p, idx2_p;
  int i, v1, v2, v1_p;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  ubyte *table = load_table;
  ubyte *src = tb_table;
  int *perm = tb_perm;
  long64 end = thread->end;
  int pos[MAX_PIECES];
  ubyte *norm = load_entry->norm[load_bside];
  int *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1_p = table[idx];
    if (v1_p < WDL_ERROR)
      break;
    table[idx] = wdl_to_dtz_c[v1_p - WDL_ERROR];
  }
  if (idx == end) return;
  for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
    pos[perm[i]] = idx2 & 0x3f;
  pos[perm[0]] = KK_inv[idx2][0];
  pos[perm[1]] = KK_inv[idx2][1];
  idx2_p = encode_piece(entry, norm, pos, factor);
  __builtin_prefetch(&src[idx2_p], 0, 3);
  idx_p = idx;

  for (idx++; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = KK_inv[idx2][0];
    pos[perm[1]] = KK_inv[idx2][1];
    idx2 = encode_piece(entry, norm, pos, factor);
    __builtin_prefetch(&src[idx2], 0, 3);
    v2 = src[idx2_p];
    table[idx_p] = wdl_to_dtz[v1_p][v2];
if(unlikely(table[idx_p]==DTZ_ERROR))
error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx_p, v1_p, v2, idx2_p);
    v1_p = v1;
    idx_p = idx;
    idx2_p = idx2;
  }

  v2 = src[idx2_p];
  table[idx_p] = wdl_to_dtz[v1_p][v2];
if(table[idx_p]==DTZ_ERROR)
error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx_p, v1_p, v2, idx2_p);
}

void load_dtz_mapped(struct thread_data *thread)
{
  long64 idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  ubyte *table = load_table;
  ubyte *src = tb_table;
  int *perm = tb_perm;
  long64 end = thread->end;
  int pos[MAX_PIECES];
  ubyte *norm = load_entry->norm[load_bside];
  int *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;
  ubyte (*map)[256] = load_map;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    int wdl = wdl_tbl_to_wdl[v1];
    for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = KK_inv[idx2][0];
    pos[perm[1]] = KK_inv[idx2][1];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = map[wdl][src[idx2]];
    table[idx] = wdl_to_dtz[v1][v2];
if(unlikely(table[idx]==DTZ_ERROR))
error("DTZ_ERROR: idx = %"PRIu64", wdl = %d, v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, wdl, v1, v2, idx2);
  }
}

static int compute(int *pcs, long64 idx0, ubyte *table, bitboard occ, int *p)
{
  int sq;
  long64 idx, idx2;
  bitboard bb;
  int best = DTZ_ILLEGAL;

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

void verify_opp(struct thread_data *thread)
{
  BEGIN_ITER;
  ubyte *opp_table = load_opp_table;
  ubyte *dtz_table = load_table;
  int *opp_pieces = load_opp_pieces;

  LOOP_ITER {
    int v = opp_table[idx];
    if (v >= WDL_ILLEGAL) {
      opp_table[idx] = wdl_to_dtz_c[v - WDL_ERROR];
      if (unlikely(v == WDL_ERROR))
	error("ERROR: opp table, idx = %"PRIu64", v = WDL_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute(opp_pieces, idx, dtz_table, occ, p);
    int z = dtz_to_opp[v][w];
    opp_table[idx] = z;
    if (unlikely(z == DTZ_ERROR))
      error("ERROR: opp table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_dtz(struct thread_data *thread)
{
  BEGIN_ITER;
  ubyte *dtz_table = load_table;
  ubyte *opp_table = load_opp_table;
  int *dtz_pieces = load_pieces;

  LOOP_ITER {
    int v = dtz_table[idx];
    if (v == DTZ_ILLEGAL || v >= DTZ_ERROR) {
      if (unlikely(v == DTZ_ERROR))
	error("ERROR: dtz table, idx = %"PRIu64", v = DTZ_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute(dtz_pieces, idx, opp_table, occ, p);
    if (unlikely(!dtz_matrix[v][w]))
      error("ERROR: dtz table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_wdl(struct thread_data *thread)
{
  BEGIN_ITER;
  ubyte *table = load_table;
  ubyte *opp_table = load_opp_table;
  int *pieces = load_pieces;

  LOOP_ITER {
    int v = table[idx];
    if (w_skip[v]) continue;
    FILL_OCC;
    int w = compute(pieces, idx, opp_table, occ, p);
    if (unlikely(!w_matrix[v][w]))
      error("ERROR: wdl table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void wdl_load_wdl(struct thread_data *thread)
{
  long64 idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  assume(n >= 3 && n <= 6);
  ubyte *table = load_table;
  ubyte *src = (ubyte *)tb_table;
  int *perm = tb_perm;
  long64 end = thread->end;
  int pos[MAX_PIECES];
  ubyte *norm = load_entry->norm[load_bside];
  int *factor = load_entry->factor[load_bside];
  struct TBEntry_piece *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) {
      table[idx] = w_ilbrok[v1 - WDL_ILLEGAL];
      continue;
    }
    for (i = n - 1, idx2 = idx; i > 1; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = KK_inv[idx2][0];
    pos[perm[1]] = KK_inv[idx2][1];
    idx2 = encode_piece(entry, norm, pos, factor);
    v2 = src[idx2];
    if (unlikely(v2 > 4)) table[idx] = W_ERROR;
    else table[idx] = w_wdl_matrix[v2][v1];
if(unlikely(table[idx]==W_ERROR))
error("W_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx, v2, v1);
  }
}

