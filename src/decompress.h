/*
  Copyright (c) 2011-2016, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include "probe.h"

#ifdef COMPRESS_H
#error decompress.h conflicts with compress.h
#endif

struct tb_handle {
  FILE *F;
  uint8_t *data;
  uint64_t data_size;
  int wdl;
  int num_files;
  int split;
  int has_pawns;
  struct {
    uint64_t idx[2];
    uint64_t size[2];
  } file[4];
  union {
    struct TBEntry entry;
    struct TBEntry_piece entry_piece;
    struct TBEntry_pawn entry_pawn;
  };
  uint8_t dtz_flags[4];
  uint8_t (*map[4])[256];
};

#endif
