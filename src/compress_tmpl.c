/*
  Copyright (c) 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define NAME(f) EVALUATOR(f,T)

static struct HuffCode *NAME(setup_code)(T *data, uint64_t size);

void NAME(compress_init_dtz)(struct dtz_map *map)
{
  int i, j;

  dtz_map = map;

  num_vals = map->max_num;

  for (i = 0; i < num_vals; i++) {
    symtable[i].pattern[0] = i;
    symtable[i].len = 1;
    if (sizeof(T) == 1) {
      for (j = 0; j < 256; j++)
        symcode[i][j] = i;
    }
  }

  compress_type = 0;
  if (num_vals == 1)
    compress_type = 1;
}


void NAME(compress_alloc_dtz)(void)
{
  NAME(countfreq_dtz) = malloc(numthreads * sizeof(*NAME(countfreq_dtz)));
}

// used only for dtz
static void NAME(adjust_work_dontcares)(uint64_t *work1, uint64_t *work2)
{
  uint64_t idx;
  uint64_t end = work1[total_work];
  T *data = compress_state.data;
  int i;
  int num = num_vals;

  work2[0] = work1[0];
  for (i = 1; i < total_work; i++) {
    idx = work1[i];
    if (idx < work2[i - 1]) {
      work2[i] = work2[i - 1];
      continue;
    }
    while (idx < end && data[idx] == num)
      idx++;
    work2[i] = idx;
  }
  work2[total_work] = work1[total_work];
}

static void NAME(fill_dontcares)(struct thread_data *thread)
{
  uint64_t idx, idx2;
  uint64_t end = thread->end;
  T *restrict data = compress_state.data;
  uint64_t size = compress_state.size;
  int s1;
  int k;
  int num = num_vals;

  idx = thread->begin;
  while (idx < end) {
    // find start of don't care sequence
    for (; idx < end; idx++)
      if (data[idx] == num)
        break;
    if (idx == end) break;
    // find end of sequence and determine max pair frequency
    for (idx2 = idx + 1; idx2 < end; idx2++)
      if (data[idx2] != num)
        break;
    int64_t max;
    if (idx2 - idx > 1)
      max = dcfreq[0][0];
    else {
      max = -1;
      if (idx > 0)
        max = pairfreq[data[idx -1]][pairfirst[0][data[idx - 1]]];
      if (idx2 < size) {
        int tmp = pairfreq[pairsecond[0][data[idx2]]][data[idx2]];
        if (tmp > max)
          max = tmp;
      }
    }
    // now replace whenever we reach max pair frequency for the sequence
    int dc = 1;
    if (idx > 0) {
      s1 = data[idx - 1];
      k = pairfirst[0][s1];
      if (pairfreq[s1][k] == max) {
        data[idx] = k;
        s1 = k;
        dc = 0;
      }
    }
    idx2 = idx + 1;
    while (idx2 < end && data[idx2] == num) {
      if (dc) {
        data[idx2 - 1] = t1[0][0];
        s1 = t2[0][0];
        data[idx2] = s1;
        dc = 0;
      } else {
        k = pairfirst[0][s1];
        dc = 1;
        if (pairfreq[s1][k] == max) {
          data[idx2] = k;
          s1 = k;
          dc = 0;
        }
      }
      idx2++;
    }
    if (dc && idx2 < size) {
      s1 = data[idx2];
      k = pairsecond[0][s1];
      if (pairfreq[k][s1] == max)
        data[idx2 - 1] = k;
    }
  }
}

static void NAME(count_pairs_dtz)(struct thread_data *thread)
{
  int s1, s2;
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  T *data = compress_state.data;
  int t = thread->thread;

  if (idx == 0) idx = 1;
  s1 = data[idx - 1];
  for (; idx < end; idx++) {
    s2 = data[idx];
    NAME(countfreq_dtz)[t][s1][s2]++;
    s1 = s2;
  }
}

static void NAME(adjust_work_replace)(uint64_t *work)
{
  uint64_t idx, idx2;
  uint64_t end = compress_state.size;
  T *data = compress_state.data;
  int i, s1, s2, j;

  for (i = 1; i < total_work; i++) {
    idx = work[i];
    if (idx <= work[i - 1]) {
      work[i] = work[i - 1];
      continue;
    }
    s1 = read_symbol(data[idx]);
    idx += symtable[s1].len;
    j = 0;
    while (idx < end) {
      s2 = read_symbol(data[idx]);
      if (newtest[s1][s2]) j = 0;
      else {
        if (j == 1) break;
        j = 1;
        idx2 = idx;
      }
      idx += symtable[s2].len;
      s1 = s2;
    }
    if (idx < end)
      work[i] = idx2;
    else
      work[i] = end;
  }
}

static void NAME(replace_pairs)(struct thread_data *thread)
{
  uint64_t idx = thread->begin;
  uint64_t end = thread->end;
  T *restrict data = compress_state.data;
  int s1, s2, a;
  int t = thread->thread;

  a = -1;
  s1 = read_symbol(data[idx]);
  idx += symtable[s1].len;
  while (idx < end) {
    s2 = read_symbol(data[idx]);
    idx += symtable[s2].len;
    if (newtest[s1][s2]) {
      struct Symbol *sym = &symtable[newpairs[newtest[s1][s2] - 1].sym];
      write_symbol(data[idx - sym->len], sym);
      if (likely(a >= 0)) countfirst[t][newtest[s1][s2] - 1][a]++;
      a = newtest[s1][s2] - 1;
      if (unlikely(idx == compress_state.size)) break;
      s1 = read_symbol(data[idx]);
      idx += symtable[s1].len;
      countsecond[t][a][s1]++;
      a = newpairs[a].sym;
    } else {
      a = s1;
      s1 = s2;
    }
  }
}

static void NAME(remove_dtz_worker)(struct thread_data *thread)
{
  uint64_t idx, idx2;
  uint64_t end = thread->end;
  T *restrict data = compress_state.data;
  uint64_t size = compress_state.size;
  int s;
  int num = num_vals;
  int max = dtz_map->high_freq_max;

  idx = thread->begin;
  while (idx < end) {
    for (; idx < end; idx++)
      if (data[idx] == num) break;
    if (idx == end) break;
    for (idx2 = idx + 1; idx2 < end; idx2++)
      if (data[idx2] != num) break;
    if (idx2 - idx >= 32) {
      idx = idx2;
      continue;
    }
    if (idx == 0)
      s = data[idx2];
    else {
      s = data[idx - 1];
      if (idx2 < size && data[idx2] < s)
        s = data[idx2];
    }
    if (s >= max)
      idx = idx2;
    else
      for (; idx < idx2; idx++)
        data[idx] = s;
  }
}

static void NAME(adjust_work_dontcares_dtz)(uint64_t *restrict work1,
    uint64_t *restrict work2)
{
  uint64_t idx;
  uint64_t end = work1[total_work];
  T *data = compress_state.data;
  int i;
  int num = num_vals;

  work2[0] = work1[0];
  for (i = 1; i < total_work; i++) {
    idx = work1[i];
    if (idx < work2[i - 1]) {
      work2[i] = work2[i - 1];
      continue;
    }
    while (idx < end && (data[idx - 1] == num || data[idx] == num
                                        || data[idx - 1] == data[idx]))
//    while (idx < end && data[idx] == num)
      idx++;
    work2[i] = idx;
  }
  work2[total_work] = work1[total_work];
}

static struct HuffCode *NAME(construct_pairs_dtz)(T *data, uint64_t size,
    int minfreq, int maxsymbols)
{
  int i, j, k, l;
  int num, t;

  if (!work)
    work = alloc_work(total_work);
  if (!work_adj)
    work_adj = alloc_work(total_work);

  num_syms = num_vals;

  num_ctrl = num_vals - 1;
  cur_ctrl_idx = 0;
  compress_state.data = data;
  compress_state.size = size;

  fill_work(total_work, size, 0, work);

  if (maxsymbols > 0) {
    maxsymbols += num_syms;
    if (maxsymbols > 4095)
      maxsymbols = 4095;
  } else {
    maxsymbols = 4095;
  }

  NAME(adjust_work_dontcares_dtz)(work, work_adj);
  run_threaded(NAME(remove_dtz_worker), work_adj, 0);

  // first round of freq counting, to fill in dont cares
  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      pairfreq[i][j] = 0;

  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        NAME(countfreq_dtz)[t][i][j] = 0;
  run_threaded(NAME(count_pairs_dtz), work, 0);
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        pairfreq[i][j] += NAME(countfreq_dtz)[t][i][j];

  for (j = 0; j < num_syms; j++)
    pairfirst[0][j] = pairsecond[0][j] = 0;
  for (i = 1; i < num_syms; i++)
    for (j = 0; j < num_syms; j++) {
      if (pairfreq[j][i] > pairfreq[j][pairfirst[0][j]])
        pairfirst[0][j] = i;
      if (pairfreq[i][j] > pairfreq[pairsecond[0][j]][j])
        pairsecond[0][j] = i;
    }

  dcfreq[0][0] = -1;
  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      if (pairfreq[i][j] > dcfreq[0][0]) {
        dcfreq[0][0] = pairfreq[i][j];
        t1[0][0] = i;
        t2[0][0] = j;
      }

  for (i = 0; i < MAXSYMB; i++)
    for (j = 0; j < MAXSYMB; j++)
      newtest[i][j] = 0;

  NAME(adjust_work_dontcares)(work, work_adj);
  run_threaded(NAME(fill_dontcares), work_adj, 0);

  for (i = 0; i < num_syms; i++)
    for (j = 0; j < num_syms; j++)
      pairfreq[i][j] = 0;

  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        NAME(countfreq_dtz)[t][i][j] = 0;
  run_threaded(NAME(count_pairs_dtz), work, 0);
  for (t = 0; t < numthreads; t++)
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        pairfreq[i][j] += NAME(countfreq_dtz)[t][i][j];

  if (sizeof(T) == 1) {
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < 256; j++)
        symcode[i][j] = i;
  }

  while (num_syms < maxsymbols) {

    num = 0;
    for (i = 0; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        if (pairfreq[i][j] >= minfreq && (num < MAX_NEW - 1 || pairfreq[i][j] > newpairs[num-1].freq) && (symtable[i].len + symtable[j].len <= 256)) {
          for (k = 0; k < num; k++)
            if (newpairs[k].freq < pairfreq[i][j]) break;
          if (num < MAX_NEW - 1) num++;
          for (l = num - 1; l > k; l--)
            newpairs[l] = newpairs[l-1];
          newpairs[k].freq = pairfreq[i][j];
          newpairs[k].s1 = i;
          newpairs[k].s2 = j;
        }

    for (i = 0; i < num_syms; i++)
      pairfirst[0][i] = pairsecond[0][i] = 0;

    // keep track of number of skipped pairs to make sure they'll be
    // considered in the next iteration (before running out of symbols)
    int skipped = 0; // just a rough estimate
    int64_t max = newpairs[0].freq;
    for (i = 0, j = 0; i < num && num_syms + j + skipped <= maxsymbols; i++) {
      while (max > newpairs[i].freq * 2) {
        skipped += i;
        max /= 2;
      }
      if (!pairsecond[0][newpairs[i].s1] && !pairfirst[0][newpairs[i].s2])
        newpairs[j++] = newpairs[i];
      else
        skipped++;
      pairfirst[0][newpairs[i].s1] = 1;
      pairsecond[0][newpairs[i].s2] = 1;
    }
    num = j;

    if (num_syms + num > maxsymbols)
      num = maxsymbols - num_syms;

    for (i = 0; i < num; i++) {
      newpairs[i].sym = num_syms;
      symtable[num_syms].len = symtable[newpairs[i].s1].len + symtable[newpairs[i].s2].len;
      symtable[num_syms].pattern[0] = newpairs[i].s1;
      symtable[num_syms].pattern[1] = newpairs[i].s2;
      if (sizeof(T) == 1) {
        if (!cur_ctrl_idx)
          num_ctrl++;
        if (num_ctrl == 256) break;
        symtable[num_syms].first = num_ctrl;
        symtable[num_syms].second = cur_ctrl_idx;
        symcode[num_ctrl][cur_ctrl_idx] = num_syms;
        cur_ctrl_idx++;
        if (cur_ctrl_idx == 256) cur_ctrl_idx = 0;
      } else {
        symtable[num_syms].sym = num_syms;
      }
      num_syms++;
    }

    if (i != num) {
      fprintf(stderr, "Ran short of symbols.\n");
      exit(EXIT_FAILURE);
    }

    if (num == 0) break;

    for (i = 0; i < num_syms - num; i++)
      for (j = num_syms - num; j < num_syms; j++)
        pairfreq[i][j] = 0;
    for (; i < num_syms; i++)
      for (j = 0; j < num_syms; j++)
        pairfreq[i][j] = 0;

    for (i = 0; i < num; i++)
      newtest[newpairs[i].s1][newpairs[i].s2] = i + 1;

// thread this later
    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num; i++)
        for (j = 0; j < num_syms; j++) {
          countfirst[t][i][j] = 0;
          countsecond[t][i][j] = 0;
        }

    NAME(adjust_work_replace)(work);
    run_threaded(NAME(replace_pairs), work, 0);

    for (t = 0; t < numthreads; t++)
      for (i = 0; i < num; i++)
        for (j = 0; j < num_syms; j++) {
          pairfreq[j][newpairs[i].s1] -= countfirst[t][i][j];
          pairfreq[j][newpairs[i].sym] += countfirst[t][i][j];
          pairfreq[newpairs[i].s2][j] -= countsecond[t][i][j];
          pairfreq[newpairs[i].sym][j] += countsecond[t][i][j];
        }

    for (i = 0; i < num; i++)
      pairfreq[newpairs[i].s1][newpairs[i].s2] = 0;

    for (i = 0; i < num; i++)
      newtest[newpairs[i].s1][newpairs[i].s2] = 0;
  }

  struct HuffCode *c = NAME(setup_code)(data, size);
  create_code(c, num_syms);

  return c;
}

static struct HuffCode *NAME(setup_code)(T *data, uint64_t size)
{
  uint64_t idx;
  int s;

  struct HuffCode *c = malloc(sizeof(*c));

  for (int i = 0; i < MAXSYMB; i++)
    c->freq[i] = 0;

  for (idx = 0; idx < size; idx += symtable[s].len) {
    s = read_symbol(data[idx]);
    c->freq[s]++;
  }

  return c;
}

static void NAME(calc_block_sizes)(T *data, uint64_t size, struct HuffCode *c,
    int maxsize)
{
  uint64_t idx;
  int i, s, t;
  int64_t block;
  int maxbits, bits, numpos;
  uint64_t avg;

  uint64_t rawsize = calc_size(c);
  printf("calc_size: %"PRIu64"\n", rawsize);

  uint64_t optsize, compsize;

  block = 0;
  compsize = INT64_MAX;
  blocksize = maxsize + 1;
  do {
    optsize = compsize;
    num_blocks = block;
    blocksize--;
    if (((rawsize * ((1 << blocksize) + 2)) >> blocksize) >= optsize) break;
    maxbits = 8 << blocksize;
    bits = 0;
    numpos = 0;
    block = 0;
    for (idx = 0; idx < size;) {
      s = read_symbol(data[idx]);
      t = symtable[s].len;
      if (bits + c->length[s] > maxbits || numpos + t > 65536) {
        block++;
        bits = 0;
        numpos = 0;
      }
      bits += c->length[s];
      numpos += t;
      idx += t;
    }
    if (numpos > 0)
      block++;
    compsize = block << blocksize;
    compsize = (compsize + 0x3f) & ~0x3f;
    avg = size / block;
    idxbits = 0;
    while (avg) {
      idxbits++;
      avg >>= 1;
    }
    idxbits += 4;
    while (idxbits > 1 && (1ULL << (idxbits - 1)) > size) idxbits--;

    num_indices = (size + (1ULL << idxbits) - 1) >> idxbits;
    t = ((2 * num_indices - 1) << (idxbits - 1)) - size;
    if (t > 0) block += (t + 65535) >> 16;
    else t = 0;
    compsize += 2 * block + 6 * num_indices;

    printf("bits = %d; blocks = %"PRIu64" (%d); size = %"PRIu64"\n", blocksize, block-((t+65535)>>16), (t+65535)>>16, compsize);

  } while (compsize < optsize);

  blocksize++;
  maxbits = 8ULL << blocksize;
  sizetable = malloc((num_blocks + 16) * sizeof(uint16_t));

  calc_symbol_tweaks(c);

  bits = 0;
  numpos = 0;
  block = 0;
  for (idx = 0; idx < size;) {
    s = read_symbol(data[idx]);
    t = symtable[s].len;
    if (bits + c->length[s] > maxbits || numpos + t > 65536) {
      if (numpos + t <= 65536) {
        if (bits + c->length[replace[s]] <= maxbits) {
          idx += t;
          sizetable[block++] = numpos + t - 1;
          bits = 0;
          numpos = 0;
          continue;
        }
        if (t > 1) {
          int s1 = symtable[s].pattern[0];
          int s2 = symtable[s].pattern[1];
          if (c->length[s1] != 0 && c->length[s2] != 0) {
            if (bits + c->length[s1] + c->length[replace[s2]] <= maxbits) {
              idx += t;
              sizetable[block++] = numpos + t - 1;
              bits = 0;
              numpos = 0;
              continue;
            }
            if (c->length[s2] < c->length[s] && bits + c->length[replace[s1]] <= maxbits) {
              sizetable[block++] = numpos + symtable[s1].len - 1;
              idx += t;
              bits = c->length[s2];
              numpos = symtable[s2].len;
              continue;
            }
          }
        }
      }
      if (numpos + t <= 65536 && bits + c->length[replace[s]] <= maxbits) {
        idx += t;
        sizetable[block++] = numpos + t - 1;
        bits = 0;
        numpos = 0;
        continue;
      }

      sizetable[block++] = numpos - 1;
      bits = 0;
      numpos = 0;
    }
    bits += c->length[s];
    numpos += t;
    idx += t;
  }
  if (numpos > 0)
    sizetable[block++] = numpos - 1;

  real_num_blocks = block;
  num_blocks = block;

  avg = (size / num_blocks);
  idxbits = 0;
  while (avg) {
    idxbits++;
    avg >>= 1;
  }
  idxbits += 4;
  while (idxbits > 1 && (1ULL << (idxbits - 1)) > size) idxbits--;

  num_indices = (size + (1ULL << idxbits) - 1) >> idxbits;
  indextable = malloc(num_indices * 6);

  uint64_t idx2 = 1ULL << (idxbits-1);
  block = 0;
  idx = 0;
  for (i = 0; i < num_indices; i++) {
    while (block < num_blocks && idx + sizetable[block] < idx2)
      idx += sizetable[block++] + 1;
    if (block == num_blocks) {
      sizetable[num_blocks++] = 65535;
      while (idx + sizetable[block] < idx2) {
        idx += sizetable[block++] + 1;
        sizetable[num_blocks++] = 65535;
      }
    }
    *(uint32_t *)(indextable + 6 * i) = block;
    *(uint16_t *)(indextable + 6 * i + 4) = idx2 - idx;
    idx2 += 1ULL << idxbits;
  }

  printf("real_num_blocks = %"PRIu64"; num_blocks = %"PRIu64"\n", real_num_blocks, num_blocks);
  printf("idxbits = %d\n", idxbits);
  printf("num_indices = %d\n", num_indices);
}

void NAME(write_ctb_data)(FILE *F, T *data, struct HuffCode *c, uint64_t size,
    int blocksize)
{
  uint64_t idx;
  int s, t, l;
  int bits, numpos;
  int maxbits;

  maxbits = 8 << blocksize;
  bits = numpos = 0;
  write_bits(F, 0, 0);
  for (idx = 0; idx < size;) {
    s = read_symbol(data[idx]);
    t = symtable[s].len;
    if (bits + c->length[s] > maxbits || numpos + t > 65536) {

      if (numpos + t <= 65536) {
        if (bits + c->length[replace[s]] <= maxbits) {
          s = replace[s];
          l = c->length[s];
          write_bits(F, c->base[l] + (c->inv[s] - c->offset[l]), l);
          idx += t;
          write_bits(F, 0, -(1 << blocksize));
          bits = 0;
          numpos = 0;
          continue;
        }
        if (t > 1) {
          int s1 = symtable[s].pattern[0];
          int s2 = symtable[s].pattern[1];
          if (c->length[s1] != 0 && c->length[s2] != 0) {
            if (bits + c->length[s1] + c->length[replace[s2]] <= maxbits) {
              l = c->length[s1];
              write_bits(F, c->base[l] + (c->inv[s1] - c->offset[l]), l);
              l = c->length[replace[s2]];
              write_bits(F, c->base[l] + (c->inv[replace[s2]] - c->offset[l]), l);
              idx += t;
              write_bits(F, 0, -(1 << blocksize));
              bits = 0;
              numpos = 0;
              continue;
            }
            if (c->length[s2] < c->length[s] && bits + c->length[replace[s1]] <= maxbits) {
              if (bits + c->length[s1] > maxbits) s1 = replace[s1];
              l = c->length[s1];
              write_bits(F, c->base[l] + (c->inv[s1] - c->offset[l]), l);
              write_bits(F, 0, -(1 << blocksize));
              l = c->length[s2];
              write_bits(F, c->base[l] + (c->inv[s2] - c->offset[l]), l);
              idx += t;
              bits = l;
              numpos = symtable[s2].len;
              continue;
            }
          }
        }
      }

      write_bits(F, 0, -(1 << blocksize));
      bits = 0;
      numpos = 0;
    }
    l = c->length[s];
    write_bits(F, c->base[l] + (c->inv[s] - c->offset[l]), l);
    bits += l;
    numpos += t;
    idx += t;
  }
  if (numpos > 0)
    write_bits(F, 0, -(1 << blocksize));
}

static void NAME(compress_data)(struct tb_handle *F, int num, FILE *G, T *data,
    uint64_t size, int minfreq)
{
  struct HuffCode *c;
  int i;

  if (F->wdl)
    c = construct_pairs_wdl((uint8_t *)data, size, minfreq, 0);
  else
    c = NAME(construct_pairs_dtz)(data, size, minfreq, 0);

  NAME(calc_block_sizes)(data, size, c, F->default_blocksize);

  F->c[num] = c;
  F->blocksize[num] = blocksize;
  F->idxbits[num] = idxbits;
  F->real_num_blocks[num] = real_num_blocks;
  F->num_blocks[num] = num_blocks;
  F->num_indices[num] = num_indices;
  F->num_syms[num] = num_syms;
  F->symtable[num] = malloc(sizeof(symtable));
  memcpy(F->symtable[num], symtable, sizeof(symtable));

  struct Symbol *stable = F->symtable[num];
  if (F->wdl) {
    F->flags[num] = wdl_flags;
  } else {
    int j, k;
    // if we are here, num_vals >= 2
    F->map[num] = dtz_map;
    F->flags[num] = dtz_map->side
                          | (dtz_map->ply_accurate_win << 2)
                          | (dtz_map->ply_accurate_loss << 3)
                          | (dtz_map->wide << 4);
    for (i = 0; i < 4; i++)
      if (dtz_map->num[i] == num_vals)
        break;
    for (j = 0; j < 4; j++)
      if (j != i) {
        for (k = 0; k < dtz_map->num[j]; k++)
          if (dtz_map->map[j][k] != dtz_map->map[i][k])
            break;
        if (k < dtz_map->num[j])
          break;
      }
    if (j == 4) {
      for (k = 0; k < num_vals; k++)
        stable[k].pattern[0] = dtz_map->map[i][k];
    } else {
      F->flags[num] |= 2;
    }
  }

  for (i = 0; i < num_indices; i++) {
    write_u32(G, *(uint32_t *)(indextable + 6 * i));
    write_u16(G, *(uint16_t *)(indextable + 6 * i + 4));
  }

  for (i = 0; i < num_blocks; i++)
    write_u16(G, sizetable[i]);

  free(indextable);
  free(sizetable);

  NAME(write_ctb_data)(G, data, c, size, F->blocksize[num]);
}

void NAME(compress_tb)(struct tb_handle *F, T *data, uint64_t tb_size,
    uint8_t *perm, int minfreq)
{
  int i;
  int num;
  char name[64];
  char ext[8];
  FILE *G;

  num = F->num_tables++;

  for (i = 0; i < numpcs; i++)
    F->perm[num][i] = perm[i];

  if (compress_type == 0) {
    sprintf(ext, ".%c", '1' + num);
    strcpy(name, F->name);
    strcat(name, F->wdl ? WDLSUFFIX : DTZSUFFIX);
    strcat(name, ext);
    if (!(G = fopen(name, "wb"))) {
      fprintf(stderr, "Could not open %s for writing.\n", name);
      exit(1);
    }

    NAME(compress_data)(F, num, G, data, tb_size, minfreq);

    fclose(G);
  } else if (compress_type == 1) {
    compress_data_single_valued(F, num);
  }
}

#undef NAME
