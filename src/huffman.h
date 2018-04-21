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
  int base[33];
  int offset[33];
};

void create_code(struct HuffCode *c, int num_syms);
uint64_t calc_size(struct HuffCode *c);

#endif
