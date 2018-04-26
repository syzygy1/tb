/*
  Copyright (c) 2011-2013, 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "decompress.h"
#include "defs.h"
#include "probe.h"
#include "threads.h"
#include "util.h"

extern int total_work;
extern int numthreads;
extern int numpcs;
extern int numpawns;

static int enc_type;

int pawns0, pawns1;
int use_envdirs = 0;

void decomp_init_piece(int *pcs)
{
  int i, k;

  for (i = 0, k = 0; i < 16; i++)
    if (pcs[i] == 1) k++;
  if (k >= 3) enc_type = 0;
  else if (k == 2) enc_type = 2;
  else { /* only possible for suicide */
    k = 16;
    for (i = 0; i < 16; i++)
      if (pcs[i] < k && pcs[i] > 1) k = pcs[i];
    enc_type = 1 + k;
  }
  pawns0 = pawns1 = 0;
}

void decomp_init_pawn(int *pcs, int *pt)
{
  if (pt[0] == WPAWN) {
    pawns0 = pcs[WPAWN];
    pawns1 = pcs[BPAWN];
  } else {
    pawns0 = pcs[BPAWN];
    pawns1 = pcs[WPAWN];
  }
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

struct PairsData *decomp_setup_pairs(struct tb_handle *H, uint64_t tb_size, uint64_t *size, uint8_t *flags)
{
  struct PairsData *d;
  int i;
  uint8_t data[256];
  FILE *F = H->F;

  fread(data, 1, 2, F);
  *flags = data[0];
  if (*flags & 0x80) {
    d = malloc(sizeof(struct PairsData));
    d->idxbits = 0;
    d->max_len = 0;
    if (H->wdl)
      d->min_len = data[1];
    else
      d->min_len = 0;
    size[0] = size[1] = size[2] = 0;
    return d;
  }
  fread(data + 2, 1, 10, F);
  int blocksize = data[1];
  int idxbits = data[2];
  uint32_t real_num_blocks = data[4] | (data[5] << 8)
                          | (data[6] << 16) | (data[7] << 24);
  uint32_t num_blocks = real_num_blocks + *(uint8_t *)(&data[3]);
  int max_len = data[8];
  int min_len = data[9];
  int h = max_len - min_len + 1;
  fread(data + 12, 1, 2 * h, F);
  int num_syms = data[10 + 2 * h] | (data[11 + 2 * h] << 8);
  d = malloc(sizeof(struct PairsData) + h * sizeof(uint64_t) + num_syms);
  d->blocksize = blocksize;
  d->idxbits = idxbits;
  d->offset = malloc(2 * h);
  memcpy(d->offset, &data[10], 2 * h);
  d->symlen = ((unsigned char *)d) + sizeof(struct PairsData) + h * sizeof(uint64_t);
  d->sympat = malloc(3 * num_syms + (num_syms & 1));
  d->max_len = max_len; // to allow checking max_len with rtbver/rtbverp
  d->min_len = min_len;
  fread(d->sympat, 1, 3 * num_syms + (num_syms & 1), F);

  int num_indices = (tb_size + (1ULL << idxbits) - 1) >> idxbits;
  size[0] = 6ULL * num_indices;
  size[1] = 2ULL * num_blocks;
  size[2] = (1ULL << blocksize) * real_num_blocks;

  char *tmp = (char *)calloc(num_syms, 1);
  for (i = 0; i < num_syms; i++)
    if (!tmp[i])
      calc_symlen(d, i, tmp);
  free(tmp);

  d->base[h - 1] = 0;
  for (i = h - 2; i >= 0; i--)
    d->base[i] = (d->base[i + 1] + d->offset[i] - d->offset[i + 1]) / 2;
  for (i = 0; i < h; i++)
    d->base[i] <<= 64 - (min_len + i);

  d->offset -= d->min_len;

  return d;
}

static void decomp_setup_pieces_piece(struct tb_handle *H, uint64_t *tb_size)
{
  int i;
  int order;
  struct TBEntry_piece *entry = &(H->entry_piece);
  FILE *F = H->F;
  uint8_t data[TBPIECES + 1];

  fread(data, 1, numpcs + 1, F);
  entry->num = numpcs;
  entry->enc_type = enc_type;

  for (i = 0; i < numpcs; i++)
    entry->pieces[0][i] = data[i + 1] & 0x0f;
  order = data[0] & 0x0f;
  entry->order[0] = order;
  set_norm_piece(entry, entry->norm[0], entry->pieces[0], order);
  tb_size[0] = calc_factors_piece(entry->factor[0], entry->num, order, entry->norm[0], entry->enc_type);

  if (H->split) {
    for (i = 0; i < numpcs; i++)
      entry->pieces[1][i] = data[i + 1] >> 4;
    order = data[0] >> 4;
    entry->order[1] = order;
    set_norm_piece(entry, entry->norm[1], entry->pieces[1], order);
    tb_size[1] = calc_factors_piece(entry->factor[1], entry->num, order, entry->norm[1], entry->enc_type);
  } else {
    for (i = 0; i < numpcs; i++) {
      entry->pieces[1][i] = entry->pieces[0][i] ^ 0x08;
      entry->factor[1][i] = entry->factor[0][i];
      entry->norm[1][i] = entry->norm[0][i];
    }
    entry->order[1] = entry->order[0];
  }
}

void decomp_setup_pieces_pawn(struct tb_handle *H, uint64_t *tb_size, int f)
{
  int i, j;
  int order, order2;
  struct TBEntry_pawn *entry = &(H->entry_pawn);
  FILE *F = H->F;
  uint8_t data[TBPIECES + 2];

  entry->num = numpcs;
  entry->pawns[0] = pawns0;
  entry->pawns[1] = pawns1;

  j = 1 + (entry->pawns[1] > 0);
  fread(data, 1, entry->num + j, F);

  order = data[0] & 0x0f;
  order2 = entry->pawns[1] ? (data[1] & 0x0f) : 0x0f;
  for (i = 0; i < entry->num; i++)
    entry->file[f].pieces[0][i] = data[i + j] & 0x0f;
  entry->file[f].order[0] = order;
  entry->file[f].order2[0] = order2;
  set_norm_pawn(entry, entry->file[f].norm[0], entry->file[f].pieces[0], order, order2);
  tb_size[0] = calc_factors_pawn(entry->file[f].factor[0], entry->num, order, order2, entry->file[f].norm[0], f);

  if (H->split) {
    order = data[0] >> 4;
    order2 = entry->pawns[1] ? (data[1] >> 4) : 0x0f0;
    for (i = 0; i < entry->num; i++)
      entry->file[f].pieces[1][i] = data[i + j] >> 4;
    entry->file[f].order[1] = order;
    entry->file[f].order2[1] = order2;
    set_norm_pawn(entry, entry->file[f].norm[1], entry->file[f].pieces[1], order, order2);
    tb_size[1] = calc_factors_pawn(entry->file[f].factor[1], entry->num, order, order2, entry->file[f].norm[1], f);
  } else {
    for (i = 0; i < numpcs; i++) {
      entry->file[f].pieces[1][i] = entry->file[f].pieces[0][i] ^ 0x08;
      entry->file[f].factor[1][i] = entry->file[f].factor[0][i];
      entry->file[f].norm[1][i] = entry->file[f].norm[0][i];
    }
    entry->file[f].order[1] = entry->file[f].order[0];
    entry->file[f].order2[1] = entry->file[f].order2[0];
  }
}

void decomp_init_table(struct tb_handle *H)
{
  uint32_t magic;
  uint8_t byte;
  int f;
  uint64_t size[8 * 3];
  FILE *F = H->F;
  int split, files;
  uint8_t dummy;

  magic = 0;
  fread(&magic, 1, 4, F);
  if (magic != (H->wdl ? WDL_MAGIC : DTZ_MAGIC)) {
    fprintf(stderr, "Corrupted table.\n");
    exit(EXIT_FAILURE);
  }

  fread(&byte, 1, 1, F);
  H->split = split = (H->wdl ? (byte & 0x01) : 0);
  H->num_files = files = (byte & 0x02) ? 4 : 1;
  H->has_pawns = (pawns0 != 0);

  if (pawns0 == 0)
    decomp_setup_pieces_piece(H, H->file[0].size);
  else if (files == 1) {
    // unsupported (and not used)
  } else {
    for (f = 0; f < 4; f++)
      decomp_setup_pieces_pawn(H, H->file[f].size, f);
  }

  if (ftell(F) & 0x01) fgetc(F);

  if (pawns0 == 0) {
    struct TBEntry_piece *entry = &(H->entry_piece);
    entry->precomp[0] = decomp_setup_pairs(H, H->file[0].size[0], &size[0], &(H->dtz_flags[0]));
    if (split)
      entry->precomp[1] = decomp_setup_pairs(H, H->file[0].size[1], &size[3], &dummy);

    if (!H->wdl) {
      if (H->dtz_flags[0] & 2) {
        if (!(H->dtz_flags[0] & 16)) {
          int i;
          uint8_t num;
          H->map[0] = malloc(4 * 256);
          for (i = 0; i < 4; i++) {
            fread(&num, 1, 1, F);
            fread(H->map[0][i], 1, num, F);
          }
        } else {
          int i;
          uint16_t num;
          H->map16[0] = malloc(4 * MAX_VALS * 2);
          for (i = 0; i < 4; i++) {
            fread(&num, 2, 1, F);
            fread(H->map16[0][i], 2, num, F);
          }
        }
      }
      if (ftell(F) & 0x01) fgetc(F);
    }

    entry->precomp[0]->indextable = malloc(size[0]);
    fread(entry->precomp[0]->indextable, 1, size[0], F);
    if (split) {
      entry->precomp[1]->indextable = malloc(size[3]);
      fread(entry->precomp[1]->indextable, 1, size[3], F);
    }

    entry->precomp[0]->sizetable = malloc(size[1]);
    fread(entry->precomp[0]->sizetable, 1, size[1], F);
    if (split) {
      entry->precomp[1]->sizetable = malloc(size[4]);
      fread(entry->precomp[1]->sizetable, 1, size[4], F);
    }

    if (!split)
      entry->precomp[1] = entry->precomp[0];

    uint64_t idx = ftell(F);
    idx = (idx + 0x3f) & ~0x3f;
    H->file[0].idx[0] = idx;
    idx += size[2];
    if (split) {
      idx = (idx + 0x3f) & ~0x3f;
      H->file[0].idx[1] = idx;
      idx += size[5];
    }
  } else {
    struct TBEntry_pawn *entry = &(H->entry_pawn);
    for (f = 0; f < files; f++) {
      entry->file[f].precomp[0] = decomp_setup_pairs(H, H->file[f].size[0], &size[6 * f], &(H->dtz_flags[f]));
      if (split)
        entry->file[f].precomp[1] = decomp_setup_pairs(H, H->file[f].size[1], &size[6 * f + 3], &dummy);
    }

    if (!H->wdl) {
      int i;
      for (f = 0; f < files; f++) {
        if (H->dtz_flags[f] & 2) {
          H->map[f] = malloc(4 * 256);
          uint8_t num;
          for (i = 0; i < 4; i++) {
            fread(&num, 1, 1, F);
            fread(H->map[f][i], 1, num, F);
          }
        }
      }
      if (ftell(F) & 0x01) fgetc(F);
    }

    for (f = 0; f < files; f++) {
      entry->file[f].precomp[0]->indextable = malloc(size[6 * f]);
      fread(entry->file[f].precomp[0]->indextable, 1, size[6 * f], F);
      if (split) {
        entry->file[f].precomp[1]->indextable = malloc(size[6 * f + 3]);
        fread(entry->file[f].precomp[1]->indextable, 1, size[6 * f + 3], F);
      }
    }

    for (f = 0; f < files; f++) {
      entry->file[f].precomp[0]->sizetable = malloc(size[6 * f + 1]);
      fread(entry->file[f].precomp[0]->sizetable, 1, size[6 * f + 1], F);
      if (split) {
        entry->file[f].precomp[1]->sizetable = malloc(size[6 * f + 4]);
        fread(entry->file[f].precomp[1]->sizetable, 1, size[6 * f + 4], F);
      }
    }

    if (!split)
      for (f = 0; f < files; f++)
        entry->file[f].precomp[1] = entry->file[f].precomp[0];

    uint64_t idx = ftell(F);
    for (f = 0; f < files; f++) {
      idx = (idx + 0x3f) & ~0x3f;
      H->file[f].idx[0] = idx;
      idx += size[6 * f + 2];
      if (split) {
        idx = (idx + 0x3f) & ~0x3f;
        H->file[f].idx[1] = idx;
        idx += size[6 * f + 5];
      }
    }
  }
}

uint64_t expand_symbol(uint8_t *dst, int sym, uint64_t idx, uint64_t end, uint8_t *sympat, uint8_t *symlen)
{
  if (idx == end) return idx;
  int w = *(int *)(sympat + 3 * sym);
  if (symlen[sym] == 0) {
    dst[idx++] = w & 0x0fff;
    return idx;
  }
  idx = expand_symbol(dst, w & 0x0fff, idx, end, sympat, symlen);
  idx = expand_symbol(dst, (w >> 12) & 0x0fff, idx, end, sympat, symlen);
  return idx;
}

static uint8_t *table;
static uint64_t table_size = 0;

static struct PairsData *decomp_d;
static uint8_t *decomp_data;
static uint64_t *work_decomp = NULL;

static void decompress_worker(struct thread_data *thread)
{
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  struct PairsData *d = decomp_d;
  uint8_t *dst = table;

  if (!d->idxbits) {
    int s = d->min_len;
    for (; idx < end; idx++)
      dst[idx] = s;
    return;
  }

  int l;
  int m = d->min_len;
  uint16_t *offset = d->offset;
  uint64_t *base = d->base - m;
  uint8_t *symlen = d->symlen;
  uint8_t *sympat = d->sympat;
  int sym, bitcnt;

  uint32_t mainidx = idx >> d->idxbits;
  int litidx = (idx & ((1 << d->idxbits) -1)) - (1 << (d->idxbits - 1));
  uint32_t block = *(uint32_t *)(d->indextable + 6 * mainidx);
  litidx += *(uint16_t *)(d->indextable + 6 * mainidx + 4);
  if (litidx < 0) {
    do {
      litidx += d->sizetable[--block] + 1;
    } while (litidx < 0);
  } else {
    mainidx++;
    while (litidx > d->sizetable[block])
      litidx -= d->sizetable[block++] + 1;
  }

  uint64_t idx2 = (1ULL << (d->idxbits - 1)) + (((uint64_t)mainidx) << d->idxbits);
  if (litidx > 0) {
    idx += d->sizetable[block++] + 1 - litidx;
    while (idx >= idx2) {
      idx2 += 1ULL << d->idxbits;
      mainidx++;
    }
  }

  uint8_t *data = decomp_data + (((uint64_t)block) << d->blocksize);
  while (idx < end) {
    int size = d->sizetable[block] + 1;
    while (idx + size > idx2) {
      if (*(uint32_t *)(d->indextable + 6 * mainidx) != block
          || *((uint16_t *)(d->indextable + 6 * mainidx + 4)) != idx2 - idx)
      {
        fprintf(stderr, "ERROR in main index!!\n");
        exit(EXIT_FAILURE);
      }
      idx2 += 1ULL << d->idxbits;
      mainidx++;
    }
    block++;

    uint64_t blockend = idx + size;
    if (blockend > table_size) blockend = table_size;

    uint32_t *ptr = (uint32_t *)data;
    uint64_t code = __builtin_bswap64(*(uint64_t *)ptr);
    ptr += 2;
    bitcnt = 0;
    while (idx < blockend) {
      l = m;
      while (code < base[l]) l++;
      sym = offset[l] + ((code - base[l]) >> (64 - l));
      idx = expand_symbol(dst, sym, idx, blockend, sympat, symlen);
      code <<= l;
      bitcnt += l;
      if (bitcnt >= 32) {
        bitcnt -= 32;
        code |= ((uint64_t)(__builtin_bswap32(*ptr++))) << bitcnt;
      }
    }
    data += 1 << d->blocksize;
  }
}

uint8_t *decompress_table(struct tb_handle *H, int bside, int f)
{
  if (!H->split && bside) return table;

  uint64_t tb_size = H->file[f].size[bside];
  if (tb_size != table_size) {
    if (table) free(table);
    table = malloc(tb_size);
    table_size = tb_size;
    if (!work_decomp)
      work_decomp = alloc_work(total_work);
    fill_work(total_work, tb_size, 0, work_decomp);
  }

  decomp_d = !H->has_pawns ? H->entry_piece.precomp[bside]
                            : H->entry_pawn.file[f].precomp[bside];
  decomp_data = H->data + H->file[f].idx[bside];

  run_threaded(decompress_worker, work_decomp, 1);

  return table;
}

struct tb_handle *open_tb(char *tablename, int wdl)
{
  char name[128];
  struct tb_handle *H = malloc(sizeof(struct tb_handle));

  char *dirptr = getenv(wdl ? WDLDIR : DTZDIR);
  if (use_envdirs && dirptr && strlen(dirptr) < 100)
    strcpy(name, dirptr);
  else
    strcpy(name, ".");
  strcat(name, "/");
  strcat(name, tablename);
  strcat(name, wdl ? WDLSUFFIX : DTZSUFFIX);
  if (!(H->F = fopen(name, "rb"))) {
    fprintf(stderr, "Could not open %s for reading.\n", name);
    exit(EXIT_FAILURE);
  }
  H->data = (uint8_t *)map_file(name, 1, &(H->data_size));
  H->wdl = wdl;

  return H;
}

// incomplete deallocation, but who cares
void close_tb(struct tb_handle *H)
{
  int f;

  fclose(H->F);
  unmap_file((char *)H->data, H->data_size);

  if (!H->has_pawns) {
    struct TBEntry_piece *entry = &(H->entry_piece);
    free(entry->precomp[0]->indextable);
    free(entry->precomp[0]->sizetable);
    free(entry->precomp[0]);
    if (H->split) {
      free(entry->precomp[1]->indextable);
      free(entry->precomp[1]->sizetable);
      free(entry->precomp[1]);
    }
  } else {
    struct TBEntry_pawn *entry = &(H->entry_pawn);
    for (f = 0; f < H->num_files; f++) {
      free(entry->file[f].precomp[0]->indextable);
      free(entry->file[f].precomp[0]->sizetable);
      free(entry->file[f].precomp[0]);
      if (H->split) {
        free(entry->file[f].precomp[1]->indextable);
        free(entry->file[f].precomp[1]->sizetable);
        free(entry->file[f].precomp[1]);
      }
    }
  }
}

void set_perm(struct tb_handle *H, int bside, int f, int *perm, int *pt)
{
  int i, j;
  uint8_t *pieces = !H->has_pawns ? H->entry_piece.pieces[bside]
                                : H->entry_pawn.file[f].pieces[bside];
  int n = !H->has_pawns ? H->entry_piece.num : H->entry_pawn.num;
  int k = 0;

  for (i = 0, j = 0; i < n;) {
    if (pieces[i] != k) {
      k = pieces[i];
      j = 0;
    }
    while (pt[j] != k) j++;
    perm[j++] = i++;
  }
}

struct TBEntry *get_entry(struct tb_handle *H)
{
  return &(H->entry);
}

int get_ply_accurate_win(struct tb_handle *H, int f)
{
  return (H->dtz_flags[f] & (1 << 2)) != 0;
}

int get_ply_accurate_loss(struct tb_handle *H, int f)
{
  return (H->dtz_flags[f] & (1 << 3)) != 0;
}

int get_dtz_side(struct tb_handle *H, int f)
{
  return H->dtz_flags[f] & 0x01;
}

uint8_t (*get_dtz_map(struct tb_handle *H, int f))[256]
{
  if (H->dtz_flags[f] & 0x02)
    return H->map[f];
  else
    return NULL;
}
