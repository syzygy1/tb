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
  uint16_t (*map16[4])[MAX_VALS];
};

void decomp_init_piece(int *pcs);
void decomp_init_pawn(int *pcs, int *pt);
struct tb_handle *open_tb(char *tablename, int wdl);
void decomp_init_table(struct tb_handle *H);
uint8_t *decompress_table(struct tb_handle *H, int bside, int f);
void close_tb(struct tb_handle *H);
void set_perm(struct tb_handle *H, int bside, int f, int *perm, int *pt);
struct TBEntry *get_entry(struct tb_handle *H);
int get_ply_accurate_win(struct tb_handle *H, int f);
int get_ply_accurate_loss(struct tb_handle *H, int f);
int get_dtz_side(struct tb_handle *H, int f);
uint8_t (*get_dtz_map(struct tb_handle *H, int f))[256];
uint16_t (*get_dtz_map16(struct tb_handle *H, int f))[4096];

#endif
