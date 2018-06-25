/*
  Copyright (c) 2011-2013, 2017, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "compress.h"
#include "defs.h"
#include "probe.h"
#include "threads.h"
#include "util.h"

uint64_t *restrict work_convert = NULL;
uint64_t *restrict work_est = NULL;

extern char *tablename;

extern int total_work;
extern int numthreads;
extern uint64_t sq_mask[64];
extern int compress_type;

#ifdef SMALL
extern short KK_map[64][64];
extern int8_t mirror[64][64];
extern uint64_t diagonal;
extern int shift[];
#endif

char name[64];

struct TBEntry_piece entry_piece;
struct TBEntry_pawn entry_pawn;

#if TBPIECES <= 6
#define MAX_PERMS 720
#define MAX_CANDS 30
#elif TBPIECES == 7
#define MAX_PERMS 5040
#define MAX_CANDS 42
#else
#error unsupported
#endif

uint8_t order_list[MAX_PERMS];
uint8_t order2_list[MAX_PERMS];

uint8_t piece_perm_list[MAX_PERMS][TBPIECES];
uint8_t pidx_list[MAX_PERMS][TBPIECES];

int num_types, num_type_perms;
uint8_t type[TBPIECES];
uint8_t type_perm_list[MAX_PERMS][TBPIECES];

uint64_t compest[MAX_PERMS];

int trylist[MAX_CANDS];

int numpawns;
int numpcs;

static int pw[TBPIECES];
int cmp[16];

void setup_pieces(struct TBEntry_piece *ptr, uint8_t *data);

static uint64_t tb_size;
static uint8_t perm_tmp[TBPIECES];

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
uint64_t *restrict segs = NULL;

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

uint64_t llrand(void)
{
  uint64_t rand1, rand2;

  rand1 = myrand();
  rand2 = myrand();

  rand1 = (rand1 << 24) + rand2;

  return rand1;
}

void generate_test_list(uint64_t size, int n)
{
  int i, j;

  if (segs) free(segs);

  if (n <= 3 || size <= 100000) {
    // 1 entry covering whole table
    num_segs = 1;
    segs = malloc(sizeof(uint64_t));
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
    uint64_t max = size - num_segs * (uint64_t)seg_size + 1;
    segs = malloc(sizeof(uint64_t) * num_segs);
    for (i = 0; i < num_segs; i++)
      segs[i] = llrand() % max;
    for (i = 0; i < num_segs; i++)
      for (j = i + 1; j < num_segs; j++)
        if (segs[i] > segs[j]) {
          uint64_t tmp = segs[i];
          segs[i] = segs[j];
          segs[j] = tmp;
        }
    for (i = 0; i < num_segs; i++)
      segs[i] += i * seg_size;
  }
}

uint64_t mask_a1h1, mask_a1h8;

#define MIRROR_A1H8(x) ((((x) & mask_a1h8) << 3) | (((x) >> 3) & mask_a1h8))

#ifndef SMALL
static const int8_t mirror[] = {
  0, 1, 1, 1, 1, 1, 1, 0,
 -1, 0, 1, 1, 1, 1, 0,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1,-1, 0, 0,-1,-1,-1,
 -1,-1, 0, 1, 1, 0,-1,-1,
 -1, 0, 1, 1, 1, 1, 0,-1,
  0, 1, 1, 1, 1, 1, 1, 0
};

static uint64_t tri0x40[] = {
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

static uint64_t sq_mask_pawn[64];

static uint64_t flip0x40[] = {
  0, 0, 1, 2, 3, 4, 5, 0
};

static void p_set_norm_piece(int *pcs, uint8_t *type_perm, uint8_t *norm, int order)
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

static void p_set_norm_pawn(int *pcs, uint8_t *type_perm, uint8_t *norm, int order, int order2)
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

static void init_0x40(int numpcs)
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

uint64_t init_permute_piece(int *pcs, int *pt)
{
  int i, j, k, m, l;
  uint64_t factor[TBPIECES];
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
        uint8_t tmp;
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

  uint8_t norm[TBPIECES];
  int order[TBPIECES];
  p_set_norm_piece(pcs, type_perm_list[0], norm, order_list[0]);
  calc_order_piece(entry_piece.num, order_list[0], order, norm);
  tb_size = calc_factors_piece(factor, entry_piece.num, order_list[0], norm, entry_piece.enc_type);
  printf("tb_size = %"PRIu64"\n", tb_size);

  generate_test_list(tb_size, entry_piece.num);

  init_0x40(entry_piece.num);
  work_convert = create_work(total_work, tb_size, 0);

  return tb_size;
}

uint64_t init_permute_file(int *pcs, int file)
{
  uint64_t factor[TBPIECES];
  uint8_t norm[TBPIECES];
  int order[TBPIECES];
  p_set_norm_pawn(pcs, type_perm_list[0], norm, order_list[0], order2_list[0]);
  calc_order_pawn(entry_pawn.num, order_list[0], order2_list[0], order, norm);
  tb_size = calc_factors_pawn(factor, entry_pawn.num, order_list[0], order2_list[0], norm, file);
  printf("tb_size = %"PRIu64"\n", tb_size);

  generate_test_list(tb_size, entry_pawn.num);

/*
  if (!tb_table && !(tb_table = malloc(TB_SIZE(*tb_table)))) {
    fprintf(stderr, "Out of memory.\n");
    exit(1);
  }
*/

  fill_work(total_work, tb_size, 0, work_convert);

  return tb_size;
}

#define T u8
#include "permute_tmpl.c"
#undef T

#define T u16
#include "permute_tmpl.c"
#undef T

void permute_piece_wdl(u8 *tb_table, int *pcs, int *pt, u8 *table,
    uint8_t *best, u8 *v)
{
  int i;

  permute_v_u8 = v;

  int bestp;

  estimate_compression_u8(table, &bestp, pcs, 1, -1);

  for (i = 0; i < entry_piece.num; i++)
    best[i] = pt[piece_perm_list[bestp][i]];
  best[0] |= order_list[bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  printf("\nbest permutation:");
  for (i = 0 ;i < entry_piece.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  convert_data_u8.src = table;
  convert_data_u8.dst = tb_table;
  convert_data_u8.pcs = pcs;
  convert_data_u8.p = bestp;

  run_threaded(convert_data_piece_u8, work_convert, 1);
}

void init_permute_pawn(int *pcs, int *pt)
{
  int i, j, j0, j1, k, m, l;
  int tidx[16];
  int pivtype;

  init_0x40(numpcs);

  for (i = 0; i < numpcs; i++)
    pw[i] = (pt[i] == WPAWN) ? 0x38 : 0x00;
  uint64_t pw_mask = 0;
  for (i = 1; i < numpcs; i++)
    pw_mask |= ((uint64_t)pw[i]) << (6 * (numpcs - i - 1));

  if (pw[0])
    for (i = 1; i < 4; i++) {
      uint64_t tmp = flip0x40[i];
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
        uint8_t tmp;
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

void permute_pawn_wdl(u8 *tb_table, int *pcs, int *pt, u8 *table,
    uint8_t *best, int file, u8 *v)
{
  int i;

  permute_v_u8 = v;

  int bestp;

  estimate_compression_u8(table, &bestp, pcs, 1, file);

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

  convert_data_u8.src = table;
  convert_data_u8.dst = tb_table;
  convert_data_u8.pcs = pcs;
  convert_data_u8.p = bestp;
  convert_data_u8.file = file;

  run_threaded(convert_data_pawn_u8, work_convert, 1);
}

void permute_piece_dtz_u16_full(u16 *tb_table, int *pcs, u16 *table, int bestp,
    u16 *v, uint64_t tb_step)
{
  char name[64];
  FILE *F;

  sprintf(name, "%s.perm", tablename);
  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(EXIT_FAILURE);
  }

  uint64_t begin = 0;
  while (1) {
    uint64_t end = min(begin + tb_step, tb_size);
    fill_work_offset(total_work, end - begin, 0, work_convert, begin);
    permute_piece_dtz_u16(tb_table - begin, pcs, table, bestp, v);

    if (end == tb_size) break;

    write_data(F, (uint8_t *)tb_table, 0, 2 * (end - begin), NULL);

    begin = end;
  }

  fclose(F);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(EXIT_FAILURE);
  }

  begin = 0;
  uint16_t *ptr = table;
  while (1) {
    uint64_t end = min(begin + tb_step, tb_size);

    if (end < tb_size) {
      uint64_t total = end - begin;
      read_data_u8(F, (uint8_t *)ptr, 2 * total, NULL);
      ptr += total;
    } else {
      memcpy(ptr, tb_table, 2 * (end - begin));
      break;
    }

    begin = end;
  }

  fclose(F);
  unlink(name);
}

void permute_pawn_dtz_u16_full(u16 *tb_table, int *pcs, u16 *table, int bestp,
    int file, u16 *v, uint64_t tb_step)
{
  char name[64];
  FILE *F;

  sprintf(name, "%s.perm", tablename);
  if (!(F = fopen(name, "wb"))) {
    fprintf(stderr, "Could not open %s for writing.\n", name);
    exit(EXIT_FAILURE);
  }

  uint64_t begin = 0;
  while (1) {
    uint64_t end = min(begin + tb_step, tb_size);
    fill_work_offset(total_work, end - begin, 0, work_convert, begin);
    permute_pawn_dtz_u16(tb_table - begin, pcs, table, bestp, file, v);

    if (end == tb_size) break;

    write_data(F, (uint8_t *)tb_table, 0, 2 * (end - begin), NULL);

    begin = end;
  }

  fclose(F);

  if (!(F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(EXIT_FAILURE);
  }

  begin = 0;
  uint16_t *ptr = table;
  while (1) {
    uint64_t end = min(begin + tb_step, tb_size);

    if (end < tb_size) {
      uint64_t total = end - begin;
      read_data_u8(F, (uint8_t *)ptr, 2 * total, NULL);
      ptr += total;
    } else {
      memcpy(ptr, tb_table, 2 * (end - begin));
      break;
    }

    begin = end;
  }

  fclose(F);
  unlink(name);
}
