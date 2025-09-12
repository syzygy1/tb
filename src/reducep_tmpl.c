/*
  Copyright (c) 2018, 2024 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define NAME(f) EVALUATOR(f,T)

void NAME(reconstruct_table_pass)(T *table, char color, int k, T *v)
{
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c.%d", tablename, color, k);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  NAME(read_data)(F, table, size, v);

  fclose(F);
}

void NAME(verify_stats)(T *table, uint64_t *tot_stats, struct dtz_map *map)
{
  uint64_t stats[MAX_STAT(*table)];
  uint64_t stats2[MAX_STAT(*table)];
  int i, j;
  uint16_t (*inv_map)[MAX_VALS] = map->inv_map;

  for (i = 0; i < MAX_STAT(*table); i++)
    stats[i] = stats2[i] = 0;

  for (i = 0; i < MAX_VALS * numthreads; i++)
    thread_stats[i] = 0;
  NAME(count_stats_table) = table;
  run_threaded(NAME(count_stats), work_g, 0);
  for (i = 0; i < numthreads; i++)
    for (j = 0; j < MAX_STAT(*table); j++)
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
  for (i = 0; i < MAX_STAT(*table); i++)
    if (stats[i] != stats2[i] && i != map->max_num) {
      fprintf(stderr, "stats[%d] = %"PRIu64"; stats2[%d] = %"PRIu64"\n",
                    i, stats[i], i, stats2[i]);
      int j;
      for (j = 0; j < 4; j++)
        fprintf(stderr, "map[%d][%d]=%d\n", j, i, map->map[j][i]);
      verify_ok = 0;
    }

  if (!verify_ok)
    exit(EXIT_FAILURE);
}

void NAME(reconstruct_table)(T *table, char color, struct dtz_map *map)
{
  int i, k;
  int num = map->max_num;
  uint16_t (*inv_map)[MAX_VALS] = map->inv_map;
  T v[256];

  for (i = 0; i < 256; i++)
    v[i] = 0;

#ifndef SUICIDE
#ifndef SHATRANJ
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
#else /* SHATRANJ */
  v[ILLEGAL] = num;
  v[BROKEN] = num;
  v[UNKNOWN] = num;
  v[PAWN_DRAW] = num;
  v[CAPT_DRAW] = num;
  v[CAPT_WIN] = num;
  if (num_saves == 1) {
    if (map->ply_accurate_win)
      for (i = 0 ; i < DRAW_RULE - REDUCE_PLY + 1; i++)
        v[WIN_RED + i + 1] = inv_map[0][i + REDUCE_PLY - 1];
    else
      for (i = 0 ; i < DRAW_RULE - REDUCE_PLY + 1; i++)
        v[WIN_RED + i + 1] = inv_map[0][(i + REDUCE_PLY - 1) / 2];
    if (map->ply_accurate_loss)
      for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][i + REDUCE_PLY - 1];
    else
      for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
        v[LOSS_IN_ONE - i] = inv_map[1][(i + REDUCE_PLY - 1) / 2];
    v[CAPT_CWIN_RED1] = num;
    v[PAWN_CWIN_RED1] = num;
    for (i = 0; i < REDUCE_PLY_RED1 - DRAW_RULE + REDUCE_PLY - 1; i++) {
      v[WIN_RED + i + 2 + DRAW_RULE - REDUCE_PLY + 1] = inv_map[2][(reduce_cnt[0] + i) / 2];
      v[LOSS_IN_ONE - i - DRAW_RULE + REDUCE_PLY - 1] = inv_map[3][(reduce_cnt[0] + i) / 2];
    }
  } else {
    v[CAPT_CWIN_RED2] = num;
    for (i = 0; i <= REDUCE_PLY_RED2; i++) {
      v[CAPT_CWIN_RED2 + i + 2] = inv_map[2][(reduce_cnt[num_saves - 1] + i) / 2];
      v[LOSS_IN_ONE - i - 1] = inv_map[3][(reduce_cnt[num_saves - 1] + i) / 2];
    }
  }
#endif
#else /* SUICIDE */
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
  NAME(transform_v) = v;
  NAME(transform_tbl) = table;
  run_threaded(NAME(transform_table), work_g, 0);

  v[0] = 0;
  int red_cnt = 0;
#ifndef SUICIDE
#ifndef SHATRANJ
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
    NAME(reconstruct_table_pass)(table, color, k, v);
  }
#else /* SHATRANJ */
  for (k = 0; k < num_saves; k++) {
    if (k == 0) {
      v[255] = inv_map[1][0];
      v[1] = num;
      if (map->ply_accurate_win)
        for (i = 0; i < REDUCE_PLY - 1; i++)
          v[i + 2] = inv_map[0][i];
      else
        for (i = 0; i < REDUCE_PLY - 1; i++)
          v[i + 2] = inv_map[0][i / 2];
      if (map->ply_accurate_loss)
        for (i = 0; i < REDUCE_PLY - 1; i++)
          v[254 - i] = inv_map[1][i];
      else
        for (i = 0; i < REDUCE_PLY - 1; i++)
          v[254 - i] = inv_map[1][i / 2];
      red_cnt = 0;
    } else if (k == 1) {
      if (map->ply_accurate_win)
        for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
          v[i + 1] = inv_map[0][i + REDUCE_PLY - 1];
      else
        for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
          v[i + 1] = inv_map[0][(i + REDUCE_PLY - 1) / 2];
      if (map->ply_accurate_loss)
        for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
          v[255 - i] = inv_map[1][i + REDUCE_PLY - 1];
      else
        for (i = 0; i < DRAW_RULE - REDUCE_PLY + 1; i++)
          v[255 - i] = inv_map[1][(i + REDUCE_PLY - 1) / 2];
      for (; i < REDUCE_PLY_RED1; i += 2) {
        v[1 + (DRAW_RULE - REDUCE_PLY + 1) + (i - DRAW_RULE + REDUCE_PLY - 1) / 2] = inv_map[2][(i - DRAW_RULE + REDUCE_PLY - 1) / 2];
        v[255 - (DRAW_RULE - REDUCE_PLY + 1) - (i - DRAW_RULE + REDUCE_PLY - 1) / 2] = inv_map[3][(i - DRAW_RULE + REDUCE_PLY - 1) / 2];
      }
      red_cnt = REDUCE_PLY_RED1 - (DRAW_RULE - REDUCE_PLY + 1);
    } else {
      for (i = 0; i <= REDUCE_PLY_RED2 + 1; i += 2) {
        v[1 + ((red_cnt & 1) + i) / 2] = inv_map[2][(red_cnt + i) / 2];
        v[255 - ((red_cnt & 1) + i) / 2] = inv_map[3][(red_cnt + i) / 2];
      }
      red_cnt += REDUCE_PLY_RED2;
    }
    NAME(reconstruct_table_pass)(table, color, k, v);
  }
#endif
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
    NAME(reconstruct_table_pass)(table, color, k, v);
  }
#endif

  if (color == 'w') {
    printf("Verifying reconstructed table_w based on collected statistics.\n");
    NAME(verify_stats)(table, total_stats_w, map);
  } else {
    printf("Verifying reconstructed table_b based on collected statistics.\n");
    NAME(verify_stats)(table, total_stats_b, map);
  }
}

void NAME(load_table)(T *table, char color)
{
  FILE *F;
  char name[64];

  sprintf(name, "%s.%c", tablename, color);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(1);
  }

  NAME(read_data)(F, table, size, NULL);

  fclose(F);
}

#undef NAME
