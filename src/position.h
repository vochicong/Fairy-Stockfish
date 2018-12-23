/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2018 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef POSITION_H_INCLUDED
#define POSITION_H_INCLUDED

#include <cassert>
#include <deque>
#include <memory> // For std::unique_ptr
#include <string>
#include <functional>

#include "bitboard.h"
#include "types.h"
#include "variant.h"


/// StateInfo struct stores information needed to restore a Position object to
/// its previous state when we retract a move. Whenever a move is made on the
/// board (by calling Position::do_move), a StateInfo object must be passed.

struct StateInfo {

  // Copied when making a move
  Key    pawnKey;
  Key    materialKey;
  Value  nonPawnMaterial[COLOR_NB];
  int    castlingRights;
  int    rule50;
  int    pliesFromNull;
  CheckCount checksGiven[COLOR_NB];
  Score  psq;
  Square epSquare;

  // Not copied when making a move (will be recomputed anyhow)
  Key        key;
  Bitboard   checkersBB;
  Piece      capturedPiece;
  Piece      unpromotedCapturedPiece;
  StateInfo* previous;
  Bitboard   blockersForKing[COLOR_NB];
  Bitboard   pinners[COLOR_NB];
  Bitboard   checkSquares[PIECE_TYPE_NB];
  Bitboard   flippedPieces;
  bool       capturedpromoted;
  bool       shak;
};

/// A list to keep track of the position states along the setup moves (from the
/// start position to the position just before the search starts). Needed by
/// 'draw by repetition' detection. Use a std::deque because pointers to
/// elements are not invalidated upon list resizing.
typedef std::unique_ptr<std::deque<StateInfo>> StateListPtr;


/// Position class stores information regarding the board representation as
/// pieces, side to move, hash keys, castling info, etc. Important methods are
/// do_move() and undo_move(), used by the search to update node info when
/// traversing the search tree.
class Thread;

class Position {
public:
  static void init();

  Position() = default;
  Position(const Position&) = delete;
  Position& operator=(const Position&) = delete;

  // FEN string input/output
  Position& set(const Variant* v, const std::string& fenStr, bool isChess960, StateInfo* si, Thread* th, bool sfen = false);
  Position& set(const std::string& code, Color c, StateInfo* si);
  const std::string fen() const;

  // Variant rule properties
  const Variant* variant() const;
  Rank max_rank() const;
  File max_file() const;
  Bitboard board_bb() const;
  const std::set<PieceType>& piece_types() const;
  const std::string piece_to_char() const;
  Rank promotion_rank() const;
  const std::set<PieceType, std::greater<PieceType> >& promotion_piece_types() const;
  bool sittuyin_promotion() const;
  PieceType promoted_piece_type(PieceType pt) const;
  bool mandatory_piece_promotion() const;
  bool piece_demotion() const;
  bool endgame_eval() const;
  bool double_step_enabled() const;
  bool first_rank_double_steps() const;
  bool castling_enabled() const;
  bool castling_dropped_piece() const;
  File castling_kingside_file() const;
  File castling_queenside_file() const;
  bool checking_permitted() const;
  bool must_capture() const;
  bool must_drop() const;
  bool piece_drops() const;
  bool drop_loop() const;
  bool captures_to_hand() const;
  bool first_rank_drops() const;
  bool drop_on_top() const;
  bool enclosing_drop() const;
  Bitboard drop_region(Color c) const;
  bool sittuyin_rook_drop() const;
  bool drop_opposite_colored_bishop() const;
  bool drop_promoted() const;
  bool shogi_doubled_pawn() const;
  bool immobility_illegal() const;
  bool flip_enclosed_pieces() const;
  // winning conditions
  Value stalemate_value(int ply = 0) const;
  Value checkmate_value(int ply = 0) const;
  Value bare_king_value(int ply = 0) const;
  Value extinction_value(int ply = 0) const;
  bool bare_king_move() const;
  const std::set<PieceType>& extinction_piece_types() const;
  PieceType capture_the_flag_piece() const;
  Bitboard capture_the_flag(Color c) const;
  bool flag_move() const;
  CheckCount max_check_count() const;
  int connect_n() const;
  CheckCount checks_given(Color c) const;
  bool is_variant_end() const;
  bool is_variant_end(Value& result, int ply = 0) const;

  // Variant-specific properties
  int count_in_hand(Color c, PieceType pt) const;

  // Position representation
  Bitboard pieces() const;
  Bitboard pieces(PieceType pt) const;
  Bitboard pieces(PieceType pt1, PieceType pt2) const;
  Bitboard pieces(Color c) const;
  Bitboard pieces(Color c, PieceType pt) const;
  Bitboard pieces(Color c, PieceType pt1, PieceType pt2) const;
  Piece piece_on(Square s) const;
  Piece unpromoted_piece_on(Square s) const;
  Square ep_square() const;
  bool empty(Square s) const;
  int count(Color c, PieceType pt) const;
  template<PieceType Pt> int count(Color c) const;
  template<PieceType Pt> int count() const;
  template<PieceType Pt> const Square* squares(Color c) const;
  const Square* squares(Color c, PieceType pt) const;
  template<PieceType Pt> Square square(Color c) const;

  // Castling
  int can_castle(Color c) const;
  int can_castle(CastlingRight cr) const;
  bool castling_impeded(CastlingRight cr) const;
  Square castling_rook_square(CastlingRight cr) const;

  // Checking
  Bitboard checkers() const;
  Bitboard blockers_for_king(Color c) const;
  Bitboard check_squares(PieceType pt) const;

  // Attacks to/from a given square
  Bitboard attackers_to(Square s) const;
  Bitboard attackers_to(Square s, Color c) const;
  Bitboard attackers_to(Square s, Bitboard occupied) const;
  Bitboard attackers_to(Square s, Bitboard occupied, Color c) const;
  Bitboard attacks_from(Color c, PieceType pt, Square s) const;
  template<PieceType> Bitboard attacks_from(Color c, Square s) const;
  Bitboard moves_from(Color c, PieceType pt, Square s) const;
  Bitboard slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const;

  // Properties of moves
  bool legal(Move m) const;
  bool pseudo_legal(const Move m) const;
  bool capture(Move m) const;
  bool capture_or_promotion(Move m) const;
  bool gives_check(Move m) const;
  bool advanced_pawn_push(Move m) const;
  Piece moved_piece(Move m) const;
  Piece captured_piece() const;

  // Piece specific
  bool pawn_passed(Color c, Square s) const;
  bool opposite_bishops() const;
  bool is_promoted(Square s) const;

  // Doing and undoing moves
  void do_move(Move m, StateInfo& newSt);
  void do_move(Move m, StateInfo& newSt, bool givesCheck);
  void undo_move(Move m);
  void do_null_move(StateInfo& newSt);
  void undo_null_move();

  // Static Exchange Evaluation
  bool see_ge(Move m, Value threshold = VALUE_ZERO) const;

  // Accessing hash keys
  Key key() const;
  Key key_after(Move m) const;
  Key material_key() const;
  Key pawn_key() const;

  // Other properties of the position
  Color side_to_move() const;
  int game_ply() const;
  bool is_chess960() const;
  Thread* this_thread() const;
  bool is_draw(int ply) const;
  bool has_game_cycle(int ply) const;
  bool has_repeated() const;
  int rule50_count() const;
  Score psq_score() const;
  Value non_pawn_material(Color c) const;
  Value non_pawn_material() const;

  // Position consistency check, for debugging
  bool pos_is_ok() const;
  void flip();

private:
  // Initialization helpers (used while setting up a position)
  void set_castling_right(Color c, Square rfrom);
  void set_state(StateInfo* si) const;
  void set_check_info(StateInfo* si) const;

  // Other helpers
  void put_piece(Piece pc, Square s);
  void remove_piece(Piece pc, Square s);
  void move_piece(Piece pc, Square from, Square to);
  template<bool Do>
  void do_castling(Color us, Square from, Square& to, Square& rfrom, Square& rto);

  // Data members
  Piece board[SQUARE_NB];
  Piece unpromotedBoard[SQUARE_NB];
  Bitboard byTypeBB[PIECE_TYPE_NB];
  Bitboard byColorBB[COLOR_NB];
  int pieceCount[PIECE_NB];
  Square pieceList[PIECE_NB][64];
  int index[SQUARE_NB];
  int castlingRightsMask[SQUARE_NB];
  Square castlingRookSquare[CASTLING_RIGHT_NB];
  Bitboard castlingPath[CASTLING_RIGHT_NB];
  int gamePly;
  Color sideToMove;
  Thread* thisThread;
  StateInfo* st;

  // variant-specific
  const Variant* var;
  bool chess960;
  int pieceCountInHand[COLOR_NB][PIECE_TYPE_NB];
  Bitboard promotedPieces;
  void add_to_hand(Color c, PieceType pt);
  void remove_from_hand(Color c, PieceType pt);
  void drop_piece(Piece pc_hand, Piece pc_drop, Square s);
  void undrop_piece(Piece pc_hand, Piece pc_drop, Square s);
};

extern std::ostream& operator<<(std::ostream& os, const Position& pos);

inline const Variant* Position::variant() const {
  assert(var != nullptr);
  return var;
}

inline Rank Position::max_rank() const {
  assert(var != nullptr);
  return var->maxRank;
}

inline File Position::max_file() const {
  assert(var != nullptr);
  return var->maxFile;
}

inline Bitboard Position::board_bb() const {
  assert(var != nullptr);
  return board_size_bb(var->maxFile, var->maxRank);
}

inline const std::set<PieceType>& Position::piece_types() const {
  assert(var != nullptr);
  return var->pieceTypes;
}

inline const std::string Position::piece_to_char() const {
  assert(var != nullptr);
  return var->pieceToChar;
}

inline Rank Position::promotion_rank() const {
  assert(var != nullptr);
  return var->promotionRank;
}

inline const std::set<PieceType, std::greater<PieceType> >& Position::promotion_piece_types() const {
  assert(var != nullptr);
  return var->promotionPieceTypes;
}

inline bool Position::sittuyin_promotion() const {
  assert(var != nullptr);
  return var->sittuyinPromotion;
}

inline PieceType Position::promoted_piece_type(PieceType pt) const {
  assert(var != nullptr);
  return var->promotedPieceType[pt];
}

inline bool Position::mandatory_piece_promotion() const {
  assert(var != nullptr);
  return var->mandatoryPiecePromotion;
}

inline bool Position::piece_demotion() const {
  assert(var != nullptr);
  return var->pieceDemotion;
}

inline bool Position::endgame_eval() const {
  assert(var != nullptr);
  return var->endgameEval;
}

inline bool Position::double_step_enabled() const {
  assert(var != nullptr);
  return var->doubleStep;
}

inline bool Position::first_rank_double_steps() const {
  assert(var != nullptr);
  return var->firstRankDoubleSteps;
}

inline bool Position::castling_enabled() const {
  assert(var != nullptr);
  return var->castling;
}

inline bool Position::castling_dropped_piece() const {
  assert(var != nullptr);
  return var->castlingDroppedPiece;
}

inline File Position::castling_kingside_file() const {
  assert(var != nullptr);
  return var->castlingKingsideFile;
}

inline File Position::castling_queenside_file() const {
  assert(var != nullptr);
  return var->castlingQueensideFile;
}

inline bool Position::checking_permitted() const {
  assert(var != nullptr);
  return var->checking;
}

inline bool Position::must_capture() const {
  assert(var != nullptr);
  return var->mustCapture;
}

inline bool Position::must_drop() const {
  assert(var != nullptr);
  return var->mustDrop;
}

inline bool Position::piece_drops() const {
  assert(var != nullptr);
  return var->pieceDrops;
}

inline bool Position::drop_loop() const {
  assert(var != nullptr);
  return var->dropLoop;
}

inline bool Position::captures_to_hand() const {
  assert(var != nullptr);
  return var->capturesToHand;
}

inline bool Position::first_rank_drops() const {
  assert(var != nullptr);
  return var->firstRankDrops;
}

inline bool Position::drop_on_top() const {
  assert(var != nullptr);
  return var->dropOnTop;
}

inline bool Position::enclosing_drop() const {
  assert(var != nullptr);
  return var->enclosingDrop;
}

inline Bitboard Position::drop_region(Color c) const {
  assert(var != nullptr);
  return c == WHITE ? var->whiteDropRegion : var->blackDropRegion;
}

inline bool Position::sittuyin_rook_drop() const {
  assert(var != nullptr);
  return var->sittuyinRookDrop;
}

inline bool Position::drop_opposite_colored_bishop() const {
  assert(var != nullptr);
  return var->dropOppositeColoredBishop;
}

inline bool Position::drop_promoted() const {
  assert(var != nullptr);
  return var->dropPromoted;
}

inline bool Position::shogi_doubled_pawn() const {
  assert(var != nullptr);
  return var->shogiDoubledPawn;
}

inline bool Position::immobility_illegal() const {
  assert(var != nullptr);
  return var->immobilityIllegal;
}

inline bool Position::flip_enclosed_pieces() const {
  assert(var != nullptr);
  return var->flipEnclosedPieces;
}

inline Value Position::stalemate_value(int ply) const {
  assert(var != nullptr);
  if (!var->stalematePieceCount)
      return convert_mate_value(var->stalemateValue, ply);
  int c = count<ALL_PIECES>(sideToMove) - count<ALL_PIECES>(~sideToMove);
  return c == 0 ? VALUE_DRAW : convert_mate_value(c < 0 ? var->stalemateValue : -var->stalemateValue, ply);
}

inline Value Position::checkmate_value(int ply) const {
  assert(var != nullptr);
  // Check for illegal mate by shogi pawn drop
  if (    var->shogiPawnDropMateIllegal
      && !(checkers() & ~pieces(SHOGI_PAWN))
      && !st->capturedPiece
      &&  st->pliesFromNull > 0
      && (st->materialKey != st->previous->materialKey))
  {
      return mate_in(ply);
  }
  // Check for shatar mate rule
  if (var->shatarMateRule)
  {
      // Mate by knight is illegal
      if (!(checkers() & ~pieces(KNIGHT)))
          return mate_in(ply);

      StateInfo* stp = st;
      while (stp->checkersBB)
      {
          // Return mate score if there is at least one shak in series of checks
          if (stp->shak)
              return convert_mate_value(var->checkmateValue, ply);

          if (stp->pliesFromNull < 2)
              break;

          stp = stp->previous->previous;
      }
      // Niol
      return VALUE_DRAW;
  }
  // Return mate value
  return convert_mate_value(var->checkmateValue, ply);
}

inline Value Position::bare_king_value(int ply) const {
  assert(var != nullptr);
  return convert_mate_value(var->bareKingValue, ply);
}

inline Value Position::extinction_value(int ply) const {
  assert(var != nullptr);
  return convert_mate_value(var->extinctionValue, ply);
}

inline bool Position::bare_king_move() const {
  assert(var != nullptr);
  return var->bareKingMove;
}

inline const std::set<PieceType>& Position::extinction_piece_types() const {
  assert(var != nullptr);
  return var->extinctionPieceTypes;
}

inline PieceType Position::capture_the_flag_piece() const {
  assert(var != nullptr);
  return var->flagPiece;
}

inline Bitboard Position::capture_the_flag(Color c) const {
  assert(var != nullptr);
  return c == WHITE ? var->whiteFlag : var->blackFlag;
}

inline bool Position::flag_move() const {
  assert(var != nullptr);
  return var->flagMove;
}

inline CheckCount Position::max_check_count() const {
  assert(var != nullptr);
  return var->maxCheckCount;
}

inline int Position::connect_n() const {
  assert(var != nullptr);
  return var->connectN;
}

inline CheckCount Position::checks_given(Color c) const {
  return st->checksGiven[c];
}

inline bool Position::is_variant_end() const {
  Value result;
  return is_variant_end(result);
}

inline bool Position::is_variant_end(Value& result, int ply) const {
  // bare king rule
  if (    bare_king_value() != VALUE_NONE
      && !bare_king_move()
      && !(count<ALL_PIECES>(sideToMove) - count<KING>(sideToMove)))
  {
      result = bare_king_value(ply);
      return true;
  }
  if (    bare_king_value() != VALUE_NONE
      &&  bare_king_move()
      && !(count<ALL_PIECES>(~sideToMove) - count<KING>(~sideToMove)))
  {
      result = -bare_king_value(ply);
      return true;
  }
  // extinction
  if (extinction_value() != VALUE_NONE)
  {
      for (PieceType pt : extinction_piece_types())
          if (!count(WHITE, pt) || !count(BLACK, pt))
          {
              result = !count(sideToMove, pt) ? extinction_value(ply) : -extinction_value(ply);
              return true;
          }
  }
  // capture the flag
  if (   capture_the_flag_piece()
      && !flag_move()
      && (capture_the_flag(~sideToMove) & pieces(~sideToMove, capture_the_flag_piece())))
  {
      result = mated_in(ply);
      return true;
  }
  if (   capture_the_flag_piece()
      && flag_move()
      && (capture_the_flag(sideToMove) & pieces(sideToMove, capture_the_flag_piece())))
  {
      result =  (capture_the_flag(~sideToMove) & pieces(~sideToMove, capture_the_flag_piece()))
              && sideToMove == WHITE ? VALUE_DRAW : mate_in(ply);
      return true;
  }
  // nCheck
  if (max_check_count() && st->checksGiven[~sideToMove] == max_check_count())
  {
      result = mated_in(ply);
      return true;
  }
  // Connect-n
  if (connect_n() > 0)
  {
      Bitboard b;
      for (Direction d : {NORTH, NORTH_EAST, EAST, SOUTH_EAST})
      {
          b = pieces(~sideToMove);
          for (int i = 1; i < connect_n() && b; i++)
              b &= shift(d, b);
          if (b)
          {
              result = mated_in(ply);
              return true;
          }
      }
  }
  return false;
}

inline Color Position::side_to_move() const {
  return sideToMove;
}

inline bool Position::empty(Square s) const {
  return board[s] == NO_PIECE;
}

inline Piece Position::piece_on(Square s) const {
  return board[s];
}

inline Piece Position::unpromoted_piece_on(Square s) const {
  return unpromotedBoard[s];
}

inline Piece Position::moved_piece(Move m) const {
  if (type_of(m) == DROP)
      return make_piece(sideToMove, dropped_piece_type(m));
  return board[from_sq(m)];
}

inline Bitboard Position::pieces() const {
  return byTypeBB[ALL_PIECES];
}

inline Bitboard Position::pieces(PieceType pt) const {
  return byTypeBB[pt];
}

inline Bitboard Position::pieces(PieceType pt1, PieceType pt2) const {
  return byTypeBB[pt1] | byTypeBB[pt2];
}

inline Bitboard Position::pieces(Color c) const {
  return byColorBB[c];
}

inline Bitboard Position::pieces(Color c, PieceType pt) const {
  return byColorBB[c] & byTypeBB[pt];
}

inline Bitboard Position::pieces(Color c, PieceType pt1, PieceType pt2) const {
  return byColorBB[c] & (byTypeBB[pt1] | byTypeBB[pt2]);
}

inline int Position::count(Color c, PieceType pt) const {
  return pieceCount[make_piece(c, pt)];
}

template<PieceType Pt> inline int Position::count(Color c) const {
  return pieceCount[make_piece(c, Pt)];
}

template<PieceType Pt> inline int Position::count() const {
  return pieceCount[make_piece(WHITE, Pt)] + pieceCount[make_piece(BLACK, Pt)];
}

template<PieceType Pt> inline const Square* Position::squares(Color c) const {
  return pieceList[make_piece(c, Pt)];
}

inline const Square* Position::squares(Color c, PieceType pt) const {
  return pieceList[make_piece(c, pt)];
}

template<PieceType Pt> inline Square Position::square(Color c) const {
  assert(pieceCount[make_piece(c, Pt)] == 1);
  return pieceList[make_piece(c, Pt)][0];
}

inline Square Position::ep_square() const {
  return st->epSquare;
}

inline int Position::can_castle(CastlingRight cr) const {
  return st->castlingRights & cr;
}

inline int Position::can_castle(Color c) const {
  return st->castlingRights & ((WHITE_OO | WHITE_OOO) << (2 * c));
}

inline bool Position::castling_impeded(CastlingRight cr) const {
  return byTypeBB[ALL_PIECES] & castlingPath[cr];
}

inline Square Position::castling_rook_square(CastlingRight cr) const {
  return castlingRookSquare[cr];
}

template<PieceType Pt>
inline Bitboard Position::attacks_from(Color c, Square s) const {
  return attacks_bb(c, Pt, s, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::attacks_from(Color c, PieceType pt, Square s) const {
  return attacks_bb(c, pt, s, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::moves_from(Color c, PieceType pt, Square s) const {
  return moves_bb(c, pt, s, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::attackers_to(Square s) const {
  return attackers_to(s, byTypeBB[ALL_PIECES]);
}

inline Bitboard Position::attackers_to(Square s, Color c) const {
  return attackers_to(s, byTypeBB[ALL_PIECES], c);
}

inline Bitboard Position::checkers() const {
  return st->checkersBB;
}

inline Bitboard Position::blockers_for_king(Color c) const {
  return st->blockersForKing[c];
}

inline Bitboard Position::check_squares(PieceType pt) const {
  return st->checkSquares[pt];
}

inline bool Position::pawn_passed(Color c, Square s) const {
  return !(pieces(~c, PAWN) & passed_pawn_mask(c, s));
}

inline bool Position::advanced_pawn_push(Move m) const {
  return   type_of(moved_piece(m)) == PAWN
        && relative_rank(sideToMove, from_sq(m), max_rank()) > (max_rank() + 1) / 2 - 1;
}

inline Key Position::key() const {
  return st->key;
}

inline Key Position::pawn_key() const {
  return st->pawnKey;
}

inline Key Position::material_key() const {
  return st->materialKey;
}

inline Score Position::psq_score() const {
  return st->psq;
}

inline Value Position::non_pawn_material(Color c) const {
  return st->nonPawnMaterial[c];
}

inline Value Position::non_pawn_material() const {
  return st->nonPawnMaterial[WHITE] + st->nonPawnMaterial[BLACK];
}

inline int Position::game_ply() const {
  return gamePly;
}

inline int Position::rule50_count() const {
  return st->rule50;
}

inline bool Position::opposite_bishops() const {
  return   pieceCount[make_piece(WHITE, BISHOP)] == 1
        && pieceCount[make_piece(BLACK, BISHOP)] == 1
        && opposite_colors(square<BISHOP>(WHITE), square<BISHOP>(BLACK));
}

inline bool Position::is_promoted(Square s) const {
  return promotedPieces & s;
}

inline bool Position::is_chess960() const {
  return chess960;
}

inline bool Position::capture_or_promotion(Move m) const {
  assert(is_ok(m));
  return type_of(m) != NORMAL && type_of(m) != PIECE_PROMOTION ? type_of(m) != DROP && type_of(m) != CASTLING : !empty(to_sq(m));
}

inline bool Position::capture(Move m) const {
  assert(is_ok(m));
  // Castling is encoded as "king captures rook"
  return (!empty(to_sq(m)) && type_of(m) != CASTLING) || type_of(m) == ENPASSANT;
}

inline Piece Position::captured_piece() const {
  return st->capturedPiece;
}

inline Thread* Position::this_thread() const {
  return thisThread;
}

inline void Position::put_piece(Piece pc, Square s) {

  board[s] = pc;
  byTypeBB[ALL_PIECES] |= s;
  byTypeBB[type_of(pc)] |= s;
  byColorBB[color_of(pc)] |= s;
  index[s] = pieceCount[pc]++;
  pieceList[pc][index[s]] = s;
  pieceCount[make_piece(color_of(pc), ALL_PIECES)]++;
}

inline void Position::remove_piece(Piece pc, Square s) {

  // WARNING: This is not a reversible operation. If we remove a piece in
  // do_move() and then replace it in undo_move() we will put it at the end of
  // the list and not in its original place, it means index[] and pieceList[]
  // are not invariant to a do_move() + undo_move() sequence.
  byTypeBB[ALL_PIECES] ^= s;
  byTypeBB[type_of(pc)] ^= s;
  byColorBB[color_of(pc)] ^= s;
  /* board[s] = NO_PIECE;  Not needed, overwritten by the capturing one */
  Square lastSquare = pieceList[pc][--pieceCount[pc]];
  index[lastSquare] = index[s];
  pieceList[pc][index[lastSquare]] = lastSquare;
  pieceList[pc][pieceCount[pc]] = SQ_NONE;
  pieceCount[make_piece(color_of(pc), ALL_PIECES)]--;
}

inline void Position::move_piece(Piece pc, Square from, Square to) {

  // index[from] is not updated and becomes stale. This works as long as index[]
  // is accessed just by known occupied squares.
  Bitboard fromTo = SquareBB[from] ^ SquareBB[to];
  byTypeBB[ALL_PIECES] ^= fromTo;
  byTypeBB[type_of(pc)] ^= fromTo;
  byColorBB[color_of(pc)] ^= fromTo;
  board[from] = NO_PIECE;
  board[to] = pc;
  index[to] = index[from];
  pieceList[pc][index[to]] = to;
}

inline void Position::do_move(Move m, StateInfo& newSt) {
  do_move(m, newSt, gives_check(m));
}

inline int Position::count_in_hand(Color c, PieceType pt) const {
  return pieceCountInHand[c][pt];
}

inline void Position::add_to_hand(Color c, PieceType pt) {
  pieceCountInHand[c][pt]++;
  pieceCountInHand[c][ALL_PIECES]++;
}

inline void Position::remove_from_hand(Color c, PieceType pt) {
  pieceCountInHand[c][pt]--;
  pieceCountInHand[c][ALL_PIECES]--;
}

inline void Position::drop_piece(Piece pc_hand, Piece pc_drop, Square s) {
  assert(pieceCountInHand[color_of(pc_hand)][type_of(pc_hand)]);
  put_piece(pc_drop, s);
  remove_from_hand(color_of(pc_hand), type_of(pc_hand));
}

inline void Position::undrop_piece(Piece pc_hand, Piece pc_drop, Square s) {
  remove_piece(pc_drop, s);
  board[s] = NO_PIECE;
  add_to_hand(color_of(pc_hand), type_of(pc_hand));
  assert(pieceCountInHand[color_of(pc_hand)][type_of(pc_hand)]);
}

#endif // #ifndef POSITION_H_INCLUDED
