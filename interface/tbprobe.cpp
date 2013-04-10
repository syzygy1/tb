/*
  Copyright (c) 2013 Ronald de Man
  This file may be redistributed and/or modified without restrictions.

  tbprobe.cpp contains the Stockfish-specific routines of the
  tablebase probing code. It should be relatively easy to adapt
  this code to other chess engines.
*/


#include "position.h"
#include "movegen.h"
#include "rkiss.h"
#include "tbprobe.h"
#include "tbcore.h"

#include "tbcore.c"

static RKISS rk;

// Given a position with 6 or fewer pieces, produce a text string
// of the form KQPvKRP, where "KQP" represent the white pieces if
// mirror == 0 and the black pieces if mirror == 1.
static void prt_str(Position& pos, char *str, int mirror)
{
  Color color;
  PieceType pt;
  int i;
  
  color = !mirror ? WHITE : BLACK;
  for (pt = KING; pt >= PAWN; pt--)
    for (i = 0; i < pos.piece_count(color, pt); i++)
      *str++ = pchr[6 - pt];
  *str++ = 'v';
  color = ~color;
  for (pt = KING; pt >= PAWN; pt--)
    for (i = 0; i < pos.piece_count(color, pt); i++)
      *str++ = pchr[6 - pt];
  *str++ = 0;
}

// Given a position, produce a 64-bit material signature key.
// If the engine supports such a key, it should equal the engine's key.
static uint64 calc_key(Position& pos, int mirror)
{
  Color color;
  PieceType pt;
  int i;
  uint64 key = 0;

  color = !mirror ? WHITE : BLACK;
  for (pt = PAWN; pt <= QUEEN; pt++)
    for (i = 0; i < pos.piece_count(color, pt); i++)
      key ^= Zobrist::psq[WHITE][pt][i];
  color = ~color;
  for (pt = PAWN; pt <= QUEEN; pt++)
    for (i = 0; i < pos.piece_count(color, pt); i++)
      key ^= Zobrist::psq[BLACK][pt][i];

  return key;
}

// Produce a 64-bit material key corresponding to the material combination
// defined by pcs[16], where pcs[1], ..., pcs[6] is the number of white
// pawns, ..., kings and pcs[9], ..., pcs[14] is the number of black
// pawns, ..., kings.
static uint64 calc_key_from_pcs(int *pcs, int mirror)
{
  int color;
  PieceType pt;
  int i;
  uint64 key = 0;

  color = !mirror ? 0 : 8;
  for (pt = PAWN; pt <= QUEEN; pt++)
    for (i = 0; i < pcs[color + pt]; i++)
      key ^= Zobrist::psq[WHITE][pt][i];
  color ^= 8;
  for (pt = PAWN; pt <= QUEEN; pt++)
    for (i = 0; i < pcs[color + pt]; i++)
      key ^= Zobrist::psq[BLACK][pt][i];

  return key;
}

// probe_wdl_table and probe_dtz_table require similar adaptations.
static int probe_wdl_table(Position& pos, int *success)
{
  struct TBEntry *ptr;
  struct TBHashEntry *ptr2;
  uint64 idx;
  uint64 key;
  int i;
  ubyte res;
  int p[TBPIECES];

  // Obtain the position's material signature key.
  key = pos.material_key();

  // Test for KvK.
  if (!key) return 0;

  ptr2 = TB_hash[key >> (64 - TBHASHBITS)];
  for (i = 0; i < HSHMAX; i++)
    if (ptr2[i].key == key) break;
  if (i == HSHMAX) {
    *success = 0;
    return 0;
  }

  ptr = ptr2[i].ptr;
  if (!ptr->ready) {
    LOCK(TB_mutex);
    if (!ptr->ready) {
      char str[16];
      prt_str(pos, str, ptr->key != key);
      if (!init_table_wdl(ptr, str)) {
	ptr->data = NULL;
	ptr2[i].key = 0ULL;
	*success = 0;
	return 0;
      }
      ptr->ready = 1;
    }
    UNLOCK(TB_mutex);
  }

  int bside, mirror, cmirror;
  if (!ptr->symmetric) {
    if (key != ptr->key) {
      cmirror = 8;
      mirror = 0x38;
      bside = (pos.side_to_move() == WHITE);
    } else {
      cmirror = mirror = 0;
      bside = !(pos.side_to_move() == WHITE);
    }
  } else {
    cmirror = pos.side_to_move() == WHITE ? 0 : 8;
    mirror = pos.side_to_move() == WHITE ? 0 : 0x38;
    bside = 0;
  }

  // p[i] is to contain the square 0-63 (A1-H8) for a piece of type
  // pc[i] ^ cmirror, where 1 = white pawn, ..., 14 = black king.
  // Pieces of the same type are guaranteed to be consecutive.
  if (!ptr->has_pawns) {
    struct TBEntry_piece *entry = (struct TBEntry_piece *)ptr;
    ubyte *pc = entry->pieces[bside];
    for (i = 0; i < entry->num;) {
      bitboard bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
				      (PieceType)(pc[i] & 0x07));
      do {
	p[i++] = FirstOne(bb);
	ClearFirst(bb);
      } while (bb);
    }
    idx = encode_piece(entry, entry->norm[bside], p, entry->factor[bside]);
    res = decompress_pairs(entry->precomp[bside], idx);
  } else {
    struct TBEntry_pawn *entry = (struct TBEntry_pawn *)ptr;
    int k = entry->file[0].pieces[0][0] ^ cmirror;
    bitboard bb = pos.pieces((Color)(k >> 3), (PieceType)(k & 0x07));
    i = 0;
    do {
      p[i++] = FirstOne(bb) ^ mirror;
      ClearFirst(bb);
    } while (bb);
    int f = pawn_file(entry, p);
    ubyte *pc = entry->file[f].pieces[bside];
    for (; i < entry->num;) {
      bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
				    (PieceType)(pc[i] & 0x07));
      do {
	p[i++] = FirstOne(bb) ^ mirror;
	ClearFirst(bb);
      } while (bb);
    }
    idx = encode_pawn(entry, entry->file[f].norm[bside], p, entry->file[f].factor[bside]);
    res = decompress_pairs(entry->file[f].precomp[bside], idx);
  }

  return ((int)res) - 2;
}

static int probe_dtz_table(Position& pos, int wdl, int *success)
{
  struct TBEntry *ptr;
  uint64 idx;
  int i, res;
  int p[TBPIECES];

  // Obtain the position's material signature key.
  uint64 key = pos.material_key();

  if (DTZ_table[0].key1 != key && DTZ_table[0].key2 != key) {
    for (i = 1; i < DTZ_ENTRIES; i++)
      if (DTZ_table[i].key1 == key) break;
    if (i < DTZ_ENTRIES) {
      struct DTZTableEntry table_entry = DTZ_table[i];
      for (; i > 0; i--)
	DTZ_table[i] = DTZ_table[i - 1];
      DTZ_table[0] = table_entry;
    } else {
      struct TBHashEntry *ptr2 = TB_hash[key >> (64 - TBHASHBITS)];
      for (i = 0; i < HSHMAX; i++)
	if (ptr2[i].key == key) break;
      if (i == HSHMAX) {
	*success = 0;
	return 0;
      }
      ptr = ptr2[i].ptr;
      char str[16];
      int mirror = (ptr->key != key);
      prt_str(pos, str, mirror);
      if (DTZ_table[DTZ_ENTRIES - 1].entry)
	free_dtz_entry(DTZ_table[DTZ_ENTRIES-1].entry);
      for (i = DTZ_ENTRIES - 1; i > 0; i--)
	DTZ_table[i] = DTZ_table[i - 1];
      load_dtz_table(str, calc_key(pos, mirror), calc_key(pos, !mirror));
    }
  }

  ptr = DTZ_table[0].entry;
  if (!ptr) {
    *success = 0;
    return 0;
  }

  int bside, mirror, cmirror;
  if (!ptr->symmetric) {
    if (key != ptr->key) {
      cmirror = 8;
      mirror = 0x38;
      bside = (pos.side_to_move() == WHITE);
    } else {
      cmirror = mirror = 0;
      bside = !(pos.side_to_move() == WHITE);
    }
  } else {
    cmirror = pos.side_to_move() == WHITE ? 0 : 8;
    mirror = pos.side_to_move() == WHITE ? 0 : 0x38;
    bside = 0;
  }

  if (!ptr->has_pawns) {
    struct DTZEntry_piece *entry = (struct DTZEntry_piece *)ptr;
    if ((entry->flags & 1) != bside && !entry->symmetric) {
      *success = -1;
      return 0;
    }
    ubyte *pc = entry->pieces;
    for (i = 0; i < entry->num;) {
      bitboard bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
				    (PieceType)(pc[i] & 0x07));
      do {
	p[i++] = FirstOne(bb);
	ClearFirst(bb);
      } while (bb);
    }
    idx = encode_piece((struct TBEntry_piece *)entry, entry->norm, p, entry->factor);
    res = decompress_pairs(entry->precomp, idx);

    if (entry->flags & 2)
      res = entry->map[entry->map_idx[wdl_to_map[wdl + 2]] + res];

    if (!(entry->flags & pa_flags[wdl + 2]) && !(wdl & 1))
      res *= 2;
  } else {
    struct DTZEntry_pawn *entry = (struct DTZEntry_pawn *)ptr;
    int k = entry->file[0].pieces[0] ^ cmirror;
    bitboard bb = pos.pieces((Color)(k >> 3), (PieceType)(k & 0x07));
    i = 0;
    do {
      p[i++] = FirstOne(bb) ^ mirror;
      ClearFirst(bb);
    } while (bb);
    int f = pawn_file((struct TBEntry_pawn *)entry, p);
    if ((entry->flags[f] & 1) != bside) {
      *success = -1;
      return 0;
    }
    ubyte *pc = entry->file[f].pieces;
    for (; i < entry->num;) {
      bb = pos.pieces((Color)((pc[i] ^ cmirror) >> 3),
			    (PieceType)(pc[i] & 0x07));
      do {
	p[i++] = FirstOne(bb) ^ mirror;
	ClearFirst(bb);
      } while (bb);
    }
    idx = encode_pawn((struct TBEntry_pawn *)entry, entry->file[f].norm, p, entry->file[f].factor);
    res = decompress_pairs(entry->file[f].precomp, idx);

    if (entry->flags[f] & 2)
      res = entry->map[entry->map_idx[f][wdl_to_map[wdl + 2]] + res];

    if (!(entry->flags[f] & pa_flags[wdl + 2]) && !(wdl & 1))
      res *= 2;
  }

  return res;
}

// Add underpromotion captures to list of captures.
static MoveStack *add_underprom_caps(Position& pos, MoveStack *stack, MoveStack *end)
{
  MoveStack *moves, *extra = end;

  for (moves = stack; moves < end; moves++) {
    Move move = moves->move;
    if (type_of(move) == PROMOTION && pos.is_empty(to_sq(move))) {
      (*extra++).move = (Move)(move - (1 << 12));
      (*extra++).move = (Move)(move - (2 << 12));
      (*extra++).move = (Move)(move - (3 << 12));
    }
  }

  return extra;
}

static int probe_ab(Position& pos, int alpha, int beta, int *success)
{
  int v;
  MoveStack stack[64];
  MoveStack *moves, *end;
  StateInfo st;

  // Generate (at least) all legal non-ep captures including (under)promotions.
  // It is OK to generate more, as long as they are filtered out below.
  if (!pos.checkers()) {
    end = generate<CAPTURES>(pos, stack);
    // Since underpromotion captures are not included, we need to add them.
    end = add_underprom_caps(pos, stack, end);
  } else
    end = generate<EVASIONS>(pos, stack);

  CheckInfo ci(pos);

  for (moves = stack; moves < end; moves++) {
    Move capture = moves->move;
    if (!pos.is_capture(capture) || type_of(capture) == ENPASSANT
			|| !pos.pl_move_is_legal(capture, ci.pinned))
      continue;
    pos.do_move(capture, st, ci, pos.move_gives_check(capture, ci));
    v = -probe_ab(pos, -beta, -alpha, success);
    pos.undo_move(capture);
    if (*success == 0) return 0;
    if (v > alpha) {
      if (v >= beta) {
        *success = 2;
	return v;
      }
      alpha = v;
    }
  }

  v = probe_wdl_table(pos, success);
  if (*success == 0) return 0;
  if (alpha >= v) {
    *success = 1 + (alpha > 0);
    return alpha;
  } else {
    *success = 1;
    return v;
  }
}

// Probe the WDL table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
// -2 : loss
// -1 : loss, but draw under 50-move rule
//  0 : draw
//  1 : win, but draw under 50-move rule
//  2 : win
int probe_wdl(Position& pos, int *success)
{
  int v;

  *success = 1;
  v = probe_ab(pos, -2, 2, success);

  // If en passant is not possible, we are done.
  if (pos.ep_square() == SQ_NONE)
    return v;
  if (!(*success)) return 0;

  // Now handle en passant.
  int v1 = -3;
  // Generate (at least) all legal en passant captures.
  MoveStack stack[192];
  MoveStack *moves, *end;
  StateInfo st;

  if (!pos.checkers())
    end = generate<CAPTURES>(pos, stack);
  else
    end = generate<EVASIONS>(pos, stack);

  CheckInfo ci(pos);

  for (moves = stack; moves < end; moves++) {
    Move capture = moves->move;
    if (type_of(capture) != ENPASSANT
	  || !pos.pl_move_is_legal(capture, ci.pinned))
      continue;
    pos.do_move(capture, st, ci, pos.move_gives_check(capture, ci));
    int v0 = -probe_ab(pos, -2, 2, success);
    pos.undo_move(capture);
    if (*success == 0) return 0;
    if (v0 > v1) v1 = v0;
  }
  if (v1 > -3) {
    if (v1 >= v) v = v1;
    else if (v == 0) {
      // Check whether there is at least one legal non-ep move.
      for (moves = stack; moves < end; moves++) {
	Move capture = moves->move;
	if (type_of(capture) == ENPASSANT) continue;
	if (pos.pl_move_is_legal(capture, ci.pinned)) break;
      }
      if (moves == end && !pos.checkers()) {
	end = generate<QUIETS>(pos, end);
	for (; moves < end; moves++) {
	  Move move = moves->move;
	  if (pos.pl_move_is_legal(move, ci.pinned))
	    break;
	}
      }
      // If not, then we are forced to play the losing ep capture.
      if (moves == end)
	v = v1;
    }
  }

  return v;
}

// This routine treats a position with en passant captures as one without.
static int probe_dtz_no_ep(Position& pos, int *success)
{
  int wdl, dtz;

  wdl = probe_ab(pos, -2, 2, success);
  if (*success == 0) return 0;

  if (wdl == 0) return 0;
  if (*success == 2) dtz = 1;
  else {
    MoveStack stack[192];
    MoveStack *moves, *end = stack;
    StateInfo st;
    CheckInfo ci(pos);

    if (wdl > 0) {
      // Generate at least all legal non-capturing pawn moves
      // including non-capturing promotions.
      if (!pos.checkers())
	end = generate<QUIETS>(pos, stack);
      else
	end = generate<EVASIONS>(pos, stack);

      for (moves = stack; moves < end; moves++) {
	Move move = moves->move;
	if (type_of(pos.piece_moved(move)) != PAWN) continue;
	if (pos.is_capture(move)) continue;
	if (!pos.pl_move_is_legal(move, ci.pinned)) continue;
	pos.do_move(move, st, ci, pos.move_gives_check(move, ci));
	int v = -probe_ab(pos, -2, 2, success);
	pos.undo_move(move);
	if (*success == 0) return 0;
	if (v == wdl)
	  return wdl == 1 ? 299 : VALUE_MATE - 101;
      }
    }
    dtz = 1 + probe_dtz_table(pos, wdl, success);
    if (*success == 0) return 0;
    if (*success == -1) {
      int v, best;
      best = -VALUE_MATE;
      // Generate at least all legal non-ep moves.
      // If wdl > 0, we can skip captures (and we already generated the rest).
      if (wdl < 0) {
	if (!pos.checkers())
	  end = generate<NON_EVASIONS>(pos, stack);
	else
	  end = generate<EVASIONS>(pos, stack);
      }
      for (moves = stack; moves < end; moves++) {
	Move move = moves->move;
	if (type_of(move) == ENPASSANT) continue;
	if (!pos.pl_move_is_legal(move, ci.pinned)) continue;
	pos.do_move(move, st, ci, pos.move_gives_check(move, ci));
	if (st.rule50 == 0) { // Did the last move reset the 50-move counter?
	  static int wdl_to_score[] = {
	    -VALUE_MATE + 100, -300, 0, 300, VALUE_MATE - 100
	  };
	  v = -probe_ab(pos, -2, 2, success);
	  v = wdl_to_score[v + 2];
	} else {
	  v = -probe_dtz(pos, success);
	}
	pos.undo_move(move);
	if (*success == 0) return 0;
	if (v > best) best = v;
      }
      if (best == -VALUE_MATE) {
	if (!pos.checkers())
	  best = 0; // Stalemate
      }
      if (best > 0) best--;
      else if (best < 0) best++;
      return best;
    }
  }

  int v;
  if (wdl & 1) {
    if (wdl > 0) v = 300 - dtz;
    else v = -300 + dtz;
  } else if (wdl > 0) v = VALUE_MATE - 100 - dtz;
  else if (wdl < 0) v = -VALUE_MATE + 100 + dtz;
  else v = 0;

  return v;
}

// Probe the DTZ table for a particular position.
// If *success != 0, the probe was successful.
// The return value is from the point of view of the side to move:
// -VALUE_MATE + n : loss in n ply.
//        -300 + n : loss, but draw under 50-move rule
//           0     : draw
//	   300 - n : win, but draw under 50-move rule
//  VALUE_MATE - n : win in n ply
// Note that the value of n can be off by 1.
int probe_dtz(Position& pos, int *success)
{
  int v;

  *success = 1;
  v = probe_dtz_no_ep(pos, success);

  if (pos.ep_square() == SQ_NONE)
    return v;
  if (!(*success)) return 0;

  // Now handle en passant.
  int v1 = -3;

  MoveStack stack[192];
  MoveStack *moves, *end;
  StateInfo st;

  if (!pos.checkers())
    end = generate<CAPTURES>(pos, stack);
  else
    end = generate<EVASIONS>(pos, stack);
  CheckInfo ci(pos);

  for (moves = stack; moves < end; moves++) {
    Move capture = moves->move;
    if (type_of(capture) != ENPASSANT
	  || !pos.pl_move_is_legal(capture, ci.pinned))
      continue;
    pos.do_move(capture, st, ci, pos.move_gives_check(capture, ci));
    int v0 = -probe_ab(pos, -2, 2, success);
    pos.undo_move(capture);
    if (*success == 0) return 0;
    if (v0 > v1) v1 = v0;
  }
  if (v1 > -3) {
    static int wdl_to_score[] = {
      -VALUE_MATE + 100 + 1, -300 + 1, 0, 300 - 1, VALUE_MATE - 100 - 1
    };
    v1 = wdl_to_score[v1 + 2];
    if (v1 >= v) v = v1;
    else if (v == 0) {
      for (moves = stack; moves < end; moves++) {
	Move capture = moves->move;
	if (type_of(capture) == ENPASSANT) continue;
	if (pos.pl_move_is_legal(capture, ci.pinned)) break;
      }
      if (moves == end && !pos.checkers()) {
	end = generate<QUIETS>(pos, end);
	for (; moves < end; moves++) {
	  Move move = moves->move;
	  if (pos.pl_move_is_legal(move, ci.pinned))
	    break;
	}
      }
      if (moves == end)
	v = v1;
    }
  }

  return v;
}

// Select a move that minimaxes DTZ.
// If *success != 0, the probe was successful.
// Returns the probe_dtz() score for the position.
// Best move is stored in *return_move.
int root_probe(Position& pos, Move *return_move, int *success)
{
  int value;

  *success = 1;
  value = probe_dtz(pos, success);
  if (!(*success)) return MOVE_NONE;

  MoveStack stack[192];
  MoveStack *moves, *end;
  StateInfo st;

  // Generate at least all legal moves.
  if (!pos.checkers())
    end = generate<NON_EVASIONS>(pos, stack);
  else
    end = generate<EVASIONS>(pos, stack);
  CheckInfo ci(pos);

  int num_best = 0;
  int best = -VALUE_INFINITE;
  int best2 = 0;
  int v, w;
  for (moves = stack; moves < end; moves++) {
    Move move = moves->move;
    if (!pos.pl_move_is_legal(move, ci.pinned))
      continue;
    pos.do_move(move, st, ci, pos.move_gives_check(move, ci));
    v = w = 0;
    if (pos.checkers()) {
      MoveStack s[192];
      if (generate<LEGAL>(pos, s) == s)
	v = VALUE_MATE;
    }
    if (!v) {
      v = -probe_dtz(pos, success);
      if (!st.rule50 && v) {
	w = v;
	if (v > 300)
	  v = VALUE_MATE - 100;
	else if (v > 0)
	  v = 300;
	else if (v < -300)
	  v = -VALUE_MATE + 100;
	else
	  v = -300;
      }
    }
    pos.undo_move(move);
    if (!(*success)) return MOVE_NONE;
    if (v > best || (v == best && w > best2)) {
      stack[0].move = move;
      best = v;
      best2 = w;
      num_best = 1;
    } else if (v == best && w == best2) {
      stack[num_best++].move = move;
    }
  }

  *return_move = stack[rk.rand<unsigned>() % num_best].move;
  return value;
}

