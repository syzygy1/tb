/*
  Copyright (c) 2011-2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

#include "compress.h"
#include "defs.h"
#include "permute.h"
#include "threads.h"
#include "util.h"

#include "board.h"

#ifndef SUICIDE
static int white_king, black_king;
#endif

#include "probe.c"
#include "board.c"

#define MAX_PIECES 8

//#define MAX_NARROW 50
#define MAX_NARROW 240

static void reduce_tables(void);

uint64_t total_stats_w[MAX_STATS];
uint64_t total_stats_b[MAX_STATS];

static void transform_table_u8(struct thread_data *thread);
static void transform_table_u16(struct thread_data *thread);

static uint64_t *work_g;
#if !defined(SMALL) && !defined(SMALL2)
static uint64_t *work_piv;
#else
static uint64_t *work_piv0, *work_piv1;
#endif

uint8_t *restrict table_w, *restrict table_b;
int numpcs;
int numpawns = 0;
int symmetric, split;
static int captured_piece;

static uint64_t size;

static int white_pcs[MAX_PIECES], black_pcs[MAX_PIECES];
static int pt[MAX_PIECES];
#ifndef SUICIDE
static int pcs2[MAX_PIECES];
#endif

static int ply;
static int finished;
static int ply_accurate_w, ply_accurate_b;

static int num_saves;
static int cursed_capt[MAX_PIECES];
static int has_cursed_capts;
#ifndef SUICIDE
static int to_fix_w, to_fix_b;
#endif

#ifdef SUICIDE
int threat_dc, wdl_threat_win, wdl_threat_cwin, wdl_threat_draw;
#endif

u8 *transform_v_u8;
u8 *transform_tbl_u8;

u16 *transform_v_u16;
u16 *transform_tbl_u16;

#if defined(SMALL)
#include "generics.c"
#elif defined(SMALL2)
#include "generics2.c"
#else
#include "generic.c"
#endif

#if defined(REGULAR)
#include "rtbgen.c"
#elif defined(SUICIDE)
#include "stbgen.c"
#elif defined(ATOMIC)
#include "atbgen.c"
#elif defined(LOSER)
#include "ltbgen.c"
#endif

#define HUGEPAGESIZE 2*1024*1024

static int minfreq = 8;
static int only_generate = 0;
static int generate_dtz = 1;
static int generate_wdl = 1;

char *tablename;

#include "stats.c"

void transform(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;
  uint8_t *v = transform_v_u8;

  for (idx = thread->begin; idx < end; idx++) {
    table_w[idx] = v[table_w[idx]];
    table_b[idx] = v[table_b[idx]];
  }
}

void transform_table_u8(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;
  uint8_t *v = transform_v_u8;
  uint8_t *table = transform_tbl_u8;

  for (idx = thread->begin; idx < end; idx++)
    table[idx] = v[table[idx]];
}

void transform_table_u16(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;
  uint16_t *v = transform_v_u16;
  uint16_t *table = transform_tbl_u16;

  for (idx = thread->begin; idx < end; idx++)
    table[idx] = v[table[idx]];
}

#include "reduce.c"

#ifndef SUICIDE
static LOCK_T tc_mutex;
static uint8_t *tc_table;
static uint8_t *tc_v;
static int tc_capt_closs, tc_closs;

static void tc_loop(struct thread_data *thread)
{
  int i;
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  uint8_t *restrict table = tc_table;
  uint8_t *restrict v = tc_v;

  for (; idx < end; idx++)
    if (v[table[idx]]) {
      LOCK(tc_mutex);
      switch (v[table[idx]]) {
      case 1:
        tc_capt_closs = 1;
        v[CAPT_CLOSS] = 0;
        break;
      case 2:
        tc_closs = 1;
        if (num_saves == 0)
          for (i = DRAW_RULE; i < REDUCE_PLY; i++)
            v[LOSS_IN_ONE - i] = 0;
        else
          for (i = LOSS_IN_ONE; i >= LOSS_IN_ONE - REDUCE_PLY_RED - 1; i--)
            v[i] = 0;
        break;
      }
      if (tc_capt_closs && tc_closs)
        idx = end;
      UNLOCK(tc_mutex);
    }
}

void test_closs(uint64_t *stats, uint8_t *table, int to_fix)
{
  int i;
  uint8_t v[256];

  tc_capt_closs = tc_closs = 0;
  if (to_fix) {
    tc_table = table;
    tc_v = v;
    for (i = 0; i < 256; i++)
      v[i] = 0;
    v[CAPT_CLOSS] = 1;
    if (num_saves == 0)
      for (i = DRAW_RULE; i < REDUCE_PLY; i++)
        v[LOSS_IN_ONE - i] = 2;
    else
      for (i = LOSS_IN_ONE; i >= LOSS_IN_ONE - REDUCE_PLY_RED - 1; i--)
        v[i] = 2;
    run_threaded(tc_loop, work_g, 0);
  } else {
    for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
      if (stats[STAT_MATE - i]) break;
    if (i <= MAX_PLY)
      tc_closs = 1;
  }
}
#endif

void prepare_wdl_map(uint64_t *stats, uint8_t *v, int pa_w, int pa_l)
{
  int i, j;
  int vals[5];
  int dc[4];

  for (i = 0; i < 5; i++)
    vals[i] = 0;

#ifndef SUICIDE
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[i]) break;
  if (i <= DRAW_RULE)
    vals[4] = 1;
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[i]) break;
  if (i <= MAX_PLY)
    vals[3] = 1;
  if (stats[STAT_DRAW])
    vals[2] = 1;
  if (tc_closs)
    vals[1] = 1;
  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i]) break;
  if (i <= DRAW_RULE)
    vals[0] = 1;
#else
  for (i = MIN_PLY_WIN; i <= DRAW_RULE; i++)
    if (stats[i]) break;
  vals[4] = (i <= DRAW_RULE) || (!threat_dc && stats[2]);
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[i]) break;
  vals[3] = (i <= MAX_PLY) || (!threat_dc && (stats[STAT_THREAT_CWIN1] + stats[STAT_THREAT_CWIN2] != 0));
  vals[2] = (stats[STAT_DRAW] != 0) || (!threat_dc && stats[STAT_THREAT_DRAW] != 0);
  // FIXME: probably should scan the table for non-CAPT_CLOSS cursed losses
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    if (stats[STAT_MATE - i]) break;
  vals[1] = (i <= MAX_PLY);
  for (i = MIN_PLY_LOSS; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i]) break;
  vals[0] = (i <= DRAW_RULE);
#endif

  for (i = 0; i < 4; i++)
    dc[i] = 0;
#ifndef SUICIDE
  dc[3] = 1;
  dc[2] = (stats[STAT_CAPT_CWIN] != 0); // CAPT_CWIN
  dc[1] = (stats[STAT_CAPT_DRAW] != 0); // CAPT_DRAW
  dc[0] = tc_capt_closs;
#else
  dc[3] = 1;
  dc[2] = threat_dc && (stats[STAT_THREAT_CWIN1] + stats[STAT_THREAT_CWIN2] != 0); // THREAT_CWIN
  dc[1] = threat_dc && (stats[STAT_THREAT_DRAW] != 0); // THREAT_DRAW
//  dc[0] = (stats[515] != 0); // THREAT_CLOSS
  dc[0] = 0;
#endif

  for (i = 0; i < 4; i++)
    if (dc[i]) break;
  for (j = 0; j < 5; j++)
    if (vals[j]) break;
  if (j > i + 1)
    vals[0] = 1;

#ifndef SUICIDE
  v[ILLEGAL] = 8;
  v[BROKEN] = 8;
  v[CAPT_WIN] = 8;
  v[CAPT_DRAW] = 6;
  v[CAPT_CLOSS] = 5;
  v[UNKNOWN] = 2;
  if (num_saves == 0) {
    for (i = 0; i < DRAW_RULE; i++)
      v[WIN_IN_ONE + i] = 4;
    v[CAPT_CWIN] = 7;
    for (i = DRAW_RULE; i <= REDUCE_PLY; i++)
      v[WIN_IN_ONE + i + 1] = 3;
    for (i = DRAW_RULE; i < REDUCE_PLY; i++)
      v[LOSS_IN_ONE - i] = 1;
    for (i = 0; i <= DRAW_RULE; i++)
      v[MATE - i] = 0;
  } else {
    v[WIN_IN_ONE] = 4;
    v[CAPT_CWIN_RED] = 7;
    for (i = 0; i <= REDUCE_PLY_RED + 2; i++)
      v[CAPT_CWIN_RED + i + 1] = 3;
    for (i = 0; i <= REDUCE_PLY_RED + 1; i++)
      v[LOSS_IN_ONE - i] = 1;
    v[MATE] = 0;
  }
#else
// FIXME: CAPT_CLOSS
  v[BROKEN] = 8;
  v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = 8;
  v[CAPT_CLOSS] = v[CAPT_LOSS] = 8;
  v[THREAT_WIN] = wdl_threat_win;
  v[THREAT_DRAW] = wdl_threat_draw;
  v[UNKNOWN] = 2;
  if (num_saves == 0) {
    for (i = 3; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = 4;
    v[THREAT_CWIN1] = v[THREAT_CWIN2] = wdl_threat_cwin;
    v[BASE_WIN + DRAW_RULE + 1] = 3;
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY + 1; i++)
      v[BASE_WIN + i + 2] = 3;
    for (i = DRAW_RULE + 1; i <= REDUCE_PLY; i++)
      v[BASE_LOSS - i] = 1;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = 0;
  } else {
    v[BASE_WIN + 3] = 4;
    v[BASE_WIN + 4] = wdl_threat_cwin;
    v[BASE_WIN + 5] = 3;
    v[BASE_LOSS - 2] = 0;
    v[BASE_LOSS - 3] = 1;
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[BASE_WIN + i + 6] = 3;
      v[BASE_LOSS - i - 4] = 1;
    }
    v[BASE_WIN + REDUCE_PLY_RED + 6] = 3;
    v[BASE_WIN + REDUCE_PLY_RED + 7] = 3;
    v[BASE_LOSS - REDUCE_PLY_RED - 4] = 1;
  }
#endif

  compress_init_wdl(vals, (pa_w << 2) | (pa_l << 3));
}

struct dtz_map map_w, map_b;

int sort_list(uint64_t *freq, uint16_t *map, uint16_t *inv_map)
{
  int i, j;
  int num;

  for (i = 0; i < MAX_VALS; i++)
    map[i] = inv_map[i] = 0;

  num = 0;
  for (i = 0; i < MAX_VALS; i++)
    if (freq[i])
      map[num++] = i;

  for (i = 0; i < num; i++)
    for (j = i + 1; j < num; j++)
      if (freq[map[i]] < freq[map[j]]) {
        uint16_t tmp = map[i];
        map[i] = map[j];
        map[j] = tmp;
      }
  for (i = 0; i < num; i++)
    inv_map[map[i]] = i;

  return num;
}

void sort_values(uint64_t *stats, struct dtz_map *dtzmap, int side, int pa_w,
    int pa_l)
{
  int i, j;
  uint64_t freq[4][MAX_VALS];
  uint16_t (*map)[MAX_VALS] = dtzmap->map;
  uint16_t (*inv_map)[MAX_VALS] = dtzmap->inv_map;

  dtzmap->side = side;
  dtzmap->ply_accurate_win = pa_w;
  dtzmap->ply_accurate_loss = pa_l;

  for (j = 0; j < 4; j++)
    for (i = 0; i < MAX_VALS; i++)
      freq[j][i] = 0;

  freq[0][0] = stats[0];
#ifndef SUICIDE
  if (dtzmap->ply_accurate_win)
    for (i = 0; i < DRAW_RULE; i++)
      freq[0][i] += stats[i + 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      freq[0][i / 2] += stats[i + 1];
#else
  if (dtzmap->ply_accurate_win)
    for (i = 2; i < DRAW_RULE; i++)
      freq[0][i] += stats[i + 1];
  else
    for (i = 2; i < DRAW_RULE; i++)
      freq[0][i / 2] += stats[i + 1];
#endif
  dtzmap->num[0] = sort_list(freq[0], map[0], inv_map[0]);

  freq[1][0] = stats[STAT_MATE];
#ifndef SUICIDE
  if (dtzmap->ply_accurate_loss)
    for (i = 0; i < DRAW_RULE; i++)
      freq[1][i] += stats[STAT_MATE - i - 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      freq[1][i / 2] += stats[STAT_MATE - i - 1];
#else
  if (dtzmap->ply_accurate_loss)
    for (i = 1; i < DRAW_RULE; i++)
      freq[1][i] += stats[STAT_MATE - i - 1];
  else
    for (i = 1; i < DRAW_RULE; i++)
      freq[1][i / 2] += stats[STAT_MATE - i - 1];
#endif
  dtzmap->num[1] = sort_list(freq[1], map[1], inv_map[1]);

  for (i = DRAW_RULE; i < MAX_PLY; i++)
    freq[2][(i - DRAW_RULE) / 2] += stats[i + 1];
  dtzmap->num[2] = sort_list(freq[2], map[2], inv_map[2]);

  for (i = DRAW_RULE; i < MAX_PLY; i++)
    freq[3][(i - DRAW_RULE) / 2] += stats[STAT_MATE - i - 1];
  dtzmap->num[3] = sort_list(freq[3], map[3], inv_map[3]);

  int num = 1;
  for (i = 0; i < 4; i++)
    if (dtzmap->num[i] > num)
      num = dtzmap->num[i];
  dtzmap->max_num = num;

  static uint64_t tot_pos[] = {
    518,
    31332,
    1911252,
    114675120,
    6765832080,
    392418260640
  };
  uint64_t tot = tot_pos[numpcs - 2] / 3500ULL;

  for (i = 0; i < num; i++) {
    uint64_t f = 0;
    for (j = 0; j < 4; j++)
      if (i < dtzmap->num[j])
        f += freq[j][map[j][i]];
    if (f < tot) break;
  }
  dtzmap->high_freq_max = i;

  dtzmap->wide = 0;
  if (num >= 256)
    dtzmap->wide = 1;
  else {
    for (j = 0; j < 4; j++)
      for (i = 0; i < dtzmap->num[j]; i++)
        if (dtzmap->map[j][i] >= 256)
          dtzmap->wide = 1;
  }
}

void prepare_dtz_map_u8(u8 *v, struct dtz_map *map)
{
  int i;
  uint16_t (*inv_map)[MAX_VALS] = map->inv_map;
  int num = map->max_num;

  if (num_saves == 0) {
    for (i = 0; i < 256; i++)
      v[i] = 0;

#ifndef SUICIDE
    v[ILLEGAL] = num;
    v[UNKNOWN] = v[BROKEN] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;

    v[MATE] = inv_map[1][0];
    if (map->ply_accurate_win)
      for (i = 0; i < DRAW_RULE; i++)
        v[WIN_IN_ONE + i] = inv_map[0][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
        v[WIN_IN_ONE + i] = inv_map[0][i / 2];
    if (map->ply_accurate_loss)
      for (i = 0; i < DRAW_RULE; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][i / 2];
    for (; i <= REDUCE_PLY; i++) {
      v[WIN_IN_ONE + i + 1] = inv_map[2][(i - DRAW_RULE) / 2];
      v[LOSS_IN_ONE - i] = inv_map[3][(i - DRAW_RULE) / 2];
    }
#else
    v[BROKEN] = v[UNKNOWN] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
    v[CAPT_CLOSS] = v[CAPT_LOSS] = num;
    v[THREAT_WIN] = num;
    v[THREAT_CWIN1] = v[THREAT_CWIN2] = num;
    v[THREAT_DRAW] = num;
    if (map->ply_accurate_win)
      for (i = 3; i <= DRAW_RULE; i++)
        v[BASE_WIN + i] = inv_map[0][i - 1];
    else
      for (i = 3; i <= DRAW_RULE; i++)
        v[BASE_WIN + i] = inv_map[0][(i - 1) / 2];
    if (map->ply_accurate_loss)
      for (i = 2; i <= DRAW_RULE; i++)
        v[BASE_LOSS - i] = inv_map[1][i - 1];
    else
      for (i = 2; i <= DRAW_RULE; i++)
        v[BASE_LOSS - i] = inv_map[1][(i - 1) / 2];
    v[BASE_WIN + DRAW_RULE + 1] = inv_map[2][0];
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY; i++)
      v[BASE_WIN + i + 2] = inv_map[2][(i - DRAW_RULE - 1) / 2];
    for (i = DRAW_RULE + 1; i <= REDUCE_PLY; i++)
      v[BASE_LOSS - i] = inv_map[3][(i - DRAW_RULE - 1) / 2];
#endif
  } else {
    for (i = 0; i < 256; i++)
      v[i] = i;
  }

  compress_init_dtz_u8(map);
}

void prepare_dtz_map_u16(u16 *v, struct dtz_map *map)
{
  int i;
  uint16_t (*inv_map)[MAX_VALS] = map->inv_map;
  int num = map->max_num;

  if (num_saves == 0) {
    for (i = 0; i < MAX_VALS; i++)
      v[i] = 0;

#ifndef SUICIDE
    v[ILLEGAL] = num;
    v[UNKNOWN] = v[BROKEN] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;

    v[MATE] = inv_map[1][0];
    if (map->ply_accurate_win)
      for (i = 0; i < DRAW_RULE; i++)
        v[WIN_IN_ONE + i] = inv_map[0][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
        v[WIN_IN_ONE + i] = inv_map[0][i / 2];
    if (map->ply_accurate_loss)
      for (i = 0; i < DRAW_RULE; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][i];
    else
      for (i = 0; i < DRAW_RULE; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][i / 2];
    for (; i <= REDUCE_PLY; i++) {
      v[WIN_IN_ONE + i + 1] = inv_map[2][(i - DRAW_RULE) / 2];
      v[LOSS_IN_ONE - i] = inv_map[3][(i - DRAW_RULE) / 2];
    }
#else
    v[BROKEN] = v[UNKNOWN] = num;
    v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
    v[CAPT_CLOSS] = v[CAPT_LOSS] = num;
    v[THREAT_WIN] = num;
    v[THREAT_CWIN1] = v[THREAT_CWIN2] = num;
    v[THREAT_DRAW] = num;
    if (map->ply_accurate_win)
      for (i = 3; i <= DRAW_RULE; i++)
        v[BASE_WIN + i] = inv_map[0][i - 1];
    else
      for (i = 3; i <= DRAW_RULE; i++)
        v[BASE_WIN + i] = inv_map[0][(i - 1) / 2];
    if (map->ply_accurate_loss)
      for (i = 2; i <= DRAW_RULE; i++)
        v[BASE_LOSS - i] = inv_map[1][i - 1];
    else
      for (i = 2; i <= DRAW_RULE; i++)
        v[BASE_LOSS - i] = inv_map[1][(i - 1) / 2];
    v[BASE_WIN + DRAW_RULE + 1] = inv_map[2][0];
    for (i = DRAW_RULE + 2; i <= REDUCE_PLY; i++)
      v[BASE_WIN + i + 2] = inv_map[2][(i - DRAW_RULE - 1) / 2];
    for (i = DRAW_RULE + 1; i <= REDUCE_PLY; i++)
      v[BASE_LOSS - i] = inv_map[3][(i - DRAW_RULE - 1) / 2];
#endif
  } else {
    for (i = 0; i < MAX_VALS; i++)
      v[i] = i;
  }

  compress_init_dtz_u16(map);
}

extern char *optarg;

static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "wdl", 0, NULL, 'w' },
  { "dtz", 0, NULL, 'z' },
  { "stats", 0, NULL, 's' },
  { "disk", 0, NULL, 'd' },
  { "affinity", 0, NULL, 'a' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int i, j;
  int color;
  int val, longindex;
  int pcs[16];
  int save_stats = 0;
  int save_to_disk = 0;
  int switched = 0;

  numthreads = 1;
  do {
    val = getopt_long(argc, argv, "at:gwzsd", options, &longindex);
    switch (val) {
    case 'a':
      thread_affinity = 1;
      break;
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'g':
      only_generate = 1;
      generate_dtz = generate_wdl = 0;
      break;
    case 'w':
      generate_dtz = 0;
      break;
    case 'z':
      generate_wdl = 0;
      break;
    case 's':
      save_stats = 1;
      break;
    case 'd':
      save_to_disk = 1;
      break;
    }
  } while (val != EOF);

  if (optind >= argc) {
    fprintf(stderr, "No tablebase specified.\n");
    exit(1);
  }
  tablename = argv[optind];

  init_tablebases();

  for (i = 0; i < 16; i++)
    pcs[i] = 0;

  numpcs = strlen(tablename) - 1;
  color = 0;
  j = 0;
  for (i = 0; i < strlen(tablename); i++)
    switch (tablename[i]) {
    case 'P':
      pcs[PAWN | color]++;
      pt[j++] = PAWN | color;
      break;
    case 'N':
      pcs[KNIGHT | color]++;
      pt[j++] = KNIGHT | color;
      break;
    case 'B':
      pcs[BISHOP | color]++;
      pt[j++] = BISHOP | color;
      break;
    case 'R':
      pcs[ROOK | color]++;
      pt[j++] = ROOK | color;
      break;
    case 'Q':
      pcs[QUEEN | color]++;
      pt[j++] = QUEEN | color;
      break;
    case 'K':
      pcs[KING | color]++;
      pt[j++] = KING | color;
      break;
    case 'v':
      if (color) exit(1);
      color = 0x08;
      break;
    default:
      exit(1);
    }
  if (!color) exit(1);

#ifndef SUICIDE
  if (pcs[WKING] != 1 || pcs[BKING] != 1) {
    fprintf(stderr, "Need one white king and one black king.\n");
    exit(1);
  }

  if (numpcs < 3) {
    fprintf(stderr, "Need at least 3 pieces.\n");
    exit(1);
  }
#else
  if (numpcs < 2) {
    fprintf(stderr, "Need at least 2 pieces.\n");
    exit(1);
  }
#endif

  if (pcs[WPAWN] || pcs[BPAWN]) {
    fprintf(stderr, "Can't handle pawns.\n");
    exit(1);
  }

  if (numthreads < 1) numthreads = 1;

  printf("number of threads = %d\n", numthreads);

  if (numthreads == 1)
    total_work = 1;
  else
    total_work = 100 + 10 * numthreads;

#ifndef SMALL2
  for (i = 0; i < numpcs; i++) {
    shift[i] = (numpcs - i - 1) * 6;
    mask[i] = 0x3fULL << shift[i];
  }
#else

  for (i = 0; i < numpcs; i++) {
  }
#endif

#if defined(SMALL)
  size = 462ULL << (6 * (numpcs-2));
  mask[0] = 0x1ffULL << shift[1];
#elif defined(SMALL2)
  size = 462ULL << (6 * (numpcs-2) - 1);
  mask[0] = 0x1ffULL << shift[1];
#else
  size = 10ULL << (6 * (numpcs-1));
#endif

  work_g = create_work(total_work, size, 0x3f);
#ifndef SMALL
  work_piv = create_work(total_work, 1ULL << shift[0], 0);
#else
  work_piv0 = create_work(total_work, 1ULL << shift[0], 0);
  work_piv1 = create_work(total_work, 10ULL << shift[1], 0);
#endif

  static int piece_order[16] = {
    0, 0, 3, 5, 7, 9, 1, 0,
    0, 0, 4, 6, 8, 10, 2, 0
  };

#ifdef SUICIDE
  j = pt[0];
  for (i = 1; i < numpcs; i++)
    if (piece_order[pt[i]] < piece_order[j])
      j = pt[i];
  if (j & 0x08) {
    for (i = 0; i < numpcs; i++)
      pt[i] ^= 0x08;
    for (i = 0; i < 8; i++) {
      int tmp = pcs[i];
      pcs[i] = pcs[i + 8];
      pcs[i + 8] = tmp;
    }
    switched = 1;
  }

  if (numpcs < 6) {
    threat_dc = 0;
    wdl_threat_win = 4;
    wdl_threat_cwin = 3;
    wdl_threat_draw = 2;
  } else {
    threat_dc = 1;
    wdl_threat_win = 8;
    wdl_threat_cwin = 7;
    wdl_threat_draw = 6;
  }
#endif

  for (i = 0; i < numpcs; i++)
    for (j = i + 1; j < numpcs; j++)
      if (piece_order[pt[i]] > piece_order[pt[j]]) {
        int tmp = pt[i];
        pt[i] = pt[j];
        pt[j] = tmp;
      }

  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08))
      white_pcs[j++] = i;
  white_pcs[j] = -1;

  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] & 0x08)
      black_pcs[j++] = i;
  black_pcs[j] = -1;

  idx_mask1[numpcs - 1] = 0xffffffffffffffc0ULL;
  idx_mask2[numpcs - 1] = 0;
  for (i = numpcs - 2; i >= 0; i--) {
    idx_mask1[i] = idx_mask1[i + 1] << 6;
    idx_mask2[i] = (idx_mask2[i + 1] << 6) | 0x3f;
  }

#ifndef SUICIDE
  for (i = 0; i < numpcs; i++)
    if (pt[i] == WKING)
      white_king = i;

  for (i = 0; i < numpcs; i++)
    if (pt[i] == BKING)
      black_king = i;
#endif

  for (i = 0; i < 8; i++)
    if (pcs[i] != pcs[i + 8]) break;
  symmetric = (i == 8);
  split = !symmetric;

  for (i = 0; i < numpcs; i++)
    cursed_capt[i] = 0;
  has_cursed_capts = 0;

  uint64_t alloc_size = 0ULL;
  if (numpcs == 3)
    alloc_size = 31332 + 1;
  else if (numpcs == 4)
    alloc_size = 31332 * 61 + 1;
  if (alloc_size < size)
    alloc_size = size;

  table_w = alloc_huge(2 * alloc_size);
  table_b = table_w + alloc_size;

  init_threads(0);
  init_tables();

#ifndef SUICIDE
  LOCK_INIT(tc_mutex);
#endif
  LOCK_INIT(stats_mutex);

  gettimeofday(&start_time, NULL);
  cur_time = start_time;

  printf("Initialising broken positions.\n");
  run_threaded(calc_broken, work_g, 1);
  printf("Calculating white captures.\n");
  calc_captures_w();
  printf("Calculating black captures.\n");
  calc_captures_b();
  for (i = 0; i < numpcs; i++)
    if (cursed_capt[i]) {
      has_cursed_capts = 1;
      break;
    }
#ifndef SUICIDE
  printf("Calculating mate positions.\n");
  run_threaded(calc_mates, work_g, 1);
#endif

  iterate();
  collect_stats(1);

  printf("\n########## %s ##########\n\n", tablename);
  if (!switched) {
    print_stats(stdout, total_stats_w, 1);
    print_stats(stdout, total_stats_b, 0);
  } else {
    print_stats(stdout, total_stats_b, 1);
    print_stats(stdout, total_stats_w, 0);
  }
  print_longest(stdout, switched);

  if (save_stats) {
    FILE *F;
    char filename[128];
    char *dirptr = getenv(STATSDIR);
    if (dirptr && strlen(dirptr) < 100)
      strcpy(filename, dirptr);
    else
      strcpy(filename, ".");
    strcat(filename, "/");
    strcat(filename, tablename);
    strcat(filename, ".txt");
    F = fopen(filename, "w");
    if (F) {
      fprintf(F, "########## %s ##########\n\n", tablename);
      if (!switched) {
        print_stats(F, total_stats_w, 1);
        print_stats(F, total_stats_b, 0);
      } else {
        print_stats(F, total_stats_b, 1);
        print_stats(F, total_stats_w, 0);
      }
      print_longest(F, switched);
      fclose(F);
    }
  }

  if (only_generate)
    exit(0);

  ply_accurate_w = 0;
  if (total_stats_w[DRAW_RULE] || total_stats_b[STAT_MATE - DRAW_RULE])
    ply_accurate_w = 1;

  ply_accurate_b = 0;
  if (total_stats_b[DRAW_RULE] || total_stats_w[STAT_MATE - DRAW_RULE])
    ply_accurate_b = 1;

  uint8_t best_w[MAX_PIECES];
  uint8_t best_b[MAX_PIECES];
  int bestp_w, bestp_b;

  sort_values(total_stats_w, &map_w, 0, ply_accurate_w, ply_accurate_b);
  if (!symmetric)
    sort_values(total_stats_b, &map_b, 1, ply_accurate_b, ply_accurate_w);

  int wide =  (map_w.max_num >= MAX_NARROW || map_b.max_num >= MAX_NARROW)
            && generate_dtz;
  compress_alloc_wdl();

  uint64_t tb_size = init_permute_piece(pcs, pt);
  uint8_t *tb_table = NULL;
  if (save_to_disk || symmetric || !generate_wdl)
    tb_table = table_b;
  else
    tb_table = malloc(tb_size + 1);

  if ((save_to_disk && !symmetric && generate_wdl) || wide) {
    store_table(table_w, 'w');
    if (!symmetric)
      store_table(table_b, 'b');
  }

  if (generate_wdl) {

    uint8_t v[256];
#ifndef SUICIDE
    reset_captures_w();
    test_closs(total_stats_w, table_w, to_fix_w);
#endif
    prepare_wdl_map(total_stats_w, v, ply_accurate_w, ply_accurate_b);
    struct tb_handle *G = create_tb(tablename, 1, 6);
    printf("find optimal permutation for wtm/wdl\n");
    permute_piece_wdl(tb_table, pcs, pt, table_w, best_w, v);
    printf("compressing data for wtm/wdl\n");
    compress_tb_u8(G, tb_table, tb_size, best_w, minfreq);

    if (!symmetric) {
      if (save_to_disk) {
        load_table_u8(table_b, 'b');
        tb_table = table_w;
      }
#ifndef SUICIDE
      reset_captures_b();
      test_closs(total_stats_b, table_b, to_fix_b);
#endif
      prepare_wdl_map(total_stats_b, v, ply_accurate_b, ply_accurate_w);
      printf("find optimal permutation for btm/wdl\n");
      permute_piece_wdl(tb_table, pcs, pt, table_b, best_b, v);
      printf("compressing data for btm/wdl\n");
      compress_tb_u8(G, tb_table, tb_size, best_b, minfreq);
    }

    merge_tb(G);

  }

  if (generate_dtz) {

    struct tb_handle *G = create_tb(tablename, 0, 10);
    uint64_t estimate_w, estimate_b;

    if (!wide) {

      compress_alloc_dtz_u8();

      if (!symmetric)
        unlink_table('b');

      if (tb_table == table_w) {
        load_table_u8(table_w, 'w');
        unlink_table('w');
      }
#if defined(REGULAR) || defined(ATOMIC)
      else
        fix_closs_w();

      fix_closs_b();
#endif

      if (num_saves > 0) {
        reconstruct_table_u8(table_w, 'w', &map_w);
        unlink_saves('w');
        if (!symmetric) {
          reconstruct_table_u8(table_b, 'b', &map_b);
          unlink_saves('b');
        }
      }

      uint8_t v[256];

      prepare_dtz_map_u8(v, &map_w);
      printf("find optimal permutation for wtm/dtz\n");
      estimate_w = estimate_piece_dtz_u8(pcs, pt, table_w, best_w, &bestp_w, v);

      if (!symmetric) {
        prepare_dtz_map_u8(v, &map_b);
        printf("find optimal permutation for btm/dtz\n");
        estimate_b = estimate_piece_dtz_u8(pcs, pt, table_b, best_b, &bestp_b, v);
      } else
        estimate_b = UINT64_MAX;

      if (estimate_w <= estimate_b) {
        tb_table = table_b;
        prepare_dtz_map_u8(v, &map_w);
        printf("permute table for wtm/dtz\n");
        permute_piece_dtz_u8(tb_table, pcs, table_w, bestp_w, v);
        printf("compressing data for wtm/dtz\n");
        compress_tb_u8(G, tb_table, tb_size, best_w, minfreq);
      } else {
        tb_table = table_w;
        prepare_dtz_map_u8(v, &map_b);
        printf("permute table for btm/dtz\n");
        permute_piece_dtz_u8(tb_table, pcs, table_b, bestp_b, v);
        printf("compressing data for btm/dtz\n");
        compress_tb_u8(G, tb_table, tb_size, best_b, minfreq);
      }

    } else {

      compress_alloc_dtz_u16();

      uint16_t v[MAX_VALS];
      uint16_t *table_u16 = (uint16_t *)table_w;
      uint16_t *tb_table_u16 = NULL;
      uint64_t tb_step = 0;

      if (tb_table != table_w && tb_table != table_b) {
        if (save_to_disk)
          free(tb_table);
        else {
          tb_table_u16 = (uint16_t *)tb_table;
          tb_step = (tb_size + 1) / 2;
        }
      }
      if (!tb_table_u16) {
        tb_step = save_to_disk ? MAX_STEP : (tb_size + 1) / 2;
        tb_table_u16 = malloc(2 * tb_step);
      }

      if (!symmetric) {
        load_table_u16(table_u16, 'b');
        if (num_saves > 0)
          reconstruct_table_u16(table_u16, 'b', &map_b);

        prepare_dtz_map_u16(v, &map_b);
        printf("find optimal permutation for btm/dtz\n");
        estimate_b = estimate_piece_dtz_u16(pcs, pt, table_u16, best_b, &bestp_b, v);
      } else
        estimate_b = UINT64_MAX;

      load_table_u16(table_u16, 'w');
      unlink_table('w');
      if (num_saves > 0) {
        reconstruct_table_u16(table_u16, 'w', &map_w);
        unlink_saves('w');
      }

      prepare_dtz_map_u16(v, &map_w);
      printf("find optimal permutation for wtm/dtz\n");
      estimate_w = estimate_piece_dtz_u16(pcs, pt, table_u16, best_w, &bestp_w, v);

      if (estimate_w <= estimate_b) {
        prepare_dtz_map_u16(v, &map_w);
        printf("permute table for wtm/dtz\n");
        permute_piece_dtz_u16_full(tb_table_u16, pcs, table_u16, bestp_w, v, tb_step);
        printf("compressing data for wtm/dtz\n");
        compress_tb_u16(G, table_u16, tb_size, best_w, minfreq);
      } else {
        load_table_u16(table_u16, 'b');
        if (num_saves > 0)
          reconstruct_table_u16(table_u16, 'b', &map_b);

        prepare_dtz_map_u16(v, &map_b);
        printf("permute table for btm/dtz\n");
        permute_piece_dtz_u16_full(tb_table_u16, pcs, table_u16, bestp_b, v, tb_step);
        printf("compressing data for btm/dtz\n");
        compress_tb_u16(G, table_u16, tb_size, best_b, minfreq);
      }

      if (!symmetric) {
        unlink_table('b');
        if (num_saves > 0)
          unlink_saves('b');
      }

    }

    merge_tb(G);

 }

  return 0;
}
