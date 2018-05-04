/*
  Copyright (c) 2011-2016, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef PROBE_H
#define PROBE_H

#include "defs.h"
#include "types.h"

#define TBPIECES 7

#if defined(SUICIDE)
#if !defined(GIVEAWAY)
#define WDLSUFFIX ".stbw"
#define DTZSUFFIX ".stbz"
#define WDLDIR "STBWDIR"
#define DTZDIR "STBZDIR"
#define STATSDIR "STBSTATSDIR"
#define LOGFILE "stblog.txt"
#else
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define WDLDIR "GTBWDIR"
#define DTZDIR "GTBZDIR"
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#endif
#elif defined(LOSER)
#define WDLSUFFIX ".ltbw"
#define DTZSUFFIX ".ltbz"
#define WDLDIR "LTBWDIR"
#define DTZDIR "LTBZDIR"
#define STATSDIR "LTBSTATSDIR"
#define LOGFILE "ltblog.txt"
#elif defined(GIVEAWAY)
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define WDLDIR "GTBWDIR"
#define DTZDIR "GTBZDIR"
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#elif defined(ATOMIC)
#define WDLSUFFIX ".atbw"
#define DTZSUFFIX ".atbz"
#define WDLDIR "ATBWDIR"
#define DTZDIR "ATBZDIR"
#define STATSDIR "ATBSTATSDIR"
#define LOGFILE "atblog.txt"
#else
#define WDLSUFFIX ".rtbw"
#define DTZSUFFIX ".rtbz"
#define WDLDIR "RTBWDIR"
#define DTZDIR "RTBZDIR"
#define STATSDIR "RTBSTATSDIR"
#define LOGFILE "rtblog.txt"
#endif

#define MAX_TBPIECES 8

#if defined(REGULAR)
#define WDL_MAGIC 0x5d23e871
#define DTZ_MAGIC 0xa50c66d7
#elif defined(ATOMIC)
#define WDL_MAGIC 0x49a48d55
#define DTZ_MAGIC 0xeb5ea991
#elif defined(SUICIDE) && !defined(GIVEAWAY)
#define WDL_MAGIC 0x1593f67b
#define DTZ_MAGIC 0x23e7cfe4
#define OTHER_MAGIC 0x21bc55bc
#elif defined(GIVEAWAY)
#define WDL_MAGIC 0x21bc55bc
#define DTZ_MAGIC 0x501bf5d6
#define OTHER_MAGIC 0x1593f67b
#endif

#ifdef SUICIDE
#define TBHASHBITS 12
#else
#define TBHASHBITS 10
#endif

struct TBHashEntry;

struct PairsData {
  char *indextable;
  uint16_t *sizetable;
  uint8_t *data;
  uint16_t *offset;
  uint8_t *symlen;
  uint8_t *sympat;
#ifdef LOOKUP
  uint16_t *lookup_len;
  uint8_t *lookup_bits;
#endif
  int blocksize;
  int idxbits;
  int min_len;
  int max_len; // to allow checking max_len with rtbver/rtbverp
  uint64_t base[];
};

struct TBEntry {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
} __attribute__((__may_alias__));

struct TBEntry_piece {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
  uint8_t enc_type;
  struct PairsData *precomp[2];
  uint64_t factor[2][TBPIECES];
  uint8_t pieces[2][TBPIECES];
  uint8_t norm[2][TBPIECES];
  uint8_t order[2];
};

struct TBEntry_pawn {
  uint8_t *data;
  uint32_t key;
  uint8_t ready;
  uint8_t num;
  uint8_t symmetric;
  uint8_t has_pawns;
  uint8_t pawns[2];
  struct {
    struct PairsData *precomp[2];
    uint64_t factor[2][TBPIECES];
    uint8_t pieces[2][TBPIECES];
    uint8_t norm[2][TBPIECES];
    uint8_t order[2];
    uint8_t order2[2];
  } file[4];
};

struct TBHashEntry {
  uint64_t key;
  struct TBEntry *ptr;
};

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta);

uint64_t encode_piece(struct TBEntry_piece *ptr, uint8_t *norm, int *pos,
    uint64_t *factor);
void decode_piece(struct TBEntry_piece *ptr, uint8_t *norm, int *pos,
    uint64_t *factor, int *order, uint64_t idx);
uint64_t encode_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, int *pos,
    uint64_t *factor);
void decode_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, int *pos,
    uint64_t *factor, int *order, uint64_t idx, int file);

void set_norm_piece(struct TBEntry_piece *ptr, uint8_t *norm, uint8_t *pieces,
    int order);
void set_norm_pawn(struct TBEntry_pawn *ptr, uint8_t *norm, uint8_t *pieces,
    int order, int order2);
uint64_t calc_factors_piece(uint64_t *factor, int num, int order,
    uint8_t *norm, uint8_t enc_type);
uint64_t calc_factors_pawn(uint64_t *factor, int num, int order, int order2,
    uint8_t *norm, int file);

void calc_order_piece(int num, int ord, int *order, uint8_t *norm);
void calc_order_pawn(int num, int ord, int ord2, int *order, uint8_t *norm);

#endif
