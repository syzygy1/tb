/*
  Copyright (c) 2011-2016 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include "lz4.h"

static char *lz4_buf = NULL;

#define MAX_SAVES 16

void reduce_tables(int local);
void count_stats(struct thread_data *thread);
void collect_stats(long64 *work, int phase, int local);

static FILE *tmp_table[MAX_SAVES][2];
static int reduce_cnt[MAX_SAVES];
static int stats_val[MAX_SAVES];
static int reduce_val[MAX_SAVES];

void save_table(ubyte *table, char color, int local, long64 begin, long64 size)
{
  int i;
  FILE *F;
  char name[64];
  ubyte v[256];

  if (!lz4_buf) {
    lz4_buf = malloc(8 + LZ4_compressBound(COPYSIZE));
    if (!lz4_buf) {
      fprintf(stderr, "Out of memory.\n");
      exit(1);
    }
  }

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

  ubyte *ptr = table + begin;
  long64 total = size;
  while (total > 0) {
    int chunk = COPYSIZE;
    if (total < chunk) chunk = total;
    total -= chunk;
    for (i = 0; i < chunk; i++)
      copybuf[i] = v[ptr[i]];
    ptr += chunk;
    uint32 lz4_size = LZ4_compress((char *)copybuf, lz4_buf + 8, chunk);
    ((uint32 *)lz4_buf)[0] = lz4_size;
    ((uint32 *)lz4_buf)[1] = chunk;
    fwrite(lz4_buf, 1, lz4_size + 8, F);
  }
}

void reconstruct_table_pass(ubyte *table, char color, int k, ubyte *v)
{
  int i;
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c.%d", tablename, color, k);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  ubyte *ptr = table;
  long64 total = size;
  while (total > 0) {
    int chunk;
    uint32 lz4_size;
    fread(&lz4_size, 1, 4, F);
    fread(&chunk, 1, 4, F);
    fread(lz4_buf, 1, lz4_size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (i = 0; i < chunk; i++)
      ptr[i] |= v[copybuf[i]];
    ptr += chunk;
    total -= chunk;
  }

  fclose(F);
  unlink(name);
}

void verify_stats(ubyte *table, long64 *tot_stats, struct dtz_map *map)
{
  long64 stats[256];
  long64 stats2[256];
  int i, j;
  ubyte (*inv_map)[256] = map->inv_map;

  for (i = 0; i < 256; i++)
    stats[i] = stats2[i] = 0;

  for (i = 0; i < 256 * numthreads; i++)
    thread_stats[i] = 0;
  count_stats_table = table;
  run_threaded(count_stats, work_g, 0);
  for (i = 0; i < numthreads; i++)
    for (j = 0; j < 256; j++)
      stats[j] += thread_data[i].stats[j];

  stats2[inv_map[0][0]] = tot_stats[0];
  stats2[inv_map[1][0]] += tot_stats[STAT_MATE];
  if (map->ply_accurate_win)
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[0][i]] += tot_stats[i + 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[0][i / 2]] += tot_stats[i + 1];
  if (map->ply_accurate_loss)
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[1][i]] += tot_stats[STAT_MATE - 1 - i];
  else
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[1][i / 2]] += tot_stats[STAT_MATE - 1 - i];
  for (i = DRAW_RULE + 1; i < MAX_PLY; i++) {
    stats2[inv_map[2][(i - DRAW_RULE - 1) / 2]] += tot_stats[i];
    stats2[inv_map[3][(i - DRAW_RULE - 1) / 2]] += tot_stats[STAT_MATE - i];
  }

  int verify_ok = 1;
  for (i = 0; i < 256; i++)
    if (stats[i] != stats2[i] && i != map->max_num) {
      fprintf(stderr, "stats[%d] = %"PRIu64"; stats2[%d] = %"PRIu64"\n",
		    i, stats[i], i, stats2[i]);
      int j;
      for (j = 0; j < 4; j++)
	fprintf(stderr, "map[%d][%d]=%d\n", j, i, map->map[j][i]);
      verify_ok = 0;
    }

  if (!verify_ok)
    exit(1);
}

void reconstruct_table(ubyte *table, char color, struct dtz_map *map)
{
  int i, k;
  int num = map->max_num;
  ubyte (*inv_map)[256] = map->inv_map;
  ubyte v[256];

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
  v[ILLEGAL] = num;
  v[BROKEN] = num;
  v[UNKNOWN] = num;
  v[PAWN_DRAW] = num;
  v[CAPT_DRAW] = num;
  v[CAPT_CWIN_RED] = num;
  v[CAPT_WIN] = num;
  for (i = 0; i <= REDUCE_PLY_RED; i++) {
    v[CAPT_CWIN_RED + i + 2] = inv_map[2][(reduce_cnt[num_saves - 1] + i) / 2];
    v[LOSS_IN_ONE - i - 1] = inv_map[3][(reduce_cnt[num_saves - 1] + i + 1) / 2];
  }
  v[CAPT_CWIN_RED + i + 2] = inv_map[2][(reduce_cnt[num_saves - 1] + i) / 2];
#else
  v[BROKEN] = num;
  v[UNKNOWN] = v[PAWN_DRAW] = num;
  v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
  v[CAPT_CLOSS] = v[CAPT_LOSS] = num;
  v[THREAT_WIN_RED] = v[THREAT_CWIN_RED] = v[THREAT_DRAW] = num;
// FIXME: add THREAT_CLOSS
  for (i = 0; i <= REDUCE_PLY_RED; i++) {
    v[BASE_CWIN_RED + i + 1] = inv_map[2][(reduce_cnt[num_saves - 1] + i) / 2];
    v[BASE_CLOSS_RED - i - 1] = inv_map[3][(reduce_cnt[num_saves - 1] + i + 1) / 2];
  }
  v[BASE_CWIN_RED + i + 1] = inv_map[2][(reduce_cnt[num_saves -1] + i) / 2];
#endif

  begin = 0;
  transform_v = v;
  transform_tbl = table;
  run_threaded(transform_table, work_g, 0);

  v[0] = 0;
  int red_cnt = 0;
#ifndef SUICIDE
  for (k = 0; k < num_saves; k++) {
    if (k == 0) {
      v[255] = inv_map[1][0];
      v[1] = num;
      if (map->ply_accurate_win)
	for (i = 0; i < DRAW_RULE; i++)
	  v[i + 2] = inv_map[0][i];
      else
	for (i = 0; i < DRAW_RULE; i++)
	  v[i + 2] = inv_map[0][i / 2];
      if (map->ply_accurate_loss)
	for (i = 0; i < DRAW_RULE; i++)
	  v[254 - i] = inv_map[1][i];
      else
	for (i = 0; i < DRAW_RULE; i++)
	  v[254 - i] = inv_map[1][i / 2];
      for (; i <= REDUCE_PLY; i += 2) {
	v[2 + DRAW_RULE + (i - DRAW_RULE) / 2] = inv_map[2][(i - DRAW_RULE) / 2];
	v[254 - DRAW_RULE - (i - DRAW_RULE) / 2] = inv_map[3][(i - DRAW_RULE) / 2];
      }
      red_cnt = REDUCE_PLY - DRAW_RULE - 2;
    } else {
      for (i = 0; i <= REDUCE_PLY_RED + 1; i += 2) {
	v[1 + ((red_cnt & 1) + i) / 2] = inv_map[2][(red_cnt + i) / 2];
	v[255 - ((red_cnt & 1) + i + 1) / 2] = inv_map[3][(red_cnt + i + 1) / 2];
      }
      red_cnt += REDUCE_PLY_RED;
    }
    reconstruct_table_pass(table, color, k, v);
  }
#else
  for (k = 0; k < num_saves; k++) {
    if (k == 0) {
      v[1] = inv_map[0][0];
      if (map->ply_accurate_win)
	for (i = 1; i <= DRAW_RULE; i++)
	  v[1 + i] = inv_map[0][i - 1];
      else
	for (i = 1; i <= DRAW_RULE; i++)
	  v[1 + i] = inv_map[0][(i - 1) / 2];
      v[255] = inv_map[1][0];
      if (map->ply_accurate_loss)
	for (i = 1; i <= DRAW_RULE; i++)
	  v[255 - i] = inv_map[1][i - 1];
      else
	for (i = 1; i <= DRAW_RULE; i++)
	  v[255 - i] = inv_map[1][(i - 1) / 2];
      for (i = DRAW_RULE + 1; i < REDUCE_PLY; i += 2) {
	v[2 + DRAW_RULE + (i - DRAW_RULE - 1) / 2] = inv_map[2][(i - DRAW_RULE - 1) / 2];
	v[254 - DRAW_RULE - (i - DRAW_RULE - 1) / 2] = inv_map[3][(i - DRAW_RULE - 1) / 2];
      }
      red_cnt = REDUCE_PLY - 1 - DRAW_RULE;
    } else {
      for (i = 0; i < REDUCE_PLY_RED; i += 2) {
	v[1 + ((red_cnt & 1) + i) / 2] = inv_map[2][(red_cnt + i) / 2];
	v[255 - ((red_cnt & 1) + i + 1) / 2] = inv_map[3][(red_cnt + i + 1) / 2];
      }
      red_cnt += REDUCE_PLY_RED;
    }
    reconstruct_table_pass(table, color, k, v);
  }
#endif

  if (color == 'w') {
    printf("Verifying reconstructed table_w based on collected statistics.\n");
    verify_stats(table_w, total_stats_w, map);
  } else {
    printf("Verifying reconstructed table_b based on collected statistics.\n");
    verify_stats(table_b, total_stats_b, map);
  }
}

void reduce_tables(int local)
{
  int i;
  ubyte v[256];
  long64 *work;
  long64 save_begin = begin;

  if (!copybuf)
    copybuf = malloc(COPYSIZE);

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

  transform_v = v;
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

void store_table(ubyte *table, char color)
{
  FILE *F;
  char name[64];

  if (!lz4_buf) {
    lz4_buf = malloc(8 + LZ4_compressBound(COPYSIZE));
    if (!lz4_buf) {
      fprintf(stderr, "Out of memory.\n");
      exit(1);
    }
  }

  sprintf(name, "%s.%c", tablename, color);

  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  ubyte *ptr = table;
  long64 total = size;
  while (total > 0) {
    int chunk = COPYSIZE;
    if (total < chunk) chunk = total;
    total -= chunk;
    uint32 lz4_size = LZ4_compress((char *)ptr, lz4_buf + 4, chunk);
    ptr += chunk;
    *(uint32 *)lz4_buf = lz4_size;
    fwrite(lz4_buf, 1, lz4_size + 4, F);
  }

  fclose(F);
}

void load_table(ubyte *table, char color)
{
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c", tablename, color);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  ubyte *ptr = table;
  long64 total = size;
  while (total > 0) {
    int chunk = COPYSIZE;
    if (total < chunk) chunk = total;
    total -= chunk;
    uint32 lz4size;
    fread(&lz4size, 1, 4, F);
    fread(lz4_buf, 1, lz4size, F);
    LZ4_uncompress(lz4_buf, (char *)ptr, chunk);
    ptr += chunk;
  }

  fclose(F);
  unlink(name);
}

