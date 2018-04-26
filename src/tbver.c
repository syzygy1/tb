/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "board.h"
#include "decompress.h"
#include "defs.h"
#include "threads.h"
#include "util.h"

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

static uint64_t *work_g;
#ifndef SMALL
static uint64_t *work_piv;
#else
static uint64_t *work_piv0, *work_piv1;
#endif

uint8_t *table_w, *table_b;
int numpcs;
int numpawns = 0;
int ply_accurate_win, ply_accurate_loss;
//int dtz_side;
uint8_t *load_table;
int load_bside;
struct TBEntry_piece *load_entry;
uint8_t *load_opp_table;
int *load_pieces, *load_opp_pieces;
uint8_t (*load_map)[256];
uint8_t *tb_table;
int tb_perm[MAX_PIECES];

static uint64_t size;

static int white_pcs[MAX_PIECES], black_pcs[MAX_PIECES];
static int pt[MAX_PIECES];
#ifndef SUICIDE
static int pcs2[MAX_PIECES];
#endif

#ifndef SMALL
#include "generic.c"
#else
#include "generics.c"
#endif

#if defined(REGULAR)
#include "rtbver.c"
#elif defined(SUICIDE)
#include "stbver.c"
#elif defined(ATOMIC)
#include "atbver.c"
#elif defined(LOSER)
#include "ltbver.c"
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
    exit(EXIT_FAILURE);
  }
}

static void check_huffman_tb(int wdl)
{
  struct tb_handle *F = open_tb(tablename, wdl);
  decomp_init_table(F);
  struct TBEntry_piece *entry = &(F->entry_piece);
  printf("%s%s:", tablename, wdl ? WDLSUFFIX : DTZSUFFIX);
  int m0 = entry->precomp[0]->max_len;
  printf(" %d", m0);
  int m1 = 0;
  if (F->split) {
    m1 = entry->precomp[1]->max_len;
    printf(" %d", m1);
  }
  if (m0 >= 32 || m1 >= 32)
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
#ifdef SUICIDE
  int switched = 0;
#endif

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
    exit(EXIT_FAILURE);
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
      if (color) exit(EXIT_FAILURE);
      color = 0x08;
      break;
    default:
      exit(EXIT_FAILURE);
    }
  if (!color) exit(EXIT_FAILURE);

  if (pcs[WPAWN] || pcs[BPAWN]) {
    fprintf(stderr, "Can't handle pawns.\n");
    exit(EXIT_FAILURE);
  }

  decomp_init_piece(pcs);

  if (check_huff) {
    check_huffman();
    return 0;
  }

  init_tablebases();

  if (numthreads < 1) numthreads = 1;

  printf("number of threads = %d\n", numthreads);

  if (numthreads == 1)
    total_work = 1;
  else
    total_work = 100 + 10 * numthreads;

  for (i = 0; i < numpcs; i++) {
    shift[i] = (numpcs - i - 1) * 6;
    mask[i] = 0x3fULL << shift[i];
  }

#ifndef SMALL
  size = 10ULL << (6 * (numpcs-1));
#else
  size = 462ULL << (6 * (numpcs-2));

  mask[0] = 0x1ffULL << shift[1];
#endif

  work_g = create_work(total_work, size, 0x3f);
#ifndef SMALL
  work_piv = create_work(total_work, 1ULL << shift[0], 0);
#else
  work_piv0 = create_work(total_work, 1ULL << shift[0], 0);
  work_piv1 = create_work(total_work, 10ULL << shift[1], 0);
#endif

  static int piece_order[16] = {
    0, 0, 3, 5, 7, 9, 1, 0,
    0, 0, 4, 6, 8, 10, 2, 0
  };

#ifdef SUICIDE
  j = pt[0];
  for (i = 1; i < numpcs; i++)
    if (piece_order[pt[i]] < piece_order[j])
      j = pt[i];
  if (j & 0x08) {
    for (i = 0; i < numpcs; i++)
      pt[i] ^= 0x08;
    for (i = 0; i < 8; i++) {
      int tmp = pcs[i];
      pcs[i] = pcs[i + 8];
      pcs[i + 8] = tmp;
    }
    switched = 1;
  }
#endif

  for (i = 0; i < numpcs; i++)
    for (j = i + 1; j < numpcs; j++)
      if (piece_order[pt[i]] > piece_order[pt[j]]) {
        int tmp = pt[i];
        pt[i] = pt[j];
        pt[j] = tmp;
      }

  for (i = 0, j = 0; i < numpcs; i++)
    if (!(pt[i] & 0x08))
      white_pcs[j++] = i;
  white_pcs[j] = -1;

  for (i = 0, j = 0; i < numpcs; i++)
    if (pt[i] & 0x08)
      black_pcs[j++] = i;
  black_pcs[j] = -1;

  idx_mask1[numpcs - 1] = 0xffffffffffffffc0ULL;
  idx_mask2[numpcs - 1] = 0;
  for (i = numpcs - 2; i >= 0; i--) {
    idx_mask1[i] = idx_mask1[i + 1] << 6;
    idx_mask2[i] = (idx_mask2[i + 1] << 6) | 0x3f;
  }

#ifndef SUICIDE
  for (i = 0; i < numpcs; i++)
    if (pt[i] == WKING)
      white_king = i;

  for (i = 0; i < numpcs; i++)
    if (pt[i] == BKING)
      black_king = i;
#endif

  table_w = alloc_huge(2 * size);
  table_b = table_w + size;

  printf("Verifying %s.\n", tablename);
  if (log) {
    L = fopen(LOGFILE, "a");
    fprintf(L, "Verifying %s...", tablename);
    fflush(L);
  }

  init_threads(0);
  init_tables();

  gettimeofday(&start_time, NULL);
  cur_time = start_time;

  printf("Initialising broken positions.\n");
  run_threaded(calc_broken, work_g, 1);
  printf("Calculating white captures.\n");
  calc_captures_w();
  printf("Calculating black captures.\n");
  calc_captures_b();
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

  // open wdl table
  struct tb_handle *H = open_tb(tablename, 1);
  decomp_init_table(H);
  load_entry = (struct TBEntry_piece *)get_entry(H);

  if (!wdl_only) {
    // white, wdl
    printf("Loading wdl, white.\n");
    tb_table = decompress_table(H, 0, 0);
    set_perm(H, 0, 0, tb_perm, pt);
    load_table = table_w;
    load_bside = 0;
    run_threaded(load_wdl, work_g, 1);

    // black, wdl
    printf("Loading wdl, black.\n");
    tb_table = decompress_table(H, 1, 0);
    set_perm(H, 1, 0, tb_perm, pt);
    load_table = table_b;
    load_bside = 1;
    run_threaded(load_wdl, work_g, 1);
    close_tb(H);

    // open dtz table
    H = open_tb(tablename, 0);
    decomp_init_table(H);
    load_entry = (struct TBEntry_piece *)get_entry(H);
    ply_accurate_win = get_ply_accurate_win(H, 0);
    ply_accurate_loss = get_ply_accurate_loss(H, 0);
    load_map = get_dtz_map(H, 0);
    int dtz_side = get_dtz_side(H, 0);
    init_wdl_dtz();

    // dtz
    printf("Loading dtz, %s.\n" , dtz_side == 0 ? "white" : "black");
    tb_table = decompress_table(H, 0, 0);
    set_perm(H, 0, 0, tb_perm, pt);
    load_table = dtz_side == 0 ? table_w : table_b;
    load_bside = 0;
    if (load_map)
      run_threaded(load_dtz_mapped, work_g, 1);
    else
      run_threaded(load_dtz, work_g, 1);
    close_tb(H);

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

    printf("Verifying %s.\n", dtz_side ? "white" : "black");
    run_threaded(verify_opp, work_g, 1);
    printf("Verifying %s.\n", dtz_side ? "black" : "white");
    run_threaded(verify_dtz, work_g, 1);
  } else { // currently broken (and disabled)
    // white, wdl
    printf("Loading wdl, white.\n");
    tb_table = decompress_table(H, 0, 0);
    set_perm(H, 0, 0, tb_perm, pt);
    load_table = table_w;
    load_bside = 0;
    run_threaded(wdl_load_wdl, work_g, 1);

    // black, wdl
    printf("Loading wdl, black.\n");
    tb_table = decompress_table(H, 1, 0);
    set_perm(H, 1, 0, tb_perm, pt);
    load_table = table_b;
    load_bside = 1;
    run_threaded(wdl_load_wdl, work_g, 1);
    close_tb(H);

    ply_accurate_win = get_ply_accurate_win(H, 0);
    ply_accurate_loss = get_ply_accurate_loss(H, 0);
    init_wdl();
    load_table = table_w;
    load_opp_table = table_b;
    load_pieces = white_pcs;
    printf("Verifying white.\n");
    run_threaded(verify_wdl, work_g, 1);

    ply_accurate_win = get_ply_accurate_loss(H, 0);
    ply_accurate_loss = get_ply_accurate_win(H, 0);
    init_wdl();
    load_table = table_b;
    load_opp_table = table_w;
    load_pieces = black_pcs;
    printf("Verifying black.\n");
    run_threaded(verify_wdl, work_g, 1);
  }

  if (num_errors == 0) {
    printf("No errors.\n");
    if (log) fprintf(L, " No errors.\n");
  }
  if (log) fclose(L);

  return 0;
}
