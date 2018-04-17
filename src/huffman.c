#include "defs.h"
#include "huffman.h"

void create_code(struct HuffCode *c, int num_syms)
{
  int i, num;
  int idx1, idx2;
  uint64_t min1, min2;

  c->num_syms = num_syms;
  num = 0;
  for (i = 0; i < num_syms; i++)
    if (c->freq[i]) {
      c->nfreq[num] = c->freq[i];
      c->map[i] = num;
      num++;
    }

  for (i = 0; i < num_syms; i++)
    c->length[i] = 0;

  if (num == 1) {
    for (i = 0; i < num_syms; i++)
      if (c->freq[i]) break;
    c->length[i] = 1;
  } else while (num  > 1) {
    min1 = min2 = INT64_MAX;
    idx1 = idx2 = 0;
    for (i = 0; i < num; i++)
      if (c->nfreq[i] < min2) {
	if (c->nfreq[i] < min1) {
	  min2 = min1;
	  idx2 = idx1;
	  min1 = c->nfreq[i];
	  idx1 = i;
	} else {
	  min2 = c->nfreq[i];
	  idx2 = i;
	}
      }
    if (idx1 > idx2) {
      int tmp = idx1;
      idx1 = idx2;
      idx2 = tmp;
    }
    c->nfreq[idx1] = min1 + min2;
    num--;
    for (i = idx2; i < num; i++)
      c->nfreq[i] = c->nfreq[i+1];
    for (i = 0; i < num_syms; i++)
      if (c->map[i] == idx1) {
	c->length[i]++;
      } else if (c->map[i] == idx2) {
	c->map[i] = idx1;
	c->length[i]++;
      } else if (c->map[i] > idx2) {
        c->map[i]--;
      }
  }
}

void sort_code(struct HuffCode *c)
{
  int max_len;

  int num = c->num_syms;

  for (int i = 0; i < num; i++) {
    c->map[i] = i;
    if (c->freq[i] == 0)
      c->length[i] = 0;
  }

  for (int i = 0; i < num; i++)
    for (int j = i + 1; j < num; j++)
      if (    c->length[c->map[i]] < c->length[c->map[j]]
          || (   c->length[c->map[i]] == c->length[c->map[j]]
              && c->freq[c->map[i]] > c->freq[c->map[j]]))
      {
	int tmp = c->map[i];
	c->map[i] = c->map[j];
	c->map[j] = tmp;
      }

  for (int i = 0; i < num; i++)
    c->inv[c->map[i]] = i;

  c->num = num;
  c->max_len = max_len = c->length[c->map[0]];
  c->offset[max_len] = 0;
  c->base[max_len] = 0;
  int k = max_len - 1;
  for (int i = 0; i < num && c->length[c->map[i]]; i++)
    while (k >= c->length[c->map[i]]) {
      c->offset[k] = i;
      c->base[k] = (c->base[k + 1] + (i - c->offset[k + 1])) / 2;
      k--;
    }
  c->min_len = k + 1;
}

uint64_t calc_size(struct HuffCode *c)
{
  uint64_t bits = 0;

  for (int i = 0; i < c->num_syms; i++)
    bits += c->length[i] * c->freq[i];

  return (bits + 7) >> 3;
}
