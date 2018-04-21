/*
  Copyright (c) 2018 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

#define NAME(f) EVALUATOR(f,T)

static T *NAME(permute_v);

static struct {
  T *src;
  T *dst;
  int *pcs;
  int p;
  int file;
} NAME(convert_data);

void NAME(convert_data_piece)(struct thread_data *thread)
{
  uint64_t idx1, idx2, idx3;
  int i;
  int sq;
#ifdef SMALL
  int sq2;
#endif
  int n = entry_piece.num;
  assume(n >= 3 && n <= TBPIECES);
  int pos[TBPIECES];
  int order[TBPIECES];
  uint64_t factor[TBPIECES];
  uint8_t norm[TBPIECES];
  T *restrict src = NAME(convert_data).src;
  T *restrict dst = NAME(convert_data).dst;
  uint8_t *restrict pidx = pidx_list[NAME(convert_data).p];
  uint64_t end = thread->end;
  T *restrict v = NAME(permute_v);

  p_set_norm_piece(NAME(convert_data).pcs, type_perm_list[NAME(convert_data).p], norm, order_list[NAME(convert_data).p]);
  calc_order_piece(n, order_list[NAME(convert_data).p], order, norm);
  calc_factors_piece(factor, n, order_list[NAME(convert_data).p], norm, entry_piece.enc_type);

  idx1 = thread->begin;

  decode_piece(&entry_piece, norm, pos, factor, order, idx1);
#ifndef SMALL
  idx3 = pos[pidx[1]];
  for (i = 2; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  sq = pos[pidx[0]];
  idx3 ^= sq_mask[sq];
  if (mirror[sq] < 0)
    idx3 = MIRROR_A1H8(idx3);
  idx3 |= tri0x40[sq];
#else
  idx3 = pos[pidx[2]];
  for (i = 3; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  sq = pos[pidx[0]];
  idx3 ^= sq_mask[sq];
  sq2 = pos[pidx[1]];
  if (unlikely(KK_map[sq][sq2] < 0))
    idx3 = diagonal;
  else {
    if (mirror[sq][sq2] < 0)
      idx3 = MIRROR_A1H8(idx3);
    idx3 |= (uint64_t)KK_map[sq][sq2] << shift[1];
  }
#endif
  __builtin_prefetch(&src[idx3], 0, 3);

  for (idx1++; idx1 < end; idx1++) {
    decode_piece(&entry_piece, norm, pos, factor, order, idx1);
#ifndef SMALL
    idx2 = pos[pidx[1]];
    for (i = 2; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    sq = pos[pidx[0]];
    idx2 ^= sq_mask[sq];
    if (mirror[sq] < 0)
      idx2 = MIRROR_A1H8(idx2);
    idx2 |= tri0x40[sq];
#else
    idx2 = pos[pidx[2]];
    for (i = 3; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    sq = pos[pidx[0]];
    idx2 ^= sq_mask[sq];
    sq2 = pos[pidx[1]];
    if (unlikely(KK_map[sq][sq2] < 0))
      idx2 = diagonal;
    else {
      if (mirror[sq][sq2] < 0)
        idx2 = MIRROR_A1H8(idx2);
      idx2 |= (uint64_t)KK_map[sq][sq2] << shift[1];
    }
#endif
    __builtin_prefetch(&src[idx2], 0, 3);
    dst[idx1 - 1] = v[src[idx3]];
    idx3 = idx2;
  }

  dst[idx1 - 1] = v[src[idx3]];
}

void NAME(convert_data_pawn)(struct thread_data *thread)
{
  uint64_t idx1, idx2, idx3;
  int i;
  int n = entry_pawn.num;
  assume(n >= 3 && n <= TBPIECES);
  int pos[TBPIECES];
  int order[TBPIECES];
  uint64_t factor[TBPIECES];
  uint8_t norm[TBPIECES];
  T *restrict src = NAME(convert_data).src;
  T *restrict dst = NAME(convert_data).dst;
  uint8_t *restrict pidx = pidx_list[NAME(convert_data).p];
  int file = NAME(convert_data).file;
  uint64_t end = thread->end;
  T *restrict v = NAME(permute_v);

  p_set_norm_pawn(NAME(convert_data).pcs, type_perm_list[NAME(convert_data).p], norm, order_list[NAME(convert_data).p], order2_list[NAME(convert_data).p]);
  calc_order_pawn(n, order_list[NAME(convert_data).p], order2_list[NAME(convert_data).p], order, norm);
  calc_factors_pawn(factor, n, order_list[NAME(convert_data).p], order2_list[NAME(convert_data).p], norm, file);

  idx1 = thread->begin;
  decode_pawn(&entry_pawn, norm, pos, factor, order, idx1, file);
  idx3 = pos[pidx[1]];
  for (i = 2; i < n; i++)
    idx3 = (idx3 << 6) | pos[pidx[i]];
  idx3 ^= sq_mask_pawn[pos[pidx[0]]];
  __builtin_prefetch(&src[idx3], 0, 3);
 
  for (idx1++; idx1 < end; idx1++) {
    decode_pawn(&entry_pawn, norm, pos, factor, order, idx1, file);
    idx2 = pos[pidx[1]];
    for (i = 2; i < n; i++)
      idx2 = (idx2 << 6) | pos[pidx[i]];
    idx2 ^= sq_mask_pawn[pos[pidx[0]]];
    __builtin_prefetch(&src[idx2], 0, 3);
    dst[idx1 - 1] = v[src[idx3]];
    idx3 = idx2;
  }

  dst[idx1 - 1] = v[src[idx3]];
}

static struct {
  T *table;
  int *pcs;
  T *dst;
  int num_cands;
  uint32_t dsize;
  int file;
} NAME(est_data);

void NAME(convert_est_data_piece)(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  T *restrict table = NAME(est_data).table;
  int num_cands = NAME(est_data).num_cands;
  int *restrict pcs = NAME(est_data).pcs;
  uint32_t dsize = NAME(est_data).dsize;
  T *restrict dst = NAME(est_data).dst;
  T *restrict v = NAME(permute_v);
  uint64_t idx;
  int n = entry_piece.num;
  assume(n >= 3 && n <= TBPIECES);
  int sq;
#ifdef SMALL
  int sq2;
#endif
  int pos[TBPIECES];
  uint64_t factor[TBPIECES];
  int order[TBPIECES];
  uint8_t norm[TBPIECES];

  uint64_t idx_cache[MAX_CANDS];

  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
        for (m = 0; m < num_types; m++)
          if (pcs[type_perm_list[trylist[p]][m]] != pcs[type_perm_list[trylist[q]][m]]) break;
        if (m < num_types) break;
      }
      int l = trylist[p];
      p_set_norm_piece(pcs, type_perm_list[l], norm, order_list[l]);
      calc_order_piece(n, order_list[l], order, norm);
      calc_factors_piece(factor, n, order_list[l], norm, entry_piece.enc_type);
      // prefetch for j = 0
      decode_piece(&entry_piece, norm, pos, factor, order, segs[i]);
      for (r = p; r < q; r++) {
        l = trylist[r];
#ifndef SMALL
        idx = pos[pidx_list[l][1]];
        for (m = 2; m < n; m++)
          idx = (idx << 6) | pos[pidx_list[l][m]];
        sq = pos[pidx_list[l][0]];
        idx ^= sq_mask[sq];
        if (mirror[sq] < 0)
          idx = MIRROR_A1H8(idx);
        idx |= tri0x40[sq];
#else
        idx = pos[pidx_list[l][2]];
        for (m = 3; m < n; m++)
          idx = (idx << 6) | pos[pidx_list[l][m]];
        sq = pos[pidx_list[l][0]];
        idx ^= sq_mask[sq];
        sq2 = pos[pidx_list[l][1]];
        if (unlikely(KK_map[sq][sq2] < 0))
          idx = diagonal;
        else {
          if (mirror[sq][sq2] < 0)
            idx = MIRROR_A1H8(idx);
          idx |= (uint64_t)KK_map[sq][sq2] << shift[1];
        }
#endif
        __builtin_prefetch(&table[idx], 0, 3);
        idx_cache[r] = idx;
      }
      for (j = 1; j < seg_size; j++) {
        // prefetch for j, copy for j - 1
        decode_piece(&entry_piece, norm, pos, factor, order, segs[i] + j);
        for (r = p; r < q; r++) {
          l = trylist[r];
#ifndef SMALL
          idx = pos[pidx_list[l][1]];
          for (m = 2; m < n; m++)
            idx = (idx << 6) | pos[pidx_list[l][m]];
          sq = pos[pidx_list[l][0]];
          idx ^= sq_mask[sq];
          if (mirror[sq] < 0)
            idx = MIRROR_A1H8(idx);
          idx |= tri0x40[sq];
#else
          idx = pos[pidx_list[l][2]];
          for (m = 3; m < n; m++)
            idx = (idx << 6) | pos[pidx_list[l][m]];
          sq = pos[pidx_list[l][0]];
          idx ^= sq_mask[sq];
          sq2 = pos[pidx_list[l][1]];
          if (unlikely(KK_map[sq][sq2] < 0))
            idx = diagonal;
          else {
            if (mirror[sq][sq2] < 0)
              idx = MIRROR_A1H8(idx);
            idx |= (uint64_t)KK_map[sq][sq2] << shift[1];
          }
#endif
          __builtin_prefetch(&table[idx], 0, 3);
          dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
          idx_cache[r] = idx;
        }
      }
      for (r = p; r < q; r++)
        dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
      p = q;
    }
  }
}

void NAME(convert_est_data_pawn)(struct thread_data *thread)
{
  int i, j, k, m, p, q, r;
  T *restrict table = NAME(est_data).table;
  int num_cands = NAME(est_data).num_cands;
  int *restrict pcs = NAME(est_data).pcs;
  uint32_t dsize = NAME(est_data).dsize;
  T *restrict dst = NAME(est_data).dst;
  T *restrict v = NAME(permute_v);
  int file = NAME(est_data).file;
  uint64_t idx;
  int n = entry_pawn.num;
  assume(n >= 3 && n <= TBPIECES);
  int pos[TBPIECES];
  uint64_t factor[TBPIECES];
  int order[TBPIECES];
  uint8_t norm[TBPIECES];

  uint64_t idx_cache[MAX_CANDS];

  for (i = thread->begin, k = i * seg_size; i < thread->end; i++, k += seg_size) {
    for (p = 0; p < num_cands;) {
      for (q = p + 1; q < num_cands; q++) {
        for (m = 0; m < num_types; m++)
          if (cmp[type_perm_list[trylist[p]][m]] != cmp[type_perm_list[trylist[q]][m]]) break;
        if (m < num_types) break;
      }
      int l = trylist[p];
      p_set_norm_pawn(pcs, type_perm_list[l], norm, order_list[l], order2_list[l]);
      calc_order_pawn(n, order_list[l], order2_list[l], order, norm);
      calc_factors_pawn(factor, n, order_list[l], order2_list[l], norm, file);
      // prefetch for j = 0
      decode_pawn(&entry_pawn, norm, pos, factor, order, segs[i], file);
      for (r = p; r < q; r++) {
        l = trylist[r];
        idx = pos[pidx_list[l][1]];
        for (m = 2; m < n; m++)
          idx = (idx << 6) | pos[pidx_list[l][m]];
        idx ^= sq_mask_pawn[pos[pidx_list[l][0]]];
        __builtin_prefetch(&table[idx], 0, 3);
        idx_cache[r] = idx;
      }
      for (j = 1; j < seg_size; j++) {
        decode_pawn(&entry_pawn, norm, pos, factor, order, segs[i] + j, file);
        for (r = p; r < q; r++) {
          l = trylist[r];
          idx = pos[pidx_list[l][1]];
          for (m = 2; m < n; m++)
            idx = (idx << 6) | pos[pidx_list[l][m]];
          idx ^= sq_mask_pawn[pos[pidx_list[l][0]]];
          __builtin_prefetch(&table[idx], 0, 3);
          dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
          idx_cache[r] = idx;
        }
      }
      for (r = p; r < q; r++)
        dst[r * dsize + k + j - 1] = v[table[idx_cache[r]]];
      p = q;
    }
  }
}

void NAME(estimate_compression_piece)(T *restrict table,
    int *restrict pcs, int wdl, int num_cands)
{
  int i, p;

  uint32_t dsize = num_segs * seg_size;
  T *restrict dst = malloc((num_cands * dsize + 1) * sizeof(T));
  NAME(est_data).table = table;
  NAME(est_data).pcs = pcs;
  NAME(est_data).dst = dst;
  NAME(est_data).num_cands = num_cands;
  NAME(est_data).dsize = dsize;
  T *dst0 = dst;

  if (num_segs > 1)
    run_threaded(NAME(convert_est_data_piece), work_est, 0);
  else
    run_single(NAME(convert_est_data_piece), work_est, 0);

  uint64_t csize;

  for (p = 0; p < num_cands; p++, dst += dsize) {
    struct HuffCode *c = NAME(construct_pairs)(dst, dsize, 20, 100, wdl);
    csize = calc_size(c);
    free(c);
    printf("[%2d] order: %d", p, order_list[trylist[p]]);
    printf("; perm:");
    for (i = 0; i < num_types; i++)
      printf(" %2d", type_perm_list[trylist[p]][i]);
    printf("; %"PRIu64"\n", csize);
    compest[trylist[p]] = csize;
  }

  free(dst0);
}

void NAME(estimate_compression_pawn)(T *restrict table, int *restrict pcs,
    int file, int wdl, int num_cands)
{
  int i, p;

  uint32_t dsize = num_segs * seg_size;
  T *restrict dst = malloc((num_cands * dsize + 1) * sizeof(T));
  NAME(est_data).table = table;
  NAME(est_data).pcs = pcs;
  NAME(est_data).dst = dst;
  NAME(est_data).num_cands = num_cands;
  NAME(est_data).dsize = dsize;
  NAME(est_data).file = file;
  T *dst0 = dst;

  if (num_segs > 1)
    run_threaded(NAME(convert_est_data_pawn), work_est, 0);
  else
    run_single(NAME(convert_est_data_pawn), work_est, 0);

  uint64_t csize;

  for (p = 0; p < num_cands; p++, dst += dsize) {
    struct HuffCode *c = NAME(construct_pairs)(dst, dsize, 20, 100, wdl);
    csize = calc_size(c);
    free(c);
    printf("[%2d] order: %d", p, order_list[trylist[p]]);
    printf("; perm:");
    for (i = 0; i < num_types; i++)
      printf(" %2d", type_perm_list[trylist[p]][i]);
    printf("; %"PRIu64"\n", csize);
    compest[trylist[p]] = csize;
  }

  free(dst0);
}

uint64_t NAME(estimate_compression)(T *restrict table, int *restrict bestp,
    int *restrict pcs, int wdl, int file)
{
  int i, j, k, p, q;
  int num_cands, bp = 0;
  uint64_t best;
  uint8_t bestperm[TBPIECES];

  if (compress_type == 1) {
    *bestp = 0;
    return 0;
  }

  if (!work_est)
    work_est = alloc_work(total_work);
  fill_work(total_work, num_segs, 0, work_est);

  for (i = 0; i < num_type_perms; i++)
    compest[i] = 0;

  assume(num_types >= 2);
  for (k = 0; k < num_types - 1; k++) {
    best = UINT64_MAX;
    num_cands = 0;
    for (p = 0; p < num_types; p++) {
      for (i = 0; i < k; i++)
        if (type[p] == bestperm[i]) break;
      if (i < k) continue;
      for (q = 0; q < num_types; q++) {
        if (q == p) continue;
        for (i = 0; i < k; i++)
          if (type[q] == bestperm[i]) break;
        if (i < k) continue;
        // look for permutation starting with bestperm[0..k-1],p,q
        for (i = 0; i < num_type_perms; i++) {
          for (j = 0; j < k; j++)
            if (type_perm_list[i][j] != bestperm[j]) break;
          if (j < k) continue;
          if (type_perm_list[i][k] == type[p] && type_perm_list[i][k+1] == type[q]) break;
        }
        if (i < num_type_perms) {
          if (compest[i]) {
            if (compest[i] < best) {
              best = compest[i];
              bp = i;
            }
          } else
            trylist[num_cands++] = i;
        }
      }
    }
    for (i = 0; i < num_cands; i++)
      for (j = i + 1; j < num_cands; j++)
        if (trylist[i] > trylist[j]) {
          int tmp = trylist[i];
          trylist[i] = trylist[j];
          trylist[j] = tmp;
        }
    if (file < 0)
      NAME(estimate_compression_piece)(table, pcs, wdl, num_cands);
    else
      NAME(estimate_compression_pawn)(table, pcs, file, wdl, num_cands);
    for (i = 0; i < num_cands; i++) {
      if (compest[trylist[i]] < best) {
        best = compest[trylist[i]];
        bp = trylist[i];
      }
    }
    bestperm[k] = type_perm_list[bp][k];
  }
  *bestp = bp;

  return best;
}

uint64_t NAME(estimate_piece_dtz)(int *pcs, int *pt, T *table, uint8_t *best,
    int *bestp, T *v)
{
  int i;

  NAME(permute_v) = v;

  uint64_t estim = NAME(estimate_compression)(table, bestp, pcs, 0, -1);

  for (i = 0; i < entry_piece.num; i++)
    best[i] = pt[piece_perm_list[*bestp][i]];
  best[0] |= order_list[*bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  printf("\nbest permutation:");
  for(i = 0; i < entry_piece.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  return estim;
}

void NAME(permute_piece_dtz)(T *tb_table, int *pcs, T *table, int bestp, T *v)
{
  NAME(permute_v) = v;

  NAME(convert_data).src = table;
  NAME(convert_data).dst = tb_table;
  NAME(convert_data).pcs = pcs;
  NAME(convert_data).p = bestp;

  run_threaded(NAME(convert_data_piece), work_convert, 1);
}

uint64_t NAME(estimate_pawn_dtz)(int *pcs, int *pt, T *table, uint8_t *best,
    int *bestp, int file, T *v)
{
  int i;

  NAME(permute_v) = v;

  uint64_t estim = NAME(estimate_compression)(table, bestp, pcs, 0, file);

  for (i = 0; i < entry_pawn.num; i++)
    best[i] = pt[piece_perm_list[*bestp][i]];
  best[0] |= order_list[*bestp] << 4;
  best[1] |= order2_list[*bestp] << 4;

  printf("best order: %d", best[0] >> 4);
  if ((best[1] >> 4) < 0x0f) printf(" %d", best[1] >> 4);
  printf("\nbest permutation:");
  for (i = 0; i < entry_pawn.num; i++)
    printf(" %d", best[i] & 0x0f);
  printf("\n");

  return estim;
}

void NAME(permute_pawn_dtz)(T *tb_table, int *pcs, T *table, int bestp,
    int file, T *v)
{
  NAME(permute_v) = v;

  NAME(convert_data).src = table;
  NAME(convert_data).dst = tb_table;
  NAME(convert_data).pcs = pcs;
  NAME(convert_data).p = bestp;
  NAME(convert_data).file = file;

  run_threaded(NAME(convert_data_pawn), work_convert, 1);
}

#undef NAME
