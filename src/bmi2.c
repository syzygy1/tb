/*
  Copyright (c) 2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

/* to be included in board.c */

#ifdef BMI2

static uint16_t attacks_table[107648];
struct BMI2Info bishop_bmi2[64];
struct BMI2Info rook_bmi2[64];

static signed char m_bishop_dir[4][2] = {
  { -9, -17 }, { -7, -15 }, { 7, 15 }, { 9, 17 }
};

static signed char m_rook_dir[4][2] = {
  { -8, -16 }, { -1, -1 }, { 1, 1}, { 8, 16 }
};

static int init_bmi2(struct BMI2Info *info, signed char dir[][2], int idx)
{
  int sq, sq88;
  int i, j, d, num;
  bitboard bb, bb2;
  int squares[12];

  for (sq = 0; sq < 64; sq++) {
    info[sq].data = &attacks_table[idx];

    // calculate mask
    sq88 = sq + (sq & ~7);
    bb = 0;
    for (i = 0; i < 4; i++) {
      if ((sq88 + dir[i][1]) & 0x88) continue;
      for (d = 2; !((sq88 + d * dir[i][1]) & 0x88); d++)
	bb |= bit[sq + (d - 1) * dir[i][0]];
    }
    info[sq].mask1 = bb;

    num = 0;
    while (bb) {
      squares[num++] = FirstOne(bb);
      ClearFirst(bb);
    }

    // loop through all possible occupations within the mask
    // and calculate the corresponding attack sets
    for (i = 0; i < (1 << num); i++) {
      bb = 0;
      for (j = 0; j < num; j++)
	if (i & (1 << j))
	  bb |= bit[squares[j]];
      bb2 = 0;
      for (j = 0; j < 4; j++) {
	for (d = 1; !((sq88 + d * dir[j][1]) & 0x88); d++) {
	  bb2 |= bit[sq + d * dir[j][0]];
	  if (bb & bit[sq + d * dir[j][0]]) break;
	}
      }
      if (i == 0)
	info[sq].mask2 = bb2;
      attacks_table[idx++] = _pext_u64(bb2, info[sq].mask2);
    }
  }

  return idx;
}

void set_up_move_gen(void)
{
  int i;

  for (i = 0; i < 97264; i++)
    attacks_table[i] = 0ULL;
  i = init_bmi2(bishop_bmi2, m_bishop_dir, 0);
  init_bmi2(rook_bmi2, m_rook_dir, i);
}

#endif

