/*
  Copyright (c) 2011-2016, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include "util.h"

#define MAX_SAVES 32

void reduce_tables(int local);
void count_stats(struct thread_data *thread);
void collect_stats(uint64_t *work, int phase, int local);

static FILE *tmp_table[MAX_SAVES][2];
static int reduce_cnt[MAX_SAVES];
static int stats_val[MAX_SAVES];
static int reduce_val[MAX_SAVES];

void save_table(uint8_t *table, char color, int local, uint64_t begin,
    uint64_t size)
{
  int i;
  FILE *F;
  char name[64];
  uint8_t v[256];

  if (local == num_saves) {
    sprintf(name, "%s.%c.%d", tablename, color, num_saves);
    if (!(F = fopen(name, "wb"))) {
      fprintf(stderr, "Could not open %s for writing.\n", name);
      exit(1);
    }
    tmp_table[num_saves][color == 'w' ? 0 : 1] = F;
  } else {
    F = tmp_table[local][color == 'w' ? 0 : 1];
  }

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
  if (local == 0) {
    v[MATE] = 255;
    v[PAWN_WIN] = v[PAWN_CWIN] = 1;
    for (i = 0; i < DRAW_RULE; i++) {
      v[WIN_IN_ONE + i] = 2 + i;
      v[LOSS_IN_ONE - i] = 254 - i;
    }
    for (; i < REDUCE_PLY - 2; i++) {
      v[WIN_IN_ONE + i + 2] = 2 + DRAW_RULE + (i - DRAW_RULE) / 2;
      v[LOSS_IN_ONE - i] = 254 - DRAW_RULE - (i - DRAW_RULE) / 2;
    }
    v[LOSS_IN_ONE - i] = 254 - DRAW_RULE - (i - DRAW_RULE) / 2;
  } else {
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[CAPT_CWIN_RED + i + 2] = 1 + ((reduce_cnt[local - 1] & 1) + i) / 2;
      v[LOSS_IN_ONE - i - 1] = 255 - ((reduce_cnt[local - 1] & 1) + i + 1) / 2;
    }
  }
#else
  if (local == 0) {
    v[STALE_WIN] = 1;
    v[STALE_WIN + 1] = 2;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = 1 + i;
    for (i = 0; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = 255 - i;
    v[BASE_WIN + DRAW_RULE + 1] = 1 + DRAW_RULE + 1;
    for (i = DRAW_RULE + 2; i < REDUCE_PLY - 1; i++)
      v[BASE_WIN + i + 2] = 2 + DRAW_RULE + (i - DRAW_RULE - 1) / 2;
    for (i = DRAW_RULE + 1; i < REDUCE_PLY; i++)
      v[BASE_LOSS - i] = 255 - DRAW_RULE - 1 - (i - DRAW_RULE - 1) / 2;
  } else {
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[BASE_CWIN_RED + i + 1] = 1 + ((reduce_cnt[local - 1] & 1) + i) / 2;
      v[BASE_CLOSS_RED - i - 1] = 255 - ((reduce_cnt[local - 1] & 1) + i + 1) / 2;
    }
  }
#endif

  write_data(F, table, begin, size, v);
}

void reduce_tables(int local)
{
  int i;
  uint8_t v[256];
  uint64_t *work;
  uint64_t save_begin = begin;

  if (local == num_saves) {
    work = work_part;
    fill_work(total_work, begin + (1ULL << shift[numpawns - 1]), 0, work);
    begin = 0;
    reduce_val[local] = ply;
    work = work_part;
  } else
    work = work_p;

  collect_stats(work, 0, local);

  if (generate_dtz) {
    save_table(table_w, 'w', local, begin, work[total_work]);
    if (!symmetric)
      save_table(table_b, 'b', local, begin, work[total_work]);
  }

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
  v[BROKEN] = BROKEN;
  v[UNKNOWN] = UNKNOWN;
  v[CHANGED] = CHANGED;
  v[CAPT_DRAW] = CAPT_DRAW;
  v[PAWN_DRAW] = PAWN_DRAW;
  v[MATE] = MATE;
  v[ILLEGAL] = ILLEGAL;
  v[CAPT_WIN] = CAPT_WIN;
  if (local == 0) {
    v[PAWN_WIN] = WIN_RED;
    for (i = 0; i < DRAW_RULE; i++) {
      v[WIN_IN_ONE + i] = WIN_RED;
      v[LOSS_IN_ONE - i] = MATE;
    }
    v[CAPT_CWIN] = CAPT_CWIN_RED;
    v[PAWN_CWIN] = CAPT_CWIN_RED + 1;
    for (; i < REDUCE_PLY - 2; i++) {
      v[WIN_IN_ONE + i + 2] = CAPT_CWIN_RED + 1;
      v[LOSS_IN_ONE - i] = LOSS_IN_ONE;
    }
    v[LOSS_IN_ONE - i] = LOSS_IN_ONE;
    v[WIN_IN_ONE + REDUCE_PLY] = CAPT_CWIN_RED + 2;
    v[WIN_IN_ONE + REDUCE_PLY + 1] = CAPT_CWIN_RED + 3;
    v[WIN_IN_ONE + REDUCE_PLY + 2] = CAPT_CWIN_RED + 4;
    v[LOSS_IN_ONE - REDUCE_PLY + 1] = LOSS_IN_ONE - 1;
  } else {
    v[WIN_RED] = WIN_RED;
    v[LOSS_IN_ONE] = LOSS_IN_ONE;
    v[CAPT_CWIN_RED] = CAPT_CWIN_RED;
    v[CAPT_CWIN_RED + 1] = CAPT_CWIN_RED + 1;
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[CAPT_CWIN_RED + i + 2] = CAPT_CWIN_RED + 1;
      v[LOSS_IN_ONE - i - 1] = LOSS_IN_ONE;
    }
    v[CAPT_CWIN_RED + REDUCE_PLY_RED + 2] = CAPT_CWIN_RED + 2;
    v[CAPT_CWIN_RED + REDUCE_PLY_RED + 3] = CAPT_CWIN_RED + 3;
    v[CAPT_CWIN_RED + REDUCE_PLY_RED + 4] = CAPT_CWIN_RED + 4;
    v[LOSS_IN_ONE - REDUCE_PLY_RED - 1] = LOSS_IN_ONE - 1;
  }
#else
  v[BROKEN] = BROKEN;
  v[UNKNOWN] = UNKNOWN;
  v[CHANGED] = CHANGED;
  v[CAPT_WIN] = CAPT_WIN;
  v[CAPT_CWIN] = CAPT_CWIN;
  v[CAPT_DRAW] = CAPT_DRAW;
  v[CAPT_CLOSS] = CAPT_CLOSS;
  v[CAPT_LOSS] = CAPT_LOSS;
  v[PAWN_DRAW] = PAWN_DRAW;
  if (local == 0) {
    v[THREAT_WIN1] = THREAT_WIN_RED;
    v[THREAT_WIN2] = THREAT_WIN_RED;
    v[STALE_WIN] = BASE_WIN_RED;
    v[STALE_WIN + 1] = BASE_WIN_RED;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = BASE_WIN_RED;
    v[THREAT_CWIN1] = THREAT_CWIN_RED;
    v[THREAT_CWIN2] = THREAT_CWIN_RED;
    v[BASE_WIN + DRAW_RULE + 1] = BASE_CWIN_RED;
    for (i = DRAW_RULE + 2; i < REDUCE_PLY - 1; i++)
      v[BASE_WIN + i + 2] = BASE_CWIN_RED;
    for (i = 0; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = BASE_LOSS_RED;
    for (; i < REDUCE_PLY; i++)
      v[BASE_LOSS - i] = BASE_CLOSS_RED;
    v[BASE_WIN + REDUCE_PLY + 1] = BASE_CWIN_RED + 1;
    v[BASE_WIN + REDUCE_PLY + 2] = BASE_CWIN_RED + 2;
    v[BASE_WIN + REDUCE_PLY + 3] = BASE_CWIN_RED + 3;
    v[BASE_LOSS - REDUCE_PLY] = BASE_CLOSS_RED - 1;
  } else {
    v[THREAT_WIN_RED] = THREAT_WIN_RED;
    v[BASE_WIN_RED] = BASE_WIN_RED;
    v[THREAT_CWIN_RED] = THREAT_CWIN_RED;
    v[BASE_CWIN_RED] = BASE_CWIN_RED;
    v[BASE_LOSS_RED] = BASE_LOSS_RED;
    v[BASE_CLOSS_RED] = BASE_CLOSS_RED;
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[BASE_CWIN_RED + i + 1] = BASE_CWIN_RED;
      v[BASE_CLOSS_RED - i - 1] = BASE_CLOSS_RED;
    }
    v[BASE_CWIN_RED + REDUCE_PLY_RED + 1] = BASE_CWIN_RED + 1;
    v[BASE_CWIN_RED + REDUCE_PLY_RED + 2] = BASE_CWIN_RED + 2;
    v[BASE_CWIN_RED + REDUCE_PLY_RED + 3] = BASE_CWIN_RED + 3;
    v[BASE_CLOSS_RED - REDUCE_PLY_RED - 1] = BASE_CLOSS_RED - 1;
  }
#endif

  transform_v_u8 = v;
  run_threaded(transform, work, 0);

  if (local == num_saves) {
    if (num_saves == 0)
      reduce_cnt[0] = ply - DRAW_RULE - 2;
    else
      reduce_cnt[num_saves] = reduce_cnt[num_saves - 1] + ply;
    begin = save_begin;
    num_saves++;
  }
}

void store_table(uint8_t *table, char color)
{
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c", tablename, color);

  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  write_data(F, table, 0, size, NULL);

  fclose(F);
}

void unlink_table(char color)
{
  char name[64];

  sprintf(name, "%s.%c", tablename, color);
  unlink(name);
}

void unlink_saves(char color)
{
  char name[64];

  for (int k = 0; k < num_saves; k++) {
    sprintf(name, "%s.%c.%d", tablename, color, k);
    unlink(name);
  }
}

#define MAX_STAT(x) _Generic((x), \
  u8: 256, \
  u16: MAX_VALS \
)

#define T u8
#include "reducep_tmpl.c"
#undef T

#define T u16
#include "reducep_tmpl.c"
#undef T
