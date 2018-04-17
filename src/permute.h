#ifndef PERMUTE_H
#define PERMUTE_H

#include "types.h"

#define MAX_STEP (1ULL << 30)

uint64_t init_permute_piece(int *pcs, int *pt);
u16 *init_permute_piece_u16(int *pcs, int *pt, u16 *tb_table);
void permute_piece_wdl(u8 *tb_table, int *pcs, int *pt, u8 *table,
    uint8_t *best, u8 *v);
uint64_t estimate_piece_dtz_u8(int *pcs, int *pt, u8 *table, uint8_t *best,
    int *bestp, u8 *v);
uint64_t estimate_piece_dtz_u16(int *pcs, int *pt, u16 *table, uint8_t *best,
    int *bestp, u16 *v);
void permute_piece_dtz_u8(u8 *tb_table, int *pcs, u8 *table, int bestp, u8 *v);
void permute_piece_dtz_u16(u16 *tb_table, int *pcs, u16 *table, int bestp,
    u16 *v);
void permute_piece_dtz_u16_full(u16 *tb_table, int *pcs, u16 *table, int bestp,
    u16 *v, uint64_t tb_step);

#endif
