/*
  Copyright (c) 2011-2013 Ronald de Man

  This file is distributed under the terms of the GNU GPL, version 2.
*/

/* to be included in board.c */

#ifdef MAGIC

static bitboard attacks_table[89524];
struct Magic bishop_magic[64];
struct Magic rook_magic[64];

struct MagicInit {
  bitboard magic;
  int index;
};

// fixed shift magics found by Volker Annuss
// from: http://talkchess.com/forum/viewtopic.php?p=670709#670709

static struct MagicInit bishop_init[64] = {
  { 0x0000404040404040u,  33104 },
  { 0x0000a060401007fcu,   4094 },
  { 0x0000401020200000u,  24764 },
  { 0x0000806004000000u,  13882 },
  { 0x0000440200000000u,  23090 },
  { 0x0000080100800000u,  32640 },
  { 0x0000104104004000u,  11558 },
  { 0x0000020020820080u,  32912 },
  { 0x0000040100202004u,  13674 },
  { 0x0000020080200802u,   6109 },
  { 0x0000010040080200u,  26494 },
  { 0x0000008060040000u,  17919 },
  { 0x0000004402000000u,  25757 },
  { 0x00000021c100b200u,  17338 },
  { 0x0000000400410080u,  16983 },
  { 0x000003f7f05fffc0u,  16659 },
  { 0x0004228040808010u,  13610 },
  { 0x0000200040404040u,   2224 },
  { 0x0000400080808080u,  60405 },
  { 0x0000200200801000u,   7983 },
  { 0x0000240080840000u,     17 },
  { 0x000018000c03fff8u,  34321 },
  { 0x00000a5840208020u,  33216 },
  { 0x0000058408404010u,  17127 },
  { 0x0002022000408020u,   6397 },
  { 0x0000402000408080u,  22169 },
  { 0x0000804000810100u,  42727 },
  { 0x000100403c0403ffu,    155 },
  { 0x00078402a8802000u,   8601 },
  { 0x0000101000804400u,  21101 },
  { 0x0000080800104100u,  29885 },
  { 0x0000400480101008u,  29340 },
  { 0x0001010102004040u,  19785 },
  { 0x0000808090402020u,  12258 },
  { 0x0007fefe08810010u,  50451 },
  { 0x0003ff0f833fc080u,   1712 },
  { 0x007fe08019003042u,  78475 },
  { 0x0000202040008040u,   7855 },
  { 0x0001004008381008u,  13642 },
  { 0x0000802003700808u,   8156 },
  { 0x0000208200400080u,   4348 },
  { 0x0000104100200040u,  28794 },
  { 0x0003ffdf7f833fc0u,  22578 },
  { 0x0000008840450020u,  50315 },
  { 0x0000020040100100u,  85452 },
  { 0x007fffdd80140028u,  32816 },
  { 0x0000202020200040u,  13930 },
  { 0x0001004010039004u,  17967 },
  { 0x0000040041008000u,  33200 },
  { 0x0003ffefe0c02200u,  32456 },
  { 0x0000001010806000u,   7762 },
  { 0x0000000008403000u,   7794 },
  { 0x0000000100202000u,  22761 },
  { 0x0000040100200800u,  14918 },
  { 0x0000404040404000u,  11620 },
  { 0x00006020601803f4u,  15925 },
  { 0x0003ffdfdfc28048u,  32528 },
  { 0x0000000820820020u,  12196 },
  { 0x0000000010108060u,  32720 },
  { 0x0000000000084030u,  26781 },
  { 0x0000000001002020u,  19817 },
  { 0x0000000040408020u,  24732 },
  { 0x0000004040404040u,  25468 },
  { 0x0000404040404040u,  10186 }
};

static struct MagicInit rook_init[64] = {
  { 0x00280077ffebfffeu,  41305 },
  { 0x2004010201097fffu,  14326 },
  { 0x0010020010053fffu,  24477 },
  { 0x0030002ff71ffffau,   8223 },
  { 0x7fd00441ffffd003u,  49795 },
  { 0x004001d9e03ffff7u,  60546 },
  { 0x004000888847ffffu,  28543 },
  { 0x006800fbff75fffdu,  79282 },
  { 0x000028010113ffffu,   6457 },
  { 0x0020040201fcffffu,   4125 },
  { 0x007fe80042ffffe8u,  81021 },
  { 0x00001800217fffe8u,  42341 },
  { 0x00001800073fffe8u,  14139 },
  { 0x007fe8009effffe8u,  19465 },
  { 0x00001800602fffe8u,   9514 },
  { 0x000030002fffffa0u,  71090 },
  { 0x00300018010bffffu,  75419 },
  { 0x0003000c0085fffbu,  33476 },
  { 0x0004000802010008u,  27117 },
  { 0x0002002004002002u,  85964 },
  { 0x0002002020010002u,  54915 },
  { 0x0001002020008001u,  36544 },
  { 0x0000004040008001u,  71854 },
  { 0x0000802000200040u,  37996 },
  { 0x0040200010080010u,  30398 },
  { 0x0000080010040010u,  55939 },
  { 0x0004010008020008u,  53891 },
  { 0x0000040020200200u,  56963 },
  { 0x0000010020020020u,  77451 },
  { 0x0000010020200080u,  12319 },
  { 0x0000008020200040u,  88500 },
  { 0x0000200020004081u,  51405 },
  { 0x00fffd1800300030u,  72878 },
  { 0x007fff7fbfd40020u,    676 },
  { 0x003fffbd00180018u,  83122 },
  { 0x001fffde80180018u,  22206 },
  { 0x000fffe0bfe80018u,  75186 },
  { 0x0001000080202001u,    681 },
  { 0x0003fffbff980180u,  36453 },
  { 0x0001fffdff9000e0u,  20369 },
  { 0x00fffeebfeffd800u,   1981 },
  { 0x007ffff7ffc01400u,  13343 },
  { 0x0000408104200204u,  10650 },
  { 0x001ffff01fc03000u,  57987 },
  { 0x000fffe7f8bfe800u,  26302 },
  { 0x0000008001002020u,  58357 },
  { 0x0003fff85fffa804u,  40546 },
  { 0x0001fffd75ffa802u,      0 },
  { 0x00ffffec00280028u,  14967 },
  { 0x007fff75ff7fbfd8u,  80361 },
  { 0x003fff863fbf7fd8u,  40905 },
  { 0x001fffbfdfd7ffd8u,  58347 },
  { 0x000ffff810280028u,  20381 },
  { 0x0007ffd7f7feffd8u,  81868 },
  { 0x0003fffc0c480048u,  59381 },
  { 0x0001ffffafd7ffd8u,  84404 },
  { 0x00ffffe4ffdfa3bau,  45811 },
  { 0x007fffef7ff3d3dau,  62898 },
  { 0x003fffbfdfeff7fau,  45796 },
  { 0x001fffeff7fbfc22u,  66994 },
  { 0x0000020408001001u,  67204 },
  { 0x0007fffeffff77fdu,  32448 },
  { 0x0003ffffbf7dfeecu,  62946 },
  { 0x0001ffff9dffa333u,  17005 }
};

static signed char m_bishop_dir[4][2] = {
  { -9, -17 }, { -7, -15 }, { 7, 15 }, { 9, 17 }
};

static signed char m_rook_dir[4][2] = {
  { -8, -16 }, { -1, -1 }, { 1, 1}, { 8, 16 }
};

static void init_magics(struct MagicInit *magic_init, struct Magic *magic,
                        signed char dir[][2], int shift)
{
  int sq, sq88;
  int i, j, d, num;
  bitboard bb, bb2;
  int squares[12];

  for (i = 0; i < 12; i++)
    squares[i] = 0;

  for (sq = 0; sq < 64; sq++) {
    magic[sq].magic = magic_init[sq].magic;
    magic[sq].data = &attacks_table[magic_init[sq].index];

    // calculate mask
    sq88 = sq + (sq & ~7);
    bb = 0;
    for (i = 0; i < 4; i++) {
      if ((sq88 + dir[i][1]) & 0x88) continue;
      for (d = 2; !((sq88 + d * dir[i][1]) & 0x88); d++)
        bb |= bit[sq + (d - 1) * dir[i][0]];
    }
    magic[sq].mask = bb;

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
      j = (bb * magic[sq].magic) >> shift;
      magic[sq].data[j] = bb2;
    }
  }
}

void set_up_move_gen(void)
{
  init_magics(bishop_init, bishop_magic, m_bishop_dir, 64 - 9);
  init_magics(rook_init, rook_magic, m_rook_dir, 64 - 12);
}

#endif
