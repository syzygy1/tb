/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "board.h"
#include "defs.h"
#include "probe.h"
#include "util.h"

#if defined(SUICIDE)
// 5-piece:
//#define TBMAX_PIECE 1260
//#define TBMAX_PAWN 1386
// 6-piece:
#define TBMAX_PIECE 3570
#define TBMAX_PAWN 4830
#define HSHMAX 12
#elif defined(LOSER)
#define TBMAX 400
#define HSHMAX 4
#elif defined(GIVEAWAY)
#define TBMAX_PIECE 1260
#define TBMAX_PAWN 1386
#define HSHMAX 12
#else
#if TBPIECES <= 6
#define TBMAX_PIECE 254
#define TBMAX_PAWN 256
#define HSHMAX 8
#else
#define TBMAX_PIECE 650
#define TBMAX_PAWN 861
#define HSHMAX 12
#endif
#endif

#if defined(SUICIDE) || defined(GIVEAWAY) || defined(ATOMIC)
#define CONNECTED_KINGS
#endif

#if defined(REGULAR) || defined(ATOMIC) || defined(LOSER)
uint64_t tb_piece_key[] = {
  0ULL,
  0x5ced000000000001ULL,
  0xe173000000000010ULL,
  0xd64d000000000100ULL,
  0xab88000000001000ULL,
  0x680b000000010000ULL,
  0x0000000000000000ULL,
  0ULL,
  0ULL,
  0xf209000000100000ULL,
  0xbb14000001000000ULL,
  0x58df000010000000ULL,
  0xa15f000100000000ULL,
  0x7c94001000000000ULL,
  0x0000000000000000ULL,
  0ULL
};

#define WHITEMATMASK 0x00000000000fffff
#define BLACKMATMASK 0x000000fffff00000
#else
uint64_t tb_piece_key[] = {
  0ULL,
  0x5ced000000000001ULL,
  0xe173000000000010ULL,
  0x234d000000000100ULL,
  0xab88000000001000ULL,
  0x780b000000010000ULL,
  0xb720000000100000ULL,
  0ULL,
  0ULL,
  0xf209000001000000ULL,
  0xbb14000010000000ULL,
  0xd8df000100000000ULL,
  0xa15f001000000000ULL,
  0x7c94010000000000ULL,
  0x8e03100000000000ULL,
  0ULL
};

#define WHITEMATMASK 0x0000000000ffffff
#define BLACKMATMASK 0x0000ffffff000000
#endif

#define Swap(a,b) {int tmp=a;a=b;b=tmp;}

static LOCK_T TB_mutex, fail_mutex;

char WDLdir[128];

static int TBnum_piece, TBnum_pawn;
static struct TBEntry_piece TB_piece[TBMAX_PIECE];
static struct TBEntry_pawn TB_pawn[TBMAX_PAWN];
static struct TBHashEntry TB_hash[1 << TBHASHBITS][HSHMAX];

void init_indices(void);

void add_to_hash(struct TBEntry *ptr, uint64_t key)
{
  int i, hshidx;

  hshidx = key >> (64 - TBHASHBITS);
  i = 0;
  while (i < HSHMAX && TB_hash[hshidx][i].ptr)
    i++;
  if (i == HSHMAX) {
    fprintf(stderr, "HSHMAX too low!\n");
    exit(1);
  } else {
    TB_hash[hshidx][i].key = key;
    TB_hash[hshidx][i].ptr = ptr;
  }
}

static char pchr[] = {'K', 'Q', 'R', 'B', 'N', 'P'};

static void init_tb(char *str)
{
  char file[256];
  int fd;
  struct TBEntry *entry;
  int i, j, pcs[16];
  uint64_t key, key2;
  int color;

  strcpy(file, WDLdir);
  strcat(file, "/");
  strcat(file, str);
  strcat(file, WDLSUFFIX);
  fd = open(file, O_RDONLY);
  if (fd < 0) return;
  close(fd);

  for (i = 0; i < 16; i++)
    pcs[i] = 0;
  color = 0;
  for (i = 0; i < strlen(str); i++)
    switch (str[i]) {
    case 'P':
      pcs[PAWN | color]++;
      break;
    case 'N':
      pcs[KNIGHT | color]++;
      break;
    case 'B':
      pcs[BISHOP | color]++;
      break;
    case 'R':
      pcs[ROOK | color]++;
      break;
    case 'Q':
      pcs[QUEEN | color]++;
      break;
    case 'K':
      pcs[KING | color]++;
      break;
    case 'v':
      color = 0x08;
      break;
    }
  for (i = 0; i < 8; i++)
    if (pcs[i] != pcs[i+8])
      break;
  key = key2 = 0;
  for (i = 0; i < 6; i++) {
    key += (tb_piece_key[i+1]*pcs[i+1]) + (tb_piece_key[i+9]*pcs[i+9]);
    key2 += (tb_piece_key[i+1]*pcs[i+9]) + (tb_piece_key[i+9]*pcs[i+1]);
  }
  if (pcs[WPAWN] + pcs[BPAWN] == 0) {
    if (TBnum_piece == TBMAX_PIECE) {
      fprintf(stderr, "TBMAX_PIECE limit too low!\n");
      exit(1);
    }
    entry = (struct TBEntry *)&TB_piece[TBnum_piece++];
  } else {
    if (TBnum_pawn == TBMAX_PAWN) {
      fprintf(stderr, "TBMAX_PAWN limit too low!\n");
      exit(1);
    }
    entry = (struct TBEntry *)&TB_pawn[TBnum_pawn++];
  }
  entry->key = (uint32_t)key;
  entry->ready = 0;
  entry->num = 0;
  for (i = 0; i < 16; i++)
    entry->num += pcs[i];
  entry->symmetric = (key == key2);
  entry->has_pawns = (pcs[WPAWN] + pcs[BPAWN] > 0);

  if (entry->has_pawns) {
    struct TBEntry_pawn *ptr = (struct TBEntry_pawn *)entry;
    ptr->pawns[0] = pcs[WPAWN];
    ptr->pawns[1] = pcs[BPAWN];
    if (pcs[BPAWN] > 0 && (pcs[WPAWN] == 0 || pcs[BPAWN] < pcs[WPAWN])) {
      ptr->pawns[0] = pcs[BPAWN];
      ptr->pawns[1] = pcs[WPAWN];
    }
  } else {
    struct TBEntry_piece *ptr = (struct TBEntry_piece *)entry;
    for (i = 0, j = 0; i < 16; i++)
      if (pcs[i] == 1) j++;
    if (j >= 3) ptr->enc_type = 0;
    else if (j == 2) ptr->enc_type = 2;
    else { /* only for suicide */
      j = 16;
      for (i = 0; i < 16; i++) {
        if (pcs[i] < j && pcs[i] > 1) j = pcs[i];
        ptr->enc_type = 1 + j;
      }
    }
  }
  add_to_hash(entry, key);
  if (key2 != key) add_to_hash(entry, key2);
}

void init_tablebases(void)
{
  char str[16], *dirptr;
  int i, j, k, l;
#ifdef SUICIDE
  int m, n;
#else
#if TBPIECES == 7
  int m;
#endif
#endif

  LOCK_INIT(TB_mutex);
  LOCK_INIT(fail_mutex);

  TBnum_piece = TBnum_pawn = 0;

  for (i = 0; i < (1 << TBHASHBITS); i++)
    for (j = 0; j < HSHMAX; j++)
      TB_hash[i][j].ptr = NULL;

  dirptr = getenv(WDLDIR);
  if (dirptr && strlen(dirptr) < 100)
    strcpy(WDLdir, dirptr);
  else
    strcpy(WDLdir, ".");

#if defined(SUICIDE)
  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++) {
      sprintf(str, "%cv%c", pchr[i], pchr[j]);
      init_tb(str);
    }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = 0; k < 6; k++) {
        sprintf(str, "%c%cv%c", pchr[i], pchr[j], pchr[k]);
        init_tb(str);
      }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = 0; l < 6; l++) {
          sprintf(str, "%c%c%cv%c", pchr[i], pchr[j], pchr[k], pchr[l]);
          init_tb(str);
        }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = i; k < 6; k++)
        for (l = (i == k) ? j : k; l < 6; l++) {
          sprintf(str, "%c%cv%c%c", pchr[i], pchr[j], pchr[k], pchr[l]);
          init_tb(str);
        }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++)
          for (m = 0; m < 6; m++) {
            sprintf(str, "%c%c%c%cv%c", pchr[i], pchr[j], pchr[k],
                                        pchr[l], pchr[m]);
            init_tb(str);
          }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = 0; l < 6; l++)
          for (m = l; m < 6; m++) {
            sprintf(str, "%c%c%cv%c%c", pchr[i], pchr[j], pchr[k],
                                        pchr[l], pchr[m]);
            init_tb(str);
          }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++)
          for (m = l; m < 6; m++)
            for (n = 0; n < 6; n++) {
              sprintf(str, "%c%c%c%c%cv%c", pchr[i], pchr[j], pchr[k],
                                            pchr[l], pchr[m], pchr[n]);
              init_tb(str);
            }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++)
          for (m = 0; m < 6; m++)
            for (n = m; n < 6; n++) {
              sprintf(str, "%c%c%c%cv%c%c", pchr[i], pchr[j], pchr[k],
                                            pchr[l], pchr[m], pchr[n]);
              init_tb(str);
            }

  for (i = 0; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = i; l < 6; l++)
          for (m = (i == l) ? j : l; m < 6; m++)
            for (n = (i == l && m == j) ? k : m; n < 6; n++) {
              sprintf(str, "%c%c%cv%c%c%c", pchr[i], pchr[j], pchr[k],
                                            pchr[l], pchr[m], pchr[n]);
              init_tb(str);
            }
#else
  for (i = 1; i < 6; i++) {
    sprintf(str, "K%cvK", pchr[i]);
    init_tb(str);
  }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++) {
      sprintf(str, "K%cvK%c", pchr[i], pchr[j]);
      init_tb(str);
    }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++) {
      sprintf(str, "K%c%cvK", pchr[i], pchr[j]);
      init_tb(str);
    }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++) {
        sprintf(str, "K%c%c%cvK", pchr[i], pchr[j], pchr[k]);
        init_tb(str);
      }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = 1; k < 6; k++) {
        sprintf(str, "K%c%cvK%c", pchr[i], pchr[j], pchr[k]);
        init_tb(str);
      }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++) {
          sprintf(str, "K%c%c%c%cvK", pchr[i], pchr[j], pchr[k], pchr[l]);
          init_tb(str);
        }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = 0; l < 6; l++) {
          sprintf(str, "K%c%c%cvK%c", pchr[i], pchr[j], pchr[k], pchr[l]);
          init_tb(str);
        }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = i; k < 6; k++)
        for (l = (i == k) ? j : k; l < 6; l++) {
          sprintf(str, "K%c%cvK%c%c", pchr[i], pchr[j], pchr[k], pchr[l]);
          init_tb(str);
        }

#if TBPIECES == 7

  for (i = 1; i < 6; i ++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++)
          for (m = l; m < 6; m++) {
            sprintf(str, "K%c%c%c%c%cvK", pchr[i], pchr[j], pchr[k],
                                          pchr[l], pchr[m]);
            init_tb(str);
          }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = k; l < 6; l++)
          for (m = 1; m < 6; m++) {
            sprintf(str, "K%c%c%c%cvK%c", pchr[i], pchr[j], pchr[k],
                                          pchr[l], pchr[m]);
            init_tb(str);
          }

  for (i = 1; i < 6; i++)
    for (j = i; j < 6; j++)
      for (k = j; k < 6; k++)
        for (l = 1; l < 6; l++)
          for (m = l; m < 6; m++) {
            sprintf(str, "K%c%c%cvK%c%c", pchr[i], pchr[j], pchr[k],
                                          pchr[l], pchr[m]);
            init_tb(str);
          }

#endif

#endif

  printf("Found %d tablebases.\n", TBnum_piece + TBnum_pawn);

  init_indices();
}

static const signed char offdiag[] = {
  0,-1,-1,-1,-1,-1,-1,-1,
  1, 0,-1,-1,-1,-1,-1,-1,
  1, 1, 0,-1,-1,-1,-1,-1,
  1, 1, 1, 0,-1,-1,-1,-1,
  1, 1, 1, 1, 0,-1,-1,-1,
  1, 1, 1, 1, 1, 0,-1,-1,
  1, 1, 1, 1, 1, 1, 0,-1,
  1, 1, 1, 1, 1, 1, 1, 0
};

static const uint8_t triangle[] = {
  6, 0, 1, 2, 2, 1, 0, 6,
  0, 7, 3, 4, 4, 3, 7, 0,
  1, 3, 8, 5, 5, 8, 3, 1,
  2, 4, 5, 9, 9, 5, 4, 2,
  2, 4, 5, 9, 9, 5, 4, 2,
  1, 3, 8, 5, 5, 8, 3, 1,
  0, 7, 3, 4, 4, 3, 7, 0,
  6, 0, 1, 2, 2, 1, 0, 6
};

static const uint8_t invtriangle[] = {
  1, 2, 3, 10, 11, 19, 0, 9, 18, 27
};

const uint8_t invdiag[] = {
  0, 9, 18, 27, 36, 45, 54, 63,
  7, 14, 21, 28, 35, 42, 49, 56
};

static const uint8_t flipdiag[] = {
   0,  8, 16, 24, 32, 40, 48, 56,
   1,  9, 17, 25, 33, 41, 49, 57,
   2, 10, 18, 26, 34, 42, 50, 58,
   3, 11, 19, 27, 35, 43, 51, 59,
   4, 12, 20, 28, 36, 44, 52, 60,
   5, 13, 21, 29, 37, 45, 53, 61,
   6, 14, 22, 30, 38, 46, 54, 62,
   7, 15, 23, 31, 39, 47, 55, 63
};

static const uint8_t lower[] = {
  28,  0,  1,  2,  3,  4,  5,  6,
   0, 29,  7,  8,  9, 10, 11, 12,
   1,  7, 30, 13, 14, 15, 16, 17,
   2,  8, 13, 31, 18, 19, 20, 21,
   3,  9, 14, 18, 32, 22, 23, 24,
   4, 10, 15, 19, 22, 33, 25, 26,
   5, 11, 16, 20, 23, 25, 34, 27,
   6, 12, 17, 21, 24, 26, 27, 35
};

const uint8_t diag[] = {
   0,  0,  0,  0,  0,  0,  0,  8,
   0,  1,  0,  0,  0,  0,  9,  0,
   0,  0,  2,  0,  0, 10,  0,  0,
   0,  0,  0,  3, 11,  0,  0,  0,
   0,  0,  0, 12,  4,  0,  0,  0,
   0,  0, 13,  0,  0,  5,  0,  0,
   0, 14,  0,  0,  0,  0,  6,  0,
  15,  0,  0,  0,  0,  0,  0,  7
};

static const uint8_t invlower[] = {
  1, 2, 3, 4, 5, 6, 7,
  10, 11, 12, 13, 14, 15,
  19, 20, 21, 22, 23,
  28, 29, 30, 31,
  37, 38, 39,
  46, 47,
  55,
  0, 9, 18, 27, 36, 45, 54, 63
};

static const uint8_t flap[] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 6, 12, 18, 18, 12, 6, 0,
  1, 7, 13, 19, 19, 13, 7, 1,
  2, 8, 14, 20, 20, 14, 8, 2,
  3, 9, 15, 21, 21, 15, 9, 3,
  4, 10, 16, 22, 22, 16, 10, 4,
  5, 11, 17, 23, 23, 17, 11, 5,
  0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t ptwist[] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  47, 35, 23, 11, 10, 22, 34, 46,
  45, 33, 21, 9, 8, 20, 32, 44,
  43, 31, 19, 7, 6, 18, 30, 42,
  41, 29, 17, 5, 4, 16, 28, 40,
  39, 27, 15, 3, 2, 14, 26, 38,
  37, 25, 13, 1, 0, 12, 24, 36,
  0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t invflap[] = {
  8, 16, 24, 32, 40, 48,
  9, 17, 25, 33, 41, 49,
  10, 18, 26, 34, 42, 50,
  11, 19, 27, 35, 43, 51
};

static const uint8_t invptwist[] = {
  52, 51, 44, 43, 36, 35, 28, 27, 20, 19, 12, 11,
  53, 50, 45, 42, 37, 34, 29, 26, 21, 18, 13, 10,
  54, 49, 46, 41, 38, 33, 30, 25, 22, 17, 14, 9,
  55, 48, 47, 40, 39, 32, 31, 24, 23, 16, 15, 8
};

static const uint8_t file_to_file[] = {
  0, 1, 2, 3, 3, 2, 1, 0
};

#ifndef CONNECTED_KINGS
static const int16_t KK_idx[10][64] = {
  { -1, -1, -1,  0,  1,  2,  3,  4,
    -1, -1, -1,  5,  6,  7,  8,  9,
    10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57 },
  { 58, -1, -1, -1, 59, 60, 61, 62,
    63, -1, -1, -1, 64, 65, 66, 67,
    68, 69, 70, 71, 72, 73, 74, 75,
    76, 77, 78, 79, 80, 81, 82, 83,
    84, 85, 86, 87, 88, 89, 90, 91,
    92, 93, 94, 95, 96, 97, 98, 99,
   100,101,102,103,104,105,106,107,
   108,109,110,111,112,113,114,115},
  {116,117, -1, -1, -1,118,119,120,
   121,122, -1, -1, -1,123,124,125,
   126,127,128,129,130,131,132,133,
   134,135,136,137,138,139,140,141,
   142,143,144,145,146,147,148,149,
   150,151,152,153,154,155,156,157,
   158,159,160,161,162,163,164,165,
   166,167,168,169,170,171,172,173 },
  {174, -1, -1, -1,175,176,177,178,
   179, -1, -1, -1,180,181,182,183,
   184, -1, -1, -1,185,186,187,188,
   189,190,191,192,193,194,195,196,
   197,198,199,200,201,202,203,204,
   205,206,207,208,209,210,211,212,
   213,214,215,216,217,218,219,220,
   221,222,223,224,225,226,227,228 },
  {229,230, -1, -1, -1,231,232,233,
   234,235, -1, -1, -1,236,237,238,
   239,240, -1, -1, -1,241,242,243,
   244,245,246,247,248,249,250,251,
   252,253,254,255,256,257,258,259,
   260,261,262,263,264,265,266,267,
   268,269,270,271,272,273,274,275,
   276,277,278,279,280,281,282,283 },
  {284,285,286,287,288,289,290,291,
   292,293, -1, -1, -1,294,295,296,
   297,298, -1, -1, -1,299,300,301,
   302,303, -1, -1, -1,304,305,306,
   307,308,309,310,311,312,313,314,
   315,316,317,318,319,320,321,322,
   323,324,325,326,327,328,329,330,
   331,332,333,334,335,336,337,338 },
  { -1, -1,339,340,341,342,343,344,
    -1, -1,345,346,347,348,349,350,
    -1, -1,441,351,352,353,354,355,
    -1, -1, -1,442,356,357,358,359,
    -1, -1, -1, -1,443,360,361,362,
    -1, -1, -1, -1, -1,444,363,364,
    -1, -1, -1, -1, -1, -1,445,365,
    -1, -1, -1, -1, -1, -1, -1,446 },
  { -1, -1, -1,366,367,368,369,370,
    -1, -1, -1,371,372,373,374,375,
    -1, -1, -1,376,377,378,379,380,
    -1, -1, -1,447,381,382,383,384,
    -1, -1, -1, -1,448,385,386,387,
    -1, -1, -1, -1, -1,449,388,389,
    -1, -1, -1, -1, -1, -1,450,390,
    -1, -1, -1, -1, -1, -1, -1,451 },
  {452,391,392,393,394,395,396,397,
    -1, -1, -1, -1,398,399,400,401,
    -1, -1, -1, -1,402,403,404,405,
    -1, -1, -1, -1,406,407,408,409,
    -1, -1, -1, -1,453,410,411,412,
    -1, -1, -1, -1, -1,454,413,414,
    -1, -1, -1, -1, -1, -1,455,415,
    -1, -1, -1, -1, -1, -1, -1,456 },
  {457,416,417,418,419,420,421,422,
    -1,458,423,424,425,426,427,428,
    -1, -1, -1, -1, -1,429,430,431,
    -1, -1, -1, -1, -1,432,433,434,
    -1, -1, -1, -1, -1,435,436,437,
    -1, -1, -1, -1, -1,459,438,439,
    -1, -1, -1, -1, -1, -1,460,440,
    -1, -1, -1, -1, -1, -1, -1,461 }
};
#else
static const int16_t PP_idx[10][64] = {
  {  0, -1,  1,  2,  3,  4,  5,  6,
     7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38,
    39, 40, 41, 42, 43, 44, 45, 46,
    -1, 47, 48, 49, 50, 51, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 61 },
  { 62, -1, -1, 63, 64, 65, -1, 66,
    -1, 67, 68, 69, 70, 71, 72, -1,
    73, 74, 75, 76, 77, 78, 79, 80,
    81, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 94, 95, 96,
    -1, 97, 98, 99,100,101,102,103,
    -1,104,105,106,107,108,109, -1,
   110, -1,111,112,113,114, -1,115 },
  {116, -1, -1, -1,117, -1, -1,118,
    -1,119,120,121,122,123,124, -1,
    -1,125,126,127,128,129,130, -1,
   131,132,133,134,135,136,137,138,
    -1,139,140,141,142,143,144,145,
    -1,146,147,148,149,150,151, -1,
    -1,152,153,154,155,156,157, -1,
   158, -1, -1,159,160, -1, -1,161 },
  {162, -1, -1, -1, -1, -1, -1,163,
    -1,164, -1,165,166,167,168, -1,
    -1,169,170,171,172,173,174, -1,
    -1,175,176,177,178,179,180, -1,
    -1,181,182,183,184,185,186, -1,
    -1, -1,187,188,189,190,191, -1,
    -1,192,193,194,195,196,197, -1,
   198, -1, -1, -1, -1, -1, -1,199 },
  {200, -1, -1, -1, -1, -1, -1,201,
    -1,202, -1, -1,203, -1,204, -1,
    -1, -1,205,206,207,208, -1, -1,
    -1,209,210,211,212,213,214, -1,
    -1, -1,215,216,217,218,219, -1,
    -1, -1,220,221,222,223, -1, -1,
    -1,224, -1,225,226, -1,227, -1,
   228, -1, -1, -1, -1, -1, -1,229 },
  {230, -1, -1, -1, -1, -1, -1,231,
    -1,232, -1, -1, -1, -1,233, -1,
    -1, -1,234, -1,235,236, -1, -1,
    -1, -1,237,238,239,240, -1, -1,
    -1, -1, -1,241,242,243, -1, -1,
    -1, -1,244,245,246,247, -1, -1,
    -1,248, -1, -1, -1, -1,249, -1,
   250, -1, -1, -1, -1, -1, -1,251 },
  { -1, -1, -1, -1, -1, -1, -1,259,
    -1,252, -1, -1, -1, -1,260, -1,
    -1, -1,253, -1, -1,261, -1, -1,
    -1, -1, -1,254,262, -1, -1, -1,
    -1, -1, -1, -1,255, -1, -1, -1,
    -1, -1, -1, -1, -1,256, -1, -1,
    -1, -1, -1, -1, -1, -1,257, -1,
    -1, -1, -1, -1, -1, -1, -1,258 },
  { -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1,268, -1,
    -1, -1,263, -1, -1,269, -1, -1,
    -1, -1, -1,264,270, -1, -1, -1,
    -1, -1, -1, -1,265, -1, -1, -1,
    -1, -1, -1, -1, -1,266, -1, -1,
    -1, -1, -1, -1, -1, -1,267, -1,
    -1, -1, -1, -1, -1, -1, -1, -1 },
  { -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1,274, -1, -1,
    -1, -1, -1,271,275, -1, -1, -1,
    -1, -1, -1, -1,272, -1, -1, -1,
    -1, -1, -1, -1, -1,273, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1 },
  { -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1,277, -1, -1, -1,
    -1, -1, -1, -1,276, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1 }
};

const uint8_t test45[] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 0, 0, 0, 0, 0,
  1, 1, 0, 0, 0, 0, 0, 0,
  1, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0
};

const uint8_t mtwist[] = {
  15, 63, 55, 47, 40, 48, 56, 12,
  62, 11, 39, 31, 24, 32,  8, 57,
  54, 38,  7, 23, 16,  4, 33, 49,
  46, 30, 22,  3,  0, 17, 25, 41,
  45, 29, 21,  2,  1, 18, 26, 42,
  53, 37,  6, 20, 19,  5, 34, 50,
  61, 10, 36, 28, 27, 35,  9, 58,
  14, 60, 52, 44, 43, 51, 59, 13
};
#endif

#ifndef CONNECTED_KINGS
uint8_t KK_sq[462][2];
#else
uint8_t PP_sq[278][2];
uint8_t invmtwist[64];
#endif

static uint64_t binomial[6][64];
static uint64_t pawnidx[6][24];
static uint64_t pfactor[6][4];
#ifdef CONNECTED_KINGS
static uint64_t multidx[6][10];
static uint64_t mfactor[6];
#endif

void init_indices(void)
{
  int i, j, k;

#ifndef CONNECTED_KINGS
  for (i = 0; i < 10; i++)
    for (j = 0; j < 64; j++)
      if (KK_idx[i][j] >= 0) {
        KK_sq[KK_idx[i][j]][0] = invtriangle[i];
        KK_sq[KK_idx[i][j]][1] = j;
      }
#else
  for (i = 0; i < 10; i++)
    for (j = 0; j < 64; j++)
      if (PP_idx[i][j] >= 0) {
        if (invtriangle[i] < j) {
          PP_sq[PP_idx[i][j]][0] = invtriangle[i];
          PP_sq[PP_idx[i][j]][1] = j;
        } else {
          PP_sq[PP_idx[i][j]][0] = j;
          PP_sq[PP_idx[i][j]][1] = invtriangle[i];
        }
      }

  for (i = 0; i < 64; i++)
    invmtwist[mtwist[i]] = i;
#endif

// binomial[k-1][n] = Bin(n, k)
  for (i = 0; i < 6; i++)
    for (j = 0; j < 64; j++) {
      uint64_t f = j;
      uint64_t l = 1;
      for (k = 1; k <= i; k++) {
        f *= (j - k);
        l *= (k + 1);
      }
      binomial[i][j] = f / l;
    }

  for (i = 0; i < 6; i++) {
    uint64_t s = 0;
    for (j = 0; j < 6; j++) {
      pawnidx[i][j] = s;
      s += (i == 0) ? 1 : binomial[i - 1][ptwist[invflap[j]]];
    }
    pfactor[i][0] = s;
    s = 0;
    for (; j < 12; j++) {
      pawnidx[i][j] = s;
      s += (i == 0) ? 1 : binomial[i - 1][ptwist[invflap[j]]];
    }
    pfactor[i][1] = s;
    s = 0;
    for (; j < 18; j++) {
      pawnidx[i][j] = s;
      s += (i == 0) ? 1 : binomial[i - 1][ptwist[invflap[j]]];
    }
    pfactor[i][2] = s;
    s = 0;
    for (; j < 24; j++) {
      pawnidx[i][j] = s;
      s += (i == 0) ? 1 : binomial[i - 1][ptwist[invflap[j]]];
    }
    pfactor[i][3] = s;
  }

//#ifdef SUICIDE
#ifdef CONNECTED_KINGS
  for (i = 0; i < 6; i++) {
    uint64_t s = 0;
    for (j = 0; j < 10; j++) {
      multidx[i][j] = s;
      s += (i == 0) ? 1 : binomial[i - 1][mtwist[invtriangle[j]]];
    }
    mfactor[i] = s;
  }
#endif
}

#ifndef CONNECTED_KINGS
uint64_t encode_piece(struct TBEntry_piece *restrict ptr,
                      uint8_t *restrict norm, int *restrict pos,
                      uint64_t *restrict factor)
{
  uint64_t idx;
  int i, j, k, m, l, p;
  int n = ptr->num;

  if (pos[0] & 0x04) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x07;
  }
  if (pos[0] & 0x20) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x38;
  }

  for (i = 0; i < n; i++)
    if (offdiag[pos[i]]) break;
  if (i < (ptr->enc_type == 0 ? 3 : 2) && offdiag[pos[i]] > 0)
    for (i = 0; i < n; i++)
      pos[i] = flipdiag[pos[i]];

  switch (ptr->enc_type) {

  case 0: /* 111 */
    i = (pos[1] > pos[0]);
    j = (pos[2] > pos[0]) + (pos[2] > pos[1]);

    if (offdiag[pos[0]])
      idx = triangle[pos[0]] * 63*62 + (pos[1] - i) * 62 + (pos[2] - j);
    else if (offdiag[pos[1]])
      idx = 6*63*62 + diag[pos[0]] * 28*62 + lower[pos[1]] * 62 + pos[2] - j;
    else if (offdiag[pos[2]])
      idx = 6*63*62 + 4*28*62 + (diag[pos[0]]) * 7*28 + (diag[pos[1]] - i) * 28 + lower[pos[2]];
    else
      idx = 6*63*62 + 4*28*62 + 4*7*28 + (diag[pos[0]] * 7*6) + (diag[pos[1]] - i) * 6 + (diag[pos[2]] - j);
    i = 3;
    break;

  case 1: /* K3 */
    j = (pos[2] > pos[0]) + (pos[2] > pos[1]);

    idx = KK_idx[triangle[pos[0]]][pos[1]];
    if (idx < 441)
      idx = idx + 441 * (pos[2] - j);
    else {
      idx = 441*62 + (idx - 441) + 21 * lower[pos[2]];
      if (!offdiag[pos[2]])
        idx -= j * 21;
    }
    i = 3;
    break;

  case 2: /* K2 */
    idx = KK_idx[triangle[pos[0]]][pos[1]];
    i = 2;
    break;

  default:
    assume(0);
    break;
  }
  idx *= factor[0];

  for (; i < n;) {
    int t = norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      p = pos[m];
      for (l = 0, j = 0; l < i; l++)
        j += (p > pos[l]);
      s += binomial[m - i][p - j];
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}
#else
uint64_t encode_piece(struct TBEntry_piece *restrict ptr,
                      uint8_t *restrict norm, int *restrict pos,
                      uint64_t *restrict factor)
{
  uint64_t idx;
  int i, j, k, m, l, p;
  int n = ptr->num;

  if (ptr->enc_type < 3) {
    if (pos[0] & 0x04) {
      for (i = 0; i < n; i++)
        pos[i] ^= 0x07;
    }
    if (pos[0] & 0x20) {
      for (i = 0; i < n; i++)
        pos[i] ^= 0x38;
    }

    for (i = 0; i < n; i++)
      if (offdiag[pos[i]]) break;
    if (i < (ptr->enc_type == 0 ? 3 : 2) && offdiag[pos[i]] > 0)
      for (i = 0; i < n; i++)
        pos[i] = flipdiag[pos[i]];

    switch (ptr->enc_type) {

    case 0: /* 111 */
      i = (pos[1] > pos[0]);
      j = (pos[2] > pos[0]) + (pos[2] > pos[1]);

      if (offdiag[pos[0]])
        idx = triangle[pos[0]] * 63*62 + (pos[1] - i) * 62 + (pos[2] - j);
      else if (offdiag[pos[1]])
        idx = 6*63*62 + diag[pos[0]] * 28*62 + lower[pos[1]] * 62 + pos[2] - j;
      else if (offdiag[pos[2]])
        idx = 6*63*62 + 4*28*62 + (diag[pos[0]]) * 7*28 + (diag[pos[1]] - i) * 28 + lower[pos[2]];
      else
        idx = 6*63*62 + 4*28*62 + 4*7*28 + (diag[pos[0]] * 7*6) + (diag[pos[1]] - i) * 6 + (diag[pos[2]] - j);
      i = 3;
      break;

    case 2: /* 11 */
      i = (pos[1] > pos[0]);

      if (offdiag[pos[0]])
        idx = triangle[pos[0]] * 63 + (pos[1] - i);
      else if (offdiag[pos[1]])
        idx = 6*63 + diag[pos[0]] * 28 + lower[pos[1]];
      else
        idx = 6*63 + 4*28 + (diag[pos[0]]) * 7 + (diag[pos[1]] - i);
      i = 2;
      break;

    default:
      assume(0);
      break;

    }
  } else if (ptr->enc_type == 3) { /* 2, e.g. KKvK */
    if (triangle[pos[0]] > triangle[pos[1]])
      Swap(pos[0], pos[1]);
    if (pos[0] & 0x04)
      for (i = 0; i < n; i++)
        pos[i] ^= 0x07;
    if (pos[0] & 0x20)
      for (i = 0; i < n; i++)
        pos[i] ^= 0x38;
    if (offdiag[pos[0]] > 0 || (offdiag[pos[0]] == 0 && offdiag[pos[1]] > 0))
      for (i = 0; i < n; i++)
        pos[i] = flipdiag[pos[i]];
    if (test45[pos[1]] && triangle[pos[0]] == triangle[pos[1]]) {
      Swap(pos[0], pos[1]);
      for (i = 0; i < n; i++)
        pos[i] = flipdiag[pos[i] ^ 0x38];
    }
    idx = PP_idx[triangle[pos[0]]][pos[1]];
    i = 2;
  } else { /* 3 and higher, e.g. KKKvK and KKKKvK */
    for (i = 1; i < norm[0]; i++)
      if (triangle[pos[0]] > triangle[pos[i]])
        Swap(pos[0], pos[i]);
    if (pos[0] & 0x04)
      for (i = 0; i < n; i++)
        pos[i] ^= 0x07;
    if (pos[0] & 0x20)
      for (i = 0; i < n; i++)
        pos[i] ^= 0x38;
    if (offdiag[pos[0]] > 0)
      for (i = 0; i < n; i++)
        pos[i] = flipdiag[pos[i]];
    for (i = 1; i < norm[0]; i++)
      for (j = i + 1; j < norm[0]; j++)
        if (mtwist[pos[i]] > mtwist[pos[j]])
          Swap(pos[i], pos[j]);

    idx = multidx[norm[0] - 1][triangle[pos[0]]];
    for (i = 1; i < norm[0]; i++)
      idx += binomial[i - 1][mtwist[pos[i]]];
  }
  idx *= factor[0];

  for (; i < n;) {
    int t = norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      p = pos[m];
      for (l = 0, j = 0; l < i; l++)
        j += (p > pos[l]);
      s += binomial[m - i][p - j];
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}
#endif

#if 0
uint64_t encode_piece(struct TBEntry *ptr, int *pos, uint64_t *factor)
{
  uint64_t idx;
  int i, j, k, m, l, p;
  int n = ptr->num;
  int sort[TBPIECES];

  if (pos[0] & 0x04) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x07;
  }
  if (pos[0] & 0x20) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x38;
  }

  for (i = 0; i < n; i++)
    if (offdiag[pos[i]]) break;
  if (i < (ptr->norm[0] > 2 ? 3 : 2) && offdiag[pos[i]] > 0)
    for (i = 0; i < n; i++)
      pos[i] = flipdiag[pos[i]];

  switch (ptr->enc_type) {

  case 0: /* 111 */
    if (pos[0] < pos[1]) {
      sort[0] = pos[0];
      sort[1] = pos[1];
      i = 1;
    } else {
      sort[0] = pos[1];
      sort[1] = pos[0];
      i = 0;
    }

    p = pos[2];
    for (j = 2; j > 0 && sort[j - 1] > p; j--)
      sort[j] = sort[j - 1];
    sort[j] = p;

    if (offdiag[pos[0]])
      idx = triangle[pos[0]] * 63*62 + (pos[1] - i) * 62 + (pos[2] - j);
    else if (offdiag[pos[1]])
      idx = 6*63*62 + diag[pos[0]] * 28*62 + lower[pos[1]] * 62 + pos[2] - j;
    else if (offdiag[pos[2]])
      idx = 6*63*62 + 4*28*62 + (diag[pos[0]]) * 7*28 + (diag[pos[1]] - i) * 28 + lower[pos[2]];
    else
      idx = 6*63*62 + 4*28*62 + 4*7*28 + (diag[pos[0]] * 7*6) + (diag[pos[1]] - i) * 6 + (diag[pos[2]] - j);
    i = 3;
    break;

  case 1: /* K3 */
    if (pos[0] < pos[1]) {
      sort[0] = pos[0];
      sort[1] = pos[1];
    } else {
      sort[0] = pos[1];
      sort[1] = pos[0];
    }

    p = pos[2];
    for (j = 2; j > 0 && sort[j - 1] > p; j--)
      sort[j] = sort[j - 1];
    sort[j] = p;

    idx = KK_idx[triangle[pos[0]]][pos[1]];
    if (idx < 441)
      idx = idx + 441 * (pos[2] - j);
    else {
      idx = 441*62 + (idx - 441) + 21 * lower[pos[2]];
      if (!offdiag[pos[2]])
        idx -= j * 21;
    }
    i = 3;
    break;

  case 2: /* K2 */
    if (pos[0] < pos[1]) {
      sort[0] = pos[0];
      sort[1] = pos[1];
    } else {
      sort[0] = pos[1];
      sort[1] = pos[0];
    }

    idx = (uint64_t)KK_idx[triangle[pos[0]]][pos[1]];
    i = 2;
    break;

  }
  idx *= factor[0];

#if 0
  for (; i < ptr->norm[0]; i++) {
    int p = pos[i];
    for (j = i; j > 0 && sort[j - 1] > p; j--)
      sort[j] = sort[j - 1];
    sort[j] = p;
    idx += (p - j) * factor[i];
  }
#endif

  for (; i < n;) {
    int t = ptr->norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      p = pos[m];
      for (j = m; j > 0; j--)
        if (sort[j - 1] < p) break;
      for (l = m; l > j; l--)
        sort[l] = sort[l - 1];
      sort[j] = p;
      s += binomial[m - i][p - j + m - i];
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}
#endif

#if 0
// KKpiece, not pieceKK
uint64_t encode_K3(struct TBEntry *ptr, int *pos, uint64_t *factor)
{
  uint64_t idx;
  int i, j, k, m, l, p;
  int n = ptr->num;
  int sort[TBPIECES];

  if (pos[0] & 0x04) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x07;
  }
  if (pos[0] & 0x20) {
    for (i = 0; i < n; i++)
      pos[i] ^= 0x38;
  }

  if (offdiag[pos[0]] > 0 ||
        (offdiag[pos[0]] == 0 &&
                (offdiag[pos[1]] > 0 ||
                        (offdiag[pos[1]] == 0 && offdiag[pos[2]] > 0)))) {
    for (i = 0; i < n; i++)
      pos[i] = flipdiag[pos[i]];
  }

// sort groups of identical pieces
// FIXME: make optional, not needed if identical pieces already sorted
  for (i = ptr->norm[0]; i < n;) {
    int t = ptr->norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    i = j;
  }

  if (pos[0] < pos[1]) {
    sort[0] = pos[0];
    sort[1] = pos[1];
  } else {
    sort[0] = pos[1];
    sort[1] = pos[0];
  }

  p = pos[2];
  for (j = 2; j > 0 && sort[j - 1] > p; j--)
    sort[j] = sort[j - 1];
  sort[j] = p;

  idx = KK_idx[triangle[pos[0]]][pos[1]];
  if (idx < 441)
    idx = idx * 62 + pos[2] - j;
  else {
    idx = 441*62 + 34 * (idx - 441) + lower[pos[2]];
    if (!offdiag[pos[2]])
      idx -= j;
  }
  idx *= factor[0];

  for (i = 3; i < ptr->norm[0]; i++) {
    int p = pos[i];
    for (j = i; j > 0 && sort[j - 1] > p; j--)
      sort[j] = sort[j - 1];
    sort[j] = p;
    idx += (p - j) * factor[i];
  }

  for (; i < n;) {
    int t = ptr->norm[i];
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      p = pos[m];
      for (j = m; j > 0; j--)
        if (sort[j - 1] < p) break;
      for (l = m; l > j; l--)
        sort[l] = sort[l - 1];
      sort[j] = p;
      p = p - j + m - i;
      int l = p;
      int f = 1;
      for (j = 1; j <= m - i; j++) {
        l *= (p - j);
        f *= j + 1;
      }
      s += l / f;
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}
#endif

//static int fac[] = {1, 1, 2, 6, 24, 120, 720, 5040, 40320};

void calc_order_piece(int num, int ord, int *order, uint8_t *norm)
{
  int i, k;
  int tmp[TBPIECES];

  for (i = norm[0], k = 0; i < num || k == ord; k++)
    if (k == ord)
      tmp[k] = 0;
    else {
      tmp[k] = i;
      i += norm[i];
    }

  for (i = 0; i < k; i++)
    order[k - i - 1] = tmp[i];
}

void calc_order_pawn(int num, int ord, int ord2, int *order, uint8_t *norm)
{
  int i, k;
  int tmp[TBPIECES];

  i = norm[0];
  if (ord2 < 0x0f) i += norm[i];

  for (k = 0; i < num || k == ord || k == ord2; k++)
    if (k == ord)
      tmp[k] = 0;
    else if (k == ord2)
      tmp[k] = norm[0];
    else {
      tmp[k] = i;
      i += norm[i];
    }

  for (i = 0; i < k; i++)
    order[k - i - 1] = tmp[i];
}

void decode_piece(struct TBEntry_piece *restrict ptr, uint8_t *restrict norm,
                  int *restrict pos, uint64_t *restrict factor,
                  int *restrict order, uint64_t idx)
{
  int i, j, k;
  int p, q;
  int sub[TBPIECES];
  int sort[TBPIECES];
  int n = ptr->num;

  for (i = 0; factor[order[i]] != 1; i++) {
    q = idx / factor[order[i]];
    idx -= (uint64_t)q * factor[order[i]];
    sub[order[i]] = q;
  }
  sub[order[i]] = idx;

  switch (ptr->enc_type) {

  case 0: /* 111 */
    q = sub[0];
    if (q < 6*63*62) {
      p = q / (63*62);
      q -= p * 63*62;
      pos[0] = invtriangle[p];
      p = q / 62;
      q -= p * 62;
      if (p >= pos[0]) {
        sort[0] = pos[0];
        sort[1] = pos[1] = p + 1; 
      } else {
        sort[0] = pos[1] = p;
        sort[1] = pos[0];
      }
      if (q >= sort[0]) q++;
      if (q >= sort[1]) q++;
    } else if (q < 6*63*62 + 4*28*62) {
      q -= 6*63*62;
      p = q / (28*62);
      q -= p * 28*62;
      pos[0] = invdiag[p];
      p = q / 62;
      q -= p * 62;
      pos[1] = invlower[p];
      if (pos[0] < pos[1]) {
        sort[0] = pos[0];
        sort[1] = pos[1];
      } else {
        sort[0] = pos[1];
        sort[1] = pos[0];
      }
      if (q >= sort[0]) q++;
      if (q >= sort[1]) q++;
    } else if (q < 6*63*62 + 4*28*62 + 4*7*28) {
      q -= 6*63*62 + 4*28*62;
      p = q / (7*28);
      q -= p * 7*28;
      pos[0] = invdiag[p];
      p = q / 28;
      q -= p * 28;
      if (invdiag[p] >= pos[0]) {
        sort[0] = pos[0];
        sort[1] = pos[1] = invdiag[p + 1];
      } else {
        sort[0] = pos[1] = invdiag[p];
        sort[1] = pos[0];
      }
      q = invlower[q];
    } else {
      q -= 6*63*62 + 4*28*62 + 4*7*28;
      p = q / (7 * 6);
      q -= p * 7*6;
      pos[0] = invdiag[p];
      p = q / 6;
      q -= p * 6;
      if (invdiag[p] >= pos[0]) {
        sort[0] = pos[0];
        sort[1] = pos[1] = invdiag[p + 1];
      } else {
        sort[0] = pos[1] = invdiag[p];
        sort[1] = pos[0];
      }
      q = invdiag[q];
      if (q >= sort[0]) q += 9;
      if (q >= sort[1]) q += 9;
    }
    for (j = 2; j > 0 && sort[j - 1] > q; j--)
      sort[j] = sort[j - 1];
    pos[2] = sort[j] = q;
    i = 3;
    break;

#ifndef CONNECTED_KINGS
  case 1: /* K3 */
    if (sub[0] < 441*62) {
      p = sub[0] / 441;
      q = sub[0] - p * 441;
      pos[0] = KK_sq[q][0];
      pos[1] = KK_sq[q][1];
      if (pos[0] < pos[1]) {
        sort[0] = pos[0];
        sort[1] = pos[1];
      } else {
        sort[0] = pos[1];
        sort[1] = pos[0];
      }
      if (p >= sort[0]) p++;
      if (p >= sort[1]) p++;
    } else {
      q = sub[0] - 441*62;
      p = q / 21;
      q -= p * 21;
      pos[0] = KK_sq[q + 441][0];
      pos[1] = KK_sq[q + 441][1];
      if (pos[0] < pos[1]) {
        sort[0] = pos[0];
        sort[1] = pos[1];
      } else {
        sort[0] = pos[1];
        sort[1] = pos[0];
      }
      if (p >= 28) {
        if (invlower[p] >= sort[0]) p++;
        if (invlower[p] >= sort[1]) p++;
      }
      p = invlower[p];
    }
    for (j = 2; j > 0 && sort[j - 1] > p; j--)
      sort[j] = sort[j - 1];
    pos[2] = sort[j] = p;
    i = 3;
    break;

  case 2: /* K2 */
    q = sub[0];
    pos[0] = KK_sq[q][0];
    pos[1] = KK_sq[q][1];
    if (pos[0] < pos[1]) {
      sort[0] = pos[0];
      sort[1] = pos[1];
    } else {
      sort[0] = pos[1];
      sort[1] = pos[0];
    }
    i = 2;
    break;
#else
  case 2: /* 11 */
    q = sub[0];
    if (q < 6*63) {
      p = q / 63;
      q -= p * 63;
      pos[0] = invtriangle[p];
      if (q >= pos[0]) {
        sort[0] = pos[0];
        sort[1] = pos[1] = q + 1;
      } else {
        sort[1] = pos[0];
        sort[0] = pos[1] = q;
      }
    } else if (q < 6*63 + 4*28) {
      q -= 6*63;
      p = q / 28;
      q -= p * 28;
      pos[0] = invdiag[p];
      pos[1] = invlower[q];
      if (pos[0] < pos[1]) {
        sort[0] = pos[0];
        sort[1] = pos[1];
      } else {
        sort[0] = pos[1];
        sort[1] = pos[0];
      }
    } else {
      q -= 6*63 + 4*28;
      p = q / 7;
      q -= p * 7;
      pos[0] = invdiag[p];
      if (invdiag[q] >= pos[0]) {
        sort[0] = pos[0];
        sort[1] = pos[1] = invdiag[q + 1];
      } else {
        sort[0] = pos[1] = invdiag[q];
        sort[1] = pos[0];
      }
    }
    i = 2;
    break;
  case 3: /* 2, e.g. KKvK */
    q = sub[0];
    sort[0] = pos[0] = PP_sq[q][0];
    sort[1] = pos[1] = PP_sq[q][1];
    i = 2;
    break;

  default: /* 3 and higher, e.g. KKKvK and KKKKvK */
    q = sub[0];
    for (i = 0; i < 9; i++)
      if (multidx[norm[0] - 1][i + 1] > q) break;
    q -= multidx[norm[0] - 1][i];
    sort[0] = pos[0] = invtriangle[i];
    for (i = norm[0] - 1; i > 1; i--) {
      p = i - 1;
      while (1) {
        int f = binomial[i - 2][p];
        if (f > q) break;
        q -= f;
        p++;
      }
      pos[i] = sort[i] = invmtwist[p];
    }
    pos[i] = sort[i] = invmtwist[q];
    for (i = 0; i < norm[0]; i++)
      for (j = i + 1; j < norm[0]; j++)
        if (sort[i] > sort[j]) Swap(sort[i], sort[j]);
    break;

#endif
  }

  for (; i < n;) {
    q = sub[i];
    for (j = norm[i] - 1; j > 0; j--) {
      p = j;
      while (1) {
        int f = binomial[j - 1][p];
        if (f > q) break;
        q -= f;
        p++;
      }
      for (k = 0; k < i; k++)
        if (sort[k] <= p) p++;
      pos[i + j] = sort[i + j] = p;
    }
    for (k = 0; k < i; k++)
      if (sort[k] <= q) q++;
    pos[i] = sort[i] = q;
    for (j = 0, k = i; j < k && k < i + norm[i]; j++)
      if (sort[j] > sort[k]) {
        int m, tmp = sort[k];
        for (m = k; m > j; m--)
          sort[m] = sort[m - 1];
        sort[j] = tmp;
        k++;
      }
    i += norm[i];
  }
}

// determine file of leftmost pawn and sort pawns
int pawn_file(struct TBEntry_pawn *ptr, int *pos)
{
  int i;

  for (i = 1; i < ptr->pawns[0]; i++)
    if (flap[pos[0]] > flap[pos[i]])
      Swap(pos[0], pos[i]);

  return file_to_file[pos[0] & 0x07];
}

uint64_t encode_pawn(struct TBEntry_pawn *restrict ptr, uint8_t *restrict norm,
                     int *restrict pos, uint64_t *restrict factor)
{
  uint64_t idx;
  int i, j, k, m, t;
  int n = ptr->num;

  if (pos[0] & 0x04)
    for (i = 0; i < n; i++)
      pos[i] ^= 0x07;

  for (i = 1; i < ptr->pawns[0]; i++)
    for (j = i + 1; j < ptr->pawns[0]; j++)
      if (ptwist[pos[i]] < ptwist[pos[j]])
        Swap(pos[i], pos[j]);

  t = ptr->pawns[0] - 1;
  idx = pawnidx[t][flap[pos[0]]];
  for (i = t; i > 0; i--)
    idx += binomial[t - i][ptwist[pos[i]]];
  idx *= factor[0];

// remaining pawns
  i = ptr->pawns[0];
  t = i + ptr->pawns[1];
  if (t > i) {
    for (j = i; j < t; j++)
      for (k = j + 1; k < t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < t; m++) {
      int p = pos[m];
      for (k = 0, j = 0; k < i; k++)
        j += (p > pos[k]);
      s += binomial[m - i][p - j - 8];
    }
    idx += s * factor[i];
    i = t;
  }

  for (; i < n;) {
    t = norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      int p = pos[m];
      for (k = 0, j = 0; k < i; k++)
        j += (p > pos[k]);
      s += binomial[m - i][p - j];
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}

#ifdef VERIFICATION
uint64_t encode_pawn_ver(struct TBEntry_pawn *restrict ptr,
                         uint8_t *restrict norm, int *restrict pos,
                         uint64_t *restrict factor)
{
  uint64_t idx;
  int i, j, k, m, t;
  int n = ptr->num;

  for (i = 1; i < ptr->pawns[0]; i++)
    if (flap[pos[0]] > flap[pos[i]])
      Swap(pos[0], pos[i]);

  if (pos[0] & 0x04)
    for (i = 0; i < n; i++)
      pos[i] ^= 0x07;

  for (i = 1; i < ptr->pawns[0]; i++)
    for (j = i + 1; j < ptr->pawns[0]; j++)
      if (ptwist[pos[i]] < ptwist[pos[j]])
        Swap(pos[i], pos[j]);

  t = ptr->pawns[0] - 1;
  idx = pawnidx[t][flap[pos[0]]];
  for (i = t; i > 0; i--)
    idx += binomial[t - i][ptwist[pos[i]]];
  idx *= factor[0];

// remaining pawns
  i = ptr->pawns[0];
  t = i + ptr->pawns[1];
  if (t > i) {
    for (j = i; j < t; j++)
      for (k = j + 1; k < t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < t; m++) {
      int p = pos[m];
      for (k = 0, j = 0; k < i; k++)
        j += (p > pos[k]);
      s += binomial[m - i][p - j - 8];
    }
    idx += s * factor[i];
    i = t;
  }

  for (; i < n;) {
    t = norm[i];
    for (j = i; j < i + t; j++)
      for (k = j + 1; k < i + t; k++)
        if (pos[j] > pos[k]) Swap(pos[j], pos[k]);
    uint64_t s = 0;
    for (m = i; m < i + t; m++) {
      int p = pos[m];
      for (k = 0, j = 0; k < i; k++)
        j += (p > pos[k]);
      s += binomial[m - i][p - j];
    }
    idx += s * factor[i];
    i += t;
  }

  return idx;
}
#endif

void decode_pawn(struct TBEntry_pawn *restrict ptr, uint8_t *restrict norm,
                 int *restrict pos, uint64_t *restrict factor,
                 int *restrict order, uint64_t idx, int file)
{
  int i, j, k;
  int p, q, t;
  int sub[TBPIECES];
  int sort[TBPIECES];
  int n = ptr->num;

  for (i = 0; factor[order[i]] != 1; i++) {
    q = idx / factor[order[i]];
    idx -= (uint64_t)q * factor[order[i]];
    sub[order[i]] = q;
  }
  sub[order[i]] = idx;

  q = sub[0];
assume(ptr->pawns[0] <= TBPIECES);
  t = ptr->pawns[0] - 1;
  for (i = 0; i < 5; i++)
    if (pawnidx[t][6 * file + i + 1] > q) break;
  q -= pawnidx[t][6 * file + i];
  sort[0] = pos[0] = invflap[6 * file + i];

  if (t > 0) {
    for (j = t; j > 1; j--) {
      p = j - 1;
      while (1) {
        int f = binomial[j - 2][p];
        if (f > q) break;
        q -= f;
        p++;
      }
      pos[j] = sort[j] = invptwist[p];
    }
    pos[1] = sort[1] = invptwist[q];
    for (j = 0; j < t; j++)
      for (k = j + 1; k <= t; k++)
        if (sort[j] > sort[k]) Swap(sort[j], sort[k]);
  }

  i = ptr->pawns[0];

  if (ptr->pawns[1] > 0) {
    q = sub[ptr->pawns[0]];
    for (j = ptr->pawns[1] - 1; j > 0; j--) {
      p = j;
      while (1) {
        int f = binomial[j - 1][p];
        if (f > q) break;
        q -= f;
        p++;
      }
      p += 8;
      for (k = 0; k < i; k++)
        if (sort[k] <= p) p++;
      pos[i + j] = sort[i + j] = p;
    }
    q += 8;
    for (k = 0; k < i; k++)
      if (sort[k] <= q) q++;
    pos[i] = sort[i] = q;
    for (j = 0, k = i; j < k && k < i + ptr->pawns[1]; j++)
      if (sort[j] > sort[k]) {
        int m, tmp = sort[k];
        for (m = k; m > j; m--)
          sort[m] = sort[m - 1];
        sort[j] = tmp;
        k++;
      }
    i += ptr->pawns[1];
  }

  for (; i < norm[0]; i++) {
    p = sub[i];
    for (j = 0; j < i; j++, p++)
      if (p < sort[j]) break;
    for (k = i; k > j; k--)
      sort[k] = sort[k - 1];
    pos[i] = sort[j] = p;
  }

  for (; i < n;) {
    q = sub[i];
    for (j = norm[i] - 1; j > 0; j--) {
      p = j;
      while (1) {
        int f = binomial[j - 1][p];
        if (f > q) break;
        q -= f;
        p++;
      }
      for (k = 0; k < i; k++)
        if (sort[k] <= p) p++;
      pos[i + j] = sort[i + j] = p;
    }
    for (k = 0; k < i; k++)
      if (sort[k] <= q) q++;
    pos[i] = sort[i] = q;
    for (j = 0, k = i; j < k && k < i + norm[i]; j++)
      if (sort[j] > sort[k]) {
        int m, tmp = sort[k];
        for (m = k; m > j; m--)
          sort[m] = sort[m - 1];
        sort[j] = tmp;
        k++;
      }
    i += norm[i];
  }
}

/******* start of actual probing and decompression code *******/

uint8_t decompress_pairs(struct PairsData *d, uint64_t index);

// place k like pieces on n squares
int subfactor(int k, int n)
{
  int i, f, l;

  f = n;
  l = 1;
  for (i = 1; i < k; i++) {
    f *= n - i;
    l *= i + 1;
  }

  return f / l;
}

uint64_t calc_factors_piece(uint64_t *factor, int num, int order,
                            uint8_t *norm, uint8_t enc_type)
{
  int i, k, n;
  uint64_t f;
#ifndef CONNECTED_KINGS
  static int pivfac[] = { 31332, 28056, 462 };
#else
  static int pivfac[] = { 31332, 0, 518, 278 };
#endif

  n = 64 - norm[0];

  f = 1;
  for (i = norm[0], k = 0; i < num || k == order; k++) {
    if (k == order) {
      factor[0] = f;
#ifndef CONNECTED_KINGS
      f *= pivfac[enc_type];
#else
      if (enc_type < 4)
        f *= pivfac[enc_type];
      else
        f *= mfactor[enc_type - 2];
#endif
    } else {
      factor[i] = f;
      f *= subfactor(norm[i], n);
      n -= norm[i];
      i += norm[i];
    }
  }

  return f;
}

uint64_t calc_factors_pawn(uint64_t *factor, int num, int order, int order2,
                           uint8_t *norm, int file)
{
  int i, k, n;
  uint64_t f;

  i = norm[0];
  if (order2 < 0x0f) i += norm[i];
  n = 64 - i;

  f = 1;
  for (k = 0; i < num || k == order || k == order2; k++) {
    if (k == order) {
      factor[0] = f;
      f *= pfactor[norm[0] - 1][file];
    } else if (k == order2) {
      factor[norm[0]] = f;
      f *= subfactor(norm[norm[0]], 48 - norm[0]);
    } else {
      factor[i] = f;
      f *= subfactor(norm[i], n);
      n -= norm[i];
      i += norm[i];
    }
  }

  return f;
}

void set_norm_piece(struct TBEntry_piece *ptr, uint8_t *norm, uint8_t *pieces,
                    int order)
{
  int i, j;

  for (i = 0; i < ptr->num; i++)
    norm[i] = 0;

  switch (ptr->enc_type) {
  case 0:
    norm[0] = 3;
    break;
  case 2:
    norm[0] = 2;
    break;
  default:
    norm[0] = ptr->enc_type - 1;
    break;
  }

  for (i = norm[0]; i < ptr->num; i += norm[i])
    for (j = i; j < ptr->num && pieces[j] == pieces[i]; j++)
      norm[i]++;
}

void set_norm_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, uint8_t *pieces,
                   int order, int order2)
{
  int i, j;

  for (i = 0; i < ptr->num; i++)
    norm[i] = 0;

  norm[0] = ptr->pawns[0];
  if (ptr->pawns[1]) norm[ptr->pawns[0]] = ptr->pawns[1];

  for (i = ptr->pawns[0] + ptr->pawns[1]; i < ptr->num; i += norm[i])
    for (j = i; j < ptr->num && pieces[j] == pieces[i]; j++)
      norm[i]++;
}

void setup_pieces_piece(struct TBEntry_piece *ptr, uint8_t *data,
    uint64_t *tb_size)
{
  int i;
  int order;

  for (i = 0; i < ptr->num; i++)
    ptr->pieces[0][i] = data[i + 1] & 0x0f;
  order = data[0] & 0x0f;
  ptr->order[0] = order;
  set_norm_piece(ptr, ptr->norm[0], ptr->pieces[0], order);
  tb_size[0] = calc_factors_piece(ptr->factor[0], ptr->num, order, ptr->norm[0], ptr->enc_type);

  for (i = 0; i < ptr->num; i++)
    ptr->pieces[1][i] = data[i + 1] >> 4;
  order = data[0] >> 4;
  ptr->order[1] = order;
  set_norm_piece(ptr, ptr->norm[1], ptr->pieces[1], order);
  tb_size[1] = calc_factors_piece(ptr->factor[1], ptr->num, order, ptr->norm[1], ptr->enc_type);
}

void setup_pieces_pawn(struct TBEntry_pawn *ptr, uint8_t *data,
    uint64_t *tb_size, int f)
{
  int i, j;
  int order, order2;

  j = 1 + (ptr->pawns[1] > 0);
  order = data[0] & 0x0f;
  order2 = ptr->pawns[1] ? (data[1] & 0x0f) : 0x0f;
  for (i = 0; i < ptr->num; i++)
    ptr->file[f].pieces[0][i] = data[i + j] & 0x0f;
  ptr->file[f].order[0] = order;
  ptr->file[f].order2[0] = order2;
  set_norm_pawn(ptr, ptr->file[f].norm[0], ptr->file[f].pieces[0], order, order2);
  tb_size[0] = calc_factors_pawn(ptr->file[f].factor[0], ptr->num, order, order2, ptr->file[f].norm[0], f);

  order = data[0] >> 4;
  order2 = ptr->pawns[1] ? (data[1] >> 4) : 0x0f;
  for (i = 0; i < ptr->num; i++)
    ptr->file[f].pieces[1][i] = data[i + j] >> 4;
  ptr->file[f].order[1] = order;
  ptr->file[f].order2[1] = order2;
  set_norm_pawn(ptr, ptr->file[f].norm[1], ptr->file[f].pieces[1], order, order2);
  tb_size[1] = calc_factors_pawn(ptr->file[f].factor[1], ptr->num, order, order2, ptr->file[f].norm[1], f);
}

static void calc_symlen(struct PairsData *d, int s, char *tmp)
{
  int s1, s2;

  int w = *(int *)(d->sympat + 3 * s);
  s2 = (w >> 12) & 0x0fff;
  if (s2 == 0x0fff)
    d->symlen[s] = 0;
  else {
    s1 = w & 0x0fff;
    if (!tmp[s1]) calc_symlen(d, s1, tmp);
    if (!tmp[s2]) calc_symlen(d, s2, tmp);
    d->symlen[s] = d->symlen[s1] + d->symlen[s2] + 1;
  }
  tmp[s] = 1;
}

#ifdef LOOKUP
static struct PairsData *setup_pairs(uint8_t *data, uint64_t tb_size,
    uint64_t *size, uint8_t **next)
{
  struct PairsData *d;
  int i;

  // data will/should always point to an even address here
  if (data[0] & 0x80) {
    d = malloc(sizeof(struct PairsData));
    d->idxbits = 0;
    d->min_len = data[1];
    *next = data + 2;
    size[0] = size[1] = size[2] = 0;
    return d;
  }

  int blocksize = data[1];
  int idxbits = data[2];
  uint32_t real_num_blocks = *(uint32_t *)(&data[4]);
  uint32_t num_blocks = real_num_blocks + *(uint8_t *)(&data[3]);
  int max_len = data[8];
  int min_len = data[9];
  int h = max_len - min_len + 1;
  int num_syms = *(uint16_t *)(&data[10 + 2 * h]);
  uint16_t *offset = (uint16_t *)(&data[10]);

  int hh = h;
  if (max_len < LUBITS)
    hh = LUBITS - min_len + 1;

  uint64_t tmp_base[hh];
  for (i = h - 1; i < hh; i++)
    tmp_base[i] = 0;
  for (i = h - 2; i >= 0; i--)
    tmp_base[i] = (tmp_base[i + 1] + offset[i] - offset[i + 1]) / 2;
  for (i = 0; i < h; i++)
    tmp_base[i] <<= 64 - (min_len + i);

  int num_lu = 0;
  if (min_len <= LUBITS)
    num_lu = (1 << LUBITS) - (tmp_base[LUBITS - min_len] >> (64 - LUBITS));

  d = malloc(sizeof(struct PairsData) + hh * sizeof(uint64_t) + num_lu * 3 + num_syms);
  for (i = 0; i < hh; i++)
    d->base[i] = tmp_base[i];
  d->offset = offset;
  d->blocksize = blocksize;
  d->idxbits = idxbits;
  d->lookup_len = (uint16_t *)((uint8_t *)d + sizeof(struct PairsData) + hh * sizeof(uint64_t));
  d->lookup_bits = (uint8_t *)((uint8_t *)d + sizeof(struct PairsData) + hh * sizeof(uint64_t) + num_lu * 2);
  d->symlen = (uint8_t *)d + sizeof(struct PairsData) + hh * sizeof(uint64_t) + num_lu * 3;
  d->sympat = &data[12 + 2 * h];
  d->min_len = min_len;
  *next = &data[12 + 2 * h + 3 * num_syms + (num_syms & 1)];

  int num_indices = (tb_size + (1ULL << idxbits) - 1) >> idxbits;
  size[0] = 6ULL * num_indices;
  size[1] = 2ULL * num_blocks;
  size[2] = (1ULL << blocksize) * real_num_blocks;

  char tmp[num_syms];
  for (i = 0; i < num_syms; i++)
    tmp[i] = 0;
  for (i = 0; i < num_syms; i++)
    if (!tmp[i])
      calc_symlen(d, i, tmp);

  for (i = 0; i < num_lu; i++) {
    uint64_t code = tmp_base[LUBITS - min_len] + (((uint64_t)i) << (64 - LUBITS));
    int bits = LUBITS;
    d->lookup_len[i] = 0;
    d->lookup_bits[i] = 0;
    for (;;) {
      int l = 0;
      while (code < tmp_base[l]) l++;
      if (l + min_len > bits) break;
      int sym = d->offset[l] + ((code - tmp_base[l]) >> (64 - (l + min_len)));
      d->lookup_len[i] += d->symlen[sym] + 1;
      d->lookup_bits[i] += l + min_len;
      bits -= l + min_len;
      code <<= (l + min_len);
    }
  }

  d->offset -= d->min_len;

  return d;
}
#else
static struct PairsData *setup_pairs(uint8_t *data, uint64_t tb_size,
    uint64_t *size, uint8_t **next)
{
  struct PairsData *d;
  int i;

  // data will/should always point to an even address here
  if (data[0] & 0x80) {
    d = malloc(sizeof(struct PairsData));
    d->idxbits = 0;
    d->min_len = data[1];
    *next = data + 2;
    size[0] = size[1] = size[2] = 0;
    return d;
  }

  int blocksize = data[1];
  int idxbits = data[2];
  uint32_t real_num_blocks = *(uint32_t *)(&data[4]);
  uint32_t num_blocks = real_num_blocks + *(uint8_t *)(&data[3]);
  int max_len = data[8];
  int min_len = data[9];
  int h = max_len - min_len + 1;
  int num_syms = *(uint16_t *)(&data[10 + 2 * h]);
  d = malloc(sizeof(struct PairsData) + h * sizeof(uint64_t) + num_syms);
  d->offset = (uint16_t *)(&data[10]);
  d->blocksize = blocksize;
  d->idxbits = idxbits;
  d->symlen = (uint8_t *)d + sizeof(struct PairsData) + h * sizeof(uint64_t);
  d->sympat = &data[12 + 2 * h];
  d->min_len = min_len;
  *next = &data[12 + 2 * h + 3 * num_syms + (num_syms & 1)];

  int num_indices = (tb_size + (1 << idxbits) - 1) >> idxbits;
  size[0] = 6ULL * num_indices;
  size[1] = 2ULL * num_blocks;
  size[2] = (1ULL << blocksize) * real_num_blocks;

  char tmp[num_syms];
  for (i = 0; i < num_syms; i++)
    tmp[i] = 0;
  for (i = 0; i < num_syms; i++)
    if (!tmp[i])
      calc_symlen(d, i, tmp);

  d->base[h - 1] = 0;
  for (i = h - 2; i >= 0; i--)
    d->base[i] = (d->base[i + 1] + d->offset[i] - d->offset[i + 1]) / 2;
  for (i = 0; i < h; i++)
    d->base[i] <<= 64 - (min_len + i);

  d->offset -= d->min_len;

  return d;
}
#endif

static char *prt_str(char *str, uint64_t key)
{
  int i, k;
  for (i = 0; i < 6; i++) {
    k = (key >> (4 * (5-i))) & 0x0f;
    while (k--)
      *str++ = pchr[i];
  }
  return str;
}

static void init_table(struct TBEntry *entry, uint64_t key)
{
  uint8_t *next;
  int f, s;
  uint64_t tb_size[8];
  uint64_t size[8 * 3];

  // first mmap the table into memory
  char file[256];
  int i;
  uint64_t k1, k2, key2;
  key2 = 0ULL;
  k1 = key;
#if defined(SUICIDE)
  for (i = 0; i < 6; i++, k1 >>= 4)
    key2 += tb_piece_key[i + 9] * (k1 & 0x0f);
  for (i = 0; i < 6; i++, k1 >>= 4)
    key2 += tb_piece_key[i + 1] * (k1 & 0x0f);
#else
  for (i = 0; i < 5; i++, k1 >>= 4)
    key2 += tb_piece_key[i + 9] * (k1 & 0x0f);
  for (i = 0; i < 5; i++, k1 >>= 4)
    key2 += tb_piece_key[i + 1] * (k1 & 0x0f);
#endif

  strcpy(file, WDLdir);
  strcat(file, "/");
  k1 = key & WHITEMATMASK;
  k2 = key & BLACKMATMASK;
#if defined(SUICIDE)
  k2 >>= 4 * 6;
#else
  k2 >>= 4 * 5;
#endif
  if (entry->key != (uint32_t)key) {
    uint64_t tmp = k1;
    k1 = k2;
    k2 = tmp;
    tmp = key;
    key = key2;
    key2 = tmp;
  }
  char *str = file + strlen(file);
#if defined(SUICIDE)
  str = prt_str(str, k1);
  *str++ = 'v';
  str = prt_str(str, k2);
#else
  *str++ = 'K';
  str = prt_str(str, k1);
  *str++ = 'v';
  *str++ = 'K';
  str = prt_str(str, k2);
#endif
  *str++ = 0;
  strcat(file, WDLSUFFIX);
  uint64_t dummy;
  entry->data = map_file(file, 1, &dummy);
  uint8_t *data = (uint8_t *)entry->data;

#if !defined(SUICIDE)
  if (((uint32_t *)data)[0] != WDL_MAGIC) {
    fprintf(stderr, "Corrupted table.\n");
    exit(1);
  }
#else
  // Allow SUICIDE and GIVEAWAY to use the pawnless tables of the other.
  if (((uint32_t *)data)[0] != WDL_MAGIC
      && (entry->has_pawns || ((uint32_t *)data)[0] != OTHER_MAGIC)) {
    fprintf(stderr, "Corrupted table.\n");
    exit(1);
  }
#endif

  int split = data[4] & 0x01;
  int files = data[4] & 0x02 ? 4 : 1;

  data += 5;

  if (!entry->has_pawns) {
    struct TBEntry_piece *ptr = (struct TBEntry_piece *)entry;
    setup_pieces_piece(ptr, data, &tb_size[0]);
    data += ptr->num + 1;
    data += ((uintptr_t)data) & 0x01;

    ptr->precomp[0] = setup_pairs(data, tb_size[0], &size[0], &next);
    data = next;
    if (split) {
      ptr->precomp[1] = setup_pairs(data, tb_size[1], &size[3], &next);
      data = next;
    } else
      ptr->precomp[1] = NULL;

    ptr->precomp[0]->indextable = (char *)data;
    data += size[0];
    if (split) {
      ptr->precomp[1]->indextable = (char *)data;
      data += size[3];
    }

    ptr->precomp[0]->sizetable = (uint16_t *)data;
    data += size[1];
    if (split) {
      ptr->precomp[1]->sizetable = (uint16_t *)data;
      data += size[4];
    }

    data = (uint8_t *)((((uintptr_t)data) + 0x3f) & ~0x3f);
    ptr->precomp[0]->data = data;
    data += size[2];
    if (split) {
      data = (uint8_t *)((((uintptr_t)data) + 0x3f) & ~0x3f);
      ptr->precomp[1]->data = data;
    }

    uint64_t key = 0;
    int i;
    for (i = 0; i < ptr->num; i++)
      key += tb_piece_key[ptr->pieces[0][i]];
    ptr->key = (uint32_t)(key);
  } else {
    struct TBEntry_pawn *ptr = (struct TBEntry_pawn *)entry;
    s = 1 + (ptr->pawns[1] > 0);
    for (f = 0; f < 4; f++) {
      setup_pieces_pawn((struct TBEntry_pawn *)ptr, data, &tb_size[2 * f], f);
      data += ptr->num + s;
    }
    data += ((uintptr_t)data) & 0x01;

    for (f = 0; f < files; f++) {
      ptr->file[f].precomp[0] = setup_pairs(data, tb_size[2 * f], &size[6 * f], &next);
      data = next;
      if (split) {
        ptr->file[f].precomp[1] = setup_pairs(data, tb_size[2 * f + 1], &size[6 * f + 3], &next);
        data = next;
      } else
        ptr->file[f].precomp[1] = NULL;
    }

    for (f = 0; f < files; f++) {
      ptr->file[f].precomp[0]->indextable = (char *)data;
      data += size[6 * f];
      if (split) {
        ptr->file[f].precomp[1]->indextable = (char *)data;
        data += size[6 * f + 3];
      }
    }

    for (f = 0; f < files; f++) {
      ptr->file[f].precomp[0]->sizetable = (uint16_t *)data;
      data += size[6 * f + 1];
      if (split) {
        ptr->file[f].precomp[1]->sizetable = (uint16_t *)data;
        data += size[6 * f + 4];
      }
    }

    for (f = 0; f < files; f++) {
      data = (uint8_t *)((((uintptr_t)data) + 0x3f) & ~0x3f);
      ptr->file[f].precomp[0]->data = data;
      data += size[6 * f + 2];
      if (split) {
        data = (uint8_t *)((((uintptr_t)data) + 0x3f) & ~0x3f);
        ptr->file[f].precomp[1]->data = data;
        data += size[6 * f + 5];
      }
    }
  }
}

uint8_t decompress_pairs(struct PairsData *d, uint64_t idx)
{
  int l;

  if (!d->idxbits)
    return d->min_len;

  uint32_t mainidx = idx >> d->idxbits;
  int litidx = (idx & ((1 << d->idxbits) - 1)) - (1 << (d->idxbits - 1));
  uint32_t block = *(uint32_t *)(d->indextable + 6 * mainidx);
  litidx += *(uint16_t *)(d->indextable + 6 * mainidx + 4);
  if (litidx < 0) {
    do {
      litidx += d->sizetable[--block] + 1;
    } while (litidx < 0);
  } else {
    while (litidx > d->sizetable[block])
      litidx -= d->sizetable[block++] + 1;
  }

  uint32_t *ptr = (uint32_t *)(d->data + ((uint64_t)block << d->blocksize));

  int m = d->min_len;
  uint16_t *offset = d->offset;
  uint64_t *base = d->base - m;
  uint8_t *symlen = d->symlen;
  int sym, bitcnt;

#ifndef LOOKUP
  uint64_t code = __builtin_bswap64(*((uint64_t *)ptr));
  ptr += 2;
  bitcnt = 0; // number of "empty bits" in code
  for (;;) {
    l = m;
    while (code < base[l]) l++;
    sym = offset[l] + ((code - base[l]) >> (64 - l));
    if (litidx < (int)symlen[sym] + 1) break;
    litidx -= (int)symlen[sym] + 1;
    code <<= l;
    bitcnt += l;
    if (bitcnt >= 32) {
      bitcnt -= 32;
      code |= (uint64_t)(__builtin_bswap32(*ptr++)) << bitcnt;
    }
  }
#else
  uint64_t code = __builtin_bswap64(*(uint64_t *)ptr);
  ptr += 2;
  bitcnt = 0; // number of "empty bits" in code
  for (;;) {
    if (m <= LUBITS && code >= base[LUBITS]) {
      int lu = (code - base[LUBITS]) >> (64 - LUBITS);
      if (litidx < d->lookup_len[lu]) {
        for (;;) {
          l = m;
          while (code < base[l]) l++;
          sym = offset[l] + ((code - base[l]) >> (64 - l));
          if (litidx < (int)symlen[sym] + 1) break;
          litidx -= (int)symlen[sym] + 1;
          code <<= l;
        }
        break;
      }
      litidx -= d->lookup_len[lu];
      l = d->lookup_bits[lu];
    } else {
      l = LUBITS + 1;
      while (code < base[l]) l++;
      sym = offset[l] + ((code - base[l]) >> (64 - l));
      if (litidx < (int)symlen[sym] + 1) break;
      litidx -= (int)symlen[sym] + 1;
    }
    code <<= l;
    bitcnt += l;
    if (bitcnt >= 32) {
      bitcnt -= 32;
      code |= (uint64_t)(__builtin_bswap32(*ptr++)) << bitcnt;
    }
  }
#endif

  uint8_t *sympat = d->sympat;
  while (symlen[sym] != 0) {
    int w = *(int *)(sympat + 3 * sym);
    int s1 = w & 0x0fff;
    if (litidx < (int)symlen[s1] + 1)
      sym = s1;
    else {
      litidx -= (int)symlen[s1] + 1;
      sym = (w >> 12) & 0x0fff;
    }
  }
//  return (int)((char)((*(int *)(sympat + 3 * sym)) & 0x0fff));
  return *(sympat + 3 * sym);
}

extern int numpcs;
#ifdef HAS_PAWNS
extern int numpawns;
#endif

static __attribute__ ((noinline)) void probe_failed(int *pieces);

int probe_table(int *restrict pieces, int *restrict gpos, int wtm)
{
  struct TBEntry *ptr;
  struct TBHashEntry *ptr2;
  uint64_t idx, key;
  int i;
  uint8_t res;
  int pos[TBPIECES];

  key = 0ULL;
  for (i = 0; i < numpcs; i++)
    key += tb_piece_key[pieces[i]];

#if !defined(SUICIDE) && !defined(LOSER) && !defined(GIVEAWAY)
  if (!key) return 0;
#endif
#ifdef LOSER
// in loser's, no non-king material at all means the other side had already won
  if (!key) return -2;
#endif

  ptr2 = TB_hash[key >> (64 - TBHASHBITS)];
  for (i = 0; i < HSHMAX; i++)
    if (ptr2[i].key == key) break;
  if (i == HSHMAX) {
#if defined(SUICIDE)
    if (!(key & WHITEMATMASK) || !(key & BLACKMATMASK)) return 2;
#endif
#ifdef LOSER
    if (!(key & WHITEMATMASK)) return wtm ? 2 : -2;
    if (!(key & BLACKMATMASK)) return wtm ? -2 : 2;
#endif
    probe_failed(pieces);
  }

  ptr = ptr2[i].ptr;
  if (!ptr->ready) {
    LOCK(TB_mutex);
    if (!ptr->ready) {
      init_table(ptr, key);
      ptr->ready = 1;
    }
    UNLOCK(TB_mutex);
  }

  int bside, mirror, cmirror;
  if (!ptr->symmetric) {
    if ((uint32_t)key != ptr->key) {
      cmirror = 8;
      mirror = 0x38;
      bside = wtm;
    } else {
      cmirror = mirror = 0;
      bside = !wtm;
    }
  } else {
    cmirror = wtm ? 0 : 8;
    mirror = wtm ? 0 : 0x38;
    bside = 0;
  }

  int j;
  if (!ptr->has_pawns) {
    struct TBEntry_piece *entry = (struct TBEntry_piece *)ptr;
    uint8_t *pc = entry->pieces[bside];
    for (i = 0, j = 0; i < entry->num; i++) {
      for (; pieces[j] != (pc[i] ^ cmirror); j++);
      pos[i] = gpos[j++];
      if (i < entry->num && pc[i] != pc[i + 1]) j = 0;
    }
    idx = encode_piece(entry, entry->norm[bside], pos, entry->factor[bside]);
    res = decompress_pairs(entry->precomp[bside], idx);
  } else {
    struct TBEntry_pawn *entry = (struct TBEntry_pawn *)ptr;
    int k = entry->file[0].pieces[0][0] ^ cmirror;
    for (i = 0, j = 0; i < entry->pawns[0]; j++)
      if (pieces[j] == k) pos[i++] = gpos[j] ^ mirror;
    int f = pawn_file(entry, pos);
    uint8_t *pc = entry->file[f].pieces[bside];
    for (j = 0; i < ptr->num; i++) {
      for (; pieces[j] != (pc[i] ^ cmirror); j++);
      pos[i] = gpos[j++] ^ mirror;
      if (i < ptr->num && pc[i] != pc[i + 1]) j = 0;
    }
    idx = encode_pawn(entry, entry->file[f].norm[bside], pos, entry->file[f].factor[bside]);
    res = decompress_pairs(entry->file[f].precomp[bside], idx);
  }

  return ((int)res) - 2;
}

#if defined(REGULAR)
int probe_tb(int *restrict pieces, int *restrict gpos, int wtm, bitboard occ,
             int alpha, int beta)
{
  int i, j, k, s, t;
  int sq, sq2;
  bitboard bb;

  if (wtm) {
    for (i = 0; i < numpcs; i++) {
      if (i == black_king) continue;
      s = pieces[i];
      if (!(s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!t || (t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
// alternatively, change pieces around
        pieces[i] = 0;
        int tmp = gpos[j];
        gpos[j] = gpos[i];
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[white_king];
        bb = occ & ~bit[sq2];
        for (k = 0; k < numpcs; k++) {
          t = pieces[k];
          if (!(t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, bb)) break;
        }
        if (k == numpcs) {
          if (pieces[j] == WPAWN && gpos[j] >= 0x38) {
            int m;
            for (m = WQUEEN; m >= WKNIGHT; m--) {
              pieces[j] = m;
              int v = -probe_tb(pieces, gpos, 0, bb, -beta, -alpha);
              if (v > alpha) {
                alpha = v;
                if (alpha >= beta) break;
              }
            }
            pieces[j] = WPAWN;
          } else {
            int v = -probe_tb(pieces, gpos, 0, bb, -beta, -alpha);
            if (v > alpha)
              alpha = v;
          }
        }
        gpos[j] = tmp;
        pieces[i] = s;
        if (alpha >= beta)
          return alpha;
      }
    }
  } else {
    for (i = 0; i < numpcs; i++) {
      if (i == white_king) continue;
      s = pieces[i];
      if (!s || (s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!(t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
// alternatively, change pieces around
        pieces[i] = 0;
        int tmp = gpos[j];
        gpos[j] = gpos[i];
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[black_king];
        bb = occ & ~bit[sq2];
        for (k = 0; k < numpcs; k++) {
          t = pieces[k];
          if (!t || (t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, bb)) break;
        }
        if (k == numpcs) {
          if (pieces[j] == BPAWN && gpos[j] < 0x08) {
            int m;
            for (m = BQUEEN; m >= BKNIGHT; m--) {
              pieces[j] = m;
              int v = -probe_tb(pieces, gpos, 1, bb, -beta, -alpha);
              if (v > alpha) {
                alpha = v;
                if (alpha >= beta) break;
              }
            }
            pieces[j] = BPAWN;
          } else {
            int v = -probe_tb(pieces, gpos, 1, bb, -beta, -alpha);
            if (v > alpha)
              alpha = v;
          }
        }
        gpos[j] = tmp;
        pieces[i] = s;
        if (alpha >= beta)
          return alpha;
      }
    }
  }

  int v = probe_table(pieces, gpos, wtm);
  return alpha > v ? alpha : v;
}
#elif defined(SUICIDE)
#if 0
#define old_probe_tb probe_tb

int old_probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

static int old_probe_tb_capts(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta, int *capts)
{
  int i, j, s, t;
  int sq, sq2;

  // first try captures

  *capts = 0;
  if (wtm) {
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
      if (!(s & 0x08)) continue;
      sq = pos[i];      /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!t || (t & 0x08)) continue;
        sq2 = pos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
// alternatively, change pieces around
        pieces[i] = 0;
        int tmp = pos[j];
        pos[j] = pos[i];
        if (pieces[j] == WPAWN && pos[j] >= 0x38) {
          int m;
          for (m = WKING; m >= WKNIGHT; m--) {
            pieces[j] = m;
            int v = -old_probe_tb(pieces, pos, 0, occ & ~bit[sq2], -beta, -alpha);
            if (v > alpha) {
              alpha = v;
              if (alpha >= beta) break;
            }
          }
          pieces[j] = WPAWN;
        } else {
          int v = -old_probe_tb(pieces, pos, 0, occ & ~bit[sq2], -beta, -alpha);
          if (v > alpha)
            alpha = v;
        }
        pos[j] = tmp;
        pieces[i] = s;
        *capts = 1;
        if (alpha >= beta)
          return alpha;
      }
    }
  } else {
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
      if (!s || (s & 0x08)) continue;
      sq = pos[i];      /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!(t & 0x08)) continue;
        sq2 = pos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
        pieces[i] = 0;
        int tmp = pos[j];
        pos[j] = pos[i];
        if (pieces[j] == BPAWN && pos[j] < 0x08) {
          int m;
          for (m = BKING; m >= BKNIGHT; m--) {
            pieces[j] = m;
            int v = -old_probe_tb(pieces, pos, 1, occ & ~bit[sq2], -beta, -alpha);
            if (v > alpha) {
              alpha = v;
              if (alpha >= beta) break;
            }
          }
          pieces[j] = BPAWN;
        } else {
          int v = -old_probe_tb(pieces, pos, 1, occ & ~bit[sq2], -beta, -alpha);
          if (v > alpha)
            alpha = v;
        }
        pos[j] = tmp;
        pieces[i] = s;
        *capts = 1;
        if (alpha >= beta)
          return alpha;
      }
    }
  }

  return alpha;
}

#if 0
int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta)
{
  int i, s, sq;
  bitboard bb;
  int capts;

  // first try captures
  int v = probe_tb_capts(pieces, pos, wtm, occ, alpha, beta, &capts);
  if (capts) return v;

  // no captures, so try threats
  if (wtm) {
#if 0
// not required for the moment
    for (i = 0; i < numpawns; i++) {
      s = pieces[i];
      if (!s || (s & 0x08)) continue;
      sq = pos[i];
      if (sq >= 0x30 || (bit[sq + 8] & occ)) continue;
      pos[i] = sq + 8;
      occ2 = occ ^ bit[sq];
      v = -probe_tb_capts(pieces, pos, 0, occ2 ^ bit[sq + 8], -beta, -alpha, &capts);
      if (capts && v > alpha) {
        alpha = v;
        if (alpha >= beta) {
          pos[i] = sq;
          return alpha;
        }
      }
      if (sq < 0x10 && !(bit[sq + 16] & occ)) {
        pos[i] = sq + 16;
        v = -probe_tb_capts(pieces, pos, 0, occ2 ^ bit[sq + 16], -beta, -alpha, &capts);
        if (capts && v > alpha) {
          alpha = v;
          if (alpha >= beta) {
            pos[i] = sq;
            return alpha;
          }
        }
      }
      pos[i] = sq;
    }
#endif
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
      if (s < 2 || (s & 0x08)) continue;
      sq = pos[i];
      bb = PieceMoves(sq, s, occ);
      bitboard occ2 = occ ^ bit[sq];
      while (bb) {
        int sq2 = FirstOne(bb);
        pos[i] = sq2;
        v = -probe_tb_capts(pieces, pos, 0, occ2 ^ bit[sq2], -beta, -alpha, &capts);
        if (capts) {
          if (v > alpha) {
            alpha = v;
            if (alpha >= beta) {
              pos[i] = sq;
              return alpha;
            }
          }
        }
        ClearFirst(bb);
      }
      pos[i] = sq;
    }
  } else {
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
//      if ((s & 0x07) < 2 || !(s & 0x08)) continue;
      if (s < 10) continue;
      sq = pos[i];
      bb = PieceMoves(sq, s, occ);
      bitboard occ2 = occ ^ bit[sq];
      while (bb) {
        int sq2 = FirstOne(bb);
        pos[i] = sq2;
        v = -probe_tb_capts(pieces, pos, 1, occ2 ^ bit[sq2], -beta, -alpha, &capts);
        if (capts) {
          if (v > alpha) {
            alpha = v;
            if (alpha >= beta) {
              pos[i] = sq;
              return alpha;
            }
          }
        }
        ClearFirst(bb);
      }
      pos[i] = sq;
    }
  }

  v = probe_table(pieces, pos, wtm);
  return alpha > v ? alpha : v;
}
#endif

int old_probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta)
{
  int i, s, sq;
  bitboard bb;
  int capts;

  int v = old_probe_tb_capts(pieces, pos, wtm, occ, alpha, beta, &capts);
  if (capts) return v;

  bitboard black = 0;
  for (i = 0; i < numpcs; i++)
    if (pieces[i] & 0x08)
      black |= bit[pos[i]];

  if (wtm) {
    bitboard atts = 0;
    for (i = 0; i < numpcs; i++)
      if (pieces[i] & 0x08)
        atts |= PieceRange(pos[i], pieces[i], occ);
    if (atts & (occ ^ black)) atts = ~occ;
    else atts &= ~occ;
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
      if (s < 2 || s >= 8) continue;
      sq = pos[i];
      bb = PieceRange(sq, s, occ) & atts;
      bitboard occ2 = occ ^ bit[sq];
      while (bb) {
        int sq2 = FirstOne(bb);
        pos[i] = sq2;
        v = -old_probe_tb_capts(pieces, pos, 0, occ2 ^ bit[sq2], -beta, -alpha, &capts);
        if (capts && v > alpha) {
          alpha = v;
          if (alpha >= beta) {
            pos[i] = sq;
            return alpha;
          }
        }
        ClearFirst(bb);
      }
      pos[i] = sq;
    }
  } else {
    bitboard atts = 0;
    for (i = 0; i < numpcs; i++)
      if (pieces[i] && !(pieces[i] & 0x08))
        atts |= PieceRange(pos[i], pieces[i], occ);
    if (atts & black) atts = ~occ;
    else atts &= ~occ;
    for (i = 0; i < numpcs; i++) {
      s = pieces[i];
//      if ((s & 0x07) < 2 || !(s & 0x08)) continue;
      if (s < 10) continue;
      sq = pos[i];
      bb = PieceRange(sq, s, occ) & atts;
      bitboard occ2 = occ ^ bit[sq];
      while (bb) {
        int sq2 = FirstOne(bb);
        pos[i] = sq2;
        v = -old_probe_tb_capts(pieces, pos, 1, occ2 ^ bit[sq2], -beta, -alpha, &capts);
        if (capts && v > alpha) {
          alpha = v;
          if (alpha >= beta) {
            pos[i] = sq;
            return alpha;
          }
        }
        ClearFirst(bb);
      }
      pos[i] = sq;
    }
  }

  v = probe_table(pieces, pos, wtm);
  return alpha > v ? alpha : v;
}
#else
#include "sprobe.c"
#endif
#elif defined(ATOMIC)
int probe_tb(int *pieces, int *gpos, int wtm, bitboard occ, int alpha, int beta)
{
  int i, j, k, s, t;
  int sq, sq2;
  int pt[TBPIECES];
  bitboard occ2;

  if (wtm) {
    for (i = 0; i < numpcs; i++) {
      if (i == black_king) continue;
      s = pieces[i];
      if (!(s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      if (bit[sq] & KingRange(gpos[white_king])) continue;
      for (j = 0; j < numpcs; j++) {
        if (j == white_king) continue;
        t = pieces[j];
        if (!t || (t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
        if (bit[sq] & KingRange(gpos[black_king])) return 2;
// perform capture
        occ2 = occ;
        for (k = 0; k < numpcs; k++)
          if (k != j && (!(bit[gpos[k]] & atom_mask[sq]) || ((pieces[k] & 0x07) == PAWN && k != i))) {
            pt[k] = pieces[k];
          } else {
            pt[k] = 0;
            occ2 &= ~bit[gpos[k]];
          }
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[white_king];
        if (bit[king_sq] & KingRange(gpos[black_king])) k = numpcs;
        else for (k = 0; k < numpcs; k++) {
          t = pt[k];
          if (!(t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, occ2)) break;
        }
        if (k == numpcs) { // legal, so probe
          int v = -probe_tb(pt, gpos, 0, occ2, -beta, -alpha);
          if (v > alpha)
            alpha = v;
        }
        if (alpha >= beta)
          return alpha;
      }
    }
  } else {
    for (i = 0; i < numpcs; i++) {
      if (i == white_king) continue;
      s = pieces[i];
      if (!s || (s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      if (bit[sq] & KingRange(gpos[black_king])) continue;
      for (j = 0; j < numpcs; j++) {
        if (j == black_king) continue;
        t = pieces[j];
        if (!(t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
        if (bit[sq] & KingRange(gpos[white_king])) return 2;
// perform capture
        occ2 = occ;
        for (k = 0; k < numpcs; k++)
          if (k != j && (!(bit[gpos[k]] & atom_mask[sq]) || ((pieces[k] & 0x07) == PAWN && k != i))) {
            pt[k] = pieces[k];
          } else {
            pt[k] = 0;
            occ2 &= ~bit[gpos[k]];
          }
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[black_king];
        if (bit[king_sq] & KingRange(gpos[white_king])) k = numpcs;
        else for (k = 0; k < numpcs; k++) {
          t = pt[k];
          if (!t || (t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, occ2)) break;
        }
        if (k == numpcs) { // legal, so probe
          int v = -probe_tb(pt, gpos, 1, occ2, -beta, -alpha);
          if (v > alpha)
            alpha = v;
        }
        if (alpha >= beta)
          return alpha;
      }
    }
  }

  int v = probe_table(pieces, gpos, wtm);
  return alpha > v ? alpha : v;
}
#elif defined(LOSER)
int probe_tb(int *pieces, int *gpos, int wtm, bitboard occ, int alpha, int beta)
{
  int i, j, k, s, t;
  int sq, sq2, capts;
  bitboard bb;

  capts = 0;

  if (wtm) {
    for (i = 0; i < numpcs; i++) {
      if (i == black_king) continue;
      s = pieces[i];
      if (!(s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!t || (t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
// alternatively, change pieces around
        pieces[i] = 0;
        int tmp = gpos[j];
        gpos[j] = gpos[i];
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[white_king];
        bb = occ & ~bit[sq2];
        for (k = 0; k < numpcs; k++) {
          t = pieces[k];
          if (!(t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, bb)) break;
        }
        if (k == numpcs) {
          capts = 1;
          if (pieces[j] == WPAWN && gpos[j] >= 0x38) {
            int m;
            for (m = WQUEEN; m >= WKNIGHT; m--) {
              pieces[j] = m;
              int v = -probe_tb(pieces, gpos, 0, bb, -beta, -alpha);
              if (v > alpha) {
                alpha = v;
                if (alpha >= beta) break;
              }
            }
            pieces[j] = WPAWN;
          } else {
            int v = -probe_tb(pieces, gpos, 0, bb, -beta, -alpha);
            if (v > alpha)
              alpha = v;
          }
        }
        gpos[j] = tmp;
        pieces[i] = s;
        if (alpha >= beta)
          return alpha;
      }
    }
  } else {
    for (i = 0; i < numpcs; i++) {
      if (i == white_king) continue;
      s = pieces[i];
      if (!s || (s & 0x08)) continue;
      sq = gpos[i];     /* square of piece to be captured */
      for (j = 0; j < numpcs; j++) {
        t = pieces[j];
        if (!(t & 0x08)) continue;
        sq2 = gpos[j];
        if (!(bit[sq] & PieceRange(sq2, t, occ))) continue;
// alternatively, change pieces around
        pieces[i] = 0;
        int tmp = gpos[j];
        gpos[j] = gpos[i];
// now check whether move was legal, i.e. white king not in check
        int king_sq = gpos[black_king];
        bb = occ & ~bit[sq2];
        for (k = 0; k < numpcs; k++) {
          t = pieces[k];
          if (!t || (t & 0x08)) continue;
          int sq3 = gpos[k];
          if (bit[king_sq] & PieceRange(sq3, t, bb)) break;
        }
        if (k == numpcs) {
          capts = 1;
          if (pieces[j] == BPAWN && gpos[j] < 0x08) {
            int m;
            for (m = BQUEEN; m >= BKNIGHT; m--) {
              pieces[j] = m;
              int v = -probe_tb(pieces, gpos, 1, bb, -beta, -alpha);
              if (v > alpha) {
                alpha = v;
                if (alpha >= beta) break;
              }
            }
            pieces[j] = BPAWN;
          } else {
            int v = -probe_tb(pieces, gpos, 1, bb, -beta, -alpha);
            if (v > alpha)
              alpha = v;
          }
        }
        gpos[j] = tmp;
        pieces[i] = s;
        if (alpha >= beta)
          return alpha;
      }
    }
  }

  if (capts) return alpha;
  return probe_table(pieces, gpos, wtm);
}
#endif

static __attribute__ ((noinline)) void probe_failed(int *pieces)
{
  int i, j, k;
  char str[32];

  LOCK(fail_mutex);
  k = 0;
  for (i = 0; i < 6; i++)
    for (j = 0; j < numpcs; j++)
      if (pieces[j] ==  WKING - i)
        str[k++] = pchr[i];
  str[k++] = 'v';
  for (i = 0; i < 6; i++)
    for (j = 0; j < numpcs; j++)
      if (pieces[j] == BKING - i)
        str[k++] = pchr[i];
  str[k] = 0;
  fprintf(stderr, "Missing table: %s\n", str);
  exit(1);
}
