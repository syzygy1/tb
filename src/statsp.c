/*
  Copyright (c) 2011-2016, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static char pc[] = { 0, 'P', 'N', 'B', 'R', 'Q', 'K', 0, 0, 'p', 'n', 'b', 'r', 'q', 'k', 0};

static char fen_buf[128];

void print_fen(uint64_t idx, int wtm)
{
  uint64_t idx2 = idx ^ pw_mask;
  int i, j;
  int n = numpcs;
  int p[MAX_PIECES];
  uint8_t bd[64];
  char *str = fen_buf;

  memset(bd, 0, 64);

  for (i = n - 1; i > 0; i--, idx2 >>= 6)
    bd[p[i] = idx2 & 0x3f] = i + 1;
  bd[p[0] = piv_sq[idx2]] = 1;

  for (i = 56; i >= 0; i -= 8) {
    int cnt = 0;
    for (j = i; j < i + 8; j++)
      if (!bd[j])
        cnt++;
      else {
        if (cnt > 0) {
          *str++ = '0' + cnt;
          cnt = 0;
        }
        *str++ = pc[pt[bd[j]-1]];
      }
    if (cnt) *str++ = '0' + cnt;
    if (i) *str++ = '/';
  }
  *str++ = ' ';
  *str++ = wtm ? 'w' : 'b';
  *str++ = ' ';
  *str++ = '-';
  *str++ = ' ';
  *str++ = '-';
  *str++ = '\n';
  *str = 0;
}

static LOCK_T stats_mutex;

uint64_t found_idx;
uint8_t *find_val_table;
uint8_t find_val_v;

void find_loop(struct thread_data *thread)
{
  uint64_t idx = begin + thread->begin;
  uint64_t end = begin + thread->end;
  uint8_t *table = find_val_table;
  uint8_t v = find_val_v;

  if (idx > found_idx) return;

  for (; idx < end; idx++)
    if (table[idx] == v) break;

  if (idx < end) {
    LOCK(stats_mutex);
    if (idx < found_idx)
      found_idx = idx;
    UNLOCK(stats_mutex);
  }
}

uint64_t find_val(uint8_t *table, uint8_t v, uint64_t *work)
{
  found_idx = UINT64_MAX;
  find_val_table = table;
  find_val_v = v;

  run_threaded(find_loop, work, 0);

  if (found_idx == UINT64_MAX) {
    fprintf(stderr, "find_val: not found!\n");
    exit(EXIT_FAILURE);
  }

  return found_idx;
}

uint8_t *count_stats_table_u8;

void count_stats_u8(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = begin + thread->end;
  uint64_t *stats = thread->stats;
  uint8_t *table = count_stats_table_u8;

  for (idx = begin + thread->begin; idx < end; idx++)
    stats[table[idx]]++;
}

uint16_t *count_stats_table_u16;

void count_stats_u16(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = begin + thread->end;
  uint64_t *stats = thread->stats;
  uint16_t *table = count_stats_table_u16;

  for (idx = begin + thread->begin; idx < end; idx++)
    stats[table[idx]]++;
}

uint64_t *thread_stats = NULL;
int lw_ply = -1;
int lb_ply = -1;
int lcw_ply = -1;
int lcb_ply = -1;
int lw_clr, lb_clr, lcw_clr, lcb_clr;
uint64_t lw_idx, lb_idx, lcw_idx, lcb_idx;
int glw_ply = -1;
int glb_ply = -1;
int glcw_ply = -1;
int glcb_ply = -1;
char glw_fen[128];
char glb_fen[128];
char glcw_fen[128];
char glcb_fen[128];

static int stats_val[];

static void collect_stats_table(uint64_t *total_stats, uint8_t *table, int wtm,
    int phase, int local, uint64_t *work)
{
  int i, j;
  int n;
  int sval;

  sval = (local == 0) ? 0 : stats_val[local - 1];

  for (i = 0; i < MAX_VALS * numthreads; i++)
    thread_stats[i] = 0;

  count_stats_table_u8 = table;
  run_threaded(count_stats_u8, work, 0);

  if (local == 0)
    n = REDUCE_PLY - 2;
  else
    n = REDUCE_PLY_RED;
  if (phase == 1) n += 2;

#ifndef SUICIDE
  for (i = 0; i < numthreads; i++) {
    uint64_t *stats = thread_data[i].stats;
    if (local == 0) {
      total_stats[STAT_CAPT_WIN] += stats[CAPT_WIN];
      total_stats[STAT_CAPT_CWIN] += stats[CAPT_CWIN];
      total_stats[STAT_PAWN_WIN] += stats[PAWN_WIN];
      total_stats[STAT_PAWN_CWIN] += stats[PAWN_CWIN];
      total_stats[STAT_MATE] += stats[MATE];
      for (j = 0; j < DRAW_RULE; j++) {
        total_stats[1 + j] += stats[WIN_IN_ONE + j];
        total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
      }
      for (; j < n; j++) {
        total_stats[1 + j] += stats[WIN_IN_ONE + j + 2];
        total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
      }
      total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
    } else {
      for (j = 0; j < n; j++) {
        total_stats[1 + sval + j] += stats[CAPT_CWIN_RED + j + 2];
        total_stats[STAT_MATE - sval - j - 2] += stats[LOSS_IN_ONE - j - 1];
      }
    }
  }
#else
  for (i = 0; i < numthreads; i++) {
    uint64_t *stats = thread_data[i].stats;
    if (local == 0) {
      total_stats[0] += stats[STALE_WIN];
      total_stats[1] += stats[STALE_WIN + 1];
      for (j = 2; j <= DRAW_RULE + 1; j++)
        total_stats[j] += stats[BASE_WIN + j];
      for (; j < n + 1; j++)
        total_stats[j] += stats[BASE_WIN + j + 2];
      for (j = 0; j < n + 2; j++)
        total_stats[STAT_MATE - j] += stats[BASE_LOSS - j];
      total_stats[STAT_CAPT_WIN] += stats[CAPT_WIN];
      total_stats[STAT_CAPT_CWIN] += stats[CAPT_CWIN];
      total_stats[STAT_CAPT_DRAW] += stats[CAPT_DRAW];
      total_stats[STAT_CAPT_LOSS] += stats[CAPT_LOSS];
      total_stats[STAT_CAPT_CLOSS] += stats[CAPT_CLOSS];
      total_stats[STAT_THREAT_WIN2] += stats[THREAT_WIN2];
      total_stats[STAT_THREAT_WIN1] += stats[THREAT_WIN1];
      total_stats[STAT_THREAT_CWIN2] += stats[THREAT_CWIN2];
      total_stats[STAT_THREAT_CWIN1] += stats[THREAT_CWIN1];
    } else {
      for (j = 0; j < n; j++) {
        total_stats[1 + sval + j] += stats[BASE_CWIN_RED + 1 + j];
        total_stats[STAT_MATE - sval - j - 2] += stats[BASE_CLOSS_RED - 1 - j];
      }
    }
  }
#endif

  if (local == 0) {
    for (i = DRAW_RULE; i >= 0; i--)
      if (total_stats[i]) break;
    if (i >= 0) {
#ifndef SUICIDE
      j = WIN_IN_ONE + i - 1;
#else
      j = (i < 2) ? STALE_WIN + i : BASE_WIN + i;
#endif
      if (wtm) {
        if (i > lw_ply) {
          lw_ply = i;
          lw_clr = 1;
          lw_idx = find_val(table, j, work);
        }
      } else {
        if (i > lb_ply) {
          lb_ply = i;
          lb_clr = 0;
          lb_idx = find_val(table, j, work);
        }
      }
    }
    for (i = DRAW_RULE; i >= 0; i--)
      if (total_stats[STAT_MATE - i]) break;
    if (i >= 0) {
#ifndef SUICIDE
      j = MATE - i;
#else
      j = BASE_LOSS - i;
#endif
      if (wtm) {
        if (i > lb_ply) {
          lb_ply = i;
          lb_clr = 1;
          lb_idx = find_val(table, j, work);
        }
      } else {
        if (i > lw_ply) {
          lw_ply = i;
          lw_clr = 0;
          lw_idx = find_val(table, j, work);
        }
      }
    }
  }

  for (i = MAX_PLY; i > DRAW_RULE; i--)
    if (total_stats[i]) break;
  if (i > DRAW_RULE) {
#ifndef SUICIDE
    if (local == 0)
      j = WIN_IN_ONE + i + 1;
    else
      j = CAPT_CWIN_RED + 1 + i - sval;
#else
    if (local == 0)
      j = (i == DRAW_RULE + 1) ? BASE_WIN + i : BASE_WIN + i + 2;
    else
      j = BASE_CWIN_RED + i - sval;
#endif
    if (wtm) {
      if (i > lcw_ply) {
        lcw_ply = i;
        lcw_clr = 1;
        lcw_idx = find_val(table, j, work);
      }
    } else {
      if (i > lcb_ply) {
        lcb_ply = i;
        lcb_clr = 0;
        lcb_idx = find_val(table, j, work);
      }
    }
  }

  for (i = MAX_PLY; i > DRAW_RULE; i--)
    if (total_stats[STAT_MATE - i]) break;
  if (i > DRAW_RULE) {
#ifndef SUICIDE
    j = MATE - i + sval;
#else
    if (local == 0)
      j = BASE_LOSS - i;
    else
      j = BASE_CLOSS_RED - i + 1 + sval;
#endif
    if (wtm) {
      if (i > lcb_ply) {
        lcb_ply = i;
        lcb_clr = 1;
        lcb_idx = find_val(table, j, work);
      }
    } else {
      if (i > lcw_ply) {
        lcw_ply = i;
        lcw_clr = 0;
        lcw_idx = find_val(table, j, work);
      }
    }
  }

#ifndef SUICIDE
  if (phase == 1)
    for (i = 0; i < numthreads; i++) {
      total_stats[STAT_DRAW] += thread_data[i].stats[UNKNOWN];
      total_stats[STAT_DRAW] += thread_data[i].stats[PAWN_DRAW];
      total_stats[STAT_CAPT_DRAW] += thread_data[i].stats[CAPT_DRAW];
    }
#else
  if (phase == 1)
    for (i = 0; i < numthreads; i++) {
      total_stats[STAT_DRAW] += thread_data[i].stats[UNKNOWN];
      total_stats[STAT_DRAW] += thread_data[i].stats[PAWN_DRAW];
      total_stats[STAT_THREAT_DRAW] += thread_data[i].stats[THREAT_DRAW];
    }
#endif
}

void collect_stats(uint64_t *work, int phase, int local)
{
  int i;

  if (local == num_saves) {
    if (num_saves == 0)
      stats_val[0] = REDUCE_PLY - 2;
    else
      stats_val[num_saves] = stats_val[num_saves - 1] + REDUCE_PLY_RED;
  }

  if (thread_stats == NULL) {
    thread_stats = (uint64_t *)alloc_aligned(8 * MAX_VALS * numthreads, 64);
    for (i = 0; i < numthreads; i++)
      thread_data[i].stats = thread_stats + i * MAX_VALS;
  }

  collect_stats_table(total_stats_w, table_w, 1, phase, local, work);
  collect_stats_table(total_stats_b, table_b, 0, phase, local, work);
}

void print_stats(FILE *F, uint64_t *stats, int wtm)
{
  int i;
  uint64_t sum;

  fprintf(F, "%s to move:\n\n", wtm ? "White" : "Black");

  if (stats[0])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[0], 0);
#ifndef SUICIDE
  if (stats[1] + stats[STAT_PAWN_WIN] + stats[STAT_CAPT_WIN])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[1] + stats[STAT_PAWN_WIN] + stats[STAT_CAPT_WIN], 1);
  i = 2;
#else
  if (stats[1] + stats[STAT_CAPT_WIN] + stats[STAT_THREAT_WIN1])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[1] + stats[STAT_CAPT_WIN] + stats[STAT_THREAT_WIN1], 1);
  if (stats[2] + stats[STAT_THREAT_WIN2])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[2] + stats[STAT_THREAT_WIN2], 2);
  i = 3;
#endif
  for (; i <= DRAW_RULE; i++)
    if (stats[i])
      fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i], i);
#ifndef SUICIDE
  if (stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_PAWN_CWIN])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_PAWN_CWIN], i);
#else
  if (stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_THREAT_CWIN1])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_THREAT_CWIN1], i);
  i++;
  if (stats[i] + stats[STAT_THREAT_CWIN2])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i] + stats[STAT_THREAT_CWIN2], i);
#endif
  for (i++; i <= MAX_PLY; i++)
    if (stats[i])
      fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i], i);

  sum = stats[STAT_CAPT_WIN];
#ifndef SUICIDE
  sum += stats[STAT_PAWN_WIN];
#else
  sum += stats[STAT_THREAT_WIN2] + stats[STAT_THREAT_WIN1];
#endif
  for (i = 0; i <= DRAW_RULE; i++)
    sum += stats[i];
  fprintf(F, "\n%"PRIu64" positions are wins.\n", sum);

  sum = stats[STAT_CAPT_CWIN];
#ifndef SUICIDE
  sum += stats[STAT_PAWN_CWIN];
#else
  sum += stats[STAT_THREAT_CWIN2] + stats[STAT_THREAT_CWIN1];
#endif
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    sum += stats[i];
  if (sum) fprintf(F, "%"PRIu64" positions are cursed wins.\n", sum);

#ifndef SUICIDE
  fprintf(F, "%"PRIu64" positions are draws.\n", stats[STAT_DRAW] + stats[STAT_CAPT_DRAW]);
#else
  fprintf(F, "%"PRIu64" positions are draws.\n", stats[STAT_DRAW] + stats[STAT_CAPT_DRAW] + stats[STAT_THREAT_DRAW]);
#endif

#ifndef SUICIDE
  sum = 0;
#else
  sum = stats[STAT_CAPT_CLOSS];
#endif
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    sum += stats[STAT_MATE - i];
  if (sum) fprintf(F, "%"PRIu64" positions are cursed losses.\n", sum);

#ifndef SUICIDE
  sum = 0;
#else
  sum = stats[STAT_CAPT_LOSS];
#endif
  for (i = 0; i <= DRAW_RULE; i++)
    sum += stats[STAT_MATE - i];
  fprintf(F, "%"PRIu64" positions are losses.\n\n", sum);

#ifndef SUICIDE
  i = 0;
#else
  if (stats[STAT_MATE])
    fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE], 0);
  if (stats[STAT_MATE - 1] + stats[STAT_CAPT_LOSS])
    fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - 1] + stats[STAT_CAPT_LOSS], 1);
  i = 2;
#endif
  for (; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i])
      fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - i], i);
#ifdef SUICIDE
  if (stats[STAT_MATE - i] + stats[STAT_CAPT_CLOSS])
    fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - i] + stats[STAT_CAPT_CLOSS], i);
  i++;
#endif
  for (; i <= MAX_PLY; i++)
    if (stats[STAT_MATE - i])
      fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - i], i);
  fprintf(F, "\n");
}

void print_longest(FILE *F)
{
  if (lw_ply >= 0) {
    fprintf(F, "Longest win for white: %d ply; ", lw_ply);
    print_fen(lw_idx, lw_clr);
    fputs(fen_buf, F);
    if (lw_ply > glw_ply) {
      glw_ply = lw_ply;
      strcpy(glw_fen, fen_buf);
    }
  }
  if (lcw_ply >= 0) {
    fprintf(F, "Longest cursed win for white: %d ply; ", lcw_ply);
    print_fen(lcw_idx, lcw_clr);
    fputs(fen_buf, F);
    if (lcw_ply > glcw_ply) {
      glcw_ply = lcw_ply;
      strcpy(glcw_fen, fen_buf);
    }
  }
  if (lcb_ply >= 0) {
    fprintf(F, "Longest cursed win for black: %d ply; ", lcb_ply);
    print_fen(lcb_idx, lcb_clr);
    fputs(fen_buf, F);
    if (lcb_ply > glcb_ply) {
      glcb_ply = lcb_ply;
      strcpy(glcb_fen, fen_buf);
    }
  }
  if (lb_ply >= 0) {
    fprintf(F, "Longest win for black: %d ply; ", lb_ply);
    print_fen(lb_idx, lb_clr);
    fputs(fen_buf, F);
    if (lb_ply > glb_ply) {
      glb_ply = lb_ply;
      strcpy(glb_fen, fen_buf);
    }
  }
  fprintf(F, "\n");
}

void print_global_longest(FILE *F)
{
  if (glw_ply >= 0) {
    fprintf(F, "Longest win for white: %d ply; ", glw_ply);
    fputs(glw_fen, F);
  }
  if (glcw_ply >= 0) {
    fprintf(F, "Longest cursed win for white: %d ply; ", glcw_ply);
    fputs(glcw_fen, F);
  }
  if (glcb_ply >= 0) {
    fprintf(F, "Longest cursed win for black: %d ply; ", glcb_ply);
    fputs(glcb_fen, F);
  }
  if (glb_ply >= 0) {
    fprintf(F, "Longest win for black: %d ply; ", glb_ply);
    fputs(glb_fen, F);
  }
  fprintf(F, "\n");
}
