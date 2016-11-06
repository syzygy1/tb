/*
  Copyright (c) 2011-2016 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include "lz4.h"

static int reduce_cnt;

static char *lz4_buf = NULL;

void save_table(ubyte *table, char color)
{
  int i;
  FILE *F;
  char name[64];
  ubyte v[256];

  if (!lz4_buf) {
    lz4_buf = malloc(4 + LZ4_compressBound(COPYSIZE));
    if (!lz4_buf) {
      fprintf(stderr, "Out of memory.\n");
      exit(1);
    }
  }

  sprintf(name, "%s.%c.%d", tablename, color, num_saves);

  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
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

  ubyte *ptr = table;
  long64 total = size;
  while (total > 0) {
    int chunk = COPYSIZE;
    if (total < chunk) chunk = total;
    total -= chunk;
    for (i = 0; i < chunk; i++)
      copybuf[i] = v[ptr[i]];
    ptr += chunk;
    uint32 lz4_size = LZ4_compress((char *)copybuf, lz4_buf + 4, chunk);
    *(uint32 *)lz4_buf = lz4_size;
    fwrite(lz4_buf, 1, lz4_size + 4, F);
  }

  fclose(F);
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
    int chunk = COPYSIZE;
    if (total < chunk) chunk = total;
    total -= chunk;
    uint32 lz4size;
    fread(&lz4size, 1, 4, F);
    fread(lz4_buf, 1, lz4size, F);
    LZ4_uncompress(lz4_buf, (char *)copybuf, chunk);
    for (i = 0; i < chunk; i++)
      ptr[i] |= v[copybuf[i]];
    ptr += chunk;
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
  stats2[inv_map[1][0]] = tot_stats[1023];
#ifndef SUICIDE
  if (map->ply_accurate_win)
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[0][i]] += tot_stats[i + 1];
  else
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[0][i / 2]] += tot_stats[i + 1];
  if (map->ply_accurate_loss)
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[1][i]] += tot_stats[1022 - i];
  else
    for (i = 0; i < DRAW_RULE; i++)
      stats2[inv_map[1][i / 2]] += tot_stats[1022 - i];
#else
  if (map->ply_accurate_win)
    for (i = 2; i < DRAW_RULE; i++)
      stats2[inv_map[0][i]] += tot_stats[i + 1];
  else
    for (i = 2; i < DRAW_RULE; i++)
      stats2[inv_map[0][i / 2]] += tot_stats[i + 1];
  if (map->ply_accurate_loss)
    for (i = 1; i < DRAW_RULE; i++)
      stats2[inv_map[1][i]] += tot_stats[1022 - i];
  else
    for (i = 1; i < DRAW_RULE; i++)
      stats2[inv_map[1][i / 2]] += tot_stats[1022 - i];
#endif
  for (i = DRAW_RULE + 1; i < MAX_PLY; i++) {
    stats2[inv_map[2][(i - DRAW_RULE - 1) / 2]] += tot_stats[i];
    stats2[inv_map[3][(i - DRAW_RULE - 1) / 2]] += tot_stats[1023 - i];
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
  v[CAPT_DRAW] = num;
  v[CAPT_CWIN_RED] = num;
  v[CAPT_WIN] = num;
  for (i = 0; i <= REDUCE_PLY_RED; i++) {
    v[CAPT_CWIN_RED + i + 2] = inv_map[2][(reduce_cnt + i) / 2];
    v[LOSS_IN_ONE - i - 1] = inv_map[3][(reduce_cnt + i + 1) / 2];
  }
  v[CAPT_CWIN_RED + i + 2] = inv_map[2][(reduce_cnt + i) / 2];
#else
  v[BROKEN] = num;
  v[UNKNOWN] = num;
  v[CAPT_WIN] = v[CAPT_CWIN] = v[CAPT_DRAW] = num;
  v[CAPT_CLOSS] = v[CAPT_LOSS] = num;
  v[THREAT_WIN] = v[BASE_WIN + 4] = v[THREAT_DRAW] = num;
  for (i = 0; i <= REDUCE_PLY_RED; i++) {
    v[BASE_WIN + i + 6] = inv_map[2][(reduce_cnt + i) / 2];
    v[BASE_LOSS - i - 4] = inv_map[3][(reduce_cnt + i + 1) / 2];
  }
  v[BASE_WIN + i + 6] = inv_map[2][(reduce_cnt + i) / 2];
#endif

  transform_v = v;
  transform_tbl = table;
  run_threaded(transform_table, work_g, 0);

  v[0] = 0;
  int red_cnt = 0;
#ifndef SUICIDE
  for (k = 0; k < num_saves; k++) {
    if (k == 0) {
      v[255] = inv_map[1][0];
      if (map->ply_accurate_win)
	for (i = 0; i < DRAW_RULE; i++)
	  v[i + 1] = inv_map[0][i];
      else
	for (i = 0; i < DRAW_RULE; i++)
	  v[i + 1] = inv_map[0][i / 2];
      if (map->ply_accurate_loss)
	for (i = 0; i < DRAW_RULE; i++)
	  v[254 - i] = inv_map[1][i];
      else
	for (i = 0; i < DRAW_RULE; i++)
	  v[254 - i] = inv_map[1][i / 2];
      for (; i <= REDUCE_PLY; i += 2) {
	v[1 + DRAW_RULE + (i - DRAW_RULE) / 2] = inv_map[2][(i - DRAW_RULE) / 2];
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
      if (map->ply_accurate_win)
	for (i = 3; i <= DRAW_RULE; i++)
	  v[1 + (i - 3)] = inv_map[0][i - 1];
      else
	for (i = 3; i <= DRAW_RULE; i++)
	  v[1 + (i - 3)] = inv_map[0][(i - 1) / 2];
      if (map->ply_accurate_loss)
	for (i = 2; i <= DRAW_RULE; i++)
	  v[255 - (i - 2)] = inv_map[1][i - 1];
      else
	for (i = 2; i <= DRAW_RULE; i++)
	  v[255 - (i - 2)] = inv_map[1][(i - 1) / 2];
      for (i = DRAW_RULE + 1; i < REDUCE_PLY; i += 2) {
	v[DRAW_RULE - 1 + (i - DRAW_RULE - 1) / 2] = inv_map[2][(i - DRAW_RULE - 1) / 2];
	v[254 - (DRAW_RULE - 2) - (i - DRAW_RULE - 1) / 2] = inv_map[3][(i - DRAW_RULE - 1) / 2];
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

void reduce_tables(void)
{
  int i;
  ubyte v[256];

  if (!copybuf)
    copybuf = malloc(COPYSIZE);

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

  transform_v = v;
  run_threaded(transform, work_g, 1);

  if (num_saves == 0)
    reduce_cnt = REDUCE_PLY - DRAW_RULE - 2;
  else
    reduce_cnt += REDUCE_PLY_RED;
}

void store_table(ubyte *table, char color)
{
  FILE *F;
  char name[64];

  if (!lz4_buf) {
    lz4_buf = malloc(4 + LZ4_compressBound(COPYSIZE));
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

