int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

#define SET_CAPT_VALUE(x,v) \
{ uint8_t dummy = v; \
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
{ uint8_t dummy = CAPT_LOSS; \
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

static uint8_t w_wdl_matrix[256][8] = {
  { W_LOSS, W_MATE, W_CAPT_LOSS, W_CAPT_CLOSS, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_CLOSS, W_ERROR, W_CLOSS, W_CAPT_CLOSS, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_DRAW, W_ERROR, W_DRAW, W_DRAW, W_CAPT_DRAW, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_CWIN, W_ERROR, W_CWIN, W_CWIN, W_CWIN, W_CAPT_CWIN, W_CAPT_WIN, 0 },
  { W_WIN, W_ERROR, W_WIN, W_WIN, W_WIN, W_WIN, W_CAPT_WIN, 0 }
};

static uint8_t w_ilbrok[2] = {
  W_ILLEGAL, W_BROKEN
};

static uint8_t w_skip[14];

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
static uint8_t wdl_matrix[256][8] = {
  { WDL_LOSS, WDL_MATE, WDL_CAPT_LOSS, WDL_CAPT_CLOSS, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_CLOSS, WDL_ERROR, WDL_CLOSS, WDL_CAPT_CLOSS, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_DRAW, WDL_ERROR, WDL_DRAW, WDL_DRAW, WDL_CAPT_DRAW, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_CWIN, WDL_ERROR, WDL_CWIN, WDL_CWIN, WDL_CWIN, WDL_CAPT_CWIN, WDL_CAPT_WIN, 0 },
  { WDL_WIN, WDL_ERROR, WDL_WIN, WDL_WIN, WDL_WIN, WDL_WIN, WDL_CAPT_WIN, 0 }
};

static uint8_t wdl_pawn[6][16];

static char wdl_to_pawn[16] = {
//  -2, -2, -2, -1, -1, 1, 2, -3, 0, 0, 1, 2, -3, -3
  2, 2, 2, 1, 1, -1, -2, -3, 0, 0, -1, -2, -3, -3
};

static uint8_t pawn_skip[16];

static int wdl_tbl_to_wdl[] = {
  1, 1, 1, 3, 3, 2, 0
};

#define DTZ_BROKEN 255
#define DTZ_ERROR 254 // CHECK!
#define DTZ_ILLEGAL 0
//#define DTZ_PAWN_WIN 1
#define DTZ_CAPT_WIN 1
#define DTZ_BASE_WIN 2
int dtz_capt_cwin;
int dtz_base_cwin;
int dtz_draw;
int dtz_capt_draw;
#define DTZ_MATE 253 // CHECK!
#define DTZ_CAPT_LOSS 252
#define DTZ_BASE_LOSS 252
int dtz_capt_closs;
int dtz_base_closs;
int max_dtz_cursed;
int opp_capt_cwin, opp_base_cwin, opp_draw, opp_capt_draw;
int opp_capt_closs, opp_base_closs;

static uint8_t wdl_to_dtz_c[7];

static uint8_t wdl_to_dtz[8][256];
static uint8_t dtz_to_opp[12][256];
static uint8_t dtz_matrix[256][256];

static uint8_t w_matrix[16][16];

static char dtz_to_wdl[256];

static uint8_t dtz_matrix[256][256];

void init_wdl_matrix(void)
{
  int i, j;

  for (i = 5; i < 256; i++)
    for (j = 0; j < 8; j++)
      wdl_matrix[i][j] = WDL_ERROR;
}

void init_w_wdl_matrix(void)
{
  int i, j;

  for (i = 5; i < 256; i++)
    for (j = 0; j < 8; j++)
      w_wdl_matrix[i][j] = W_ERROR;
}

// MATE, LOSS, LOSS_C, CLOSS, CLOSS_C, CWIN, WIN
void init_wdl_dtz(void)
{
  int i, j;
  int win_num, loss_num;

  win_num = ply_accurate_win ? 100 : 50;
  loss_num = ply_accurate_loss ? 100 : 50;
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
#if 1
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
//    dtz_to_opp[WDL_LOSS][DTZ_PAWN_WIN] = DTZ_BASE_LOSS - 1;
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

  for (i = 0; i < 14; i++)
    pawn_skip[i] = 0;
  pawn_skip[WDL_ILLEGAL] = 1;
  pawn_skip[WDL_BROKEN] = 1;
  pawn_skip[WDL_CAPT_WIN] = 1;

// we can ignore ILLEGAL, BROKEN, CAPT_WIN
  for (i = 0; i < 6; i++)
    for (j = 0; j < 16; j++)
      wdl_pawn[i][j] = WDL_ERROR;
  for (i = 0; i <= WDL_BROKEN; i++)
    wdl_pawn[0][i] = i;
  for (i = WDL_CAPT_LOSS; i <= WDL_CAPT_WIN; i++)
    wdl_pawn[1][i] = i;
  wdl_pawn[1][WDL_LOSS] = WDL_CAPT_LOSS; // in fact PAWN_LOSS
  for (i = WDL_CAPT_CLOSS; i <= WDL_CAPT_WIN; i++)
    wdl_pawn[2][i] = i;
  wdl_pawn[2][WDL_CLOSS] = WDL_CAPT_CLOSS; // in fact PAWN_CLOSS
  wdl_pawn[3][WDL_WIN] = WDL_WIN;
  for (i = WDL_CAPT_DRAW; i <= WDL_CAPT_WIN; i++)
    wdl_pawn[3][i] = i;
  wdl_pawn[3][WDL_WIN] = WDL_WIN;
  wdl_pawn[3][WDL_CWIN] = WDL_CWIN;
  wdl_pawn[3][WDL_DRAW] = WDL_CAPT_DRAW; // in fact PAWN_DRAW
  wdl_pawn[4][WDL_WIN] = WDL_WIN;
  wdl_pawn[4][WDL_CAPT_CWIN] = WDL_CAPT_CWIN;
  wdl_pawn[4][WDL_CWIN] = WDL_CAPT_CWIN; // in fact PAWN_CWIN
  wdl_pawn[5][WDL_WIN] = WDL_CAPT_WIN; // in fact PAWN_WIN
}

void init_pawn_dtz(int opp)
{
  int i;

  if (!opp) {
    dtz_to_wdl[DTZ_ILLEGAL] = -3;
    dtz_to_wdl[dtz_draw] = dtz_to_wdl[dtz_capt_draw] = 0;
    for (i = DTZ_CAPT_WIN; i < dtz_capt_cwin; i++)
      dtz_to_wdl[i] = -2;
    for (; i <= dtz_capt_cwin + max_dtz_cursed; i++)
      dtz_to_wdl[i] = -1;
    for (i = DTZ_MATE; i > dtz_capt_closs; i--)
      dtz_to_wdl[i] = 2;
    for (; i >= dtz_capt_closs - max_dtz_cursed; i--)
      dtz_to_wdl[i] = 1;
  } else {
    dtz_to_wdl[DTZ_ILLEGAL] = -3;
    dtz_to_wdl[opp_draw] = dtz_to_wdl[opp_capt_draw] = 0;
    for (i = DTZ_CAPT_WIN; i < opp_capt_cwin; i++)
      dtz_to_wdl[i] = -2;
    for (; i <= opp_capt_cwin + max_dtz_cursed; i++)
      dtz_to_wdl[i] = -1;
    for (i = DTZ_MATE; i > opp_capt_closs; i--)
      dtz_to_wdl[i] = 2;
    for (; i >= opp_capt_closs - max_dtz_cursed; i--)
      dtz_to_wdl[i] = 1;
  }
}

void init_wdl(void)
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

  dtz_to_wdl[W_ILLEGAL] = -3;
  dtz_to_wdl[W_DRAW] = dtz_to_wdl[W_CAPT_DRAW] = 0;
  dtz_to_wdl[W_CAPT_WIN] = dtz_to_wdl[W_WIN] = -2;
  dtz_to_wdl[W_CAPT_CWIN] = dtz_to_wdl[W_CWIN] = -1;
  dtz_to_wdl[W_CAPT_CLOSS] = dtz_to_wdl[W_CLOSS] = 1;
  dtz_to_wdl[W_CAPT_LOSS] = dtz_to_wdl[W_LOSS] = dtz_to_wdl[W_MATE] = 2;
}

int is_attacked(int sq, int *pcs, bitboard occ, int *p)
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

// captures should be taken care of already
int check_mate_pieces(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ, int *p)
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
      if (table[idx2] < WDL_ILLEGAL) return 0;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return 1;
}

int check_mate_pawns_w(uint64_t idx0, uint8_t *table, bitboard occ, int *p)
{
  int i;
  int sq;
  uint64_t idx, idx2;

  for (i = 0; i < numpawns; i++) {
    if (!pw[i]) continue;
    sq = p[i];
    if (bit[sq + 8] & occ) continue;
    if (sq >= 0x30) {
      if (!is_attacked(p[white_king], black_all, occ ^ bit[sq] ^ bit[sq + 8], p))
        return 0;
    } else {
      idx = idx0 & ~mask[i];
      if (i) idx2 = idx | ((uint64_t)((p[i] + 0x08) ^ 0x38) << shift[i]);
      else idx2 = idx | piv_idx[p[i] + 0x08];
      if (table[idx2] < WDL_ILLEGAL) return 0;
      if (sq < 0x10 && !(bit[sq + 0x10] & occ)) {
        if (i) idx2 = idx | ((uint64_t)((p[i] + 0x10) ^ 0x38) << shift[i]);
        else idx2 = idx | piv_idx[p[i] + 0x10];
        if (table[idx2] < WDL_ILLEGAL) return 0;
      }
    }
  }

  return 1;
}

int check_mate_pawns_b(uint64_t idx0, uint8_t *table, bitboard occ, int *p)
{
  int i;
  int sq;
  uint64_t idx, idx2;

  for (i = 0; i < numpawns; i++) {
    if (pw[i]) continue;
    sq = p[i];
    if (bit[sq - 8] & occ) continue;
    if (sq < 0x10) {
      if (!is_attacked(p[black_king], white_all, occ ^ bit[sq] ^ bit[sq - 8], p))
        return 0;
    } else {
      idx = idx0 & ~mask[i];
      if (i) idx2 = idx | ((uint64_t)(p[i] - 0x08) << shift[i]);
      else idx2 = idx | piv_idx[p[i] - 0x08];
      if (table[idx2] < WDL_ILLEGAL) return 0;
      if (sq >= 0x30 && !(bit[sq - 0x10] & occ)) {
        if (i) idx2 = idx | ((uint64_t)(p[i] - 0x10) << shift[i]);
        else idx2 = idx | piv_idx[p[i] - 0x10];
        if (table[idx2] < WDL_ILLEGAL) return 0;
      }
    }
  }

  return 1;
}

void calc_broken(struct thread_data *thread)
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
          table_w[idx + i] = table_b[idx + i] = WDL_BROKEN;
        else
          table_w[idx + i] = table_b[idx + i] = 0;
    } else {
      // two or more on one square or a pawn on rank 1 or 8
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = WDL_BROKEN;
    }
  }
}

// TODO: adapt this part
void calc_broken_pp(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ, bb;
  uint64_t end = thread->end;
  int p[MAX_PIECES];

  for (idx = thread->begin; idx < end; idx += 64) {
    bb = 0;
    for (i = pp_num, idx2 = (idx ^ pw_mask) >> pp_shift; i > 0; i--, idx2 >>= 6)
      bb |= bit[p[i] = idx2 & 0x3f];
    bb |= bit[p[0] = piv_sq[idx2]];
    if (bb & pp_mask) {
      for (i = 0; i < 64; i++)
        table_w[idx + i] = table_b[idx + i] = WDL_BROKEN;
      continue;
    }
    occ = 0;
    for (i = n - 2, idx2 = (idx ^ pw_mask) >> 6; i >= numpawns; i--, idx2 >>= 6)
      occ |= bit[p[i] = idx2 & 0x3f];
    for (; i > pp_num; i--, idx2 >>= 6)
      bb |= bit[p[i] = idx2 & 0x3f];
    occ |= bb;
    if (PopCount(occ) == n - 1 && !(bb & PAWN_MASK)) {
      if (n == numpawns) { // FIXME: only possible in suicide?
        for (i = 0; i < 8; i++) {
          table_w[idx + i] = WDL_BROKEN;
          table_b[idx + i] = WDL_BROKEN;
        }
        for (bb = 1ULL << 8; i < 56; i++, bb <<= 1)
          if (occ & bb) {
            table_w[idx + i] = WDL_BROKEN;
            table_b[idx + i] = WDL_BROKEN;
          } else {
            table_w[idx + i] = table_b[idx + i] = 0;
            p[n - 1] = i; 
            if (is_attacked(p[black_king], white_all, occ | bb, p))
              table_w[idx + i] = WDL_ILLEGAL;
            if (is_attacked(p[white_king], black_all, occ | bb, p))
              table_b[idx + i] = WDL_ILLEGAL;
          }
        for (; i < 64; i++) {
          table_w[idx + i] = WDL_BROKEN;
          table_b[idx + i] = WDL_BROKEN;
        }
      } else {
        for (i = 0, bb = 1ULL; i < 64; i++, bb <<= 1)
          if (occ & bb) {
            table_w[idx + i] = WDL_BROKEN;
            table_b[idx + i] = WDL_BROKEN;
          } else {
            table_w[idx + i] = table_b[idx + i] = 0;
            p[n - 1] = i;
            if (is_attacked(p[black_king], white_all, occ | bb, p))
              table_w[idx + i] = WDL_ILLEGAL;
            if (is_attacked(p[white_king], black_all, occ | bb, p))
              table_b[idx + i] = WDL_ILLEGAL;
          }
      }
    } else {
      // two or more on one square or a pawn on rank 1 or 8
      for (i = 0; i < 64; i++) {
        table_w[idx + i] = WDL_BROKEN;
        table_b[idx + i] = WDL_BROKEN;
      }
    }
  }
}

void calc_mates(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i;
  int n = numpcs;
  bitboard occ;
  int *p = thread->p;
  uint64_t end = thread->end;

  for (idx = thread->begin; idx < end; idx++) {
    if (table_w[idx] == WDL_BROKEN) continue;
    int chk_b = (table_w[idx] == WDL_ILLEGAL);
    int chk_w = (table_b[idx] == WDL_ILLEGAL);
    if (chk_w == chk_b) continue;
    FILL_OCC;
    if (chk_w) {
      if (!table_w[idx] && check_mate_pieces(white_pcs, idx, table_b, occ, p)
                        && check_mate_pawns_w(idx, table_b, occ, p))
        table_w[idx] = CAPT_MATE;
    } else {
      if (!table_b[idx] && check_mate_pieces(black_pcs, idx, table_w, occ, p)
                        && check_mate_pawns_b(idx, table_w, occ, p))
        table_b[idx] = CAPT_MATE;
    }
  }
}

MARK(mark_illegal)
{
  MARK_BEGIN;
  table[idx2] = WDL_ILLEGAL;
  MARK_END;
}

MARK(mark_capt_wins)
{
  MARK_BEGIN;
  if (table[idx2] < WDL_ILLEGAL)
    table[idx2] = CAPT_WIN;
  MARK_END;
}

MARK(mark_capt_value, uint8_t v)
{
  MARK_BEGIN;
  SET_CAPT_VALUE(table[idx2], v);
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
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_WHITE_PIECES(mark_illegal);
    }
  }
}

void calc_illegal_b(struct thread_data *thread)
{
  BEGIN_CAPTS_NOPROBE;

  LOOP_CAPTS {
    FILL_OCC_CAPTS {
      MAKE_IDX2;
      LOOP_BLACK_PIECES(mark_illegal);
    }
  }
}

void probe_captures_w(struct thread_data *thread)
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
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          mark_capt_value(k, table_w, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_capt_losses(k, table_w, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }
}

void probe_captures_b(struct thread_data *thread)
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
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          mark_capt_value(k, table_b, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_capt_losses(k, table_b, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }
}

void probe_pivot_captures(struct thread_data *thread)
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
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_CWIN);
          break;
        case 0:
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_DRAW);
          break;
        case 1:
          mark_capt_value(k, table, idx3 & ~mask[k], occ, p, CAPT_CLOSS);
          break;
        case 2:
          mark_capt_losses(k, table, idx3 & ~mask[k], occ, p);
          break;
        }
      }
    }
  }
}

static int capturing_pawn;

void calc_pawn_illegal_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int k = capturing_pawn;
  int l = black_king;
  int m = white_king;

  LOOP_ITER_ALL {
    if (table_w[idx]) continue;
    FILL_OCC;
    if (white_pawn_range[p[k]] & bit[p[l]] & ~king_range[p[m]])
      table_w[idx] = WDL_ILLEGAL;
  }
}

void calc_pawn_illegal_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  int k = capturing_pawn;
  int l = white_king;
  int m = black_king;

  LOOP_ITER_ALL {
    if (table_b[idx]) continue;
    FILL_OCC;
    if (black_pawn_range[p[k]] & bit[p[l]] & ~king_range[p[m]])
      table_b[idx] = WDL_ILLEGAL;
  }
}

void calc_captures_w(void)
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

void calc_captures_b(void)
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

int probe_pawn_capt(int k, int sq, uint64_t idx, int king, int opp_king, int wtm, bitboard occ, bitboard pawns, int *p)
{
  int i;
  int best = -CAPT_DRAW;
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

void calc_pawn_captures_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;

  LOOP_ITER_ALL {
    if (table_w[idx] >= CAPT_WIN) continue;
    int w = table_w[idx];
    FILL_OCC_PAWNS_PIECES;
    int v = -CAPT_DRAW;
    for (i = 0; i < numpawns; i++) {
      if (!pw[i]) continue; // loop through white pawns
      int val = probe_pawn_capt(i, p[i] + 0x08, idx & ~mask[i], white_king, black_king, 0, occ & ~bit[p[i]], pawns, p);
      if (val > v) v = val;
    }
    v += CAPT_DRAW;
    if (v > w) table_w[idx] = v;
  }
}

void calc_pawn_captures_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  bitboard pawns;

  LOOP_ITER_ALL {
    if (table_b[idx] >= CAPT_WIN) continue;
    int w = table_b[idx];
    FILL_OCC_PAWNS_PIECES;
    int v = -CAPT_DRAW;
    for (i = 0; i < numpawns; i++) {
      if (pw[i]) continue; // loop through black pawns
      int val = probe_pawn_capt(i, p[i] - 0x08, idx & ~mask[i], black_king, white_king, 1, occ & ~bit[p[i]], pawns, p);
      if (val > v) v = val;
    }
    v += CAPT_DRAW;
    if (v > w) table_b[idx] = v;
  }
}

#if 1
void load_wdl(struct thread_data *thread)
{
  uint64_t idx, idx2, idx_p, idx2_p;
  int i, v1, v2, v1_p;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1_p = table[idx];
    if (v1_p < WDL_ILLEGAL) break;
  }
  if (idx == end) return;
  for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
    pos[perm[i]] = idx2 & 0x3f;
  pos[perm[0]] = piv_sq[idx2];
  idx2_p = encode_pawn_ver(entry, norm, pos, factor);
  __builtin_prefetch(&src[idx2_p], 0, 3);
  idx_p = idx;

  for (idx++; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) continue;
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    __builtin_prefetch(&src[idx2], 0, 3);
    v2 = src[idx2_p];
    table[idx_p] = wdl_matrix[v2][v1_p];
//if(table[idx_p]==WDL_ERROR)
//printf("WDL_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx, v2, v1);
    v1_p = v1;
    idx_p = idx;
    idx2_p = idx2;
  }

  v2 = src[idx2_p];
  table[idx_p] = wdl_matrix[v2][v1_p];
}
#else
void load_wdl(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) continue;
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = src[idx2];
    table[idx] = wdl_matrix[v2][v1];
//if(table[idx]==WDL_ERROR)
//printf("WDL_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx, v2, v1);
  }
}
#endif

void load_dtz(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = src[idx2];
    table[idx] = wdl_to_dtz[v1][v2];
if(table[idx]==DTZ_ERROR)
error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, v1, v2, idx2);
  }
}

void load_dtz_mapped(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;
  uint8_t (*map)[256] = load_map;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ERROR) {
      table[idx] = wdl_to_dtz_c[v1 - WDL_ERROR];
      continue;
    }
    int wdl = wdl_tbl_to_wdl[v1];
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = map[wdl][src[idx2]];
    table[idx] = wdl_to_dtz[v1][v2];
if(table[idx]==DTZ_ERROR)
error("DTZ_ERROR: idx = %"PRIu64", v1 = %d, v2 = %d, idx2 = %"PRIu64"\n", idx, v1, v2, idx2);
  }
}

static int has_moves_pieces(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ, int *p)
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
      if (table[idx2] != WDL_ILLEGAL) return 1;
      ClearFirst(bb);
    }
  } while (*(++pcs) >= 0);

  return 0;
}

int has_moves_pawns_w(uint64_t idx0, bitboard occ, int *p)
{
  int i;
  int sq;
  uint64_t idx, idx2;
  uint8_t *table = table_b;

  for (i = 0; i < numpawns; i++) {
    if (!pw[i]) continue;
    sq = p[i];
    if (bit[sq + 8] & occ) continue;
    if (sq >= 0x30) {
      if (!is_attacked(p[white_king], black_all, occ ^ bit[sq] ^ bit[sq + 8], p))
        return 1;
    } else {
      idx = idx0 & ~mask[i];
      if (i) idx2 = idx | ((uint64_t)((p[i] + 0x08) ^ 0x38) << shift[i]);
      else idx2 = idx | piv_idx[p[i] + 0x08];
      if (table[idx2] != WDL_ILLEGAL) return 1;
      if (sq < 0x10 && !(bit[sq + 0x10] & occ)) {
        if (i) idx2 = idx | ((uint64_t)((p[i] + 0x10) ^ 0x38) << shift[i]);
        else idx2 = idx | piv_idx[p[i] + 0x10];
        if (table[idx2] < WDL_ILLEGAL) return 1;
      }
    }
  }

  return 0;
}

int has_moves_pawns_b(uint64_t idx0, bitboard occ, int *p)
{
  int i;
  int sq;
  uint64_t idx, idx2;
  uint8_t *table = table_w;

  for (i = 0; i < numpawns; i++) {
    if (pw[i]) continue;
    sq = p[i];
    if (bit[sq - 8] & occ) continue;
    if (sq < 0x10) {
      if (!is_attacked(p[black_king], white_all, occ ^ bit[sq] ^ bit[sq - 8], p))
        return 1;
    } else {
      idx = idx0 & ~mask[i];
      if (i) idx2 = idx | ((uint64_t)(p[i] - 0x08) << shift[i]);
      else idx2 = idx | piv_idx[p[i] - 0x08];
      if (table[idx2] != WDL_ILLEGAL) return 1;
      if (sq >= 0x30 && !(bit[sq - 0x10] & occ)) {
        if (i) idx2 = idx | ((uint64_t)(p[i] - 0x10) << shift[i]);
        else idx2 = idx | piv_idx[p[i] - 0x10];
        if (table[idx2] != WDL_ILLEGAL) return 1;
      }
    }
  }

  return 0;
}

static int compute_pieces(int *pcs, uint64_t idx0, uint8_t *table, bitboard occ, int *p)
{
  int sq;
  uint64_t idx, idx2;
  bitboard bb;
  int best = DTZ_ILLEGAL; // equal to W_ILLEGAL

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

static int eval_ep(int k, int l, int sq, int ep, int king, int opp_king, int wtm, bitboard occ, bitboard pawns, int *p)
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

// FIXME: WDL_ILLEGAL, so does not work for W_ILLEGAL
void calc_pawn_moves_w(struct thread_data *thread)
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

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (pawn_skip[table_w[idx]]) continue;
    FILL_OCC_PIECES;
    best = -3;
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
        if (wdl_to_pawn[table_b[idx2]] > best) best = wdl_to_pawn[table_b[idx2]];
        if (sq < 0x10 && !(bit[sq + 0x10] & occ)) {
          if (i) idx2 = idx0 | ((uint64_t)((p[i] + 0x10) ^ 0x38) << shift[i]);
          else idx2 = idx0 | piv_idx[p[i] + 0x10];
          if (table_b[idx2] == WDL_ILLEGAL) continue;
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
          int v = wdl_to_pawn[table_b[idx2]];
          if (v1 < 3) {
            if (v1 < v || (table_b[idx2] == WDL_DRAW &&
              !has_moves_pieces(black_pcs, idx2, table_w,
                                occ ^ bit[p[i]] ^ bit[p[i] + 0x10], p) &&
              !has_moves_pawns_b(idx2, occ ^ bit[p[i]] ^ bit[p[i] + 0x10], p)))
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
    table_w[idx] = wdl_pawn[best + 3][table_w[idx]];
//    if (table_w[idx] == WDL_ERROR)
//      error("calc_pawn_moves_w: idx = %"PRIu64", best = %d\n", idx, best);
  }
}

void calc_pawn_moves_b(struct thread_data *thread)
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

  for (idx = begin + thread->begin; idx < end; idx++) {
    if (pawn_skip[table_b[idx]]) continue;
    FILL_OCC_PIECES;
    best = -3;
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
        if (wdl_to_pawn[table_w[idx2]] > best) best = wdl_to_pawn[table_w[idx2]];
        if (sq >= 0x30 && !(bit[sq - 0x10] & occ)) {
          if (i) idx2 = idx0 | ((uint64_t)(p[i] - 0x10) << shift[i]);
          else idx2 = idx0 | piv_idx[p[i] - 0x10];
          if (table_w[idx2] == WDL_ILLEGAL) continue;
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
          int v = wdl_to_pawn[table_w[idx2]];
          if (v1 < 3) {
            if (v1 < v || (table_w[idx2] == WDL_DRAW &&
              !has_moves_pieces(white_pcs, idx2, table_b,
                                occ ^ bit[p[i]] ^ bit[p[i] - 0x10], p) &&
              !has_moves_pawns_w(idx2, occ ^ bit[p[i]] ^ bit[p[i] - 0x10], p)))
              v = v1;
          }
          if (v > best) best = v;
        }
      }
    }
lab:
//    if (wdl_pawn[best + 3][table_b[idx]] == WDL_ERROR)
//      error("calc_pawn_moves_b: idx = %"PRIu64", best = %d, table_b[idx] = %d\n", idx, best, table_b[idx]);
    table_b[idx] = wdl_pawn[best + 3][table_b[idx]];
  }
}

void verify_opp(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *opp_table = load_opp_table;
  uint8_t *dtz_table = load_table;
  int *opp_pieces = load_opp_pieces;

  LOOP_ITER_ALL {
    int v = opp_table[idx];
    if (v >= WDL_ILLEGAL) {
      opp_table[idx] = wdl_to_dtz_c[v - WDL_ERROR];
      if (v == WDL_ERROR)
        error("ERROR: opp table, idx = %"PRIu64", v = WDL_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute_pieces(opp_pieces, idx, dtz_table, occ, p);
    int z = dtz_to_opp[v][w];
    opp_table[idx] = z;
    if (z == DTZ_ERROR)
      error("ERROR: opp table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_dtz(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *dtz_table = load_table;
  uint8_t *opp_table = load_opp_table;
  int *dtz_pieces = load_pieces;

  LOOP_ITER_ALL {
    int v = dtz_table[idx];
    if (v == DTZ_ILLEGAL || v >= DTZ_ERROR) {
      if (v == DTZ_ERROR)
        error("ERROR: dtz table, idx = %"PRIu64", v = DTZ_ERROR\n", idx);
      continue;
    }
    FILL_OCC;
    int w = compute_pieces(dtz_pieces, idx, opp_table, occ, p);
    if (!dtz_matrix[v][w])
      error("ERROR: dtz table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_wdl_w(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *table = load_table;
  uint8_t *opp_table = load_opp_table;
  int *pieces = load_pieces;

  LOOP_ITER_ALL {
    int v = table[idx];
    if (w_skip[v]) continue;
    FILL_OCC;
    int w = compute_pieces(pieces, idx, opp_table, occ, p);
    if (!w_matrix[v][w])
      error("ERROR: wdl table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void verify_wdl_b(struct thread_data *thread)
{
  BEGIN_ITER_ALL;
  uint8_t *table = load_table;
  uint8_t *opp_table = load_opp_table;
  int *pieces = load_pieces;

  LOOP_ITER_ALL {
    int v = table[idx];
    if (w_skip[v]) continue;
    FILL_OCC;
    int w = compute_pieces(pieces, idx, opp_table, occ, p);
    if (!w_matrix[v][w])
      error("ERROR: wdl table, idx = %"PRIu64", v = %d, w = %d\n", idx, v, w);
  }
}

void wdl_load_wdl(struct thread_data *thread)
{
  uint64_t idx, idx2;
  int i, v1, v2;
  int n = numpcs;
  uint8_t *table = load_table;
  uint8_t *src = tb_table;
  int *perm = tb_perm;
  uint64_t end = thread->end;
  int pos[MAX_PIECES];
  uint8_t *norm = load_entry->file[file].norm[load_bside];
  uint64_t *factor = load_entry->file[file].factor[load_bside];
  struct TBEntry_pawn *entry = load_entry;

  for (idx = thread->begin; idx < end; idx++) {
    v1 = table[idx];
    if (v1 >= WDL_ILLEGAL) {
      table[idx] = w_ilbrok[v1 - WDL_ILLEGAL];
      continue;
    }
    for (i = n - 1, idx2 = idx ^ pw_mask; i > 0; i--, idx2 >>= 6)
      pos[perm[i]] = idx2 & 0x3f;
    pos[perm[0]] = piv_sq[idx2];
    idx2 = encode_pawn_ver(entry, norm, pos, factor);
    v2 = src[idx2];
    table[idx] = w_wdl_matrix[v2][v1];
if(table[idx]==W_ERROR)
error("W_ERROR: idx = %"PRIu64", v2 = %d, v1 = %d\n", idx, v2, v1);
  }
}

