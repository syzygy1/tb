/*
  Copyright (c) 2011-2016 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef PROBE_H
#define PROBE_H

#if defined(SUICIDE)
#if !defined(GIVEAWAY)
#define WDLSUFFIX ".stbw"
#define DTZSUFFIX ".stbz"
#define WDLDIR "STBWDIR"
#define DTZDIR "STBZDIR"
#define TBPIECES 6
#define STATSDIR "STBSTATSDIR"
#define LOGFILE "stblog.txt"
#else
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define WDLDIR "GTBWDIR"
#define DTZDIR "GTBZDIR"
#define TBPIECES 6
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#endif
#elif defined(LOSER)
#define WDLSUFFIX ".ltbw"
#define DTZSUFFIX ".ltbz"
#define WDLDIR "LTBWDIR"
#define DTZDIR "LTBZDIR"
#define TBPIECES 6
#define STATSDIR "LTBSTATSDIR"
#define LOGFILE "ltblog.txt"
#elif defined(GIVEAWAY)
#define WDLSUFFIX ".gtbw"
#define DTZSUFFIX ".gtbz"
#define WDLDIR "GTBWDIR"
#define DTZDIR "GTBZDIR"
#define TBPIECES 6
#define STATSDIR "GTBSTATSDIR"
#define LOGFILE "gtblog.txt"
#elif defined(ATOMIC)
#define WDLSUFFIX ".atbw"
#define DTZSUFFIX ".atbz"
#define WDLDIR "ATBWDIR"
#define DTZDIR "ATBZDIR"
#define TBPIECES 6
#define STATSDIR "ATBSTATSDIR"
#define LOGFILE "atblog.txt"
#else
#define WDLSUFFIX ".rtbw"
#define DTZSUFFIX ".rtbz"
#define WDLDIR "RTBWDIR"
#define DTZDIR "RTBZDIR"
#define TBPIECES 7
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
struct PairsData;

struct TBEntry {
  char *data;
  uint32 key;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
} __attribute__((__may_alias__));

struct TBEntry_piece {
  char *data;
  uint32 key;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte enc_type;
  struct PairsData *precomp[2];
  long64 factor[2][TBPIECES];
  ubyte pieces[2][TBPIECES];
  ubyte norm[2][TBPIECES];
  ubyte order[2];
};

struct TBEntry_pawn {
  char *data;
  uint32 key;
  ubyte ready;
  ubyte num;
  ubyte symmetric;
  ubyte has_pawns;
  ubyte pawns[2];
  struct {
    struct PairsData *precomp[2];
    long64 factor[2][TBPIECES];
    ubyte pieces[2][TBPIECES];
    ubyte norm[2][TBPIECES];
    ubyte order[2];
    ubyte order2[2];
  } file[4];
};

struct TBHashEntry {
  long64 key;
  struct TBEntry *ptr;
};
#endif

