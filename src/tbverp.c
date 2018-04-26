/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <getopt.h>
#include <stdarg.h>
#include <inttypes.h>

#define HAS_PAWNS
#define VERIFICATION

#include "board.h"
#include "decompress.h"
#include "defs.h"
#include "threads.h"

#ifndef SUICIDE
static int white_king, black_king;
#endif

#include "probe.c"
#include "board.c"

#define MAX_PIECES 8

void error(char *str, ...);

extern int use_envdirs;

extern int total_work;
extern struct thread_data *thread_data;
extern int numthreads;
extern struct timeval start_time, cur_time;

extern struct TBEntry entry;

static uint64_t *work_g, *work_piv, *work_p, *work_part;

uint8_t *table_w, *table_b;

static uint64_t size, pawnsize;
static uint64_t begin;
static int slice_threading_low, slice_threading_high;

static int file;

int numpcs;
int numpawns;
int pp_num, pp_shift;
uint64_t pp_mask;
int ply_accurate_win, ply_accurate_loss;
uint8_t *load_table;
int load_bside;
struct TBEntry_pawn *load_entry;
uint8_t *load_opp_table;
int *load_pieces, *load_opp_pieces;
uint8_t (*load_map)[256];
uint8_t *tb_table;
int tb_perm[MAX_PIECES];

int has_white_pawns, has_black_pawns;
#ifdef SUICIDE
int stalemate_w, stalemate_b;
int last_w, last_b;
#endif
int symmetric, split;

static int white_pcs[MAX_PIECES], black_pcs[MAX_PIECES];
static int white_all[MAX_PIECES], black_all[MAX_PIECES];
static int pt[MAX_PIECES], pw[MAX_PIECES];
#ifndef SUICIDE
static int pcs2[MAX_PIECES];
#endif

#include "genericp.c"

#if defined(REGULAR)
#include "rtbverp.c"
#elif defined(SUICIDE)
#include "stbverp.c"
#elif defined(ATOMIC)
#include "atbverp.c"
#elif defined(LOSER)
#include "ltbverp.c"
#endif

#define HUGEPAGESIZE 2*1024*1024

extern char *optarg;

static char *tablename;

static int log = 0;
static int num_errors = 0;
static FILE *L;

void error(char *str, ...)
{
  va_list ap;

  va_start(ap, str);
  vprintf(str, ap);
  va_end(ap);
  fflush(stdout);
  if (log) {
    va_start(ap, str);
    vfprintf(L, str, ap);
    va_end(ap);
    fflush(L);
  }
  num_errors++;
  if (num_errors == 10) {
    if (log) fclose(L);
    exit(1);
  }
}

void calc_pawn_table_unthreaded(void)
{
  uint64_t idx, idx2;
  uint64_t size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  bitboard occ;
  int p[MAX_PIECES];

  // set_tbl_to_wdl(0);

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;

  int *old_p = thread_data[0].p;
  thread_data[0].p = p;

  for (idx = 0; idx < pawnsize; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5 - cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      thread_data[0].occ = occ;
      if (has_white_pawns)
        run_single(calc_pawn_moves_w, work_p, 0);
      if (has_black_pawns)
        run_single(calc_pawn_moves_b, work_p, 0);
    }
  }
  printf("\n");
  thread_data[0].p = old_p;
}

void calc_pawn_table_threaded(void)
{
  uint64_t idx, idx2;
  uint64_t size_p;
  int i;
  int cnt = 0, cnt2 = 0;
  bitboard occ;
  int p[MAX_PIECES];

  // set_tbl_to_wdl(0);

  size_p = 1ULL << shift[numpawns - 1];
  begin = 0;

  for (idx = 0; idx < pawnsize; idx++, begin += size_p) {
    if (!cnt) {
      printf("%c%c ", 'a' + file, '2' + (pw[0] ? 5 - cnt2 : cnt2));
      fflush(stdout);
      cnt2++;
      cnt = pawnsize / 6;
    }
    cnt--;
    FILL_OCC_PAWNS {
      for (i = 0; i < numthreads; i++)
        thread_data[i].occ = occ;
      for (i = 0; i < numthreads; i++)
        memcpy(thread_data[i].p, p, MAX_PIECES * sizeof(int));
      if (has_white_pawns)
        run_threaded(calc_pawn_moves_w, work_p, 0);
      if (has_black_pawns)
        run_threaded(calc_pawn_moves_b, work_p, 0);
    }
  }
  printf("\n");
}

static void check_huffman_tb(int wdl)
{
  struct tb_handle *F = open_tb(tablename, wdl);
  decomp_init_table(F);
  struct TBEntry_pawn *entry = &(F->entry_pawn);
  printf("%s%s:", tablename, wdl ? WDLSUFFIX : DTZSUFFIX);
  int warning = 0;
  for (int f = 0; f < 4; f++) {
    int m0 = entry->file[f].precomp[0]->max_len;
    printf(" %d", m0);
    int m1 = 0;
    if (F->split) {
      m1 = entry->file[f].precomp[1]->max_len;
      printf(" %d", m1);
    }
    if (m0 >= 32 || m1 >= 32)
      warning = 1;
  }
  if (warning)
    printf(" ----- WARNING!!!!!");
  printf("\n");
  close_tb(F);
}

static void check_huffman()
{
  check_huffman_tb(1);
  check_huffman_tb(0);
}

static struct option options[] = {
  { "threads", 1, NULL, 't' },
  { "log", 0, NULL, 'l' },
  { "huffman", 0, NULL, 'h' },
//  { "wdl", 0, NULL, 'w' },
  { 0, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
  int i, j;
  int color;
  int val, longindex;
  int pcs[16];
  int wdl_only = 0;
  int check_huff = 0;

  numthreads = 1;
  do {
//    val = getopt_long(argc, argv, "t:lwc", options, &longindex);
    val = getopt_long(argc, argv, "t:ldh", options, &longindex);
    switch (val) {
    case 't':
      numthreads = atoi(optarg);
      break;
    case 'l':
      log = 1;
      break;
    case 'w':
      wdl_only = 1;
      break;
    case 'd':
      use_envdirs = 1;
      break;
    case 'h':
      check_huff = 1;
      break;
    }
  } while (val != EOF);

  if (optind >= argc) {
    fprintf(stderr, "No tablebase specified.\n");
    exit(1);
  }
  tablename = argv[optind];

  for (i = 0; i < 16; i++)
    pcs[i] = 0;

  numpcs = strlen(tablename) - 1;
  color = 0;
  j = 0;
  for (i = 0; i < strlen(tablename); i++)
    switch (tablename[i]) {
    case 'P':
      pcs[PAWN | color]++;
      pt[j++] = PAWN | color;
      break;
    case 'N':
      pcs[KNIGHT | color]++;
      pt[j++] = KNIGHT | color;
      break;
    case 'B':
      pcs[BISHOP | color]++;
      pt[j++] = BISHOP | color;
      break;
    case 'R':
      pcs[ROOK | color]++;
      pt[j++] = ROOK | color;
      break;
    case 'Q':
      pcs[QUEEN | color]++;
      pt[j++] = QUEEN | color;
      break;
    case 'K':
      pcs[KING | color]++;
      pt[j++] = KING | color;
      break;
    case 'v':
      if (color) exit(1);
      color = 0x08;
      break;
    default:
      exit(1);
    }
  if (!color) exit(1);

  numpawns = pcs[WPAWN] + pcs[BPAWN];
  has_white_pawns = (pcs[WPAWN] != 0);
  has_black_pawns = (pcs[BPAWN] != 0);

  // move pieces to back
  for (i = j = numpcs - 1; i >= numpawns; i--, j--) {
    while ((pt[j] & 0x07) == 1) j--;
    pt[i] = pt[j];
  }
  if (pcs[WPAWN] > 0 && (pcs[BPAWN] == 0 || pcs[WPAWN] <= pcs[BPAWN])) {
    for (i = 0; i < pcs[WPAWN]; i++)
      pt[i] = WPAWN;
    for (; i < numpawns; i++)
      pt[i] = BPAWN;
  } else {
    for (i = 0; i < pcs[BPAWN]; i++)
      pt[i] = BPAWN;
    for (; i < numpawns; i++)
      pt[i] = WPAWN;
  }

  if (numpawns == 0) {
    fprintf(stderr, "Expecting pawns.\n");
    exit(1);
  }

  decomp_init_pawn(pcs, pt);

  if (check_huff) {
    check_huffman();
    return 0;
  }

  for (i = 0; i < numpcs - 2; i++)
    if (pt[i + 1] != pt[0])
      break;
  pp_num = i;
  pp_shift = 6 * (numpcs - 1 - i);

  for (i = 0; i < 8; i++)
    if (pcs[i] != pcs[i + 8]) break;
  symmetric = (i == 8);

  if (symmetric) {
    fprintf(stderr, "Can't handle symmetric tables.\n");
    exit(1);
  }

  init_tablebases();

  if (numthreads < 1) numthreads = 1;

  printf("number of threads = %d\n", numthreads);

  if (numthreads == 1)
    total_work = 1;
  else
    total_work = 100 + 10 * numthreads;

  slice_threading_low = (numthreads > 1) && (numpcs - numpawns) >= 2;
  slice_threading_high = (numthreads > 1) && (numpcs - numpawns) >= 3;

  size = 6ULL << (6 * (numpcs-1));
  pawnsize = 6ULL << (6 * (numpawns - 1));

  for (i = 0; i < numpcs; i++) {
    shift[i] = (numpcs - i - 1) * 6;
    mask[i] = 0x3fULL << shift[i];
  }

  work_g = create_work(total_work, size, 0x3f);
  work_piv = create_work(total_work, 1ULL << shift[0], 0);
  work_p = create_work(total_work, 1ULL << shift[numpawns - 1], 0x3f);
  work_part = alloc_work(total_work);

#if 1
  static int piece_order[16] = {
    0, 0, 3, 5, 7, 9, 1, 0,
    0, 0, 4, 6, 8, 10, 2, 0
  };

  for (i = 0; i < numpcs; i++)
    for (j = i + 1; j < numpcs; j++)
      if (piece_order[pt[i]] > piece_order[pt[j]]) {
        int tmp = pt[i];
        pt[i] = pt[j];
        pt[j] = tmp;
      }
#endif

#ifdef ATOMIC
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] == WKING) break;
  white_all[j++] = i;
  for (i = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08) && pt[i] != WKING)
      white_all[j++] = i;
  white_all[j] = -1;
#else
  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08))
      white_all[j++] = i;
  white_all[j] = -1;
#endif

#ifdef ATOMIC
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] == BKING) break;
  black_all[j++] = i;
  for (i = 0; i < numpcs; i++)
    if ((pt[i] & 0x08) && pt[i] != BKING)
      black_all[j++] = i;
  black_all[j] = -1;
#else
  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] & 0x08)
      black_all[j++] = i;
  black_all[j] = -1;
#endif

  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08) && pt[i] != 0x01)
      white_pcs[j++] = i;
  white_pcs[j] = -1;

  for (i = 0, j = 0; i < numpcs; i++)
    if ((pt[i] & 0x08) && pt[i] != 0x09)
      black_pcs[j++] = i;
  black_pcs[j] = -1;

  for (i = 0; i < numpcs; i++)
    pw[i] = (pt[i] == WPAWN) ? 0x38 : 0x00;
  pw_mask = 0;
  for (i = 1; i < numpcs; i++)
    pw_mask |= (uint64_t)pw[i] << (6 * (numpcs - i - 1));
  pw_pawnmask = pw_mask >> (6 * (numpcs - numpawns));

  idx_mask1[numpcs - 1] = 0xffffffffffffffc0ULL;
  idx_mask2[numpcs - 1] = 0;
  for (i = numpcs - 2; i >= 0; i--) {
    idx_mask1[i] = idx_mask1[i + 1] << 6;
    idx_mask2[i] = (idx_mask2[i + 1] << 6) | 0x3f;
  }

  for (i = 0; i < numpcs; i++)
    pw_capt_mask[i] = ((pw_mask & idx_mask1[i]) >> 6)
                                    | (pw_mask & idx_mask2[i]);

#ifndef SUICIDE
  for (i = 0; i < numpcs; i++)
    if (pt[i] == WKING)
      white_king = i;

  for (i = 0; i < numpcs; i++)
    if (pt[i] == BKING)
      black_king = i;
#else
  int stalemate = 0;
  for (i = 0; i < numpcs; i++)
    stalemate += (pt[i] & 0x08) ? 1 : -1;
  if (stalemate > 0) {
    stalemate_w = 2;
    stalemate_b = -2;
  } else if (stalemate < 0) {
    stalemate_w = -2;
    stalemate_b = 2;
  } else
    stalemate_w = stalemate_b = 0;

  last_w = -1;
  j = 0;
  for (i = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08)) j++;
  if (j == 1) {
    for (i = 0; i < numpcs; i++)
      if (!(pt[i] & 0x08)) break;
    last_w = i;
  }
  last_b = -1;
  j = 0;
  for (i = 0; i < numpcs; i++)
    if (pt[i] & 0x08) j++;
  if (j == 1) {
    for (i = 0; i < numpcs; i++)
      if (pt[i] & 0x08) break;
    last_b = i;
  }
#endif

  int has_white_pawns = 0;
  int has_black_pawns = 0;
  for (i = 0; i < numpawns; i++) {
    if (pt[i] == WPAWN) has_white_pawns = 1;
    if (pt[i] == BPAWN) has_black_pawns = 1;
  }

  table_w = alloc_huge(2 * size);
  table_b = table_w + size;

  printf("Verifying %s.\n", tablename);
  if (log) {
    L = fopen(LOGFILE, "a");
    fprintf(L, "Verifying %s...", tablename);
    fflush(L);
  }

  init_threads(1);
  init_tables();

  gettimeofday(&start_time, NULL);
  cur_time = start_time;

  struct tb_handle *G = open_tb(tablename, 1);
  decomp_init_table(G);

  struct tb_handle *H = NULL;
  if (!wdl_only) {
    H = open_tb(tablename, 0);
    decomp_init_table(H);
    init_wdl_matrix();
  } else {
    init_w_wdl_matrix();
  }

  for (file = 0; file < 4; file++) {
    printf("Verifying the %c-file.\n", 'a' + file);

    memset(piv_idx, 0, 8 * 64);
    memset(piv_valid, 0, 64);
    for (j = 0; j < 6; j++) {
      piv_sq[j] = ((j + 1) << 3 | file) ^ pw[0];
      piv_idx[piv_sq[j]] = ((uint64_t)j) << shift[0];
      piv_valid[piv_sq[j]] = 1;
    }

    if (pp_num > 0) {
      pp_mask = 0xff000000000000ffULL;
      for (j = 0; j < file; j++) {
        pp_mask |= (0x0101010101010101ULL << j)
                    | (0x0101010101010101ULL << (7 - j));
      }
    }

    printf("Initialising broken positions.\n");
    run_threaded(pp_num == 0 ? calc_broken : calc_broken_pp, work_g, 1);
//    run_threaded(calc_broken, work_g, 1);
    printf("Calculating white captures.\n");
    calc_captures_w();
    if (has_white_pawns) {
      printf("Calculating white pawn captures.\n");
#ifdef SUICIDE
      run_threaded(last_b >= 0 ? calc_last_pawn_capture_w : calc_pawn_captures_w, work_g, 1);
#else
      run_threaded(calc_pawn_captures_w, work_g, 1);
#endif
    }
    printf("Calculating black captures.\n");
    calc_captures_b();
    if (has_black_pawns) {
      printf("Calculating black pawn captures.\n");
#ifdef SUICIDE
      run_threaded(last_w >= 0 ? calc_last_pawn_capture_b : calc_pawn_captures_b, work_g, 1);
#else
      run_threaded(calc_pawn_captures_b, work_g, 1);
#endif
    }
#ifndef SUICIDE
    printf("Calculating mate positions.\n");
    run_threaded(calc_mates, work_g, 1);
#else
    init_capt_threat();
    printf("Calculating white threats.\n");
    iter_table = table_b;
    iter_table_opp = table_w;
    iter_pcs_opp = white_pcs;
    run_threaded(calc_threats, work_g, 1);
    printf("Calculating black threats.\n");
    iter_table = table_w;
    iter_table_opp = table_b;
    iter_pcs_opp = black_pcs;
    run_threaded(calc_threats, work_g, 1);
#endif

    load_entry = (struct TBEntry_pawn *)get_entry(G);

    if (!wdl_only) {
      printf("Loading wdl, white.\n");
      tb_table = decompress_table(G, 0, file);
      set_perm(G, 0, file, tb_perm, pt);
      load_table = table_w;
      load_bside = 0;
      run_threaded(load_wdl, work_g, 1);

      printf("Loading wdl, black.\n");
      tb_table = decompress_table(G, 1, file);
      set_perm(G, 1, file, tb_perm, pt);
      load_table = table_b;
      load_bside = 1;
      run_threaded(load_wdl, work_g, 1);

      load_entry = (struct TBEntry_pawn *)get_entry(H);
      ply_accurate_win = get_ply_accurate_win(H, file);
      ply_accurate_loss = get_ply_accurate_loss(H, file);
      load_map = get_dtz_map(H, file);
      int dtz_side = get_dtz_side(H, file);
      init_wdl_dtz();

      if (slice_threading_low)
        calc_pawn_table_threaded();
      else
        calc_pawn_table_unthreaded();

      printf("Loading dtz, %s.\n", dtz_side == 0 ? "white" : "black");
      tb_table = decompress_table(H, 0, file);
      set_perm(H, 0, file, tb_perm, pt);
      load_table = dtz_side == 0 ? table_w : table_b;
      load_bside = dtz_side;
      load_opp_table = dtz_side == 0 ? table_b : table_w;
      if (load_map)
        run_threaded(load_dtz_mapped, work_g, 1);
      else
        run_threaded(load_dtz, work_g, 1);

      if (dtz_side == 0) {
        load_table = table_w;
        load_opp_table = table_b;
        load_pieces = white_pcs;
        load_opp_pieces = black_pcs;
      } else {
        load_table = table_b;
        load_opp_table = table_w;
        load_pieces = black_pcs;
        load_opp_pieces = white_pcs;
      }

      init_pawn_dtz(0);
      printf("Verifying %s.\n", dtz_side ? "white" : "black");
      run_threaded(verify_opp, work_g, 1);
      init_pawn_dtz(1);
      printf("Verifying %s.\n", dtz_side ? "black" : "white");
      run_threaded(verify_dtz, work_g, 1);
    } else { // currently broken (and disabled)
      // white, wdl
      printf("Loading wdl, white.\n");
      tb_table = decompress_table(G, 0, file);
      set_perm(G, 0, file, tb_perm, pt);
      load_table = table_w;
      load_bside = 0;
      run_threaded(wdl_load_wdl, work_g, 1);

      // black, wdl
      printf("Loading wdl, black.\n");
      tb_table = decompress_table(G, 1, file);
      set_perm(G, 1, file, tb_perm, pt);
      load_table = table_b;
      load_bside = 1;
      run_threaded(wdl_load_wdl, work_g, 1);

      ply_accurate_win = get_ply_accurate_win(G, file);
      ply_accurate_loss = get_ply_accurate_loss(G, file);
      init_wdl();
      load_table = table_w;
      load_opp_table = table_b;
      load_pieces = white_pcs;
      printf("Verifying white.\n");
      run_threaded(verify_wdl_w, work_g, 1);

      ply_accurate_win = get_ply_accurate_loss(G, file);
      ply_accurate_loss = get_ply_accurate_win(G, file);
      init_wdl();
      load_table = table_b;
      load_opp_table = table_w;
      load_pieces = black_pcs;
      printf("Verifying black.\n");
      run_threaded(verify_wdl_b, work_g, 1);
    }

    printf("\n");
  }

  if (num_errors == 0) {
    printf("No errors.\n");
    if (log) fprintf(L, " No errors.\n");
  }
  if (log) fclose(L);

  return 0;
}
