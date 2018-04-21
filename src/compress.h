#ifndef COMPRESS_H
#define COMPRESS_H

#include "defs.h"
#include "huffman.h"
#include "types.h"

#ifdef DECOMPRESS_H
#error compress.h conflicts with decompress.h
#endif

struct tb_handle {
  char name[64];
  int num_tables;
  int wdl;
  int split;
  uint8_t perm[8][TBPIECES];
  int default_blocksize;
  int blocksize[8];
  int idxbits[8];
  uint64_t real_num_blocks[8];
  uint64_t num_blocks[8];
  int num_indices[8];
  int num_syms[8];
  int num_values[8];
  struct HuffCode *c[8];
  struct Symbol *symtable[8];
  struct dtz_map *map[4];
  int flags[8];
  uint8_t single_val[8];
  FILE *H[8];
};

void compress_alloc_wdl(void);
void compress_alloc_dtz_u8(void);
void compress_alloc_dtz_u16(void);
void compress_init_wdl(int *vals, int flags);
void compress_init_dtz_u8(struct dtz_map *map);
void compress_init_dtz_u16(struct dtz_map *map);
void compress_tb_u8(struct tb_handle *F, u8 *data, uint64_t tb_size,
    uint8_t *perm, int minfreq);
void compress_tb_u16(struct tb_handle *F, u16 *data, uint64_t tb_size,
    uint8_t *perm, int minfreq);
void merge_tb(struct tb_handle *F);
struct tb_handle *create_tb(char *tablename, int wdl, int blocksize);
struct HuffCode *construct_pairs_u8(u8 *data, uint64_t size, int minfreq,
    int maxsymbols, int wdl);
struct HuffCode *construct_pairs_u16(u16 *data, uint64_t size, int minfreq,
    int maxsymbols, int wdl);

#endif
