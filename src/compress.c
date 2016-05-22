/*
  Copyright (c) 2011-2016 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include "defs.h"
#include "probe.h"
#include "threads.h"
#include "checksum.h"

#define MAXSYMB (4095 + 8)
#define MAXINT64 0x7fffffffffffffff
#define MAX_NEW 250

typedef signed long long int64;

struct HuffCode {
  int64 freq[MAXSYMB];
  int64 nfreq[MAXSYMB];
  int map[MAXSYMB];
  int inv[MAXSYMB];
  int length[MAXSYMB];
  int num, max_len, min_len;
  int base[32];
  int offset[32];
};

static struct dtz_map {
  ubyte map[4][256];
  ubyte inv_map[4][256];
  ubyte num[4];
  ubyte max_num;
  ubyte side;
  ubyte ply_accurate_win;
  ubyte ply_accurate_loss;
  ubyte high_freq_max;
} *dtz_map;

extern int numthreads;
extern int split;
extern int numpcs;
extern int numpawns;
extern long64 tb_size;

int blockbits = 8;

unsigned char pieces[TBPIECES];

int freq[MAXSYMB];
int low, high;
unsigned char buf[8192];

char name[64];

static struct HuffCode *setup_code(unsigned char *data, long64 size);
static void create_code(struct HuffCode *c);
static void sort_code(struct HuffCode *c, int num);

void write_long(FILE *F, uint32 l)
{
  union {
    uint32 l;
    char c[4];
  } c;

  c.l = l;
#ifndef BIG_ARCH
  fputc(c.c[0], F);
  fputc(c.c[1], F);
  fputc(c.c[2], F);
  fputc(c.c[3], F);
#else
  fputc(c.c[3], F);
  fputc(c.c[2], F);
  fputc(c.c[1], F);
  fputc(c.c[0], F);
#endif
}

void write_short(FILE *F, ushort s)
{
  union {
    ushort s;
    char c[2];
  } c;

  c.s = s;
#ifndef BIG_ARCH
  fputc(c.c[0], F);
  fputc(c.c[1], F);
#else
  fputc(c.c[1], F);
  fputc(c.c[0], F);
#endif
}

void write_byte(FILE *F, ubyte b)
{
  fputc((char)b, F);
}

int write_to_buf(uint32 bits, int n)
{
  static int numBytes, numBits;

  if (n < 0) {
    numBytes = numBits = 0;
  } else if (n == 0)
    return numBytes;
  else {
    if (numBits) {
      buf[numBytes-1] |= (bits<<numBits);
      if (numBits+n < 8) {
	numBits += n;
	n = 0;
      } else {
	n -= (8-numBits);
	bits >>= 8-numBits;
	numBits = 0;
      }
    }
    while (n >= 8) {
      buf[numBytes++] = bits;
      bits >>= 8;
      n -= 8;
    }
    if (n > 0) {
      buf[numBytes++] = bits;
      numBits = n;
    }
  }
  return 0;
}

void write_bits(FILE *F, uint32 bits, int n)
{
  static int numBytes, numBits;

  if (n > 0) {
    if (numBits) {
      if (numBits >= n) {
	buf[numBytes-1] |= (bits << (numBits - n));
	numBits -= n;
	n = 0;
      } else {
	buf[numBytes-1] |= (bits >> (n - numBits));
	n -= numBits;
	numBits = 0;
      }
    }
    while (n >= 8) {
      buf[numBytes++] = bits >> (n - 8);
      n -= 8;
    }
    if (n > 0) {
      buf[numBytes++] = bits << (8 - n);
      numBits = 8 - n;
    }
  } else if (n == 0) {
    numBytes = 0;
    numBits = 0;
  } else if (n < 0) {
    n = -n;
    while (numBytes < n)
      buf[numBytes++] = 0;
    fwrite(buf, 1, n, F);
    numBytes = 0;
    numBits = 0;
  }
}

struct Symbol {
  ushort pattern[2];
  unsigned char first, second;
  int len;
};

static struct Symbol symtable[MAXSYMB];
static int64 pairfreq[MAXSYMB][MAXSYMB];
static ushort symcode[256][256];
static int replace[MAXSYMB];

static int num_syms;
static long64 num_blocks, real_num_blocks;
static uint32 num_indices;
static ushort *sizetable;
static ubyte *indextable;
static int blocksize;
static int idxbits;
static int num_vals;

static int num_ctrl, cur_ctrl_idx;

struct {
  int64 freq; // maybe remove later; currently still used for dtz
  int s1, s2;
  int sym;
} newpairs[MAX_NEW + 25];

struct {
  int64 freq;
  int s1, s2;
} paircands[MAX_NEW];

ubyte newtest[MAXSYMB][MAXSYMB];
uint32 countfirst[MAX_THREADS][MAX_NEW][MAXSYMB];
uint32 countsecond[MAX_THREADS][MAX_NEW][MAXSYMB];

extern int total_work;
static long64 *restrict work = NULL, *restrict work_adj = NULL;

static struct {
  unsigned char *data;
  long64 size;
} compress_state;

unsigned char pairfirst[4][MAXSYMB], pairsecond[4][MAXSYMB];

static int t1[4][4];
static int t2[4][4];
static int64 dcfreq[4][4];
static int wdl_vals[5];
static ubyte wdl_flags;
int compress_type;
static ubyte dc_to_val[4];

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

void compress_init_dtz(struct dtz_map *map)
{
  int i, j;

  dtz_map = map;

  num_vals = map->max_num;

  for (i = 0; i < num_vals; i++) {
    symtable[i].pattern[0] = i;
    symtable[i].len = 1;
    for (j = 0; j < 256; j++)
      symcode[i][j] = i;
  }

  compress_type = 0;
  if (num_vals == 1)
    compress_type = 1;
}

// only used for dtz
void adjust_work_dontcares(long64 *restrict work1, long64 *restrict work2)
{
  long64 idx;
  long64 end = work1[total_work];
  ubyte *restrict data = compress_state.data;
  int i;
  int num = num_vals;

  work2[0] = work1[0];
  for (i = 1; i < total_work; i++) {
    idx = work1[i];
    if (idx < work2[i - 1]) {
      work2[i] = work2[i - 1];
      continue;
    }
    while (idx < end && data[idx] == num)
      idx++;
    work2[i] = idx;
  }
  work2[total_work] = work1[total_work];
}

// only used for dtz
void fill_dontcares(struct thread_data *thread)
{
  long64 idx, idx2;
  long64 end = thread->end;
  ubyte *restrict data = compress_state.data;
  long64 size = compress_state.size;
  int s1;
  int k;
  int num = num_vals;

  idx = thread->begin;
  while (idx < end) {
    // find start of don't care sequence
    for (; idx < end; idx++)
      if (data[idx] == num)
	break;
    if (idx == end) break;
    // find end of sequence and determine max pair frequency
    for (idx2 = idx + 1; idx2 < end; idx2++)
      if (data[idx2] != num)
	break;
    int64 max;
    if (idx2 - idx > 1)
      max = dcfreq[0][0];
    else {
      max = -1;
      if (idx > 0)
	max = pairfreq[data[idx -1]][pairfirst[0][data[idx - 1]]];
      if (idx2 < size) {
	int tmp = pairfreq[pairsecond[0][data[idx2]]][data[idx2]];
	if (tmp > max)
	  max = tmp;
      }
    }
    // now replace whenever we reach max pair frequency for the sequence
    int dc = 1;
    if (idx > 0) {
      s1 = data[idx - 1];
      k = pairfirst[0][s1];
      if (pairfreq[s1][k] == max) {
	data[idx] = k;
	s1 = k;
	dc = 0;
      }
    }
    idx2 = idx + 1;
    while (idx2 < end && data[idx2] == num) {
      if (dc) {
	data[idx2 - 1] = t1[0][0];
	s1 = t2[0][0];
	data[idx2] = s1;
	dc = 0;
      } else {
	k = pairfirst[0][s1];
	dc = 1;
	if (pairfreq[s1][k] == max) {
	  data[idx2] = k;
	  s1 = k;
	  dc = 0;
	}
      }
      idx2++;
    }
    if (dc && idx2 < size) {
      s1 = data[idx2];
      k = pairsecond[0][s1];
      if (pairfreq[k][s1] == max)
	data[idx2 - 1] = k;
    }
  }
}

static int64 countfreq[MAX_THREADS][9][16];

static void count_pairs_wdl(struct thread_data *thread)
{
  int s1, s2;
  long64 idx = thread->begin;
  long64 end = thread->end;
  ubyte *restrict data = compress_state.data;
  int t = thread->thread;

  if (idx == 0) idx = 1;
  s1 = data[idx - 1];
  for (; idx < end; idx++) {
    s2 = data[idx];
    countfreq[t][s1][s2]++;
    s1 = s2;
  }
}

static int64 countfreq_dtz[MAX_THREADS][255][255];

static void count_pairs_dtz(struct thread_data *thread)
{
  int s1, s2;
  long64 idx = thread->begin;
  long64 end = thread->end;
  ubyte *restrict data = compress_state.data;
  int t = thread->thread;

  if (idx == 0) idx = 1;
  s1 = data[idx - 1];
  for (; idx < end; idx++) {
    s2 = data[idx];
    countfreq_dtz[t][s1][s2]++;
    s1 = s2;
  }
}

void adjust_work_replace(long64 *restrict work)
{
  long64 idx, idx2;
  long64 end = compress_state.size;
  unsigned char *restrict data = compress_state.data;
  int i, s1, s2, j;

  for (i = 1; i < total_work; i++) {
    idx = work[i];
    if (idx <= work[i - 1]) {
      work[i] = work[i - 1];
      continue;
    }
    s1 = symcode[data[idx]][data[idx + 1]];
    idx += symtable[s1].len;
    j = 0;
    while (idx < end) {
      s2 = symcode[data[idx]][data[idx + 1]];
      if (newtest[s1][s2]) j = 0;
      else {
	if (j == 1) break;
	j = 1;
	idx2 = idx;
      }
      idx += symtable[s2].len;
      s1 = s2;
    }
    if (idx < end)
      work[i] = idx2;
    else
      work[i] = end;
  }
}

void replace_pairs(struct thread_data *thread)
{
  long64 idx = thread->begin;
  long64 end = thread->end;
  unsigned char *restrict data = compress_state.data;
  int s1, s2, a;
  int t = thread->thread;

  a = -1;
  s1 = symcode[data[idx]][data[idx + 1]];
  idx += symtable[s1].len;
  while (idx < end) {
    s2 = symcode[data[idx]][data[idx + 1]];
    idx += symtable[s2].len;
    if (newtest[s1][s2]) {
      struct Symbol *sym = &symtable[newpairs[newtest[s1][s2] - 1].sym];
      data[idx - sym->len] = sym->first;
      data[idx - sym->len + 1] = sym->second;
      if (likely(a >= 0)) countfirst[t][newtest[s1][s2] - 1][a]++;
      a = newtest[s1][s2] - 1;
      if (unlikely(idx == compress_state.size)) break;
      s1 = symcode[data[idx]][data[idx + 1]];
      idx += symtable[s1].len;
      countsecond[t][a][s1]++;
      a = newpairs[a].sym;
    } else {
      a = s1;
      s1 = s2;
    }
  }
}

#define max(a,b) (((a) > (b)) ? (a) : (b))

static void remove_wdl_worker(struct thread_data *thread)
{
  long64 idx, idx2;
  long64 end = thread->end;
  ubyte *restrict data = compress_state.data;
  long64 size = compress_state.size;
  int s, t;

  static ubyte dc_map[5][16] = {
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

void adjust_work_dontcares_wdl(long64 *restrict work1, long64 *restrict work2)
{
  long64 idx;
  long64 end = work1[total_work];
  ubyte *restrict data = compress_state.data;
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

struct HuffCode *construct_pairs_wdl(unsigned char *restrict data, long64 size,
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
	int64 pf = pairfreq[i][j];
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
	int64 pf = pairfreq[i][j];
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
	int64 pf = pairfreq[i][j];
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
	int64 pf = pairfreq[i][j];
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
    int64 max = paircands[0].freq;
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

    adjust_work_replace(work);
    run_threaded(replace_pairs, work, 0);

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

  struct HuffCode *restrict c = setup_code(data, size);

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
  ushort map[num_syms];
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

  create_code(c);
  sort_code(c, num_syms);

  return c;
}

// might not be a win
static void remove_dtz_worker(struct thread_data *thread)
{
  long64 idx, idx2;
  long64 end = thread->end;
  ubyte *restrict data = compress_state.data;
  long64 size = compress_state.size;
  int s;
  int num = num_vals;
  int max = dtz_map->high_freq_max;

  idx = thread->begin;
  while (idx < end) {
    for (; idx < end; idx++)
      if (data[idx] == num) break;
    if (idx == end) break;
    for (idx2 = idx + 1; idx2 < end; idx2++)
      if (data[idx2] != num) break;
    if (idx2 - idx >= 32) {
      idx = idx2;
      continue;
    }
    if (idx == 0)
      s = data[idx2];
    else {
      s = data[idx - 1];
      if (idx2 < size && data[idx2] < s)
	s = data[idx2];
    }
    if (s >= max)
      idx = idx2;
    else
      for (; idx < idx2; idx++)
	data[idx] = s;
  }
}

void adjust_work_dontcares_dtz(long64 *restrict work1, long64 *restrict work2)
{
  long64 idx;
  long64 end = work1[total_work];
  ubyte *data = compress_state.data;
  int i;
  int num = num_vals;

  work2[0] = work1[0];
  for (i = 1; i < total_work; i++) {
    idx = work1[i];
    if (idx < work2[i - 1]) {
      work2[i] = work2[i - 1];
      continue;
    }
    while (idx < end && (data[idx - 1] == num || data[idx] == num
					|| data[idx - 1] == data[idx]))
//    while (idx < end && data[idx] == num)
      idx++;
    work2[i] = idx;
  }
  work2[total_work] = work1[total_work];
}

struct HuffCode *construct_pairs_dtz(unsigned char *restrict data, long64 size,
				      int minfreq, int maxsymbols)
{
  int i, j, k, l;
  int num, t;

  if (!work)
    work = alloc_work(total_work);
  if (!work_adj)
    work_adj = alloc_work(total_work);

  num_syms = num_vals;

  num_ctrl = num_vals - 1;
  cur_ctrl_idx = 0;
  compress_state.data = data;
  compress_state.size = size;

  fill_work(total_work, size, 0, work);

  if (maxsymbols > 0) {
    maxsymbols += num_syms;
    if (maxsymbols > 4095)
      maxsymbols = 4095;
  } else {
    maxsymbols = 4095;
  }

  adjust_work_dontcares_dtz(work, work_adj);
  run_threaded(remove_dtz_worker, work_adj, 0);

  // first round of freq counting, to fill in dont cares
  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      pairfreq[i][j] = 0;

  if (num_syms > 255) {
    fprintf(stderr, "error\n");
    exit(1);
  }
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	countfreq_dtz[t][i][j] = 0;
  run_threaded(count_pairs_dtz, work, 0);
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	pairfreq[i][j] += countfreq_dtz[t][i][j];

  for (j = 0; j < num_syms; j++)
    pairfirst[0][j] = pairsecond[0][j] = 0;
  for (i = 1; i < num_syms; i++)
    for (j = 0; j < num_syms; j++) {
      if (pairfreq[j][i] > pairfreq[j][pairfirst[0][j]])
	pairfirst[0][j] = i;
      if (pairfreq[i][j] > pairfreq[pairsecond[0][j]][j])
	pairsecond[0][j] = i;
    }

  dcfreq[0][0] = -1;
  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      if (pairfreq[i][j] > dcfreq[0][0]) {
	dcfreq[0][0] = pairfreq[i][j];
	t1[0][0] = i;
	t2[0][0] = j;
      }

  for (i = 0; i < MAXSYMB; i++)
    for (j = 0; j < MAXSYMB; j++)
      newtest[i][j] = 0;

  adjust_work_dontcares(work, work_adj);
  run_threaded(fill_dontcares, work_adj, 0);

  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      pairfreq[i][j] = 0;

  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	countfreq_dtz[t][i][j] = 0;
  run_threaded(count_pairs_dtz, work, 0);
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	pairfreq[i][j] += countfreq_dtz[t][i][j];

  for (i = 0; i < num_syms; i++)
    for (j = 0; j < 256; j++)
      symcode[i][j] = i;

  while (num_syms < maxsymbols) {

    num = 0;
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	if (pairfreq[i][j] >= minfreq && (num < MAX_NEW - 1 || pairfreq[i][j] > newpairs[num-1].freq) && (symtable[i].len + symtable[j].len <= 256)) {
	  for (k = 0; k < num; k++)
	    if (newpairs[k].freq < pairfreq[i][j]) break;
	  if (num < MAX_NEW - 1) num++;
	  for (l = num - 1; l > k; l--)
	    newpairs[l] = newpairs[l-1];
	  newpairs[k].freq = pairfreq[i][j];
	  newpairs[k].s1 = i;
	  newpairs[k].s2 = j;
	}

    for (i = 0; i < num_syms; i++)
      pairfirst[0][i] = pairsecond[0][i] = 0;

    // keep track of number of skipped pairs to make sure they'll be
    // considered in the next iteration (before running out of symbols)
    int skipped = 0; // just a rough estimate
    int64 max = newpairs[0].freq;
    for (i = 0, j = 0; i < num && num_syms + j + skipped <= maxsymbols; i++) {
      while (max > newpairs[i].freq * 2) {
	skipped += i;
	max /= 2;
      }
      if (!pairsecond[0][newpairs[i].s1] && !pairfirst[0][newpairs[i].s2])
	newpairs[j++] = newpairs[i];
      else
	skipped++;
      pairfirst[0][newpairs[i].s1] = 1;
      pairsecond[0][newpairs[i].s2] = 1;
    }
    num = j;

    if (num_syms + num > maxsymbols)
      num = maxsymbols - num_syms;

    for (i = 0; i < num; i++) {
      newpairs[i].sym = num_syms;
      symtable[num_syms].len = symtable[newpairs[i].s1].len + symtable[newpairs[i].s2].len;
      symtable[num_syms].pattern[0] = newpairs[i].s1;
      symtable[num_syms].pattern[1] = newpairs[i].s2;
      if (!cur_ctrl_idx)
	num_ctrl++;
      if (num_ctrl == 256) break;
      symtable[num_syms].first = num_ctrl;
      symtable[num_syms].second = cur_ctrl_idx;
      symcode[num_ctrl][cur_ctrl_idx] = num_syms;
      cur_ctrl_idx++;
      if (cur_ctrl_idx == 256) cur_ctrl_idx = 0;
      num_syms++;
    }

    if (i != num) {
      printf("Ran short of symbols.\n"); // not an error
      num = i;
      num_ctrl--;
    }

    if (num == 0) break;

    for (i = 0; i < num_syms - num; i++)
      for (j = num_syms - num; j < num_syms; j++)
	pairfreq[i][j] = 0;
    for (; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
	pairfreq[i][j] = 0;

    for (i = 0; i < num; i++)
      newtest[newpairs[i].s1][newpairs[i].s2] = i + 1;

// thread this later
    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num; i++)
        for (j = 0; j < num_syms; j++) {
	  countfirst[t][i][j] = 0;
	  countsecond[t][i][j] = 0;
	}

    adjust_work_replace(work);
    run_threaded(replace_pairs, work, 0);

    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num; i++)
	for (j = 0; j < num_syms; j++) {
	  pairfreq[j][newpairs[i].s1] -= countfirst[t][i][j];
          pairfreq[j][newpairs[i].sym] += countfirst[t][i][j];
	  pairfreq[newpairs[i].s2][j] -= countsecond[t][i][j];
	  pairfreq[newpairs[i].sym][j] += countsecond[t][i][j];
	}

    for (i = 0; i < num; i++)
      pairfreq[newpairs[i].s1][newpairs[i].s2] = 0;

    for (i = 0; i < num; i++)
      newtest[newpairs[i].s1][newpairs[i].s2] = 0;
  }

  struct HuffCode *restrict c = setup_code(data, size);
  create_code(c);
  sort_code(c, num_syms);

  return c;
}

struct HuffCode *construct_pairs(unsigned char *restrict data, long64 size,
				  int minfreq, int maxsymbols, int wdl)
{
  if (wdl)
    return construct_pairs_wdl(data, size, minfreq, maxsymbols);
  else
    return construct_pairs_dtz(data, size, minfreq, maxsymbols);
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

static struct HuffCode *setup_code(unsigned char *restrict data, long64 size)
{
  long64 idx;
  int i, s;

  struct HuffCode *restrict c = malloc(sizeof(struct HuffCode));
  for (i = 0; i < MAXSYMB; i++)
    c->freq[i] = 0;

  for (idx = 0; idx < size; idx += symtable[s].len) {
    s = symcode[data[idx]][data[idx + 1]];
    c->freq[s]++;
  }

  return c;
}

long64 calc_size(struct HuffCode *restrict c)
{
  int i;
  long64 bits = 0;

  for (i = 0; i < num_syms; i++)
    bits += c->length[i] * c->freq[i];

  return (bits + 7) >> 3;
}

void calc_block_sizes(ubyte *restrict data, long64 size,
		      struct HuffCode *restrict c, int maxsize)
{
  long64 idx;
  int i, s, t;
  int64 block;
  int maxbits, bits, numpos;
  uint32 avg;

  long64 rawsize = calc_size(c);
  printf("calc_size: %"PRIu64"\n", rawsize);

  long64 optsize, compsize;

  block = 0;
  compsize = MAXINT64;
  blocksize = maxsize + 1;
  do {
    optsize = compsize;
    num_blocks = block;
    blocksize--;
    if (((rawsize * ((1 << blocksize) + 2)) >> blocksize) >= optsize) break;
    maxbits = 8 << blocksize;
    bits = 0;
    numpos = 0;
    block = 0;
    for (idx = 0; idx < size;) {
      s = symcode[data[idx]][data[idx+1]];
      t = symtable[s].len;
      if (bits + c->length[s] > maxbits || numpos + t > 65536) {
	block++;
	bits = 0;
	numpos = 0;
      }
      bits += c->length[s];
      numpos += t;
      idx += t;
    }
    if (numpos > 0)
      block++;
    compsize = block << blocksize;
    compsize = (compsize + 0x3f) & ~0x3f;
    avg = size / block;
    idxbits = 0;
    while (avg) {
      idxbits++;
      avg >>= 1;
    }
    idxbits += 4;
    while (idxbits > 1 && (1ULL << (idxbits - 1)) > size) idxbits--;

    num_indices = (size + (1ULL << idxbits) - 1) >> idxbits;
    t = ((2 * num_indices - 1) << (idxbits - 1)) - size;
    if (t > 0) block += (t + 65535) >> 16;
    else t = 0;
    compsize += 2 * block + 6 * num_indices;

    printf("bits = %d; blocks = %"PRIu64" (%d); size = %"PRIu64"\n", blocksize, block-((t+65535)>>16), (t+65535)>>16, compsize);

  } while (compsize < optsize);

  blocksize++;
  maxbits = 8ULL << blocksize;
  sizetable = malloc((num_blocks + 16) * sizeof(ushort));

  calc_symbol_tweaks(c);

  bits = 0;
  numpos = 0;
  block = 0;
  for (idx = 0; idx < size;) {
    s = symcode[data[idx]][data[idx+1]];
    t = symtable[s].len;
    if (bits + c->length[s] > maxbits || numpos + t > 65536) {
      if (numpos + t <= 65536) {
	if (bits + c->length[replace[s]] <= maxbits) {
	  idx += t;
	  sizetable[block++] = numpos + t - 1;
	  bits = 0;
	  numpos = 0;
	  continue;
	}
	if (t > 1) {
	  int s1 = symtable[s].pattern[0];
	  int s2 = symtable[s].pattern[1];
	  if (c->length[s1] != 0 && c->length[s2] != 0) {
	    if (bits + c->length[s1] + c->length[replace[s2]] <= maxbits) {
	      idx += t;
	      sizetable[block++] = numpos + t - 1;
	      bits = 0;
	      numpos = 0;
	      continue;
	    }
	    if (c->length[s2] < c->length[s] && bits + c->length[replace[s1]] <= maxbits) {
	      sizetable[block++] = numpos + symtable[s1].len - 1;
	      idx += t;
	      bits = c->length[s2];
	      numpos = symtable[s2].len;
	      continue;
	    }
	  }
	}
      }
      if (numpos + t <= 65536 && bits + c->length[replace[s]] <= maxbits) {
	idx += t;
	sizetable[block++] = numpos + t - 1;
	bits = 0;
	numpos = 0;
	continue;
      }

      sizetable[block++] = numpos - 1;
      bits = 0;
      numpos = 0;
    }
    bits += c->length[s];
    numpos += t;
    idx += t;
  }
  if (numpos > 0)
    sizetable[block++] = numpos - 1;

  real_num_blocks = block;
  num_blocks = block;

  avg = (size / num_blocks);
  idxbits = 0;
  while (avg) {
    idxbits++;
    avg >>= 1;
  }
  idxbits += 4;
  while (idxbits > 1 && (1ULL << (idxbits - 1)) > size) idxbits--;

  num_indices = (size + (1ULL << idxbits) - 1) >> idxbits;
  indextable = malloc(num_indices * 6);

  long64 idx2 = 1ULL << (idxbits-1);
  block = 0;
  idx = 0;
  for (i = 0; i < num_indices; i++) {
    while (block < num_blocks && idx + sizetable[block] < idx2)
      idx += sizetable[block++] + 1;
    if (block == num_blocks) {
      sizetable[num_blocks++] = 65535;
      while (idx + sizetable[block] < idx2) {
	idx += sizetable[block++] + 1;
	sizetable[num_blocks++] = 65535;
      }
    }
    *((uint32 *)(indextable + 6 * i)) = block;
    *((ushort *)(indextable + 6 * i + 4)) = idx2 - idx;
    idx2 += 1ULL << idxbits;
  }

  printf("real_num_blocks = %"PRIu64"; num_blocks = %"PRIu64"\n", real_num_blocks, num_blocks);
  printf("idxbits = %d\n", idxbits);
  printf("num_indices = %d\n", num_indices);
}

// FIXME: should put COPYSIZE in some header
#define COPYSIZE 10*1024*1024
extern ubyte *restrict copybuf;

void copy_bytes(FILE *F, FILE *G, long64 n)
{
  if (!copybuf)
    copybuf = malloc(COPYSIZE);

  while (n > COPYSIZE) {
    fread(copybuf, 1, COPYSIZE, G);
    fwrite(copybuf, 1, COPYSIZE, F);
    n -= COPYSIZE;
  }
  fread(copybuf, 1, n, G);
  fwrite(copybuf, 1, n, F);
}

struct tb_handle {
  char name[64];
  int num_tables;
  int wdl;
  int split;
  ubyte perm[8][TBPIECES];
  int default_blocksize;
  int blocksize[8];
  int idxbits[8];
  int real_num_blocks[8];
  int num_blocks[8];
  int num_indices[8];
  int num_syms[8];
  int num_values[8];
  struct HuffCode *c[8];
  struct Symbol *symtable[8];
  struct dtz_map *map[4];
  int flags[8];
  ubyte single_val[8];
  FILE *H[8];
};

void write_final(struct tb_handle *F, FILE *G)
{
  int i, j, k;

  write_long(G, F->wdl ? WDL_MAGIC : DTZ_MAGIC);
  write_byte(G, (numpcs << 4) | (F->split ? 0x01: 0x00)
			      | (F->num_tables > 2 ? 0x02 : 0x00));

  int numorder = 1;
  if (numpawns > 0 && (F->perm[0][1] >> 4) != 0x0f)
    numorder = 2;

  if (F->wdl) {
    if (F->split) {
      for (i = 0; i < F->num_tables; i += 2) {
	for (j = 0; j < numorder; j++)
	  write_byte(G, (F->perm[i][j] >> 4) | (F->perm[i + 1][j] & 0xf0));
	for (j = 0; j < numpcs; j++)
	  write_byte(G, (F->perm[i][j] & 0x0f)
	      | ((F->perm[i + 1][j] & 0x0f) << 4));
      }
    } else {
      for (i = 0; i < F->num_tables; i++) {
	for (j = 0; j < numorder; j++)
	  write_byte(G, (F->perm[i][j] >> 4) | (F->perm[i][j] & 0xf0));
	for (j = 0; j < numpcs; j++)
	  write_byte(G, (F->perm[i][j] & 0x0f)
	      | ((F->perm[i][j] & 0x0f) << 4));
      }
    }
  } else {
    for (i = 0; i < F->num_tables; i++) {
      for (j = 0; j < numorder; j++)
	write_byte(G, (F->perm[i][j] >> 4));
      for (j = 0; j < numpcs; j++)
	write_byte(G, (F->perm[i][j] & 0x0f));
    }
  }

  if (ftell(G) & 0x01)
    write_byte(G, 0);
 
  for (i = 0; i < F->num_tables; i++) {
    if (!(F->flags[i] & 0x80)) {
      write_byte(G, F->flags[i]);
      write_byte(G, F->blocksize[i]);
      write_byte(G, F->idxbits[i]);
      write_byte(G, F->num_blocks[i] - F->real_num_blocks[i]);
      write_long(G, F->real_num_blocks[i]);

      struct HuffCode *c = F->c[i];

      write_byte(G, c->max_len);
      write_byte(G, c->min_len);
      for (j = c->min_len; j <= c->max_len; j++)
	write_short(G, c->offset[j]);

      struct Symbol *stable = F->symtable[i];
      write_short(G, F->num_syms[i]);
      for (j = 0; j < F->num_syms[i]; j++) {
	k = c->map[j];
	if (stable[k].len == 1) {
	  int s1 = stable[k].pattern[0];
	  write_byte(G, s1 & 0xff);
	  write_byte(G, (s1 >> 8) | 0xf0);
	  write_byte(G, 0xff);
	} else {
	  int s1 = c->inv[stable[k].pattern[0]];
	  int s2 = c->inv[stable[k].pattern[1]];
	  write_byte(G, s1 & 0xff);
	  write_byte(G, (s1 >> 8) | ((s2 << 4) & 0xff));
	  write_byte(G, (s2 >> 4));
	}
      }
    } else {
      write_byte(G, F->flags[i]);
      if (F->wdl) {
	write_byte(G, F->single_val[i]);
      } else {
//	for (j = 0; j < 4; j++)
//	  write_byte(G, F->map[i]->num[j] == 1 ? F->map[i]->map[j][0] : 0);
      }
    }
    if (ftell(G) & 0x01)
      write_byte(G, 0);
  }

  if (!F->wdl) {
    for (i = 0; i < F->num_tables; i++) {
//      if (F->flags[i] & 0x80) continue;
      if (F->flags[i] & 2) {
	for (j = 0; j < 4; j++) {
	  write_byte(G, F->map[i]->num[j]);
	  for (k = 0; k < F->map[i]->num[j]; k++)
	    write_byte(G, F->map[i]->map[j][k]);
	}
      }
    }
    if (ftell(G) & 0x01)
      write_byte(G, 0);
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    copy_bytes(G, F->H[i], 6 * F->num_indices[i]);
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    copy_bytes(G, F->H[i], 2 * F->num_blocks[i]);
  }

// align to 64-byte boundary
  long64 idx = ftell(G);
  while (idx & 0x3f) {
    write_byte(G, 0);
    idx++;
  }

  for (i = 0; i < F->num_tables; i++) {
    if (F->flags[i] & 0x80) continue;
    long64 datasize = F->real_num_blocks[i] * (1 << F->blocksize[i]);
    copy_bytes(G, F->H[i], datasize);
    while (datasize & 0x3f) {
      write_byte(G, 0);
      datasize++;
    }
  }
}

void write_ctb_data(FILE *F, unsigned char *restrict data,
		    struct HuffCode *restrict c, long64 size, int blocksize)
{
  long64 idx;
  int s, t, l;
  int bits, numpos;
  int maxbits;

  maxbits = 8 << blocksize;
  bits = numpos = 0;
  write_bits(F, 0, 0);
  for (idx = 0; idx < size;) {
    s = symcode[data[idx]][data[idx+1]];
    t = symtable[s].len;
    if (bits + c->length[s] > maxbits || numpos + t > 65536) {

      if (numpos + t <= 65536) {
	if (bits + c->length[replace[s]] <= maxbits) {
	  s = replace[s];
	  l = c->length[s];
	  write_bits(F, c->base[l] + (c->inv[s] - c->offset[l]), l);
	  idx += t;
	  write_bits(F, 0, -(1 << blocksize));
	  bits = 0;
	  numpos = 0;
	  continue;
	}
	if (t > 1) {
	  int s1 = symtable[s].pattern[0];
	  int s2 = symtable[s].pattern[1];
	  if (c->length[s1] != 0 && c->length[s2] != 0) {
	    if (bits + c->length[s1] + c->length[replace[s2]] <= maxbits) {
	      l = c->length[s1];
	      write_bits(F, c->base[l] + (c->inv[s1] - c->offset[l]), l);
	      l = c->length[replace[s2]];
	      write_bits(F, c->base[l] + (c->inv[replace[s2]] - c->offset[l]), l);
	      idx += t;
	      write_bits(F, 0, -(1 << blocksize));
	      bits = 0;
	      numpos = 0;
	      continue;
	    }
	    if (c->length[s2] < c->length[s] && bits + c->length[replace[s1]] <= maxbits) {
	      if (bits + c->length[s1] > maxbits) s1 = replace[s1];
	      l = c->length[s1];
	      write_bits(F, c->base[l] + (c->inv[s1] - c->offset[l]), l);
	      write_bits(F, 0, -(1 << blocksize));
	      l = c->length[s2];
	      write_bits(F, c->base[l] + (c->inv[s2] - c->offset[l]), l);
	      idx += t;
	      bits = l;
	      numpos = symtable[s2].len;
	      continue;
	    }
	  }
	}
      }

      write_bits(F, 0, -(1 << blocksize));
      bits = 0;
      numpos = 0;
    }
    l = c->length[s];
    write_bits(F, c->base[l] + (c->inv[s] - c->offset[l]), l);
    bits += l;
    numpos += t;
    idx += t;
  }
  if (numpos > 0)
    write_bits(F, 0, -(1 << blocksize));
}

static void compress_data(struct tb_handle *F, int num, FILE *G,
			  ubyte *restrict data, long64 size, int minfreq)
{
  struct HuffCode *restrict c;
  int i;

  if (F->wdl)
    c = construct_pairs_wdl(data, size, minfreq, 0);
  else
    c = construct_pairs_dtz(data, size, minfreq, 0);

  calc_block_sizes(data, size, c, F->default_blocksize);

  F->c[num] = c;
  F->blocksize[num] = blocksize;
  F->idxbits[num] = idxbits;
  F->real_num_blocks[num] = real_num_blocks;
  F->num_blocks[num] = num_blocks;
  F->num_indices[num] = num_indices;
  F->num_syms[num] = num_syms;
  F->symtable[num] = malloc(sizeof(symtable));
  memcpy(F->symtable[num], symtable, sizeof(symtable));

  struct Symbol *stable = F->symtable[num];
  if (F->wdl) {
    F->flags[num] = wdl_flags;
  } else {
    int j, k;
    // if we are here, num_vals >= 2
    F->map[num] = dtz_map;
    F->flags[num] = dtz_map->side
			  | (dtz_map->ply_accurate_win << 2)
			  | (dtz_map->ply_accurate_loss << 3);
    for (i = 0; i < 4; i++)
      if (dtz_map->num[i] == num_vals)
	break;
    for (j = 0; j < 4; j++)
      if (j != i) {
	for (k = 0; k < dtz_map->num[j]; k++)
	  if (dtz_map->map[j][k] != dtz_map->map[i][k])
	    break;
	if (k < dtz_map->num[j])
	  break;
      }
    if (j == 4) {
      for (k = 0; k < num_vals; k++)
	stable[k].pattern[0] = dtz_map->map[i][k];
    } else {
      F->flags[num] |= 2;
    }
  }

  for (i = 0; i < num_indices; i++) {
    write_long(G, *((uint32 *)(indextable + 6 * i)));
    write_short(G, *((ushort *)(indextable + 6 * i + 4)));
  }

  for (i = 0; i < num_blocks; i++)
    write_short(G, sizetable[i]);

  free(indextable);
  free(sizetable);

  write_ctb_data(G, data, c, size, F->blocksize[num]);
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

void compress_tb(struct tb_handle *F, ubyte *restrict data,
		  ubyte *restrict perm, int minfreq)
{
  int i;
  int num;
  char name[64];
  char ext[8];
  FILE *G;

  num = F->num_tables++;

  for (i = 0; i < numpcs; i++)
    F->perm[num][i] = perm[i];

  if (compress_type == 0) {
    sprintf(ext, ".%c", '1' + num);
    strcpy(name, F->name);
    strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
    strcat(name, ext);
    if (!(G = fopen(name, "wb"))) {
      fprintf(stderr, "Could not open %s for writing.\n", name);
      exit(1);
    }

    compress_data(F, num, G, data, tb_size, minfreq);

    fclose(G);
  } else if (compress_type == 1) {
    compress_data_single_valued(F, num);
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

static void create_code(struct HuffCode *restrict c)
{
  int i, num;
  int idx1, idx2;
  long64 min1, min2;

  num = 0;
  for (i = 0; i < num_syms; i++)
    if (c->freq[i]) {
      c->nfreq[num] = c->freq[i];
      c->map[i] = num;
      num++;
    }

  for (i = 0; i < num_syms; i++)
    c->length[i] = 0;

  if (num == 1) {
    for (i = 0; i < num_syms; i++)
      if (c->freq[i]) break;
    c->length[i] = 1;
  } else while (num  > 1) {
    min1 = min2 = MAXINT64;
    idx1 = idx2 = 0;
    for (i = 0; i < num; i++)
      if (c->nfreq[i] < min2) {
	if (c->nfreq[i] < min1) {
	  min2 = min1;
	  idx2 = idx1;
	  min1 = c->nfreq[i];
	  idx1 = i;
	} else {
	  min2 = c->nfreq[i];
	  idx2 = i;
	}
      }
    if (idx1 > idx2) {
      int tmp = idx1;
      idx1 = idx2;
      idx2 = tmp;
    }
    c->nfreq[idx1] = min1 + min2;
    num--;
    for (i = idx2; i < num; i++)
      c->nfreq[i] = c->nfreq[i+1];
    for (i = 0; i < num_syms; i++)
      if (c->map[i] == idx1) {
	c->length[i]++;
      } else if (c->map[i] == idx2) {
	c->map[i] = idx1;
	c->length[i]++;
      } else
	if (c->map[i] > idx2) c->map[i]--;
  }
}

static void sort_code(struct HuffCode *restrict c, int num)
{
  int i, j, max_len;

  for (i = 0; i < num; i++) {
    c->map[i] = i;
    if (c->freq[i] == 0)
      c->length[i] = 0;
  }

  for (i = 0; i < num; i++)
    for (j = i+1; j < num; j++)
      if ((c->length[c->map[i]] < c->length[c->map[j]]) || (c->length[c->map[i]] == c->length[c->map[j]] && c->freq[c->map[i]] > c->freq[c->map[j]])) {
	int tmp = c->map[i];
	c->map[i] = c->map[j];
	c->map[j] = tmp;
      }
  for (i = 0; i < num; i++)
    c->inv[c->map[i]] = i;

  c->num = num;
  c->max_len = max_len = c->length[c->map[0]];
  c->offset[max_len] = 0;
  c->base[max_len] = 0;
  for (i = 0, j = max_len-1; i < num && c->length[c->map[i]]; i++)
    while (j >= c->length[c->map[i]]) {
      c->offset[j] = i;
      c->base[j] = (c->base[j + 1] + (i - c->offset[j + 1])) / 2;
      j--;
    }
  c->min_len = j + 1;
}

