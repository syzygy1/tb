/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include "defs.h"
#include "threads.h"
#include "probe.h"

long64 calc_factors_piece(int *factor, int num, int order, ubyte *norm, ubyte enc_type);
long64 calc_factors_pawn(int *factor, int num, int order, int order2, ubyte *norm, int file);
void calc_order_piece(int num, int ord, int *order, ubyte *norm);
void calc_order_pawn(int num, int ord, int ord2, int *order, ubyte *norm);

static long64 *restrict work_convert = NULL;
static long64 *restrict work_est = NULL;

extern int total_work;
extern int numthreads;
extern long64 sq_mask[64];
extern int compress_type;

#ifdef SMALL
extern short KK_map[64][64];
extern char mirror[64][64];
extern long64 diagonal;
extern int shift[];
#endif

char name[64];
long64 tb_size;

static struct TBEntry_piece entry_piece;
static struct TBEntry_pawn entry_pawn;

static ubyte order_list[720];
static ubyte order2_list[720];

static ubyte piece_perm_list[720][TBPIECES];
static ubyte pidx_list[720][TBPIECES];

static int num_types, num_type_perms;
static ubyte type[TBPIECES];
static ubyte type_perm_list[720][TBPIECES];

static long64 compest[720];

#define MAX_CANDS 30
static int trylist[MAX_CANDS];

extern int numpawns;
extern int numpcs;

static int pw[TBPIECES];
static int cmp[16];

#if TBPIECES > 6
#error number of allocated permutations too small
#endif

void setup_pieces(struct TBEntry_piece *ptr, unsigned char *data);

static ubyte perm_tmp[TBPIECES];

void generate_type_perms2(int n, int k)
{
  int i, j;

  if (k == n) {
    for (i = 0; i < n; i++)
      type_perm_list[num_type_perms][i] = perm_tmp[i];
    num_type_perms++;
    return;
  }

  for (j = 0; j < n; j++)
    if (perm_tmp[j] == 0xff) {
      perm_tmp[j] = type[k];
      generate_type_perms2(n, k + 1);
      perm_tmp[j] = 0xff;
    }
}

void generate_type_perms(int n)
{
  int i;

  num_type_perms = 0;
  for (i = 0; i < n; i++)
    perm_tmp[i] = 0xff;
  generate_type_perms2(n, 0);
}

int num_segs, seg_size;
long64 *restrict segs = NULL;

#define NUM_SEGS 1000
#define SEG_SIZE (64*256)

// adapted from glibc, random.c:

int myrand(void)
{
  static int tbl[31] = {
    -1726662223, 379960547, 1735697613, 1040273694, 1313901226,
    1627687941, -179304937, -2073333483, 1780058412, -1989503057,
    -615974602, 344556628, 939512070, -1249116260, 1507946756,
    -812545463, 154635395, 1388815473, -1926676823, 525320961,
    -1009028674, 968117788, -123449607, 1284210865, 435012392,
    -2017506339, -911064859, -370259173, 1132637927, 1398500161,
    -205601318,
  };

  static int f = 3;
  static int r = 0;

  int result;

  tbl[f] += tbl[r];
  result = (tbl[f] >> 1) & 0x7fffffff;

  f++;
  if (f >= 31) f = 0;
  r++;
  if (r >= 31) r= 0;

  return result;
}

long64 llrand(void)
{
  long64 rand1, rand2;

  rand1 = myrand();
  rand2 = myrand();

  rand1 = (rand1 << 24) + rand2;

  return rand1;
}

void generate_test_list(long64 size, int n)
{
  int i, j;

  if (segs) free(segs);

  if (n <= 3 || size <= 100000) {
    // 1 entry covering whole table
    segs = malloc(sizeof(long64));
    seg_size = size;
    segs[0] = 0;
  } else {
    if (n <= 4 || (NUM_SEGS + 1) * SEG_SIZE >= size) {
      num_segs = 100;
      seg_size = size / num_segs;
    } else {
      num_segs = n > 5 ? NUM_SEGS : NUM_SEGS / 2;
      seg_size = n > 5 ? SEG_SIZE : SEG_SIZE / 1;
    }
    long64 max = size - num_segs * (long64)seg_size + 1;
    segs = malloc(sizeof(long64) * num_segs);
    for (i = 0; i < num_segs; i++)
      segs[i] = llrand() % max;
    for (i = 0; i < num_segs; i++)
      for (j = i + 1; j < num_segs; j++)
        if (segs[i] > segs[j]) {
	  long64 tmp = segs[i];
	  segs[i] = segs[j];
	  segs[j] = tmp;
	}
    for (i = 0; i < num_segs; i++)
      segs[i] += i * seg_size;
  }
}

static long64 mask_a1h1, mask_a1h8;

#define MIRROR_A1H8(x) ((((x) & mask_a1h8) << 3) | (((x) >> 3) & mask_a1h8))

#ifndef SMALL
static const char mirror[] = {
  0, 1, 1, 1, 1, 1, 1, 0,
 -1, 0, 1, 1, 1, 1, 0,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1, 0, 1, 1, 1, 1, 0,-1,
  0, 1, 1, 1, 1, 1, 1, 0
};

static long64 tri0x40[] = {
  6, 0, 1, 2, 2, 1, 0, 6,
  0, 7, 3, 4, 4, 3, 7, 0,
  1, 3, 8, 5, 5, 8, 3, 1,
  2, 4, 5, 9, 9, 5, 4, 2,
  2, 4, 5, 9, 9, 5, 4, 2,
  1, 3, 8, 5, 5, 8, 3, 1,
  0, 7, 3, 4, 4, 3, 7, 0,
  6, 0, 1, 2, 2, 1, 0, 6
};
#endif

static long64 sq_mask_pawn[64];

static long64 flip0x40[] = {
  0, 0, 1, 2, 3, 4, 5, 0
};

long64 encode_piece(struct TBEntry_piece *restrict ptr, ubyte *restrict norm, int *restrict pos, int *restrict factor);
void decode_piece(struct TBEntry_piece *restrict ptr, ubyte *restrict norm, int *restrict pos, int *restrict factor, int *restrict order, long64 idx);
long64 encode_pawn(struct TBEntry_pawn *restrict ptr, ubyte *restrict norm, int *restrict pos, int *restrict factor);
void decode_pawn(struct TBEntry_pawn *restrict ptr, ubyte *restrict norm, int *restrict pos, int *restrict factor, int *restrict order, long64 idx, int file);

ubyte *restrict permute_v;

static void set_norm_piece(int *pcs, ubyte *type_perm, ubyte *norm, int order)
{
  int i, j;

  for (i = 0; i < entry_piece.num; i++)
    norm[i] = 0;

  switch (entry_piece.enc_type) {
  case 0:
    norm[0] = 3;
    break;
  case 2:
    norm[0] = 2;
    break;
  default:
    norm[0] = entry_piece.enc_type - 1;
    break;
  }

  if (entry_piece.enc_type <= 2) {
    for (i = 0, j = norm[0]; i < num_types;)
      if (i != order) {
	norm[j] = pcs[type_perm[i]];
	j += pcs[type_perm[i]];
	i++;
      } else
	i += norm[0];
  } else {
    for (i = 0, j = norm[0]; i < num_types; i++)
      if (i != order) {
	norm[j] = pcs[type_perm[i]];
	j += pcs[type_perm[i]];
      }
  }
}

static void set_norm_pawn(int *pcs, ubyte *type_perm, ubyte *norm, int order, int order2)
{
  int i, j;

  for (i = 0; i < entry_pawn.num; i++)
    norm[i] = 0;

  norm[0] = entry_pawn.pawns[0];
  if (entry_pawn.pawns[1])
    norm[entry_pawn.pawns[0]] = entry_pawn.pawns[1];
  for (i = 0, j = entry_pawn.pawns[0] + entry_pawn.pawns[1]; i < num_types; i++)
    if (i != order && i != order2) {
      norm[j] = pcs[type_perm[i]];
      j += pcs[type_perm[i]];
    }
}

static struct {
  ubyte *src;
  ubyte *dst;
  int *pcs;
  int p;
  int file;
} convert_data;

void convert_data_piece(struct thread_data *thread)
{
  long64 idx1, idx2, idx3;
  int i;
  int sq;
#ifdef SMALL
  int sq2;
#endif
  int n = entry_piece.num;
  int pos[TBPIECES];
  int order[TBPIECES];
  int factor[TBPIECES];
  ubyte norm[TBPIECES];
  ubyte *restrict src = convert_data.src;
  ubyte *restrict dst = convert_data.dst;
  ubyte *restrict pidx = pidx_list[convert_data.p];
  long64 end = thread->end;
  ubyte *restrict v = permute_v;

  set_norm_piece(convert_data.pcs, type_perm_list[convert_data.p], norm, order_list[convert_data.p]);
  calc_order_piece(n, order_list[convert_data.p], order, norm);
  calc_factors_piece(factor, n, order_list[convert_data.p], norm, entry_piece.enc_type);

  idx1 = thread->begin;

  decode_piece(&entry_piece, norm, pos, factor, order, idx1);
#ifndef SMALL
  idx3 = pos[pidx[1]];
  for (i = 2; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  sq = pos[pidx[0]];
  idx3 ^= sq_mask[sq];
  if (mirror[sq] < 0)
    idx3 = MIRROR_A1H8(idx3);
  idx3 |= tri0x40[sq];
#else
  idx3 = pos[pidx[2]];
  for (i = 3; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  sq = pos[pidx[0]];
  idx3 ^= sq_mask[sq];
  sq2 = pos[pidx[1]];
  if (KK_map[sq][sq2] < 0)
    idx3 = diagonal;
  else {
    if (mirror[sq][sq2] < 0)
      idx3 = MIRROR_A1H8(idx3);
    idx3 |= ((long64)KK_map[sq][sq2]) << shift[1];
  }
#endif
  __builtin_prefetch(&src[idx3], 0, 3);

  for (idx1++; idx1 < end; idx1++) {
    decode_piece(&entry_piece, norm, pos, factor, order, idx1);
#ifndef SMALL
    idx2 = pos[pidx[1]];
    for (i = 2; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    sq = pos[pidx[0]];
    idx2 ^= sq_mask[sq];
    if (mirror[sq] < 0)
      idx2 = MIRROR_A1H8(idx2);
    idx2 |= tri0x40[sq];
#else
    idx2 = pos[pidx[2]];
    for (i = 3; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    sq = pos[pidx[0]];
    idx2 ^= sq_mask[sq];
    sq2 = pos[pidx[1]];
    if (KK_map[sq][sq2] < 0)
      idx2 = diagonal;
    else {
      if (mirror[sq][sq2] < 0)
	idx2 = MIRROR_A1H8(idx2);
      idx2 |= ((long64)KK_map[sq][sq2]) << shift[1];
    }
#endif
    __builtin_prefetch(&src[idx2], 0, 3);
    dst[idx1 - 1] = v[src[idx3]];
    idx3 = idx2;
  }

  dst[idx1 - 1] = v[src[idx3]];
}

void convert_data_pawn(struct thread_data *thread)
{
  long64 idx1, idx2, idx3;
  int i;
  int n = entry_pawn.num;
  int pos[TBPIECES];
  int order[TBPIECES];
  int factor[TBPIECES];
  ubyte norm[TBPIECES];
  ubyte *restrict src = convert_data.src;
  ubyte *restrict dst = convert_data.dst;
  ubyte *restrict pidx = pidx_list[convert_data.p];
  int file = convert_data.file;
  long64 end = thread->end;
  ubyte *v = permute_v;

  set_norm_pawn(convert_data.pcs, type_perm_list[convert_data.p], norm, order_list[convert_data.p], order2_list[convert_data.p]);
  calc_order_pawn(n, order_list[convert_data.p], order2_list[convert_data.p], order, norm);
  calc_factors_pawn(factor, n, order_list[convert_data.p], order2_list[convert_data.p], norm, file);

  idx1 = thread->begin;
  decode_pawn(&entry_pawn, norm, pos, factor, order, idx1, file);
  idx3 = pos[pidx[1]];
  for (i = 2; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  idx3 ^= sq_mask_pawn[pos[pidx[0]]];
  __builtin_prefetch(&src[idx3], 0, 3);
 
  for (idx1++; idx1 < end; idx1++) {
    decode_pawn(&entry_pawn, norm, pos, factor, order, idx1, file);
    idx2 = pos[pidx[1]];
    for (i = 2; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    idx2 ^= sq_mask_pawn[pos[pidx[0]]];
    __builtin_prefetch(&src[idx2], 0, 3);
    dst[idx1 - 1] = v[src[idx3]];
    idx3 = idx2;
  }

  dst[idx1 - 1] = v[src[idx3]];
}

struct HuffCode *construct_pairs(unsigned char *restrict data, long64 size, int minfreq, int maxsymbols, int wdl);
long64 calc_size(struct HuffCode *restrict c);

void init_0x40(int numpcs)
{
  int i;
  static int done = 0;

  if (done) return;

#ifndef SMALL
  for (i = 0; i < 64; i++)
    tri0x40[i] <<= 6ULL * (numpcs - 1);
#endif

  for (i = 0; i < 8; i++)
    flip0x40[i] <<= 6ULL * (numpcs - 1);

  mask_a1h1 = mask_a1h8 = 0;
  for (i = 1; i < numpcs; i++) {
    mask_a1h1 = (mask_a1h1 << 6) | 0x07;
    mask_a1h8 = (mask_a1h8 << 6) | 0x07;
  }

  done = 1;
}

static struct {
  ubyte *table;
  int *pcs;
  ubyte *dst;
  int num_cands;
  uint32 dsize;
  int file;
} est_data;

#if 1
void convert_est_data_piece(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  ubyte *restrict table = est_data.table;
  int num_cands = est_data.num_cands;
  int *restrict pcs = est_data.pcs;
  uint32 dsize = est_data.dsize;
  ubyte *restrict dst = est_data.dst;
  ubyte *restrict v = permute_v;
  long64 idx;
  int n = entry_piece.num;
  int sq;
#ifdef SMALL
  int sq2;
#endif
  int pos[TBPIECES];
  int factor[TBPIECES];
  int order[TBPIECES];
  ubyte norm[TBPIECES];

  long64 idx_cache[MAX_CANDS];
  
  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
	for (m = 0; m < num_types; m++)
	  if (pcs[type_perm_list[trylist[p]][m]] != pcs[type_perm_list[trylist[q]][m]]) break;
	if (m < num_types) break;
      }
      int l = trylist[p];
      set_norm_piece(pcs, type_perm_list[l], norm, order_list[l]);
      calc_order_piece(n, order_list[l], order, norm);
      calc_factors_piece(factor, n, order_list[l], norm, entry_piece.enc_type);
      // prefetch for j = 0
      decode_piece(&entry_piece, norm, pos, factor, order, segs[i]);
      for (r = p; r < q; r++) {
	l = trylist[r];
#ifndef SMALL
	idx = pos[pidx_list[l][1]];
	for (m = 2; m < n; m++)
	  idx = (idx << 6) | pos[pidx_list[l][m]];
	sq = pos[pidx_list[l][0]];
	idx ^= sq_mask[sq];
	if (mirror[sq] < 0)
	  idx = MIRROR_A1H8(idx);
	idx |= tri0x40[sq];
#else
	idx = pos[pidx_list[l][2]];
	for (m = 3; m < n; m++)
	  idx = (idx << 6) | pos[pidx_list[l][m]];
	sq = pos[pidx_list[l][0]];
	idx ^= sq_mask[sq];
	sq2 = pos[pidx_list[l][1]];
	if (KK_map[sq][sq2] < 0)
	  idx = diagonal;
	else {
	  if (mirror[sq][sq2] < 0)
	    idx = MIRROR_A1H8(idx);
	  idx |= ((long64)KK_map[sq][sq2]) << shift[1];
	}
#endif
	__builtin_prefetch(&table[idx], 0, 3);
	idx_cache[r] = idx;
      }
      for (j = 1; j < seg_size; j++) {
	// prefetch for j, copy for j - 1
	decode_piece(&entry_piece, norm, pos, factor, order, segs[i] + j);
	for (r = p; r < q; r++) {
	  l = trylist[r];
#ifndef SMALL
	  idx = pos[pidx_list[l][1]];
	  for (m = 2; m < n; m++)
	    idx = (idx << 6) | pos[pidx_list[l][m]];
	  sq = pos[pidx_list[l][0]];
	  idx ^= sq_mask[sq];
	  if (mirror[sq] < 0)
	    idx = MIRROR_A1H8(idx);
	  idx |= tri0x40[sq];
#else
	  idx = pos[pidx_list[l][2]];
	  for (m = 3; m < n; m++)
	    idx = (idx << 6) | pos[pidx_list[l][m]];
	  sq = pos[pidx_list[l][0]];
	  idx ^= sq_mask[sq];
	  sq2 = pos[pidx_list[l][1]];
	  if (KK_map[sq][sq2] < 0)
	    idx = diagonal;
	  else {
	    if (mirror[sq][sq2] < 0)
	      idx = MIRROR_A1H8(idx);
	    idx |= ((long64)KK_map[sq][sq2]) << shift[1];
	  }
#endif
	  __builtin_prefetch(&table[idx], 0, 3);
	  dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
	  idx_cache[r] = idx;
	}
      }
      for (r = p; r < q; r++)
	dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
      p = q;
    }
  }
}
#else
void convert_est_data_piece(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  ubyte *restrict table = est_data.table;
  int num_cands = est_data.num_cands;
  int *restrict pcs = est_data.pcs;
  uint32 dsize = est_data.dsize;
  char *restrict dst = est_data.dst;
  char *restrict v = permute_v;
  long64 idx;
  int n = entry_piece.num;
  int sq;
  int pos[TBPIECES];
  int factor[TBPIECES];
  int order[TBPIECES];
  ubyte norm[TBPIECES];
  
  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
	for (m = 0; m < num_types; m++)
	  if (pcs[type_perm_list[trylist[p]][m]] != pcs[type_perm_list[trylist[q]][m]]) break;
	if (m < num_types) break;
      }
      int l = trylist[p];
      set_norm_piece(pcs, type_perm_list[l], norm, order_list[l]);
      calc_order_piece(n, order_list[l], order, norm);
      calc_factors_piece(factor, n, order_list[l], norm, entry_piece.enc_type);
      for (j = 0; j < seg_size; j++) {
	decode_piece(&entry_piece, norm, pos, factor, order, segs[i] + j);
	for (r = p; r < q; r++) {
	  l = trylist[r];
	  idx = pos[pidx_list[l][1]];
	  for (m = 2; m < n; m++)
	    idx = (idx << 6) | pos[pidx_list[l][m]];
	  sq = pos[pidx_list[l][0]];
	  idx ^= sq_mask[sq];
	  if (mirror[sq] < 0)
	    idx = MIRROR_A1H8(idx);
	  idx |= tri0x40[sq];
	  dst[r * dsize + k + j] = v[table[idx]];
	}
      }
      p = q;
    }
  }
}
#endif

#if 1
void convert_est_data_pawn(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  ubyte *restrict table = est_data.table;
  int num_cands = est_data.num_cands;
  int *restrict pcs = est_data.pcs;
  uint32 dsize = est_data.dsize;
  ubyte *restrict dst = est_data.dst;
  ubyte *restrict v = permute_v;
  int file = est_data.file;
  long64 idx;
  int n = entry_pawn.num;
  int pos[TBPIECES];
  int factor[TBPIECES];
  int order[TBPIECES];
  ubyte norm[TBPIECES];

  long64 idx_cache[MAX_CANDS];
  
  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
	for (m = 0; m < num_types; m++)
	  if (cmp[type_perm_list[trylist[p]][m]] != cmp[type_perm_list[trylist[q]][m]]) break;
	if (m < num_types) break;
      }
      int l = trylist[p];
      set_norm_pawn(pcs, type_perm_list[l], norm, order_list[l], order2_list[l]);
      calc_order_pawn(n, order_list[l], order2_list[l], order, norm);
      calc_factors_pawn(factor, n, order_list[l], order2_list[l], norm, file);
      // prefetch for j = 0
      decode_pawn(&entry_pawn, norm, pos, factor, order, segs[i], file);
      for (r = p; r < q; r++) {
	l = trylist[r];
	idx = pos[pidx_list[l][1]];
	for (m = 2; m < n; m++)
	  idx = (idx << 6) | pos[pidx_list[l][m]];
	idx ^= sq_mask_pawn[pos[pidx_list[l][0]]];
	__builtin_prefetch(&table[idx], 0, 3);
	idx_cache[r] = idx;
      }
      for (j = 1; j < seg_size; j++) {
	decode_pawn(&entry_pawn, norm, pos, factor, order, segs[i] + j, file);
	for (r = p; r < q; r++) {
	  l = trylist[r];
	  idx = pos[pidx_list[l][1]];
	  for (m = 2; m < n; m++)
	    idx = (idx << 6) | pos[pidx_list[l][m]];
	  idx ^= sq_mask_pawn[pos[pidx_list[l][0]]];
	  __builtin_prefetch(&table[idx], 0, 3);
	  dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
	  idx_cache[r] = idx;
	}
      }
      for (r = p; r < q; r++)
	dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
      p = q;
    }
  }
}
#else
void convert_est_data_pawn(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  ubyte *restrict table = est_data.table;
  int num_cands = est_data.num_cands;
  int *restrict pcs = est_data.pcs;
  uint32 dsize = est_data.dsize;
  ubyte *restrict dst = est_data.dst;
  ubyte *restrict v = permute_v;
  int file = est_data.file;
  long64 idx;
  int n = entry_pawn.num;
  int pos[TBPIECES];
  int factor[TBPIECES];
  int order[TBPIECES];
  ubyte norm[TBPIECES];
  
  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
	for (m = 0; m < num_types; m++)
	  if (cmp[type_perm_list[trylist[p]][m]] != cmp[type_perm_list[trylist[q]][m]]) break;
	if (m < num_types) break;
      }
      int l = trylist[p];
      set_norm_pawn(pcs, type_perm_list[l], norm, order_list[l], order2_list[l]);
      calc_order_pawn(n, order_list[l], order2_list[l], order, norm);
      calc_factors_pawn(factor, n, order_list[l], order2_list[l], norm, file);
      for (j = 0; j < seg_size; j++) {
	decode_pawn(&entry_pawn, norm, pos, factor, order, segs[i] + j, file);
	for (r = p; r < q; r++) {
	  l = trylist[r];
	  idx = pos[pidx_list[l][1]];
	  for (m = 2; m < n; m++)
	    idx = (idx << 6) | pos[pidx_list[l][m]];
	  idx ^= sq_mask_pawn[pos[pidx_list[l][0]]];
	  dst[r * dsize + k + j] = v[table[idx]];
	}
      }
      p = q;
    }
  }
}
#endif

void estimate_compression_piece(ubyte *restrict table, int *restrict pcs,
				  int wdl, int num_cands)
{
  int i, p;

  uint32 dsize = num_segs * seg_size;
  ubyte *restrict dst = malloc(num_cands * dsize);
  est_data.table = table;
  est_data.pcs = pcs;
  est_data.dst = dst;
  est_data.num_cands = num_cands;
  est_data.dsize = dsize;
  ubyte *dst0 = dst;

  if (num_segs > 1)
    run_threaded(convert_est_data_piece, work_est, 0);
  else
    run_single(convert_est_data_piece, work_est, 0);

  long64 csize;

  for (p = 0; p < num_cands; p++, dst += dsize) {
    struct HuffCode *restrict c = construct_pairs(dst, dsize, 20, 100, wdl);
    csize = calc_size(c);
    free(c);
    printf("[%2d] order: %d", p, order_list[trylist[p]]);
    printf("; perm:");
    for (i = 0; i < num_types; i++)
      printf(" %2d", type_perm_list[trylist[p]][i]);
    printf("; %"PRIu64"\n", csize);
    compest[trylist[p]] = csize;
  }

  free(dst0);
}

void estimate_compression_pawn(ubyte *restrict table, int *restrict pcs,
				int file, int wdl, int num_cands)
{
  int i, p;

  uint32 dsize = num_segs * seg_size;
  ubyte *restrict dst = malloc(num_cands * dsize);
  est_data.table = table;
  est_data.pcs = pcs;
  est_data.dst = dst;
  est_data.num_cands = num_cands;
  est_data.dsize = dsize;
  est_data.file = file;
  ubyte *dst0 = dst;

  if (num_segs > 1)
    run_threaded(convert_est_data_pawn, work_est, 0);
  else
    run_single(convert_est_data_pawn, work_est, 0);

  long64 csize;

  for (p = 0; p < num_cands; p++, dst += dsize) {
    struct HuffCode *restrict c = construct_pairs((ubyte *)dst, dsize, 20, 100, wdl);
    csize = calc_size(c);
    free(c);
    printf("[%2d] order: %d", p, order_list[trylist[p]]);
    printf("; perm:");
    for (i = 0; i < num_types; i++)
      printf(" %2d", type_perm_list[trylist[p]][i]);
    printf("; %"PRIu64"\n", csize);
    compest[trylist[p]] = csize;
  }

  free(dst0);
}

long64 estimate_compression(ubyte *restrict table, int *restrict bestp,
			    int *restrict pcs, int wdl, int file)
{
  int i, j, k, p, q;
  int num_cands, bp = 0;
  long64 best;
  ubyte bestperm[TBPIECES];

  if (compress_type == 1) {
    *bestp = 0;
    return 0;
  }

  if (!work_est)
    work_est = alloc_work(total_work);
  fill_work(total_work, num_segs, 0, work_est);

  for (i = 0; i < num_type_perms; i++)
    compest[i] = 0;

  for (k = 0; k < num_types - 1; k++) {
    best = UINT64_MAX;
    num_cands = 0;
    for (p = 0; p < num_types; p++) {
      for (i = 0; i < k; i++)
	if (type[p] == bestperm[i]) break;
      if (i < k) continue;
      for (q = 0; q < num_types; q++) {
	if (q == p) continue;
	for (i = 0; i < k; i++)
	  if (type[q] == bestperm[i]) break;
	if (i < k) continue;
	// look for permutation starting with bestperm[0..k-1],p,q
	for (i = 0; i < num_type_perms; i++) {
	  for (j = 0; j < k; j++)
	    if (type_perm_list[i][j] != bestperm[j]) break;
	  if (j < k) continue;
	  if (type_perm_list[i][k] == type[p] && type_perm_list[i][k+1] == type[q]) break;
	}
	if (i < num_type_perms) {
	  if (compest[i]) {
	    if (compest[i] < best) {
	      best = compest[i];
	      bp = i;
	    }
	  } else
	    trylist[num_cands++] = i;
	}
      }
    }
    for (i = 0; i < num_cands; i++)
      for (j = i + 1; j < num_cands; j++)
	if (trylist[i] > trylist[j]) {
	  int tmp = trylist[i];
	  trylist[i] = trylist[j];
	  trylist[j] = tmp;
	}
    if (file < 0)
      estimate_compression_piece(table, pcs, wdl, num_cands);
    else
      estimate_compression_pawn(table, pcs, file, wdl, num_cands);
    for (i = 0; i < num_cands; i++) {
      if (compest[trylist[i]] < best) {
	best = compest[trylist[i]];
	bp = trylist[i];
      }
    }
    bestperm[k] = type_perm_list[bp][k];
  }
  *bestp = bp;

  return best;
}

ubyte *init_permute_piece(int *pcs, int *pt, ubyte *tb_table)
{
  int i, j, k, m, l;
  int factor[TBPIECES];
  int tidx[16];

  for (i = 0, k = 0; i < 16; i++)
    if (pcs[i]) type[k++] = i;
  num_types = k;

  for (i = 0, k = 0; i < 16; i++)
    if (pcs[i] == 1) k++;
  if (k >= 3) entry_piece.enc_type = 0;
  else if (k == 2) entry_piece.enc_type = 2;
  else { /* only possible for suicide chess */
    k = 16;
    for (i = 0; i < 16; i++)
      if (pcs[i] < k && pcs[i] > 1) k = pcs[i];
    entry_piece.enc_type = 1 + k;
  }

  entry_piece.num = 0;
  for (i = 0; i < 16; i++)
    entry_piece.num += pcs[i];

  generate_type_perms(num_types);

  for (i = 0; i < num_types; i++) {
    for (j = 0; j < entry_piece.num; j++)
      if (pt[j] == type[i]) break;
    tidx[type[i]] = j;
  }
  
  if (entry_piece.enc_type == 0) { /* 111 */
    for (i = 0; i < num_type_perms;) {
      for (j = num_types - 3; j >= 0; j--)
	if (pcs[type_perm_list[i][j]] == 1 &&
		pcs[type_perm_list[i][j + 1]] == 1 &&
		pcs[type_perm_list[i][j + 2]] == 1) break;
      if (j < 0) {
	num_type_perms--;
	for (k = 0; k < num_types; k++)
	  type_perm_list[i][k] = type_perm_list[num_type_perms][k];
      } else {
	piece_perm_list[i][2] = tidx[type_perm_list[i][j]];
	piece_perm_list[i][1] = tidx[type_perm_list[i][j + 1]];
	piece_perm_list[i][0] = tidx[type_perm_list[i][j + 2]];
	for (k = 0, m = 3; k < num_types;)
	  if (k != j) {
	    for (l = 0; l < pcs[type_perm_list[i][k]]; l++)
	      piece_perm_list[i][m++] = tidx[type_perm_list[i][k]] + l;
	    k++;
	  } else
	    k += 3;
	order_list[i] = j;
	i++;
      }
    }
  } else if (entry_piece.enc_type == 2) { /* KK or 11 */
    for (i = 0; i < num_type_perms;) {
      for (j = num_types - 2; j >= 0; j--)
	if (pcs[type_perm_list[i][j]] == 1 &&
		pcs[type_perm_list[i][j + 1]] == 1) break;
      if (j < 0) {
	num_type_perms--;
	for (k = 0; k < num_types; k++)
	  type_perm_list[i][k] = type_perm_list[num_type_perms][k];
      } else {
	piece_perm_list[i][1] = tidx[type_perm_list[i][j]];
	piece_perm_list[i][0] = tidx[type_perm_list[i][j + 1]];
	for (k = 0, m = 2; k < num_types;)
	  if (k != j) {
	    for (l = 0; l < pcs[type_perm_list[i][k]]; l++)
	      piece_perm_list[i][m++] = tidx[type_perm_list[i][k]] + l;
	    k++;
	  } else
	    k += 2;
	order_list[i] = j;
	i++;
      }
    }
  } else { /* 2, or 3, or 4, or higher; only possible for suicide chess */
    int p = entry_piece.enc_type - 1;
    for (i = 0; i < num_type_perms;) {
      for (j = num_types - 1; j >= 0; j--)
	if (pcs[type_perm_list[i][j]] == p) break;
      for (k = 0; k < p; k++)
	piece_perm_list[i][k] = tidx[type_perm_list[i][j]] + k;
      for (k = 0, m = p; k < num_types; k++)
	if (k != j) {
	  for (l = 0; l < pcs[type_perm_list[i][k]]; l++)
	    piece_perm_list[i][m++] = tidx[type_perm_list[i][k]] + l;
	}
      order_list[i] = j;
      i++;
    }
  }

  for (i = 0; i < num_type_perms; i++)
    for (j = i + 1; j < num_type_perms; j++) {
      for (k = 0; k < num_types; k++)
	if (pcs[type_perm_list[i][k]] != pcs[type_perm_list[j][k]]) break;
      if (k < num_types && pcs[type_perm_list[i][k]] > pcs[type_perm_list[j][k]]) {
	ubyte tmp;
	for (k = 0; k < num_types; k++) {
	  tmp = type_perm_list[i][k];
	  type_perm_list[i][k] = type_perm_list[j][k];
	  type_perm_list[j][k] = tmp;
	}
	for (k = 0; k < entry_piece.num; k++) {
	  tmp = piece_perm_list[i][k];
	  piece_perm_list[i][k] = piece_perm_list[j][k];
	  piece_perm_list[j][k] = tmp;
	}
	tmp = order_list[i];
	order_list[i] = order_list[j];
	order_list[j] = tmp;
      }
    }

  for (i = 0; i < num_type_perms; i++)
    for (j = 0; j < entry_piece.num; j++)
      pidx_list[i][piece_perm_list[i][j]] = j;

  ubyte norm[TBPIECES];
  int order[TBPIECES];
  set_norm_piece(pcs, type_perm_list[0], norm, order_list[0]);
  calc_order_piece(entry_piece.num, order_list[0], order, norm);
  tb_size = calc_factors_piece(factor, entry_piece.num, order_list[0], norm, entry_piece.enc_type);
  printf("tb_size = %"PRIu64"\n", tb_size);

  generate_test_list(tb_size, entry_piece.num);
  if (!tb_table && !(tb_table = malloc(tb_size))) {
    printf("Out of memory.\n");
    exit(1);
  }

  init_0x40(entry_piece.num);
  work_convert = create_work(total_work, tb_size, 0);

  return tb_table;
}

void permute_piece_wdl(ubyte *tb_table, int *pcs, int *pt, ubyte *table,
			ubyte *best, ubyte *v)
{
  int i;

  permute_v = v;

  int bestp;

  estimate_compression(table, &bestp, pcs, 1, -1);

  for (i = 0; i < entry_piece.num; i++)
    best[i] = pt[piece_perm_list[bestp][i]];
  best[0] |= order_list[bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  printf("\nbest permutation:");
  for (i =0 ;i < entry_piece.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  convert_data.src = table;
  convert_data.dst = tb_table;
  convert_data.pcs = pcs;
  convert_data.p = bestp;

  run_threaded(convert_data_piece, work_convert, 1);
}

long64 estimate_piece_dtz(int *pcs, int *pt, ubyte *table, ubyte *best,
			  int *bestp, ubyte *v)
{
  int i;

  permute_v = v;

  long64 estim = estimate_compression(table, bestp, pcs, 0, -1);

  for (i = 0; i < entry_piece.num; i++)
    best[i] = pt[piece_perm_list[*bestp][i]];
  best[0] |= order_list[*bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  printf("\nbest permutation:");
  for(i = 0; i < entry_piece.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  return estim;
}

void permute_piece_dtz(ubyte *tb_table, int *pcs, ubyte *table, int bestp, ubyte *v)
{
  permute_v = v;

  convert_data.src = table;
  convert_data.dst = tb_table;
  convert_data.pcs = pcs;
  convert_data.p = bestp;

  run_threaded(convert_data_piece, work_convert, 1);
}

void init_permute_pawn(int *pcs, int *pt)
{
  int i, j, j0, j1, k, m, l;
  int tidx[16];
  int pivtype;

  init_0x40(numpcs);

  for (i = 0; i < numpcs; i++)
    pw[i] = (pt[i] == WPAWN) ? 0x38 : 0x00;
  long64 pw_mask = 0;
  for (i = 1; i < numpcs; i++)
    pw_mask |= ((long64)pw[i]) << (6 * (numpcs - i - 1));

  if (pw[0])
    for (i = 1; i < 4; i++) {
      long64 tmp = flip0x40[i];
      flip0x40[i] = flip0x40[7 - i];
      flip0x40[7 - i] = tmp;
    }

  for (i = 0; i < 64; i++) {
    sq_mask_pawn[i] = (i & 0x04) ? mask_a1h1 : 0ULL;
    sq_mask_pawn[i] |= flip0x40[i >> 3];
    sq_mask_pawn[i] ^= pw_mask;
  }

  for (i = 0, k = 0; i < 16; i++)
    if (pcs[i]) type[k++] = i;
  num_types = k;

  pivtype = pt[0];
  if (pivtype == WPAWN) {
    entry_pawn.pawns[0] = pcs[WPAWN];
    entry_pawn.pawns[1] = pcs[BPAWN];
  } else {
    entry_pawn.pawns[0] = pcs[BPAWN];
    entry_pawn.pawns[1] = pcs[WPAWN];
  }

  entry_pawn.num = 0;
  for (i = 0; i < 16; i++)
    entry_pawn.num += pcs[i];

  generate_type_perms(num_types);

  for (i = 0; i < num_types; i++) {
    for (j = 0; j < entry_pawn.num; j++)
      if (pt[j] == type[i]) break;
    tidx[type[i]] = j;
  }

  for (i = 0; i < num_type_perms; i++) {
    for (j0 = 0; j0 < num_types; j0++)
      if (type_perm_list[i][j0] == pivtype) break;
    for (j1 = 0; j1 < num_types; j1++)
      if (type_perm_list[i][j1] == (pivtype ^ 0x08)) break;
    if (j1 == num_types) j1 = 0x0f;
    order_list[i] = j0;
    order2_list[i] = j1;
    for (m = 0; m < entry_pawn.pawns[0]; m++)
      piece_perm_list[i][m] = tidx[pivtype] + m;
    for (; m < entry_pawn.pawns[0] + entry_pawn.pawns[1]; m++)
      piece_perm_list[i][m] = tidx[pivtype ^ 0x08] + (m - entry_pawn.pawns[0]);
    for (k = 0; k < num_types; k++)
      if (k != j0 && k != j1)
	for (l = 0; l < pcs[type_perm_list[i][k]]; l++)
	  piece_perm_list[i][m++] = tidx[type_perm_list[i][k]] + l;
  }

  for (i = 0; i < 16; i++)
    cmp[i] = pcs[i];
  cmp[WPAWN] = 13;
  cmp[BPAWN] = 14;

  for (i = 0; i < num_type_perms; i++)
    for (j = i + 1; j < num_type_perms; j++) {
      for (k = 0; k < num_types; k++)
	if (cmp[type_perm_list[i][k]] != cmp[type_perm_list[j][k]]) break;
      if (k < num_types && cmp[type_perm_list[i][k]] > cmp[type_perm_list[j][k]]) {
	ubyte tmp;
	for (k = 0; k < num_types; k++) {
	  tmp = type_perm_list[i][k];
	  type_perm_list[i][k] = type_perm_list[j][k];
	  type_perm_list[j][k] = tmp;
	}
	for (k = 0; k < entry_pawn.num; k++) {
	  tmp = piece_perm_list[i][k];
	  piece_perm_list[i][k] = piece_perm_list[j][k];
	  piece_perm_list[j][k] = tmp;
	}
	tmp = order_list[i];
	order_list[i] = order_list[j];
	order_list[j] = tmp;
	tmp = order2_list[i];
	order2_list[i] = order2_list[j];
	order2_list[j] = tmp;
      }
    }

  for (i = 0; i < num_type_perms; i++)
    for (j = 0; j < entry_pawn.num; j++)
      pidx_list[i][piece_perm_list[i][j]] = j;

  work_convert = alloc_work(total_work);
}

ubyte *init_permute_file(int *pcs, int file, ubyte *tb_table)
{
  int factor[TBPIECES];
  ubyte norm[TBPIECES];
  int order[TBPIECES];
  set_norm_pawn(pcs, type_perm_list[0], norm, order_list[0], order2_list[0]);
  calc_order_pawn(entry_pawn.num, order_list[0], order2_list[0], order, norm);
  tb_size = calc_factors_pawn(factor, entry_pawn.num, order_list[0], order2_list[0], norm, file);
  printf("tb_size = %"PRIu64"\n", tb_size);

  generate_test_list(tb_size, entry_pawn.num);

  if (!tb_table && !(tb_table = malloc(tb_size))) {
    printf("Out of memory.\n");
    exit(1);
  }

  fill_work(total_work, tb_size, 0, work_convert);

  return tb_table;
}

void permute_pawn_wdl(ubyte *tb_table, int *pcs, int *pt, ubyte *table, ubyte *best, int file, ubyte *v)
{
  int i;

  permute_v = v;

  int bestp;

  estimate_compression(table, &bestp, pcs, 1, file);

  for (i = 0; i < entry_pawn.num; i++)
    best[i] = pt[piece_perm_list[bestp][i]];
  best[0] |= order_list[bestp] << 4;
  best[1] |= order2_list[bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  if ((best[1] >> 4) < 0x0f)
    printf(" %d", best[1] >> 4);
  printf("\nbest permutation:");
  for (i = 0; i < entry_pawn.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  convert_data.src = table;
  convert_data.dst = tb_table;
  convert_data.pcs = pcs;
  convert_data.p = bestp;
  convert_data.file = file;

  run_threaded(convert_data_pawn, work_convert, 1);
}

long64 estimate_pawn_dtz(int *pcs, int *pt, ubyte *table, ubyte *best, int *bestp, int file, ubyte *v)
{
  int i;

  permute_v = v;

  long64 estim = estimate_compression(table, bestp, pcs, 0, file);

  for (i = 0; i < entry_pawn.num; i++)
    best[i] = pt[piece_perm_list[*bestp][i]];
  best[0] |= order_list[*bestp] << 4;
  best[1] |= order2_list[*bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  if ((best[1] >> 4) < 0x0f) printf(" %d", best[1] >> 4);
  printf("\nbest permutation:");
  for (i = 0; i < entry_pawn.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  return estim;
}

void permute_pawn_dtz(ubyte *tb_table, int *pcs, ubyte *table, int bestp, int file, ubyte *v)
{
  permute_v = v;

  convert_data.src = table;
  convert_data.dst = tb_table;
  convert_data.pcs = pcs;
  convert_data.p = bestp;
  convert_data.file = file;

  run_threaded(convert_data_pawn, work_convert, 1);
}

