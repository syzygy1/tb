/*
  Copyright (c) 2011-2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

static char pc[] = { 0, 'P', 'N', 'B', 'R', 'Q', 'K', 0, 0, 'p', 'n', 'b', 'r', 'q', 'k', 0};

static void print_fen(FILE *F, uint64_t idx, int wtm, int switched)
{
  uint64_t idx2 = idx;
  int i, j;
  int n = numpcs;
  uint8_t bd[64];

  memset(bd, 0, 64);

#ifndef SMALL
  for (i = n - 1; i > 0; i--, idx2 >>= 6)
    bd[idx2 & 0x3f] = i + 1;
  bd[inv_tri0x40[idx2]] = 1;
#else
  for (i = n - 1; i > 1; i--, idx2 >>= 6)
    bd[idx2 & 0x3f] = i + 1;
  bd[KK_inv[idx2][0]] = 1;
  bd[KK_inv[idx2][1]] = 2;
#endif

  for (i = 56; i >= 0; i -= 8) {
    int cnt = 0;
    for (j = i; j < i + 8; j++)
      if (!bd[j])
        cnt++;
      else {
        if (cnt > 0) {
          fprintf(F, "%c", '0' + cnt);
          cnt = 0;
        }
        fprintf(F, "%c", pc[pt[bd[j]-1] ^ switched]);
      }
    if (cnt)
      fprintf(F, "%c", '0' + cnt);
    if (i) fprintf(F, "/");
  }
  if (switched) wtm ^= 1;
  fprintf(F, " %c - -\n", wtm ? 'w' : 'b');
}

static LOCK_T stats_mutex;

uint64_t found_idx;
uint8_t *find_val_table;
uint8_t find_val_v;

static void find_loop(struct thread_data *thread)
{
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  uint8_t *table = find_val_table;
  uint8_t v = find_val_v;

  if (idx > found_idx) return;

  for (; idx < end; idx++)
    if (table[idx] == v) break;

  if (idx < end) {
    LOCK(stats_mutex);
    if (idx < end && idx < found_idx)
      found_idx = idx;
    UNLOCK(stats_mutex);
  }
}

static uint64_t find_val(uint8_t *table, uint8_t v)
{
  found_idx = UINT64_MAX;
  find_val_table = table;
  find_val_v = v;

  run_threaded(find_loop, work_g, 0);

  if (found_idx == UINT64_MAX) {
    fprintf(stderr, "find_val: not found!\n");
    exit(EXIT_FAILURE);
  }

  return found_idx;
}

uint8_t *count_stats_table_u8;

static void count_stats_u8(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;
  uint64_t *stats = thread->stats;
  uint8_t *table = count_stats_table_u8;

  if (end <= diagonal) {
    for (idx = thread->begin; idx < end; idx++)
      stats[table[idx]] += 2;
  } else {
    for (idx = thread->begin; idx < diagonal; idx++)
      stats[table[idx]] += 2;
    for (; idx < end; idx++) {
      uint64_t idx2 = MIRROR_A1H8(idx) | (idx & mask[0]);
      if (idx == idx2)
        stats[table[idx]] += 2;
      else
        stats[table[idx]]++;
    }
  }
}

uint16_t *count_stats_table_u16;

static void count_stats_u16(struct thread_data *thread)
{
  uint64_t idx;
  uint64_t end = thread->end;
  uint64_t *stats = thread->stats;
  uint16_t *table = count_stats_table_u16;

  if (end <= diagonal) {
    for (idx = thread->begin; idx < end; idx++)
      stats[table[idx]] += 2;
  } else {
    for (idx = thread->begin; idx < diagonal; idx++)
      stats[table[idx]] += 2;
    for (; idx < end; idx++) {
      uint64_t idx2 = MIRROR_A1H8(idx) | (idx & mask[0]);
      if (idx == idx2)
        stats[table[idx]] += 2;
      else
        stats[table[idx]]++;
    }
  }
}

uint64_t *thread_stats = NULL;
static int stats_val = 0;
static int lw_ply = -1;
static int lb_ply = -1;
static int lcw_ply = -1;
static int lcb_ply = -1;
static int lw_clr, lb_clr, lcw_clr, lcb_clr;
static uint64_t lw_idx, lb_idx, lcw_idx, lcb_idx;

static void collect_stats_table(uint64_t *total_stats, uint8_t *table, int wtm,
    int phase)
{
  int i, j, n;

  for (i = 0; i < MAX_VALS * numthreads; i++)
    thread_stats[i] = 0;

  count_stats_table_u8 = table;
  run_threaded(count_stats_u8, work_g, 0);

  if (num_saves == 0)
    n = REDUCE_PLY - 2;
  else
    n = REDUCE_PLY_RED;
  if (phase == 1) n += 2;

#ifndef SUICIDE
  for (i = 0; i < numthreads; i++) {
    uint64_t *stats = thread_data[i].stats;
    if (num_saves == 0) {
      total_stats[STAT_CAPT_WIN] += stats[CAPT_WIN];
      total_stats[STAT_CAPT_CWIN] += stats[CAPT_CWIN];
      total_stats[STAT_MATE] += stats[MATE];
      for (j = 0; j < DRAW_RULE; j++) {
        total_stats[1 + j] += stats[WIN_IN_ONE + j];
        total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
      }
      for (; j < n; j++) {
        total_stats[1 + j] += stats[WIN_IN_ONE + j + 1];
        total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
      }
      total_stats[STAT_MATE - 1 - j] += stats[LOSS_IN_ONE - j];
    } else {
      for (j = 0; j < n; j++) {
        total_stats[1 + stats_val + j] += stats[CAPT_CWIN_RED + j + 2];
        total_stats[STAT_MATE - 1 - stats_val - j - 1] += stats[LOSS_IN_ONE - j - 1];
      }
    }
  }
#else
  for (i = 0; i < numthreads; i++) {
    uint64_t *stats = thread_data[i].stats;
    if (num_saves == 0) {
      total_stats[STAT_CAPT_WIN] += stats[CAPT_WIN];
      total_stats[STAT_THREAT_WIN] += stats[THREAT_WIN];
      total_stats[STAT_CAPT_CWIN] += stats[CAPT_CWIN];
      total_stats[STAT_THREAT_CWIN1] += stats[THREAT_CWIN1];
      total_stats[STAT_THREAT_CWIN2] += stats[THREAT_CWIN2];
      total_stats[STAT_CAPT_LOSS] += stats[CAPT_LOSS];
      total_stats[STAT_CAPT_CLOSS] += stats[CAPT_CLOSS];
      for (j = 3; j <= DRAW_RULE + 1; j++)
        total_stats[j] += stats[BASE_WIN + j];
      for (; j < n + 1; j++)
        total_stats[j] += stats[BASE_WIN + j + 2];
      for (j = 2; j < n + 2; j++)
        total_stats[STAT_MATE - j] += stats[BASE_LOSS - j];
    } else {
      for (j = 0; j < n; j++) {
        total_stats[1 + stats_val + j] += stats[BASE_WIN + j + 6];
        total_stats[STAT_MATE - 2 - stats_val - j] += stats[BASE_LOSS - j - 4];
      }
    }
  }
#endif

  if (num_saves == 0) {
    for (i = DRAW_RULE; i >= MIN_PLY_WIN; i--)
      if (total_stats[i]) break;
    if (i >= MIN_PLY_WIN) {
#ifndef SUICIDE
      j = CAPT_WIN + i;
#else
      j = BASE_WIN + i;
#endif
      if (wtm) {
        if (i > lw_ply) {
          lw_ply = i;
          lw_clr = 1;
          lw_idx = find_val(table, j);
        }
      } else {
        if (i > lb_ply) {
          lb_ply = i;
          lb_clr = 0;
          lb_idx = find_val(table, j);
        }
      }
    }
    for (i = DRAW_RULE; i >= MIN_PLY_LOSS; i--)
      if (total_stats[STAT_MATE - i]) break;
    if (i >= MIN_PLY_LOSS) {
#ifndef SUICIDE
      j = MATE - i;
#else
      j = BASE_LOSS - i;
#endif
      if (wtm) {
        if (i > lb_ply) {
          lb_ply = i;
          lb_clr = 1;
          lb_idx = find_val(table, j);
        }
      } else {
        if (i > lw_ply) {
          lw_ply = i;
          lw_clr = 0;
          lw_idx = find_val(table, j);
        }
      }
    }
  }

  for (i = MAX_PLY; i > DRAW_RULE; i--)
    if (total_stats[i]) break;
  if (i > DRAW_RULE) {
#ifndef SUICIDE
    if (num_saves == 0)
      j = WIN_IN_ONE + i;
    else
      j = CAPT_CWIN_RED + 1 + i - stats_val;
#else
    if (num_saves == 0)
      j = (i == DRAW_RULE + 1) ? BASE_WIN + i : BASE_WIN + i + 2;
    else
      j = BASE_WIN + i + 5 - stats_val;
#endif
    if (wtm) {
      if (i > lcw_ply) {
        lcw_ply = i;
        lcw_clr = 1;
        lcw_idx = find_val(table, j);
      }
    } else {
      if (i > lcb_ply) {
        lcb_ply = i;
        lcb_clr = 0;
        lcb_idx = find_val(table, j);
      }
    }
  }

  for (i = MAX_PLY; i > DRAW_RULE; i--)
    if (total_stats[STAT_MATE - i]) break;
  if (i > DRAW_RULE) {
#ifndef SUICIDE
    j = MATE - i + stats_val;
#else
    j = (num_saves == 0) ? BASE_LOSS - i : BASE_LOSS - i - 2 + stats_val;
#endif
    if (wtm) {
      if (i > lcb_ply) {
        lcb_ply = i;
        lcb_clr = 1;
        lcb_idx = find_val(table, j);
      }
    } else {
      if (i > lcw_ply) {
        lcw_ply = i;
        lcw_clr = 0;
        lcw_idx = find_val(table, j);
      }
    }
  }

  if (phase == 1)
    for (i = 0; i < numthreads; i++) {
      total_stats[STAT_DRAW] += thread_data[i].stats[UNKNOWN];
      total_stats[STAT_CAPT_DRAW] += thread_data[i].stats[CAPT_DRAW];
#ifdef SUICIDE
      total_stats[STAT_THREAT_DRAW] += thread_data[i].stats[THREAT_DRAW];
#endif
    }
}

static void collect_stats(int phase)
{
  int i;

  if (thread_stats == NULL) {
    thread_stats = (uint64_t *)alloc_aligned(8 * MAX_VALS * numthreads, 64);
    for (i = 0; i < numthreads; i++)
      thread_data[i].stats = thread_stats + i * MAX_VALS;

    for (i = 0; i < MAX_STATS; i++)
      total_stats_w[i] = total_stats_b[i] = 0;
  }

  collect_stats_table(total_stats_w, table_w, 1, phase);
  collect_stats_table(total_stats_b, table_b, 0, phase);

  if (num_saves == 0)
    stats_val = REDUCE_PLY - 2;
  else
    stats_val += REDUCE_PLY_RED;
}

static void print_stats(FILE *F, uint64_t *stats, int wtm)
{
  int i;
  uint64_t sum;

  fprintf(F, "%s to move:\n\n", wtm ? "White" : "Black");

  if (stats[0])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[0] >> 1, 0);
#ifndef SUICIDE
  if (stats[1] + stats[STAT_CAPT_WIN])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", (stats[1] + stats[STAT_CAPT_WIN]) >> 1, 1);
#else
  if (stats[1])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[STAT_CAPT_WIN] >> 1, 1);
#endif
  for (i = 2; i <= DRAW_RULE; i++)
    if (stats[i])
      fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i] >> 1, i);
#ifndef SUICIDE
  if (stats[i] + stats[510])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", (stats[i] + stats[STAT_CAPT_CWIN]) >> 1, i);
  i++;
#else
  if (stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_THREAT_CWIN1])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", (stats[i] + stats[STAT_CAPT_CWIN] + stats[STAT_THREAT_CWIN1]) >> 1, i);
  i++;
  if (stats[i] + stats[STAT_THREAT_CWIN2])
    fprintf(F, "%"PRIu64" positions win in %d ply.\n", (stats[i] + stats[STAT_THREAT_CWIN2]) >> 1, i);
  i++;
#endif
  for (; i <= MAX_PLY; i++)
    if (stats[i])
      fprintf(F, "%"PRIu64" positions win in %d ply.\n", stats[i] >> 1, i);

#ifndef SUICIDE
  sum = stats[STAT_CAPT_WIN];
#else
  sum = 0;
#endif
  for (i = 0; i <= DRAW_RULE; i++)
    sum += stats[i];
  fprintf(F, "\n%"PRIu64" positions are wins.\n", sum >> 1);

#ifndef SUICIDE
  sum = stats[STAT_CAPT_CWIN];
#else
  sum = stats[STAT_CAPT_CWIN] + stats[STAT_THREAT_CWIN1] + stats[STAT_THREAT_CWIN2];
#endif
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    sum += stats[i];
  if (sum) fprintf(F, "%"PRIu64" positions are cursed wins.\n", sum >> 1);

#ifndef SUICIDE
  fprintf(F, "%"PRIu64" positions are draws.\n", (stats[STAT_DRAW] + stats[STAT_CAPT_DRAW]) >> 1);
#else
  fprintf(F, "%"PRIu64" positions are draws.\n", (stats[STAT_DRAW] + stats[STAT_CAPT_DRAW] + stats[STAT_THREAT_DRAW]) >> 1);
#endif

#ifndef SUICIDE
  sum = 0;
#else
  sum = stats[STAT_CAPT_CLOSS];
#endif
  for (i = DRAW_RULE + 1; i <= MAX_PLY; i++)
    sum += stats[STAT_MATE - i];
  if (sum) fprintf(F, "%"PRIu64" positions are cursed losses.\n", sum >> 1);

  sum = 0;
  for (i = 0; i <= DRAW_RULE; i++)
    sum += stats[STAT_MATE - i];
  fprintf(F, "%"PRIu64" positions are losses.\n\n", sum >> 1);

  for (i = 0; i <= DRAW_RULE; i++)
    if (stats[STAT_MATE - i])
      fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - i] >> 1, i);
#ifdef SUICIDE
  if (stats[STAT_MATE - i] + stats[STAT_CAPT_CLOSS])
    fprintf(F, "%"PRIu64" positions lose in %d ply.\n", (stats[STAT_MATE - i] + stats[STAT_CAPT_CLOSS]) >> 1, i);
  i++;
#endif
  for (; i <= MAX_PLY; i++)
    if (stats[STAT_MATE - i])
      fprintf(F, "%"PRIu64" positions lose in %d ply.\n", stats[STAT_MATE - i] >> 1, i);
  fprintf(F, "\n");
}

static void print_longest(FILE *F, int switched)
{
  if (!switched) {
    if (lw_ply >= 0) {
      fprintf(F, "Longest win for white: %d ply; ", lw_ply);
      print_fen(F, lw_idx, lw_clr, 0);
    }
    if (lcw_ply >= 0) {
      fprintf(F, "Longest cursed win for white: %d ply; ", lcw_ply);
      print_fen(F, lcw_idx, lcw_clr, 0);
    }
    if (lcb_ply >= 0) {
      fprintf(F, "Longest cursed win for black: %d ply; ", lcb_ply);
      print_fen(F, lcb_idx, lcb_clr, 0);
    }
    if (lb_ply >= 0) {
      fprintf(F, "Longest win for black: %d ply; ", lb_ply);
      print_fen(F, lb_idx, lb_clr, 0);
    }
  } else {
    if (lcb_ply >= 0) {
      fprintf(F, "Longest cursed win for white: %d ply; ", lcb_ply);
      print_fen(F, lcb_idx, lcb_clr, 8);
    }
    if (lb_ply >= 0) {
      fprintf(F, "Longest win for white: %d ply; ", lb_ply);
      print_fen(F, lb_idx, lb_clr, 8);
    }
    if (lw_ply >= 0) {
      fprintf(F, "Longest win for black: %d ply; ", lw_ply);
      print_fen(F, lw_idx, lw_clr, 8);
    }
    if (lcw_ply >= 0) {
      fprintf(F, "Longest cursed win for black: %d ply; ", lcw_ply);
      print_fen(F, lcw_idx, lcw_clr, 8);
    }
  }
  fprintf(F, "\n");
}
