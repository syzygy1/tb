#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <inttypes.h>

#include "defs.h"

struct HuffCode {
  int64_t freq[MAXSYMB];
  int64_t nfreq[MAXSYMB];
  int map[MAXSYMB];
  int inv[MAXSYMB];
  int length[MAXSYMB];
  int num_syms, num, max_len, min_len;
  int base[32];
  int offset[32];
};

void create_code(struct HuffCode *c, int num_syms);
void sort_code(struct HuffCode *c);
uint64_t calc_size(struct HuffCode *c);

#endif
