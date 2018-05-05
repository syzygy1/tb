/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "checksum.h"
#include "compress.h"
#include "defs.h"
#include "huffman.h"
#include "probe.h"
#include "threads.h"
#include "types.h"
#include "util.h"

#define MAX_NEW 250

static struct dtz_map *dtz_map;

extern int split;
extern int numpcs;
extern int numpawns;

int blockbits = 8;

uint8_t pieces[TBPIECES];

int freq[MAXSYMB];
int low, high;

char name[64];

static void calc_symbol_tweaks(struct HuffCode *restrict c);
struct HuffCode *construct_pairs_wdl(uint8_t *restrict data, uint64_t size,
    int minfreq, int maxsymbols);
static void compress_data_single_valued(struct tb_handle *F, int num);

struct Symbol {
  uint16_t pattern[2];
  union {
    struct {
      uint8_t first, second;
    };
    uint16_t sym;
  };
  int len;
};

static struct Symbol symtable[MAXSYMB];
static int64_t pairfreq[MAXSYMB][MAXSYMB];
static uint16_t symcode[256][256];
static int replace[MAXSYMB];

static int num_syms;
static uint64_t num_blocks, real_num_blocks;
static uint32_t num_indices;
static uint16_t *sizetable;
static uint8_t *indextable;
static int blocksize;
static int idxbits;
static int num_vals;

static int num_ctrl, cur_ctrl_idx;

struct {
  int64_t freq; // maybe remove later; currently still used for dtz
  int s1, s2;
  int sym;
} newpairs[MAX_NEW + 25];

struct {
  int64_t freq;
  int s1, s2;
} paircands[MAX_NEW];

static uint8_t newtest[MAXSYMB][MAXSYMB];
static uint64_t (*countfirst)[MAX_NEW][MAXSYMB];
static uint64_t (*countsecond)[MAX_NEW][MAXSYMB];

extern int total_work;
static uint64_t *restrict work = NULL, *restrict work_adj = NULL;

static struct {
  void *data;
  uint64_t size;
} compress_state;

static uint8_t pairfirst[4][MAXSYMB], pairsecond[4][MAXSYMB];

static int t1[4][4];
static int t2[4][4];
static int64_t dcfreq[4][4];
static int wdl_vals[5];
static uint8_t wdl_flags;
int compress_type;
static uint8_t dc_to_val[4];

static int64_t (*countfreq)[9][16];
static int64_t (*countfreq_dtz_u8)[255][255];
static int64_t (*countfreq_dtz_u16)[MAX_VALS][MAX_VALS];

#define read_symbol(x) _Generic((x), \
  u8: symcode[x][(&(x))[1]], \
  u16: x \
);

#define write_symbol(x, y) _Generic((x), \
  u8: ((x) = (y)->first, (&(x))[1] = (y)->second ), \
  u16: (x) = (y)->sym \
);

#define T u8
#include "compress_tmpl.c"
#undef T

#define T u16
#include "compress_tmpl.c"
#undef T

void compress_alloc_wdl()
{
  countfirst = malloc(numthreads * sizeof(*countfirst));
  countsecond = malloc(numthreads * sizeof(*countsecond));
  countfreq = malloc(numthreads * sizeof(*countfreq));
}

void compress_init_wdl(int *vals, int flags)
{
  int i;

  for (i = 0; i < 5; i++)
    wdl_vals[i] = vals[i];

  num_vals = 0;
  for (i = 0; i < 5; i++)
    if (wdl_vals[i])
      num_vals++;

  wdl_flags = flags;

  dc_to_val[0] = wdl_vals[0] ? 0 : 1;
  dc_to_val[1] = wdl_vals[2] ? 2 : dc_to_val[0];
  dc_to_val[2] = wdl_vals[2] ? 2 : (wdl_vals[0] ? 0 : (wdl_vals[3] ? 3 : 1));
  dc_to_val[3] = wdl_vals[4] ? 4 : dc_to_val[2];

  compress_type = 0;
  if (num_vals == 1)
    compress_type = 1;
}

static void count_pairs_wdl(struct thread_data *thread)
{
  int s1, s2;
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  uint8_t *restrict data = compress_state.data;
  int t = thread->thread;

  if (idx == 0) idx = 1;
  s1 = data[idx - 1];
  for (; idx < end; idx++) {
    s2 = data[idx];
    countfreq[t][s1][s2]++;
    s1 = s2;
  }
}

#define max(a,b) (((a) > (b)) ? (a) : (b))

static void remove_wdl_worker(struct thread_data *thread)
{
  uint64_t idx, idx2;
  uint64_t end = thread->end;
  uint8_t *restrict data = compress_state.data;
  uint64_t size = compress_state.size;
  int s, t;

  static uint8_t dc_map[5][16] = {
    { 1, 0, 0, 0, 0, 1, 1, 1, 1 },
    { 0, 1, 0, 0, 0, 1, 1, 1, 1 },
    { 0, 0, 1, 0, 0, 0, 1, 1, 1 },
    { 0, 0, 0, 1, 0, 0, 0, 1, 1 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1 }
  };

  idx = thread->begin;
  while (idx < end) {
    for (; idx < end; idx++)
      if (data[idx] >= 5) break;
    if (idx == end) break;
    // now idx points to a first dontcare of possibly more
    s = data[idx];
    for (idx2 = idx + 1; idx2 < end; idx2++) {
      if (data[idx2] < 5) break;
      if (data[idx2] < s)
        s = data[idx2];
    }
    int f = 0;
    if (idx > 0 && dc_map[data[idx - 1]][s]) {
      t = data[idx - 1];
      f = 1;
      if (idx > 1 && data[idx - 2] == t)
        f = 2;
    }
    if (idx2 < size && dc_map[data[idx2]][s] && f != 2) {
      if (!f || (idx2 < size - 1 && dc_map[data[idx2]][data[idx2 + 1]]))
        t = data[idx2];
      f = 1;
    }
    if (f)
      for (; idx < idx2; idx++)
        data[idx] = t;
    else if (idx2 - idx > 5) {
      s = dc_to_val[s - 5];
      for (; idx < idx2; idx++)
        data[idx] = s;
    }
    idx = idx2;
  }
}

void adjust_work_dontcares_wdl(uint64_t *restrict work1,
    uint64_t *restrict work2)
{
  uint64_t idx;
  uint64_t end = work1[total_work];
  uint8_t *restrict data = compress_state.data;
  int i;

  work2[0] = work1[0];
  for (i = 1; i < total_work; i++) {
    idx = work1[i];
    if (idx < work2[i - 1]) {
      work2[i] = work2[i - 1];
      continue;
    }
    while (idx < end && (data[idx -1] >= 5 || data[idx] >= 5))
      idx++;
    work2[i] = idx;
  }
  work2[total_work] = work1[total_work];
}

static int d[5] = { 5, 5, 6, 7, 8 };

struct HuffCode *construct_pairs_wdl(uint8_t *restrict data, uint64_t size,
    int minfreq, int maxsymbols)
{
  int i, j, k, l;
  int s1, s2;

  if (!work) {
    work = alloc_work(total_work);
    work_adj = alloc_work(total_work);
  }

  compress_state.data = data;
  compress_state.size = size;
  fill_work(total_work, size, 0, work);

  for (i = 0; i < 9; i++) {
    symtable[i].pattern[0] = i;
    symtable[i].len = 1;
    for (j = 0; j < 256; j++)
      symcode[i][j] = i;
  }

  adjust_work_dontcares_wdl(work, work_adj);
  run_threaded(remove_wdl_worker, work_adj, 0);

  num_syms = 9;

  num_ctrl = num_syms - 1;
  cur_ctrl_idx = 0;

  if (maxsymbols > 0) {
    maxsymbols += num_syms;
    if (maxsymbols > MAXSYMB)
      maxsymbols = MAXSYMB;
  } else {
    maxsymbols = MAXSYMB + 1 - num_vals;
  }

  // FIXME: do this only once
  for (i = 0; i < MAXSYMB; i++)
    for (j = 0; j < MAXSYMB; j++)
      newtest[i][j] = 0;

  int num, t;

  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      pairfreq[i][j] = 0;

  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        countfreq[t][i][j] = 0;
  run_threaded(count_pairs_wdl, work, 0);
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        pairfreq[i][j] += countfreq[t][i][j];

  while (num_syms < maxsymbols) {

    num = 0;
    for (i = 0; i < 5; i++) {
      if (!wdl_vals[i]) continue;
      for (j = 0; j < 5; j++) {
        if (!wdl_vals[j]) continue;
        int64_t pf = pairfreq[i][j];
        for (k = d[i]; k < 9; k++)
          pf += pairfreq[k][j];
        for (l = d[j]; l < 9; l++)
          pf += pairfreq[i][l];
        for (k = d[i]; k < 9; k++)
          for (l = d[j]; l < 9; l++)
            pf += pairfreq[k][l];
        if (pf >= minfreq && (num < MAX_NEW || pf > paircands[MAX_NEW - 1].freq)) {
          for (k = 0; k < num; k++)
            if (paircands[k].freq < pf) break;
          if (num < MAX_NEW) num++;
          for (l = num - 1; l > k; l--)
            paircands[l] = paircands[l - 1];
          paircands[k].freq = pf;
          paircands[k].s1 = i;
          paircands[k].s2 = j;
        }
      }
    }

    for (i = 0; i < 5; i++) {
      if (!wdl_vals[i]) continue;
      for (j = 9; j < num_syms; j++) {
        if (1 + symtable[j].len > 256) continue;
        int64_t pf = pairfreq[i][j];
        for (k = d[i]; k < 9; k++)
          pf += pairfreq[k][j];
        if (pf >= minfreq && (num < MAX_NEW || pf > paircands[MAX_NEW - 1].freq)) {
          for (k = 0; k < num; k++)
            if (paircands[k].freq < pf) break;
          if (num < MAX_NEW - 1) num++;
          for (l = num - 1; l > k; l--)
            paircands[l] = paircands[l - 1];
          paircands[k].freq = pf;
          paircands[k].s1 = i;
          paircands[k].s2 = j;
        }
      }
    }

    for (i = 9; i < num_syms; i++) {
      if (symtable[i].len + 1 > 256) continue;
      for (j = 0; j < 5; j++) {
        if (!wdl_vals[j]) continue;
        int64_t pf = pairfreq[i][j];
        for (l = d[j]; l < 9; l++)
          pf += pairfreq[i][l];
        if (pf >= minfreq && (num < MAX_NEW || pf > paircands[MAX_NEW - 1].freq)) {
          for (k = 0; k < num; k++)
            if (paircands[k].freq < pf) break;
          if (num < MAX_NEW - 1) num++;
          for (l = num - 1; l > k; l--)
            paircands[l] = paircands[l - 1];
          paircands[k].freq = pf;
          paircands[k].s1 = i;
          paircands[k].s2 = j;
        }
      }
    }

    for (i = 9; i < num_syms; i++)
      for (j = 9; j < num_syms; j++) {
        int64_t pf = pairfreq[i][j];
        if (pf >= minfreq && (num < MAX_NEW || pf > paircands[MAX_NEW - 1].freq) && (symtable[i].len + symtable[j].len <= 256)) {
          for (k = 0; k < num; k++)
            if (paircands[k].freq < pf) break;
          if (num < MAX_NEW - 1) num++;
          for (l = num - 1; l > k; l--)
            paircands[l] = paircands[l - 1];
          paircands[k].freq = pf;
          paircands[k].s1 = i;
          paircands[k].s2 = j;
        }
      }

    // estimate the number of skipped pairs to make sure they'll be
    // considered in the next iteration (before running out of symbols)
    int skipped = 0;
    int64_t max = paircands[0].freq;
    int num2 = 0; /* count new symbols */
    int num3 = 0; /* count new pairs */
    int m;
    for (i = 0; i < num && num_syms + skipped < maxsymbols; i++) {
      while (max > paircands[i].freq * 2) {
        skipped += i;
        max /= 2;
      }
      s1 = paircands[i].s1;
      s2 = paircands[i].s2;
      // check for conflicts with more frequent candidates
      for (j = 0; j < i; j++) {
        int t1 = paircands[j].s1;
        int t2 = paircands[j].s2;
        if (s2 == t1) break;
        if (s1 == t2) break;
        if (s2 < 5 && t1 < 5) {
          for (k = max(d[s2], d[t1]); k < 9; k++)
            if (pairfreq[s1][k] && pairfreq[k][t2]) goto lab;
          if (s1 < 5)
            for (l = d[s1]; l < 9; l++)
              for (k = max(d[s2], d[t1]); k < 9; k++)
                if (pairfreq[l][k] && pairfreq[k][t2]) goto lab;
          if (t2 < 5)
            for (l = d[t2]; l < 9; l++)
              for (k = max(d[s2], d[t1]); k < 9; k++)
                if (pairfreq[s1][k] && pairfreq[k][l]) goto lab;
          if (s1 < 5 && t2 < 5)
            for (l = d[s1]; l < 9; l++)
              for (m = d[t2]; m < 9; m++)
                for (k = max(d[s2], d[t1]); k < 9; k++)
                  if (pairfreq[l][k] && pairfreq[k][m]) goto lab;
        }
        if (t2 < 5 && s1 < 5) {
          for (k = max(d[t2], d[s1]); k < 9; k++)
            if (pairfreq[t1][k] && pairfreq[k][s2]) goto lab;
          if (t1 < 5)
            for (l = d[t1]; l < 9; l++)
              for (k = max(d[t2], d[s1]); k < 9; k++)
                if (pairfreq[l][k] && pairfreq[k][s2]) goto lab;
          if (s2 < 5)
            for (l = d[s2]; l < 9; l++)
              for (k = max(d[t2], d[s1]); k < 9; k++)
                if (pairfreq[t1][k] && pairfreq[k][l]) goto lab;
          if (t1 < 5 && s2 < 5)
            for (l = d[t1]; l < 9; l++)
              for (m = d[s2]; m < 9; m++)
                for (k = max(d[t2], d[s1]); k < 9; k++)
                  if (pairfreq[l][k] && pairfreq[k][m]) goto lab;
        }
        if (s1 < 5 && t1 < 5) {
          if (s2 == t2)
            for (k = max(d[s1], d[t1]); k < 9; k++)
              if (pairfreq[k][s2]) goto lab;
          if (s2 < 5 && t2 < 5)
            for (k = max(d[s1], d[t1]); k < 9; k++)
              for (l = max(d[s2], d[t2]); l < 9; l++)
                if (pairfreq[k][l]) goto lab;
        }
        if (s2 < 5 && t2 < 5)
          if (s1 == t1)
            for (k = max(d[s2], d[t2]); k < 9; k++)
              if (pairfreq[s1][k]) goto lab;
      }
lab:
      if (j == i) { // no conflict
        int tmp = num3;
        newpairs[tmp].sym = num_syms;
        newpairs[tmp].s1 = s1;
        newpairs[tmp].s2 = s2;
        tmp++;
        if (s1 < 5)
          for (k = d[s1]; k < 9; k++)
            if (pairfreq[k][s2]) {
              newpairs[tmp].sym = num_syms;
              newpairs[tmp].s1 = k;
              newpairs[tmp].s2 = s2;
              tmp++;
            }
        if (s2 < 5)
          for (l = d[s2]; l < 9; l++)
            if (pairfreq[s1][l]) {
              newpairs[tmp].sym = num_syms;
              newpairs[tmp].s1 = s1;
              newpairs[tmp].s2 = l;
              tmp++;
            }
        if (s1 < 5 && s2 < 5)
          for (k = d[s1]; k < 9; k++)
            for (l = d[s2]; l < 9; l++)
              if (pairfreq[k][l]) {
                newpairs[tmp].sym = num_syms;
                newpairs[tmp].s1 = k;
                newpairs[tmp].s2 = l;
                tmp++;
              }
        if (tmp > MAX_NEW) break;
        num3 = tmp;
        num_syms++;
        num2++;
      } else
        skipped++;
    }

    num = num2;
    if (num == 0) break;

    for (i = 0; i < num3;) {
      k = newpairs[i].sym;
      symtable[k].len = symtable[newpairs[i].s1].len + symtable[newpairs[i].s2].len;
      symtable[k].pattern[0] = newpairs[i].s1;
      symtable[k].pattern[1] = newpairs[i].s2;
      if (!cur_ctrl_idx)
        num_ctrl++;
      if (num_ctrl == 256) break;
      symtable[k].first = num_ctrl;
      symtable[k].second = cur_ctrl_idx;
      symcode[num_ctrl][cur_ctrl_idx] = k;
      for (i++; i < num3 && newpairs[i].sym == k; i++);
      cur_ctrl_idx++;
      if (cur_ctrl_idx == 256) cur_ctrl_idx = 0;
    }

    for (i = 0; i < num_syms - num; i++)
      for (j = num_syms - num; j < num_syms; j++)
        pairfreq[i][j] = 0;
    for (; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        pairfreq[i][j] = 0;

    for (i = 0; i < num3; i++)
      newtest[newpairs[i].s1][newpairs[i].s2] = i + 1;

// thread this later
    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num3; i++)
        for (j = 0; j < num_syms; j++) {
          countfirst[t][i][j] = 0;
          countsecond[t][i][j] = 0;
        }

    adjust_work_replace_u8(work);
    run_threaded(replace_pairs_u8, work, 0);

    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num3; i++)
        for (j = 0; j < num_syms; j++) {
          pairfreq[j][newpairs[i].s1] -= countfirst[t][i][j];
          pairfreq[j][newpairs[i].sym] += countfirst[t][i][j];
          pairfreq[newpairs[i].s2][j] -= countsecond[t][i][j];
          pairfreq[newpairs[i].sym][j] += countsecond[t][i][j];
        }

    for (i = 0; i < num3; i++) {
      pairfreq[newpairs[i].s1][newpairs[i].s2] = 0;
      newtest[newpairs[i].s1][newpairs[i].s2] = 0;
    }

  }

  struct HuffCode *restrict c = setup_code_u8(data, size);

  // map remaining don't cares to a value
  for (k = 5; k < 9; k++)
    if (c->freq[k] > 0) {
      for (i = 0; i < 5; i++)
        if (wdl_vals[i]) break;
      for (j = i + 1; j < 5; j++)
        if (d[j] <= k && c->freq[j] > c->freq[i])
          i = j;
      symcode[k][0] = i;
      c->freq[i] += c->freq[k];
      c->freq[k] = 0;
    }

  // remove unused symbols
  uint16_t map[num_syms];
  for (i = 0, k = 0; i < 5; i++)
    if (wdl_vals[i])
      map[i] = k++;
  for (i = 9; i < num_syms; i++)
    map[i] = k++;
  for (i = 0, k = 0; i < 5; i++)
    if (wdl_vals[i]) {
      symtable[k] = symtable[i];
      for (j = 0; j < 256; j++)
        symcode[i][j] = k;
      for (l = 5; l < 9; l++)
        if (symcode[l][0] == i)
          for (j = 0; j < 256; j++)
            symcode[l][j] = k;
      c->freq[k] = c->freq[i];
      k++;
    }
  for (i = 9; i < num_syms; i++) {
    symtable[k] = symtable[i];
    symcode[symtable[k].first][symtable[k].second] = k;
    symtable[k].pattern[0] = map[symtable[k].pattern[0]];
    symtable[k].pattern[1] = map[symtable[k].pattern[1]];
    c->freq[k] = c->freq[i];
    k++;
  }
  num_syms = k;

  create_code(c, num_syms);

  return c;
}

struct HuffCode *construct_pairs_u8(u8 *data, uint64_t size, int minfreq,
    int maxsymbols, int wdl)
{
  if (wdl)
    return construct_pairs_wdl(data, size, minfreq, maxsymbols);
  else
    return construct_pairs_dtz_u8(data, size, minfreq, maxsymbols);
}

struct HuffCode *construct_pairs_u16(u16 *data, uint64_t size, int minfreq,
    int maxsymbols, int wdl)
{
  assert(!wdl);
  return construct_pairs_dtz_u16(data, size, minfreq, maxsymbols);
}

void calc_symbol_tweaks(struct HuffCode *restrict c)
{
  int i, l, s;

  // first find longer symbols with shorter code

  for (i = 0; i < num_syms; i++)
    replace[i] = i;

  for (i = 0; i < num_syms; i++) {
    if (c->freq[i] == 0) continue;
    l = c->length[i];
    s = i;
    while (symtable[s].len > 1) {
//printf("symbol %d: s1 = %d, s2 = %d\n", s, symtable[s].pattern[0], symtable[s].pattern[1]);
      s = symtable[s].pattern[0];
      if (c->length[replace[s]] > l)
        replace[s] = i;
    }
  }
}

void write_final(struct tb_handle *F, FILE *G)
{
  int i, j, k;

  write_u32(G, F->wdl ? WDL_MAGIC : DTZ_MAGIC);
  write_u8(G, (numpcs << 4) | (F->split ? 0x01: 0x00)
                              | (F->num_tables > 2 ? 0x02 : 0x00));

  int numorder = 1;
  if (numpawns > 0 && (F->perm[0][1] >> 4) != 0x0f)
    numorder = 2;

  if (F->wdl) {
    if (F->split) {
      for (i = 0; i < F->num_tables; i += 2) {
        for (j = 0; j < numorder; j++)
          write_u8(G, (F->perm[i][j] >> 4) | (F->perm[i + 1][j] & 0xf0));
        for (j = 0; j < numpcs; j++)
          write_u8(G, (F->perm[i][j] & 0x0f)
              | ((F->perm[i + 1][j] & 0x0f) << 4));
      }
    } else {
      for (i = 0; i < F->num_tables; i++) {
        for (j = 0; j < numorder; j++)
          write_u8(G, (F->perm[i][j] >> 4) | (F->perm[i][j] & 0xf0));
        for (j = 0; j < numpcs; j++)
          write_u8(G, (F->perm[i][j] & 0x0f)
              | ((F->perm[i][j] & 0x0f) << 4));
      }
    }
  } else {
    for (i = 0; i < F->num_tables; i++) {
      for (j = 0; j < numorder; j++)
        write_u8(G, (F->perm[i][j] >> 4));
      for (j = 0; j < numpcs; j++)
        write_u8(G, (F->perm[i][j] & 0x0f));
    }
  }

  if (ftell(G) & 0x01)
    write_u8(G, 0);
 
  for (i = 0; i < F->num_tables; i++) {
    if (!(F->flags[i] & 0x80)) {
      write_u8(G, F->flags[i]);
      write_u8(G, F->blocksize[i]);
      write_u8(G, F->idxbits[i]);
      write_u8(G, F->num_blocks[i] - F->real_num_blocks[i]);
      write_u32(G, F->real_num_blocks[i]);

      struct HuffCode *c = F->c[i];

      write_u8(G, c->max_len);
      write_u8(G, c->min_len);
      for (j = c->min_len; j <= c->max_len; j++)
        write_u16(G, c->offset[j]);

      struct Symbol *stable = F->symtable[i];
      write_u16(G, F->num_syms[i]);
      for (j = 0; j < F->num_syms[i]; j++) {
        k = c->map[j];
        if (stable[k].len == 1) {
          int s1 = stable[k].pattern[0];
          write_u8(G, s1 & 0xff);
          write_u8(G, (s1 >> 8) | 0xf0);
          write_u8(G, 0xff);
        } else {
          int s1 = c->inv[stable[k].pattern[0]];
          int s2 = c->inv[stable[k].pattern[1]];
          write_u8(G, s1 & 0xff);
          write_u8(G, (s1 >> 8) | ((s2 << 4) & 0xff));
          write_u8(G, (s2 >> 4));
        }
      }
    } else {
      write_u8(G, F->flags[i]);
      if (F->wdl) {
        write_u8(G, F->single_val[i]);
      } else {
//      for (j = 0; j < 4; j++)
//        write_u8(G, F->map[i]->num[j] == 1 ? F->map[i]->map[j][0] : 0);
      }
    }
    if (ftell(G) & 0x01)
      write_u8(G, 0);
  }

  if (!F->wdl) {
    for (i = 0; i < F->num_tables; i++) {
//      if (F->flags[i] & 0x80) continue;
      if (F->flags[i] & 2) {
        if (!(F->flags[i] & 16)) {
          for (j = 0; j < 4; j++) {
            write_u8(G, F->map[i]->num[j]);
            for (k = 0; k < F->map[i]->num[j]; k++)
              write_u8(G, F->map[i]->map[j][k]);
          }
        } else {
          if (ftell(G) & 0x01)
            write_u8(G, 0);
          for (j = 0; j < 4; j++) {
            write_u16(G, F->map[i]->num[j]);
            for (k = 0; k < F->map[i]->num[j]; k++)
              write_u16(G, F->map[i]->map[j][k]);
          }
        }
      }
    }
    if (ftell(G) & 0x01)
      write_u8(G, 0);
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    copy_data(G, F->H[i], 6 * F->num_indices[i]);
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    copy_data(G, F->H[i], 2 * F->num_blocks[i]);
  }

// align to 64-byte boundary
  uint64_t idx = ftell(G);
  while (idx & 0x3f) {
    write_u8(G, 0);
    idx++;
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    uint64_t datasize = F->real_num_blocks[i] * (1 << F->blocksize[i]);
    copy_data(G, F->H[i], datasize);
    while (datasize & 0x3f) {
      write_u8(G, 0);
      datasize++;
    }
  }
}

struct tb_handle *create_tb(char *tablename, int wdl, int blocksize)
{
  struct tb_handle *F;

  F = malloc(sizeof(struct tb_handle));

  F->num_tables = 0;
  F->wdl = wdl;
  F->split = split;
  F->default_blocksize = blocksize;
  strcpy(F->name, tablename);

  return F;
}

static void compress_data_single_valued(struct tb_handle *F, int num)
{
  int i;

  F->flags[num] = 0x80;
  if (F->wdl) {
    F->flags[num] = wdl_flags | 0x80;
    for (i = 0; i < 5; i++)
      if (wdl_vals[i]) break;
    F->single_val[num] = i;
  } else {
    F->map[num] = dtz_map;
    F->flags[num] = dtz_map->side | 0x80
                          | (dtz_map->ply_accurate_win << 2)
                          | (dtz_map->ply_accurate_loss << 3);
  }
}

void merge_tb(struct tb_handle *F)
{
  FILE *G;
  int i;
  char name[64];
  char ext[8];

  strcpy(name, F->name);
  strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
  if (!(G = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(1);
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    sprintf(ext, ".%c", '1' + i);
    strcpy(name, F->name);
    strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
    strcat(name, ext);
    if (!(F->H[i] = fopen(name, "rb"))) {
      fprintf(stderr, "Could not open %s for reading.\n", name);
      exit(1);
    }
  }

  write_final(F, G);

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    fclose(F->H[i]);
  }

  fclose(G);

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    sprintf(ext, ".%c", '1' + i);
    strcpy(name, F->name);
    strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
    strcat(name, ext);
    unlink(name);
  }

  strcpy(name, F->name);
  strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
  add_checksum(name);
}
