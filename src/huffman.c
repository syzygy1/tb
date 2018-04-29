#include <stdlib.h>

#include "defs.h"
#include "huffman.h"

struct List {
  uint64_t w[8192];
  int p[8192];
  int len;
};

#define L 32

static void package_merge(struct HuffCode *c, int num, int *a)
{
  struct List *lists = malloc(L * sizeof(struct List));

  for (int m = 0; m < L; m++) {
    int prev_len = m == 0 ? 0 : lists[m - 1].len;
    int i = 0, j = 0, k = 1;
    while (i < 2 * num - 2 && j < num && k < prev_len) {
      if (c->freq[c->map[j]] < lists[m - 1].w[k - 1] + lists[m - 1].w[k]) {
        lists[m].w[i] = c->freq[c->map[j]];
        lists[m].p[i] = 0;
        j++;
      } else {
        lists[m].w[i] = lists[m - 1].w[k - 1] + lists[m - 1].w[k];
        lists[m].p[i] = k;
        k += 2;
      }
      i++;
    }
    while (i < 2 * num - 2 && j < num) {
      lists[m].w[i] = c->freq[c->map[j]];
      lists[m].p[i] = 0;
      j++;
      i++;
    }
    while (i < 2 * num - 2 && k < prev_len) {
      lists[m].w[i] = lists[m - 1].w[k - 1] + lists[m - 1].w[k];
      lists[m].p[i] = k;
      k += 2;
      i++;
    }
    lists[m].len = i;
  }

  int k = lists[L - 1].len;
  for (int m = L - 1; m >= 0; m--) {
    int l = 0;
    int n = 0;
    for (int i = 0; i < k; i++) {
      if (lists[m].p[i])
        n = lists[m].p[i] + 1;
      else
        l++;
    }
    a[L - 1 - m] = l;
    k = n;
  }

  for (int l = 0; l < L - 1; l++)
    a[l] -= a[l + 1];

  free(lists);
}

static void create_code_old(struct HuffCode *c, int num_syms);
static int sort_code(struct HuffCode *c);

void create_code(struct HuffCode *c, int num_syms)
{
  create_code_old(c, num_syms);
  if (sort_code(c)) return;

  int a[L];

  c->num_syms = num_syms;

  int num = 0;
  for (int i = 0; i < num_syms; i++)
    if (c->freq[i])
      c->map[num++] = i;

  for (int i = 0; i < num; i++)
    for (int j = i + 1; j < num; j++)
      if (c->freq[c->map[i]] > c->freq[c->map[j]]) {
        int tmp = c->map[i];
        c->map[i] = c->map[j];
        c->map[j] = tmp;
      }

  package_merge(c, num, a);

  int k = 0;
  for (int l = L - 1; l >= 0; l--)
    for (int i = 0; i < a[l]; i++)
      c->length[c->map[k++]] = l + 1;

  sort_code(c);
}

void create_code_old(struct HuffCode *c, int num_syms)
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

int sort_code(struct HuffCode *c)
{
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

  int max_len = c->length[c->map[0]];
  if (max_len > L) return 0;

  c->num = num;
  c->max_len = max_len;
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

  return 1;
}

uint64_t calc_size(struct HuffCode *c)
{
  uint64_t bits = 0;

  for (int i = 0; i < c->num_syms; i++)
    bits += c->length[i] * c->freq[i];

  return (bits + 7) >> 3;
}
