/*
  Copyright (c) 2011-2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include "util.h"

static int reduce_cnt;

static void save_table(uint8_t *table, char color)
{
  int i;
  FILE *F;
  char name[64];
  uint8_t v[256];

  sprintf(name, "%s.%c.%d", tablename, color, num_saves);

  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(EXIT_FAILURE);
  }

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
  if (num_saves == 0) {
    v[MATE] = 255;
    for (i = 0; i < DRAW_RULE; i++) {
      v[WIN_IN_ONE + i] = 1 + i;
      v[LOSS_IN_ONE - i] = 254 - i;
    }
    for (; i < REDUCE_PLY - 2; i++) {
      v[WIN_IN_ONE + i + 1] = 1 + DRAW_RULE + (i - DRAW_RULE) / 2;
      v[LOSS_IN_ONE - i] = 254 - DRAW_RULE - (i - DRAW_RULE) / 2;
    }
    v[LOSS_IN_ONE - i] = 254 - DRAW_RULE - (i - DRAW_RULE) / 2;
  } else {
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[CAPT_CWIN_RED + i + 2] = 1 + ((reduce_cnt & 1) + i) / 2;
      v[LOSS_IN_ONE - i - 1] = 255 - ((reduce_cnt & 1) + i + 1) / 2;
    }
  }
#else
  if (num_saves == 0) {
    for (i = 3; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = 1 + (i - 3);
    v[BASE_WIN + DRAW_RULE + 1] = DRAW_RULE - 1;
    // should we save THREAT_CWIN1/2 (separately) ?
    for (i = DRAW_RULE + 2; i < REDUCE_PLY - 1; i++)
      v[BASE_WIN + i + 2] = DRAW_RULE - 1 + (i - DRAW_RULE - 1) / 2;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = 255 - (i - 2);
    for (; i < REDUCE_PLY; i++)
      v[BASE_LOSS - i] = 255 - (DRAW_RULE - 2) - 1 - (i - DRAW_RULE - 1) / 2;
  } else {
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[BASE_WIN + i + 6] = 1 + ((reduce_cnt & 1) + i) / 2;
      v[BASE_LOSS - i - 4] = 255 - ((reduce_cnt & 1) + i + 1) / 2;
    }
  }
#endif

  write_data(F, table, 0, size, v);

  fclose(F);
}

static void reduce_tables(void)
{
  int i;
  uint8_t v[256];

  collect_stats(0);

  if (generate_dtz) {
    save_table(table_w, 'w');
    if (!symmetric)
      save_table(table_b, 'b');
  }

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
  v[BROKEN] = BROKEN;
  v[UNKNOWN] = UNKNOWN;
  v[CHANGED] = CHANGED;
  v[CAPT_DRAW] = CAPT_DRAW;
  v[MATE] = MATE;
  v[ILLEGAL] = ILLEGAL;
  v[CAPT_WIN] = CAPT_WIN;
  if (num_saves == 0) {
    for (i = 0; i < DRAW_RULE; i++) {
      v[WIN_IN_ONE + i] = WIN_IN_ONE;
      v[LOSS_IN_ONE - i] = MATE;
    }
    v[CAPT_CWIN] = CAPT_CWIN_RED;
    for (; i < REDUCE_PLY - 2; i++) {
      v[WIN_IN_ONE + i + 1] = CAPT_CWIN_RED + 1;
      v[LOSS_IN_ONE - i] = LOSS_IN_ONE;
    }
    v[LOSS_IN_ONE - i] = LOSS_IN_ONE;
    v[WIN_IN_ONE + REDUCE_PLY - 1] = CAPT_CWIN_RED + 2;
    v[WIN_IN_ONE + REDUCE_PLY] = CAPT_CWIN_RED + 3;
    v[WIN_IN_ONE + REDUCE_PLY + 1] = CAPT_CWIN_RED + 4;
    v[LOSS_IN_ONE - REDUCE_PLY + 1] = LOSS_IN_ONE - 1;
  } else {
    v[WIN_IN_ONE] = WIN_IN_ONE;
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
  v[THREAT_WIN] = THREAT_WIN;
  if (num_saves == 0) {
    for (i = 3; i <= DRAW_RULE; i++)
      v[BASE_WIN + i] = BASE_WIN + 3;
    v[THREAT_CWIN1] = BASE_WIN + 4;
    v[THREAT_CWIN2] = BASE_WIN + 4;
    v[BASE_WIN + DRAW_RULE + 1] = BASE_WIN + 5;
    for (i = DRAW_RULE + 2; i < REDUCE_PLY - 1; i++)
      v[BASE_WIN + i + 2] = BASE_WIN + 5;
    for (i = 2; i <= DRAW_RULE; i++)
      v[BASE_LOSS - i] = BASE_LOSS - 2;
    for (; i < REDUCE_PLY; i++)
      v[BASE_LOSS - i] = BASE_LOSS - 3;
    v[BASE_WIN + REDUCE_PLY + 1] = BASE_WIN + 6;
    v[BASE_WIN + REDUCE_PLY + 2] = BASE_WIN + 7;
    v[BASE_WIN + REDUCE_PLY + 3] = BASE_WIN + 8;
    v[BASE_LOSS - REDUCE_PLY] = BASE_LOSS - 4;
  } else {
    v[BASE_WIN + 3] = BASE_WIN + 3;
    v[BASE_WIN + 4] = BASE_WIN + 4;
    v[BASE_WIN + 5] = BASE_WIN + 5;
    v[BASE_LOSS - 2] = BASE_LOSS - 2;
    v[BASE_LOSS - 3] = BASE_LOSS - 3;
    for (i = 0; i < REDUCE_PLY_RED; i++) {
      v[BASE_WIN + i + 6] = BASE_WIN + 5;
      v[BASE_LOSS - i - 4] = BASE_LOSS - 3;
    }
    v[BASE_WIN + REDUCE_PLY_RED + 6] = BASE_WIN + 6;
    v[BASE_WIN + REDUCE_PLY_RED + 7] = BASE_WIN + 7;
    v[BASE_WIN + REDUCE_PLY_RED + 8] = BASE_WIN + 8;
    v[BASE_LOSS - REDUCE_PLY_RED - 4] = BASE_LOSS - 4;
  }
#endif

  transform_v_u8 = v;
  run_threaded(transform, work_g, 1);

  if (num_saves == 0)
    reduce_cnt = REDUCE_PLY - DRAW_RULE - 2;
  else
    reduce_cnt += REDUCE_PLY_RED;
}

static void store_table(uint8_t *table, char color)
{
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c", tablename, color);

  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(EXIT_FAILURE);
  }

  write_data(F, table, 0, size, NULL);

  fclose(F);
}

static void unlink_table(char color)
{
  char name[64];

  sprintf(name, "%s.%c", tablename, color);
  unlink(name);
}

static void unlink_saves(char color)
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
#include "reduce_tmpl.c"
#undef T

#define T u16
#include "reduce_tmpl.c"
#undef T
