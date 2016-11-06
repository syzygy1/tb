struct Player {
  int num;
  int type[5];
  int pos[5];
};

static int probe_tb2(struct Player *player, struct Player *opp, bitboard player_occ, bitboard opp_occ, int alpha, int beta);

static int probe_tb_capts(struct Player *player, struct Player *opp, bitboard player_occ, bitboard opp_occ, int alpha, int beta, int *capts)
{
  int i, j;

  *capts = 0;

  if (opp->num > 1) {
    for (i = 0; i < player->num; i++) {
      int from = player->pos[i];
      bitboard bb = PieceRange(from, player->type[i], player_occ | opp_occ) & opp_occ;
      if (bb) {
	*capts = 1;
	opp->num--;
	bitboard occ2 = player_occ ^ bit[from];
	do {
	  int to = FirstOne(bb);
	  player->pos[i] = to;
	  for (j = 0; opp->pos[j] != to; j++);
	  int tmp_type = opp->type[j];
	  opp->type[j] = opp->type[opp->num];
	  opp->pos[j] = opp->pos[opp->num];
#ifdef HAS_PAWNS
	  // FIXME: optimise
	  if ((player->type[i] & 0x07) == PAWN && ((to + 0x08) & 0x30) == 0) {
	    int t = player->type[i];
	    int m;
	    for (m = KING - PAWN; m >= KNIGHT - PAWN; m--) {
	      player->type[i] = t + m;
	      int v = -probe_tb2(opp, player, opp_occ ^ bit[to], occ2 ^ bit[to], -beta, -alpha);
	      if (v > alpha) {
		alpha = v;
		if (alpha >= beta) break;
	      }
	    }
	    player->type[i] = t;
	  } else {
#endif
	    int v = -probe_tb2(opp, player, opp_occ ^ bit[to], occ2 ^ bit[to], -beta, -alpha);
	    if (v > alpha)
	      alpha = v;
#ifdef HAS_PAWNS
	  }
#endif
	  opp->type[j] = tmp_type;
	  opp->pos[j] = to;
	  if (alpha >= beta) {
	    player->pos[i] = from;
	    opp->num++;
	    return alpha;
	  }
	  ClearFirst(bb);
	} while (bb);
	player->pos[i] = from;
	opp->num++;
      }
    }
  } else {
    for (i = 0; i < player->num; i++) {
      bitboard bb = PieceRange(player->pos[i], player->type[i], player_occ) & opp_occ;
      if (bb) {
	*capts = 1;
	return -2;
      }
    }
  }

  return alpha;
}

static int probe_tb2(struct Player *player, struct Player *opp, bitboard player_occ, bitboard opp_occ, int alpha, int beta)
{
  int i, j;
  bitboard player_att[5];
  bitboard opp_att[5];

  // first try captures
  if (opp->num > 1) {
    int capts = 0;
    for (i = 0; i < player->num; i++) {
      int from = player->pos[i];
      bitboard bb = PieceRange(from, player->type[i], player_occ | opp_occ);
      player_att[i] = bb;
      bb &= opp_occ;
      if (bb) {
	capts = 1;
	opp->num--;
	bitboard occ2 = player_occ ^ bit[from];
	do {
	  int to = FirstOne(bb);
	  player->pos[i] = to;
	  for (j = 0; opp->pos[j] != to; j++);
	  int tmp_type = opp->type[j];
	  opp->type[j] = opp->type[opp->num];
	  opp->pos[j] = opp->pos[opp->num];
#ifdef HAS_PAWNS
	  if ((player->type[i] & 0x07) == PAWN && ((to + 0x08) & 0x30) == 0) {
	    int t = player->type[i];
	    int m;
	    for (m = KING - PAWN; m >= KNIGHT - PAWN; m--) {
	      player->type[i] = t + m;
	      int v = -probe_tb2(opp, player, opp_occ ^ bit[to], occ2 ^ bit[to], -beta, -alpha);
	      if (v > alpha) {
		alpha = v;
		if (alpha >= beta) break;
	      }
	    }
	    player->type[i] = t;
	  } else {
#endif
	    int v = -probe_tb2(opp, player, opp_occ ^ bit[to], occ2 ^ bit[to], -beta, -alpha);
	    if (v > alpha)
	      alpha = v;
#ifdef HAS_PAWNS
	  }
#endif
	  opp->type[j] = tmp_type;
	  opp->pos[j] = to;
	  if (alpha >= beta) {
	    player->pos[i] = from;
	    opp->num++;
	    return alpha;
	  }
	  ClearFirst(bb);
	} while (bb);
	player->pos[i] = from;
	opp->num++;
      }
    }
    if (capts) return alpha;
  } else {
    for (i = 0; i < player->num; i++) {
      bitboard bb = PieceRange(player->pos[i], player->type[i], player_occ);
      player_att[i] = bb;
      if (bb & opp_occ)
	return -2;
    }
  }

#if 1
  if (player->num + opp->num < 6)
    goto skip_threats;
#endif

  // now try threats. there are two cases
  bitboard atts = 0ULL;
  for (i = 0; i < opp->num; i++) {
    opp_att[i] = PieceRange(opp->pos[i], opp->type[i], player_occ | opp_occ);
    atts |= opp_att[i];
  }
  // first case: opponent currently is not attacking any pieces
  // we only need to consider moves moving into opponent attacks
  if (!(atts & player_occ)) {
    if (player->num > 1) {
      player->num--;
      for (i = 0; i <= player->num; i++) {
#ifdef HAS_PAWNS
	if ((player->type[i] & 0x07) == 1) continue;
#endif
	bitboard bb = player_att[i] & atts;
	if (!bb) continue;
	int pos = player->pos[i];
	int type = player->type[i];
	player->pos[i] = player->pos[player->num];
	player->type[i] = player->type[player->num];
	do {
	  int sq = FirstOne(bb);
	  int beta2 = beta;
	  for (j = 0; j < opp->num; j++) {
	    if (!(bit[sq] & opp_att[j])) continue;
	    int tmp_pos = opp->pos[j];
	    opp->pos[j] = sq;
#ifdef HAS_PAWNS
	    if ((opp->type[j] & 0x07) == PAWN && ((sq + 0x08) & 0x30) == 0) {
	      int t = opp->type[j];
	      int m;
	      for (m = KING - PAWN; m >= KNIGHT - PAWN; m--) {
		opp->type[j] = t + m;
		int v = probe_tb2(player, opp, player_occ ^ bit[pos], opp_occ ^ bit[sq] ^ bit[tmp_pos], alpha, beta2);
		if (v < beta2) {
		  beta2 = v;
		  if (beta2 <= alpha)
		    break;
		}
	      }
	      opp->type[j] = t;
	    } else {
#endif
	      int v = probe_tb2(player, opp, player_occ ^ bit[pos], opp_occ ^ bit[sq] ^ bit[tmp_pos], alpha, beta2);
	      if (v < beta2)
		beta2 = v;
#ifdef HAS_PAWNS
	    }
#endif
	    opp->pos[j] = tmp_pos;
	    if (beta2 <= alpha) break;
	  }
	  if (beta2 > alpha) {
	    if (beta2 >= beta) {
	      player->pos[i] = pos;
	      player->type[i] = type;
	      player->num++;
	      return beta2;
	    }
	    alpha = beta2;
	  }
	  ClearFirst(bb);
	} while (bb);
	player->pos[i] = pos;
	player->type[i] = type;
      }
      player->num++;
    } else {
      for (i = 0; i < player->num; i++) {
#ifdef HAS_PAWNS
	if ((player->type[i] & 0x07) == 1) continue;
#endif
	if (player_att[i] & atts) return 2;
      }
    }
  } else { // second case: just try all moves
    for (i = 0; i < player->num; i++) {
#ifdef HAS_PAWNS
      if ((player->type[i] & 0x07) == 1) continue;
#endif
      bitboard bb = player_att[i] & ~player_occ;
      if (bb) {
	int from = player->pos[i];
	do {
	  int capts;
	  int to = FirstOne(bb);
	  player->pos[i] = to;
	  int v = -probe_tb_capts(opp, player, opp_occ, player_occ ^ bit[from] ^ bit[to], -beta, -alpha, &capts);
	  if (capts && v > alpha) {
	    if (v >= beta) {
	      player->pos[i] = from;
	      return v;
	    }
	    alpha = v;
	  }
	  ClearFirst(bb);
	} while (bb);
	player->pos[i] = from;
      }
    }
  }

  int pieces[6];
  int pos[6];
skip_threats:
  for (i = 0; i < player->num; i++) {
    pieces[i] = player->type[i];
    pos[i] = player->pos[i];
  }
  for (j = 0; j < opp->num; j++, i++) {
    pieces[i] = opp->type[j];
    pos[i] = opp->pos[j];
  }
  for (; i < numpcs; i++)
    pieces[i] = 0;
  int v = probe_table(pieces, pos, pieces[0] < 8);
  return alpha > v ? alpha : v;
}

int probe_tb(int *pieces, int *pos, int wtm, bitboard occ, int alpha, int beta)
{
  int i, j, k;
  struct Player white, black;
  bitboard bb;

  bb = 0ULL;
  for (i = j = k = 0; i < numpcs; i++) {
    if (!pieces[i]) continue;
    if (!(pieces[i] & 0x08)) {
      white.type[j] = pieces[i];
      white.pos[j] = pos[i];
      j++;
    } else {
      black.type[k] = pieces[i];
      black.pos[k] = pos[i];
      bb |= bit[pos[i]];
      k++;
    }
  }
  white.num = j;
  black.num = k;

//if (j == 0 || k == 0) return 2;

#if 1
  if (wtm)
    return probe_tb2(&white, &black, occ ^ bb, bb, alpha, beta);
  else
    return probe_tb2(&black, &white, bb, occ ^ bb, alpha, beta);
#else
#define min(a,b) ((a) < (b) ? a : b)
#define max(a,b) ((a) > (b) ? a : b)
  int v, v2;
  skip = 0;
  if (wtm)
    v = probe_tb2(&white, &black, occ ^ bb, bb, alpha, beta);
  else
    v = probe_tb2(&black, &white, bb, occ ^ bb, alpha, beta);
  skip = 1;
  if (wtm)
    v2 = probe_tb2(&white, &black, occ ^ bb, bb, alpha, beta);
  else
    v2 = probe_tb2(&black, &white, bb, occ ^ bb, alpha, beta);
  //int v2 = old_probe_tb(pieces, pos, wtm, occ, alpha, beta);
  if (max(v,alpha) != max(v2,alpha) && min(v,beta) != min(v2,beta)) {
    printf("hey! v = %d, v2 = %d\n", v, v2);
    for (i = 0; i < numpcs; i++)
      printf("pieces[%d] = %d, pos[%d] = %d\n", i, pieces[i], i, pos[i]);
    if (wtm)
      v = probe_tb2(&white, &black, occ ^ bb, bb, alpha, beta);
    else
      v = probe_tb2(&black, &white, bb, occ ^ bb, alpha, beta);
  }
  return v;
#endif
}

