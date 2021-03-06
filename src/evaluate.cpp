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

#include <algorithm>
#include <cassert>
#include <cstring>   // For std::memset
#include <iomanip>
#include <sstream>

#include "bitboard.h"
#include "evaluate.h"
#include "material.h"
#include "pawns.h"
#include "thread.h"

namespace Trace {

  enum Tracing { NO_TRACE, TRACE };

  enum Term { // The first PIECE_TYPE_NB entries are reserved for PieceType
    MATERIAL = PIECE_TYPE_NB, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, INITIATIVE, VARIANT, TOTAL, TERM_NB
  };

  Score scores[TERM_NB][COLOR_NB];

  double to_cp(Value v) { return double(v) / PawnValueEg; }

  void add(int idx, Color c, Score s) {
    scores[idx][c] = s;
  }

  void add(int idx, Score w, Score b = SCORE_ZERO) {
    scores[idx][WHITE] = w;
    scores[idx][BLACK] = b;
  }

  std::ostream& operator<<(std::ostream& os, Score s) {
    os << std::setw(5) << to_cp(mg_value(s)) << " "
       << std::setw(5) << to_cp(eg_value(s));
    return os;
  }

  std::ostream& operator<<(std::ostream& os, Term t) {

    if (t == MATERIAL || t == IMBALANCE || t == INITIATIVE || t == TOTAL)
        os << " ----  ----"    << " | " << " ----  ----";
    else
        os << scores[t][WHITE] << " | " << scores[t][BLACK];

    os << " | " << scores[t][WHITE] - scores[t][BLACK] << "\n";
    return os;
  }
}

using namespace Trace;

namespace {

  constexpr Bitboard QueenSide   = FileABB | FileBBB | FileCBB | FileDBB;
  constexpr Bitboard CenterFiles = FileCBB | FileDBB | FileEBB | FileFBB;
  constexpr Bitboard KingSide    = FileEBB | FileFBB | FileGBB | FileHBB;
  constexpr Bitboard Center      = (FileDBB | FileEBB) & (Rank4BB | Rank5BB);

  constexpr Bitboard KingFlank[FILE_NB] = {
    QueenSide,   QueenSide, QueenSide,
    CenterFiles, CenterFiles,
    KingSide,    KingSide,  KingSide
  };

  // Threshold for space evaluation
  constexpr Value SpaceThreshold = Value(12222);

  // KingAttackWeights[PieceType] contains king attack weights by piece type
  constexpr int KingAttackWeights[PIECE_TYPE_NB] = { 0, 0, 77, 55, 44, 10 };

  // Penalties for enemy's safe checks
  constexpr int QueenSafeCheck  = 780;
  constexpr int RookSafeCheck   = 880;
  constexpr int BishopSafeCheck = 435;
  constexpr int KnightSafeCheck = 790;
  constexpr int OtherSafeCheck  = 600;

#define S(mg, eg) make_score(mg, eg)

  // MobilityBonus[PieceType-2][attacked] contains bonuses for middle and end game,
  // indexed by piece type and number of attacked squares in the mobility area.
#ifdef LARGEBOARDS
  constexpr Score MobilityBonus[][38] = {
#else
  constexpr Score MobilityBonus[][32] = {
#endif
    { S(-75,-76), S(-57,-54), S( -9,-28), S( -2,-10), S(  6,  5), S( 14, 12), // Knights
      S( 22, 26), S( 29, 29), S( 36, 29) },
    { S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42), // Bishops
      S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
      S( 91, 88), S( 98, 97) },
    { S(-58,-76), S(-27,-18), S(-15, 28), S(-10, 55), S( -5, 69), S( -2, 82), // Rooks
      S(  9,112), S( 16,118), S( 30,132), S( 29,142), S( 32,155), S( 38,165),
      S( 46,166), S( 48,169), S( 58,171) },
    { S(-39,-36), S(-21,-15), S(  3,  8), S(  3, 18), S( 14, 34), S( 22, 54), // Queens
      S( 28, 61), S( 41, 73), S( 43, 79), S( 48, 92), S( 56, 94), S( 60,104),
      S( 60,113), S( 66,120), S( 67,123), S( 70,126), S( 71,133), S( 73,136),
      S( 79,140), S( 88,143), S( 88,148), S( 99,166), S(102,170), S(102,175),
      S(106,184), S(109,191), S(113,206), S(116,212) }
  };
  constexpr Score MaxMobility  = S(300, 300);
  constexpr Score DropMobility = S(10, 10);

  // Outpost[knight/bishop][supported by pawn] contains bonuses for minor
  // pieces if they occupy or can reach an outpost square, bigger if that
  // square is supported by a pawn.
  constexpr Score Outpost[][2] = {
    { S(22, 6), S(36,12) }, // Knight
    { S( 9, 2), S(15, 5) }  // Bishop
  };

  // RookOnFile[semiopen/open] contains bonuses for each rook when there is
  // no (friendly) pawn on the rook file.
  constexpr Score RookOnFile[] = { S(20, 7), S(45, 20) };

  // ThreatByMinor/ByRook[attacked PieceType] contains bonuses according to
  // which piece type attacks which one. Attacks on lesser pieces which are
  // pawn-defended are not considered.
  constexpr Score ThreatByMinor[PIECE_TYPE_NB] = {
    S(0, 0), S(0, 31), S(39, 42), S(57, 44), S(68, 112), S(47, 120)
  };

  constexpr Score ThreatByRook[PIECE_TYPE_NB] = {
    S(0, 0), S(0, 24), S(38, 71), S(38, 61), S(0, 38), S(36, 38)
  };

  // ThreatByKing[on one/on many] contains bonuses for king attacks on
  // pawns or pieces which are not pawn-defended.
  constexpr Score ThreatByKing[] = { S(3, 65), S(9, 145) };

  // PassedRank[Rank] contains a bonus according to the rank of a passed pawn
  constexpr Score PassedRank[RANK_NB] = {
    S(0, 0), S(5, 7), S(5, 13), S(18, 23), S(74, 58), S(164, 166), S(268, 243)
  };

  // PassedFile[File] contains a bonus according to the file of a passed pawn
  constexpr Score PassedFile[FILE_NB] = {
    S( 15,  7), S(-5, 14), S( 1, -5), S(-22,-11),
    S(-22,-11), S( 1, -5), S(-5, 14), S( 15,  7)
  };

  // PassedDanger[Rank] contains a term to weight the passed score
  constexpr int PassedDanger[RANK_NB] = { 0, 0, 0, 3, 6, 12, 21 };

  // KingProtector[PieceType-2] contains a penalty according to distance from king
  constexpr Score KingProtector[PIECE_TYPE_NB - 2] = { S(3, 5), S(4, 3), S(3, 0), S(1, -1), S(2, 2) };

  // Assorted bonuses and penalties
  constexpr Score BishopPawns        = S(  3,  5);
  constexpr Score CloseEnemies       = S(  7,  0);
  constexpr Score Connectivity       = S(  3,  1);
  constexpr Score CorneredBishop     = S( 50, 50);
  constexpr Score Hanging            = S( 52, 30);
  constexpr Score HinderPassedPawn   = S(  8,  1);
  constexpr Score KnightOnQueen      = S( 21, 11);
  constexpr Score LongDiagonalBishop = S( 22,  0);
  constexpr Score MinorBehindPawn    = S( 16,  0);
  constexpr Score Overload           = S( 10,  5);
  constexpr Score PawnlessFlank      = S( 20, 80);
  constexpr Score RookOnPawn         = S(  8, 24);
  constexpr Score SliderOnQueen      = S( 42, 21);
  constexpr Score ThreatByPawnPush   = S( 47, 26);
  constexpr Score ThreatByRank       = S( 16,  3);
  constexpr Score ThreatBySafePawn   = S(175,168);
  constexpr Score TrappedRook        = S( 92,  0);
  constexpr Score WeakQueen          = S( 50, 10);
  constexpr Score WeakUnopposedPawn  = S(  5, 25);

#undef S

  // Evaluation class computes and stores attacks tables and other working data
  template<Tracing T>
  class Evaluation {

  public:
    Evaluation() = delete;
    explicit Evaluation(const Position& p) : pos(p) {}
    Evaluation& operator=(const Evaluation&) = delete;
    Value value();

  private:
    template<Color Us> void initialize();
    template<Color Us> Score pieces(PieceType Pt);
    template<Color Us> Score hand(PieceType pt);
    template<Color Us> Score king() const;
    template<Color Us> Score threats() const;
    template<Color Us> Score passed() const;
    template<Color Us> Score space() const;
    template<Color Us> Score variant() const;
    ScaleFactor scale_factor(Value eg) const;
    Score initiative(Value eg) const;

    const Position& pos;
    Material::Entry* me;
    Pawns::Entry* pe;
    Bitboard mobilityArea[COLOR_NB];
    Score mobility[COLOR_NB] = { SCORE_ZERO, SCORE_ZERO };

    // attackedBy[color][piece type] is a bitboard representing all squares
    // attacked by a given color and piece type. Special "piece types" which
    // is also calculated is ALL_PIECES.
    Bitboard attackedBy[COLOR_NB][PIECE_TYPE_NB];

    // attackedBy2[color] are the squares attacked by 2 pieces of a given color,
    // possibly via x-ray or by one pawn and one piece. Diagonal x-ray through
    // pawn or squares attacked by 2 pawns are not explicitly added.
    Bitboard attackedBy2[COLOR_NB];

    // kingRing[color] are the squares adjacent to the king, plus (only for a
    // king on its first rank) the squares two ranks in front. For instance,
    // if black's king is on g8, kingRing[BLACK] is f8, h8, f7, g7, h7, f6, g6
    // and h6. It is set to 0 when king safety evaluation is skipped.
    Bitboard kingRing[COLOR_NB];

    // kingAttackersCount[color] is the number of pieces of the given color
    // which attack a square in the kingRing of the enemy king.
    int kingAttackersCount[COLOR_NB];

    // kingAttackersWeight[color] is the sum of the "weights" of the pieces of
    // the given color which attack a square in the kingRing of the enemy king.
    // The weights of the individual piece types are given by the elements in
    // the KingAttackWeights array.
    int kingAttackersWeight[COLOR_NB];

    // kingAttacksCount[color] is the number of attacks by the given color to
    // squares directly adjacent to the enemy king. Pieces which attack more
    // than one square are counted multiple times. For instance, if there is
    // a white knight on g5 and black's king is on g8, this white knight adds 2
    // to kingAttacksCount[WHITE].
    int kingAttacksCount[COLOR_NB];
  };


  // Evaluation::initialize() computes king and pawn attacks, and the king ring
  // bitboard for a given color. This is done at the beginning of the evaluation.
  template<Tracing T> template<Color Us>
  void Evaluation<T>::initialize() {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Up   = (Us == WHITE ? NORTH : SOUTH);
    constexpr Direction Down = (Us == WHITE ? SOUTH : NORTH);
    Bitboard LowRanks = rank_bb(relative_rank(Us, RANK_2, pos.max_rank())) | rank_bb(relative_rank(Us, RANK_3, pos.max_rank()));

    // Find our pawns that are blocked or on the first two ranks
    Bitboard b = pos.pieces(Us, PAWN) & (shift<Down>(pos.pieces()) | LowRanks);

    // Squares occupied by those pawns, by our king or queen, or controlled by enemy pawns
    // are excluded from the mobility area.
    if (pos.must_capture())
        mobilityArea[Us] = AllSquares;
    else
        mobilityArea[Us] = ~(b | pos.pieces(Us, KING, QUEEN) | pe->pawn_attacks(Them) | shift<Down>(pos.pieces(Them, SHOGI_PAWN)));

    // Initialise attackedBy bitboards for kings and pawns
    attackedBy[Us][KING] = pos.count<KING>(Us) ? pos.attacks_from<KING>(Us, pos.square<KING>(Us)) : 0;
    attackedBy[Us][PAWN] = pe->pawn_attacks(Us);
    attackedBy[Us][ALL_PIECES] = attackedBy[Us][KING] | attackedBy[Us][PAWN];
    attackedBy2[Us]            = attackedBy[Us][KING] & attackedBy[Us][PAWN];

    // Init our king safety tables only if we are going to use them
    if ((pos.count<KING>(Us) && pos.non_pawn_material(Them) >= RookValueMg + KnightValueMg) || pos.captures_to_hand())
    {
        kingRing[Us] = attackedBy[Us][KING];
        if (relative_rank(Us, pos.square<KING>(Us), pos.max_rank()) == RANK_1)
            kingRing[Us] |= shift<Up>(kingRing[Us]);

        if (file_of(pos.square<KING>(Us)) == pos.max_file())
            kingRing[Us] |= shift<WEST>(kingRing[Us]);

        else if (file_of(pos.square<KING>(Us)) == FILE_A)
            kingRing[Us] |= shift<EAST>(kingRing[Us]);

        kingRing[Us] &= pos.board_bb();

        kingAttackersCount[Them] = popcount(kingRing[Us] & pe->pawn_attacks(Them));
        kingAttacksCount[Them] = kingAttackersWeight[Them] = 0;
    }
    else
        kingRing[Us] = kingAttackersCount[Them] = 0;
  }


  // Evaluation::pieces() scores pieces of a given color and type
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::pieces(PieceType Pt) {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Down = (Us == WHITE ? SOUTH : NORTH);
    constexpr Bitboard OutpostRanks = (Us == WHITE ? Rank4BB | Rank5BB | Rank6BB
                                                   : Rank5BB | Rank4BB | Rank3BB);
    const Square* pl = pos.squares(Us, Pt);

    Bitboard b, bb;
    Square s;
    Score score = SCORE_ZERO;

    attackedBy[Us][Pt] = 0;

    while ((s = *pl++) != SQ_NONE)
    {
        // Find attacked squares, including x-ray attacks for bishops and rooks
        b = Pt == BISHOP ? attacks_bb<BISHOP>(s, pos.pieces() ^ pos.pieces(QUEEN))
          : Pt ==   ROOK ? attacks_bb<  ROOK>(s, pos.pieces() ^ pos.pieces(QUEEN) ^ pos.pieces(Us, ROOK))
                         : (  (pos.attacks_from(Us, Pt, s) & pos.pieces())
                            | (pos.moves_from(Us, Pt, s) & ~pos.pieces()));

        // Restrict mobility to actual squares of board
        b &= pos.board_bb();

        if (pos.blockers_for_king(Us) & s)
            b &= LineBB[pos.square<KING>(Us)][s];

        attackedBy2[Us] |= attackedBy[Us][ALL_PIECES] & b;
        attackedBy[Us][Pt] |= b;
        attackedBy[Us][ALL_PIECES] |= b;

        if (b & kingRing[Them])
        {
            kingAttackersCount[Us]++;
            kingAttackersWeight[Us] += KingAttackWeights[std::min(Pt, QUEEN)];
            kingAttacksCount[Us] += popcount(b & attackedBy[Them][KING]);
        }

        int mob = popcount(b & mobilityArea[Us]);

        if (Pt <= QUEEN)
            mobility[Us] += MobilityBonus[Pt - 2][mob];
        else
            mobility[Us] += MaxMobility * (mob - 1) / (10 + mob);

        // Piece promotion bonus
        if (pos.promoted_piece_type(Pt) != NO_PIECE_TYPE)
        {
            if (promotion_zone_bb(Us, pos.promotion_rank(), pos.max_rank()) & (b | s))
                score += make_score(PieceValue[MG][pos.promoted_piece_type(Pt)] - PieceValue[MG][Pt],
                                    PieceValue[EG][pos.promoted_piece_type(Pt)] - PieceValue[EG][Pt]) / 10;
        }
        else if (pos.captures_to_hand() && pos.unpromoted_piece_on(s))
            score += make_score(PieceValue[MG][Pt] - PieceValue[MG][pos.unpromoted_piece_on(s)],
                                PieceValue[EG][Pt] - PieceValue[EG][pos.unpromoted_piece_on(s)]) / 8;

        // Penalty if the piece is far from the king
        if (pos.count<KING>(Us))
        {
            int dist = distance(s, pos.square<KING>(Us));
            if (pos.captures_to_hand() && pos.count<KING>(Them))
                dist *= distance(s, pos.square<KING>(Them));
            score -= KingProtector[std::min(Pt - 2, QUEEN - 1)] * dist;
        }

        if (Pt == BISHOP || Pt == KNIGHT)
        {
            // Bonus if piece is on an outpost square or can reach one
            bb = OutpostRanks & ~pe->pawn_attacks_span(Them);
            if (bb & s)
                score += Outpost[Pt == BISHOP][bool(attackedBy[Us][PAWN] & s)] * 2;

            else if (bb &= b & ~pos.pieces(Us))
                score += Outpost[Pt == BISHOP][bool(attackedBy[Us][PAWN] & bb)];

            // Bonus when behind a pawn
            if (    relative_rank(Us, s, pos.max_rank()) < RANK_5
                && (pos.pieces(PAWN) & (s + pawn_push(Us))))
                score += MinorBehindPawn;

            if (Pt == BISHOP)
            {
                // Penalty according to number of pawns on the same color square as the
                // bishop, bigger when the center files are blocked with pawns.
                Bitboard blocked = pos.pieces(Us, PAWN) & shift<Down>(pos.pieces());

                score -= BishopPawns * pe->pawns_on_same_color_squares(Us, s)
                                     * (1 + popcount(blocked & CenterFiles));

                // Bonus for bishop on a long diagonal which can "see" both center squares
                if (more_than_one(Center & (attacks_bb<BISHOP>(s, pos.pieces(PAWN)) | s)))
                    score += LongDiagonalBishop;
            }

            // An important Chess960 pattern: A cornered bishop blocked by a friendly
            // pawn diagonally in front of it is a very serious problem, especially
            // when that pawn is also blocked.
            if (   Pt == BISHOP
                && pos.is_chess960()
                && (s == relative_square(Us, SQ_A1) || s == relative_square(Us, SQ_H1)))
            {
                Direction d = pawn_push(Us) + (file_of(s) == FILE_A ? EAST : WEST);
                if (pos.piece_on(s + d) == make_piece(Us, PAWN))
                    score -= !pos.empty(s + d + pawn_push(Us))                ? CorneredBishop * 4
                            : pos.piece_on(s + d + d) == make_piece(Us, PAWN) ? CorneredBishop * 2
                                                                              : CorneredBishop;
            }
        }

        if (Pt == ROOK)
        {
            // Bonus for aligning rook with enemy pawns on the same rank/file
            if (relative_rank(Us, s, pos.max_rank()) >= RANK_5)
                score += RookOnPawn * popcount(pos.pieces(Them, PAWN) & PseudoAttacks[Us][ROOK][s]);

            // Bonus for rook on an open or semi-open file
            if (pe->semiopen_file(Us, file_of(s)))
                score += RookOnFile[bool(pe->semiopen_file(Them, file_of(s)))];

            // Penalty when trapped by the king, even more if the king cannot castle
            else if (mob <= 3 && pos.count<KING>(Us))
            {
                File kf = file_of(pos.square<KING>(Us));
                if ((kf < FILE_E) == (file_of(s) < kf))
                    score -= (TrappedRook - make_score(mob * 22, 0)) * (1 + !pos.can_castle(Us));
            }
        }

        if (Pt == QUEEN)
        {
            // Penalty if any relative pin or discovered attack against the queen
            Bitboard queenPinners;
            if (pos.slider_blockers(pos.pieces(Them, ROOK, BISHOP), s, queenPinners))
                score -= WeakQueen;
        }
    }
    if (T)
        Trace::add(Pt, Us, score);

    return score;
  }

  // Evaluation::hand() scores pieces of a given color and type in hand
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::hand(PieceType pt) {

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);

    Score score = SCORE_ZERO;

    if (pos.count_in_hand(Us, pt))
    {
        Bitboard b = pos.drop_region(Us, pt) & ~pos.pieces() & (~attackedBy2[Them] | attackedBy[Us][ALL_PIECES]);
        if ((b & kingRing[Them]) && pt != SHOGI_PAWN)
        {
            kingAttackersCount[Us] += pos.count_in_hand(Us, pt);
            kingAttackersWeight[Us] += KingAttackWeights[std::min(pt, QUEEN)] * pos.count_in_hand(Us, pt);
            kingAttacksCount[Us] += popcount(b & attackedBy[Them][KING]);
        }
        Bitboard theirHalf = pos.board_bb() & ~forward_ranks_bb(Them, relative_rank(Them, Rank((pos.max_rank() - 1) / 2), pos.max_rank()));
        mobility[Us] += DropMobility * popcount(b & theirHalf & ~attackedBy[Them][ALL_PIECES]);
    }

    return score;
  }

  // Evaluation::king() assigns bonuses and penalties to a king of a given color
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::king() const {

    constexpr Color    Them = (Us == WHITE ? BLACK : WHITE);
    Rank r = relative_rank(Us, std::min(Rank((pos.max_rank() - 1) / 2 + 1), pos.max_rank()), pos.max_rank());
    Bitboard Camp = AllSquares ^ forward_ranks_bb(Us, r);

    if (!pos.count<KING>(Us) || !pos.checking_permitted())
        return SCORE_ZERO;

    const Square ksq = pos.square<KING>(Us);
    Bitboard weak, b, b1, b2, safe, unsafeChecks;

    // King shelter and enemy pawns storm
    Score score = pe->king_safety<Us>(pos, ksq);

    // Main king safety evaluation
    if ((kingAttackersCount[Them] > 1 - pos.count<QUEEN>(Them)) || pos.captures_to_hand())
    {
        int kingDanger = 0;
        unsafeChecks = 0;

        // Attacked squares defended at most once by our queen or king
        weak =  attackedBy[Them][ALL_PIECES]
              & ~attackedBy2[Us]
              & (~attackedBy[Us][ALL_PIECES] | attackedBy[Us][KING] | attackedBy[Us][QUEEN]);

        // Analyse the safe enemy's checks which are possible on next move
        safe  = ~pos.pieces(Them);
        safe &= ~attackedBy[Us][ALL_PIECES] | (weak & attackedBy2[Them]);

        std::function <Bitboard (Color, PieceType)> get_attacks = [this](Color c, PieceType pt) {
            return attackedBy[c][pt] | (pos.captures_to_hand() && pos.count_in_hand(c, pt) ? ~pos.pieces() : 0);
        };
        for (PieceType pt : pos.piece_types())
        {
            switch (pt)
            {
            case QUEEN:
                b = attacks_bb(Us, pt, ksq, pos.pieces() ^ pos.pieces(Us, QUEEN)) & get_attacks(Them, pt) & safe & ~attackedBy[Us][QUEEN] & pos.board_bb();
                if (b)
                    kingDanger += QueenSafeCheck;
                break;
            case ROOK:
            case BISHOP:
            case KNIGHT:
                b = attacks_bb(Us, pt, ksq, pos.pieces() ^ pos.pieces(Us, QUEEN)) & get_attacks(Them, pt) & pos.board_bb();
                if (b & safe)
                    kingDanger +=  pt == ROOK   ? RookSafeCheck
                                 : pt == BISHOP ? BishopSafeCheck
                                                : KnightSafeCheck;
                else
                    unsafeChecks |= b;
                break;
            case PAWN:
                if (pos.captures_to_hand() && pos.count_in_hand(Them, pt))
                {
                    b = attacks_bb(Us, pt, ksq, pos.pieces()) & ~pos.pieces() & pos.board_bb();
                    if (b & safe)
                        kingDanger += OtherSafeCheck;
                    else
                        unsafeChecks |= b;
                }
                break;
            case SHOGI_PAWN:
            case KING:
                break;
            default:
                b = attacks_bb(Us, pt, ksq, pos.pieces()) & get_attacks(Them, pt) & pos.board_bb();
                if (b & safe)
                    kingDanger += OtherSafeCheck;
                else
                    unsafeChecks |= b;
            }
        }

        if (pos.max_check_count())
            kingDanger *= 2;

        // Unsafe or occupied checking squares will also be considered, as long as
        // the square is in the attacker's mobility area.
        unsafeChecks &= mobilityArea[Them];

        kingDanger +=        kingAttackersCount[Them] * kingAttackersWeight[Them]
                     + 102 * kingAttacksCount[Them] * (1 + pos.captures_to_hand() + !!pos.max_check_count())
                     + 191 * popcount(kingRing[Us] & weak) * (1 + pos.captures_to_hand() + !!pos.max_check_count())
                     + 143 * popcount(pos.blockers_for_king(Us) | unsafeChecks)
                     - 848 * !(pos.count<QUEEN>(Them) || pos.captures_to_hand()) / (1 + !!pos.max_check_count())
                     -   9 * mg_value(score) / 8
                     +  40;

        // Transform the kingDanger units into a Score, and subtract it from the evaluation
        if (kingDanger > 0)
        {
            int mobilityDanger = mg_value(mobility[Them] - mobility[Us]);
            kingDanger = std::max(0, kingDanger + mobilityDanger);
            score -= make_score(std::min(kingDanger * kingDanger / 4096, 3000), kingDanger / 16);
        }
    }

    File f = std::max(std::min(file_of(ksq), File(pos.max_file() - 1)), FILE_B);
    Bitboard kf = pos.max_file() == FILE_H ? KingFlank[f] : file_bb(f) | adjacent_files_bb(f);

    // Penalty when our king is on a pawnless flank
    if (!(pos.pieces(PAWN) & kf))
        score -= PawnlessFlank;

    // Find the squares that opponent attacks in our king flank, and the squares
    // which are attacked twice in that flank but not defended by our pawns.
    b1 = attackedBy[Them][ALL_PIECES] & kf & Camp;
    b2 = b1 & attackedBy2[Them] & ~(attackedBy[Us][PAWN] | attackedBy[Us][SHOGI_PAWN]);

    // King tropism, to anticipate slow motion attacks on our king
    score -= CloseEnemies * (popcount(b1) + popcount(b2)) * (1 + pos.captures_to_hand() + !!pos.max_check_count());

    // For drop games, king danger is independent of game phase
    if (pos.captures_to_hand())
        score = make_score(mg_value(score), mg_value(score)) / (1 + 2 * !pos.shogi_doubled_pawn());

    if (T)
        Trace::add(KING, Us, score);

    return score;
  }


  // Evaluation::threats() assigns bonuses according to the types of the
  // attacking and the attacked pieces.
  template<Tracing T> template<Color Us>
  Score Evaluation<T>::threats() const {

    constexpr Color     Them     = (Us == WHITE ? BLACK   : WHITE);
    constexpr Direction Up       = (Us == WHITE ? NORTH   : SOUTH);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);

    Bitboard b, weak, defended, nonPawnEnemies, stronglyProtected, safeThreats;
    Score score = SCORE_ZERO;

    // Bonuses for variants with mandatory captures
    if (pos.must_capture())
    {
        // Penalties for possible captures
        score -= make_score(100, 100) * popcount(attackedBy[Us][ALL_PIECES] & pos.pieces(Them));

        // Bonus if we threaten to force captures
        Bitboard moves = 0, piecebb = pos.pieces(Us);
        while (piecebb)
        {
            Square s = pop_lsb(&piecebb);
            if (type_of(pos.piece_on(s)) != KING)
                moves |= pos.moves_from(Us, type_of(pos.piece_on(s)), s);
        }
        score += make_score(200, 200) * popcount(attackedBy[Them][ALL_PIECES] & moves & ~pos.pieces());
        score += make_score(200, 200) * popcount(attackedBy[Them][ALL_PIECES] & moves & ~pos.pieces() & ~attackedBy2[Us]);
    }

    // Non-pawn enemies
    nonPawnEnemies = pos.pieces(Them) ^ pos.pieces(Them, PAWN, SHOGI_PAWN);

    // Squares strongly protected by the enemy, either because they defend the
    // square with a pawn, or because they defend the square twice and we don't.
    stronglyProtected =  attackedBy[Them][PAWN]
                       | (attackedBy2[Them] & ~attackedBy2[Us]);

    // Non-pawn enemies, strongly protected
    defended = nonPawnEnemies & stronglyProtected;

    // Enemies not strongly protected and under our attack
    weak = pos.pieces(Them) & ~stronglyProtected & attackedBy[Us][ALL_PIECES];

    // Bonus according to the kind of attacking pieces
    if (defended | weak)
    {
        b = (defended | weak) & (attackedBy[Us][KNIGHT] | attackedBy[Us][BISHOP]);
        while (b)
        {
            Square s = pop_lsb(&b);
            score += ThreatByMinor[type_of(pos.piece_on(s))];
            if (type_of(pos.piece_on(s)) != PAWN && type_of(pos.piece_on(s)) != SHOGI_PAWN)
                score += ThreatByRank * (int)relative_rank(Them, s, pos.max_rank());
        }

        b = (pos.pieces(Them, QUEEN) | weak) & attackedBy[Us][ROOK];
        while (b)
        {
            Square s = pop_lsb(&b);
            score += ThreatByRook[type_of(pos.piece_on(s))];
            if (type_of(pos.piece_on(s)) != PAWN && type_of(pos.piece_on(s)) != SHOGI_PAWN)
                score += ThreatByRank * (int)relative_rank(Them, s, pos.max_rank());
        }

        b = weak & attackedBy[Us][KING];
        if (b)
            score += ThreatByKing[more_than_one(b)];

        score += Hanging * popcount(weak & ~attackedBy[Them][ALL_PIECES]);

        // Bonus for overload (non-pawn enemies attacked and defended exactly once)
        b =  nonPawnEnemies
           & attackedBy[Us][ALL_PIECES]   & ~attackedBy2[Us]
           & attackedBy[Them][ALL_PIECES] & ~attackedBy2[Them];
        score += Overload * popcount(b);
    }

    // Bonus for enemy unopposed weak pawns
    if (pos.pieces(Us, ROOK, QUEEN))
        score += WeakUnopposedPawn * pe->weak_unopposed(Them);

    // Our safe or protected pawns
    b =   pos.pieces(Us, PAWN)
       & (~attackedBy[Them][ALL_PIECES] | attackedBy[Us][ALL_PIECES]);

    safeThreats = (pawn_attacks_bb<Us>(b) | shift<Up>(pos.pieces(Us, SHOGI_PAWN))) & nonPawnEnemies;
    score += ThreatBySafePawn * popcount(safeThreats);

    // Find squares where our pawns can push on the next move
    b  = shift<Up>(pos.pieces(Us, PAWN)) & ~pos.pieces();
    b |= shift<Up>(b & TRank3BB) & ~pos.pieces();

    // Keep only the squares which are not completely unsafe
    b &= ~attackedBy[Them][PAWN]
        & (attackedBy[Us][ALL_PIECES] | ~attackedBy[Them][ALL_PIECES]);

    // Bonus for safe pawn threats on the next move
    b =   pawn_attacks_bb<Us>(b)
       &  pos.pieces(Them)
       & ~attackedBy[Us][PAWN];

    score += ThreatByPawnPush * popcount(b);

    // Bonus for threats on the next moves against enemy queen
    if (pos.count<QUEEN>(Them) == 1)
    {
        Square s = pos.square<QUEEN>(Them);
        safeThreats = mobilityArea[Us] & ~stronglyProtected;

        b = attackedBy[Us][KNIGHT] & pos.attacks_from<KNIGHT>(Us, s);

        score += KnightOnQueen * popcount(b & safeThreats);

        b =  (attackedBy[Us][BISHOP] & pos.attacks_from<BISHOP>(Us, s))
           | (attackedBy[Us][ROOK  ] & pos.attacks_from<ROOK  >(Us, s));

        score += SliderOnQueen * popcount(b & safeThreats & attackedBy2[Us]);
    }

    // Connectivity: ensure that knights, bishops, rooks, and queens are protected
    b = (pos.pieces(Us) ^ pos.pieces(Us, PAWN, KING) ^ pos.pieces(Us, SHOGI_PAWN)) & attackedBy[Us][ALL_PIECES];
    score += Connectivity * popcount(b) * (1 + 2 * pos.captures_to_hand());

    if (T)
        Trace::add(THREAT, Us, score);

    return score;
  }

  // Evaluation::passed() evaluates the passed pawns and candidate passed
  // pawns of the given color.

  template<Tracing T> template<Color Us>
  Score Evaluation<T>::passed() const {

    constexpr Color     Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Direction Up   = (Us == WHITE ? NORTH : SOUTH);

    auto king_proximity = [&](Color c, Square s) {
      return pos.count<KING>(c) ? std::min(distance(pos.square<KING>(c), s), 5) : 5;
    };

    Bitboard b, bb, squaresToQueen, defendedSquares, unsafeSquares;
    Score score = SCORE_ZERO;

    b = pe->passed_pawns(Us);

    while (b)
    {
        Square s = pop_lsb(&b);

        assert(!(pos.pieces(Them, PAWN) & forward_file_bb(Us, s + Up)));

        bb = forward_file_bb(Us, s) & (attackedBy[Them][ALL_PIECES] | pos.pieces(Them));
        score -= HinderPassedPawn * popcount(bb);

        int r = relative_rank(Us, s, pos.max_rank());
        int w = PassedDanger[r];

        Score bonus = PassedRank[r];

        if (w)
        {
            Square blockSq = s + Up;

            // Skip bonus for antichess variants
            if (pos.extinction_value() != VALUE_MATE)
            {
                // Adjust bonus based on the king's proximity
                bonus += make_score(0, (  king_proximity(Them, blockSq) * 5
                                        - king_proximity(Us,   blockSq) * 2) * w);

                // If blockSq is not the queening square then consider also a second push
                if (r != RANK_7)
                    bonus -= make_score(0, king_proximity(Us, blockSq + Up) * w);
            }

            // If the pawn is free to advance, then increase the bonus
            if (pos.empty(blockSq))
            {
                // If there is a rook or queen attacking/defending the pawn from behind,
                // consider all the squaresToQueen. Otherwise consider only the squares
                // in the pawn's path attacked or occupied by the enemy.
                defendedSquares = unsafeSquares = squaresToQueen = forward_file_bb(Us, s);

                bb = forward_file_bb(Them, s) & pos.pieces(ROOK, QUEEN) & pos.attacks_from<ROOK>(Us, s);

                if (!(pos.pieces(Us) & bb))
                    defendedSquares &= attackedBy[Us][ALL_PIECES];

                if (!(pos.pieces(Them) & bb))
                    unsafeSquares &= attackedBy[Them][ALL_PIECES] | pos.pieces(Them);

                // If there aren't any enemy attacks, assign a big bonus. Otherwise
                // assign a smaller bonus if the block square isn't attacked.
                int k = !unsafeSquares ? 20 : !(unsafeSquares & blockSq) ? 9 : 0;

                // If the path to the queen is fully defended, assign a big bonus.
                // Otherwise assign a smaller bonus if the block square is defended.
                if (defendedSquares == squaresToQueen)
                    k += 6;

                else if (defendedSquares & blockSq)
                    k += 4;

                bonus += make_score(k * w, k * w);
            }
            else if (pos.pieces(Us) & blockSq)
                bonus += make_score(w + r * 2, w + r * 2);
        } // w != 0

        // Scale down bonus for candidate passers which need more than one
        // pawn push to become passed, or have a pawn in front of them.
        if (   !pos.pawn_passed(Us, s + Up)
            || (pos.pieces(PAWN) & forward_file_bb(Us, s)))
            bonus = bonus / 2;

        score += bonus + PassedFile[file_of(s)];
    }

    // Scale by maximum promotion piece value
    Value maxMg = VALUE_ZERO, maxEg = VALUE_ZERO;
    for (PieceType pt : pos.promotion_piece_types())
    {
        maxMg = std::max(maxMg, PieceValue[MG][pt]);
        maxEg = std::max(maxEg, PieceValue[EG][pt]);
    }
    score = make_score(mg_value(score) * int(maxMg) / QueenValueMg,
                       eg_value(score) * int(maxEg) / QueenValueEg);

    if (T)
        Trace::add(PASSED, Us, score);

    return score;
  }


  // Evaluation::space() computes the space evaluation for a given side. The
  // space evaluation is a simple bonus based on the number of safe squares
  // available for minor pieces on the central four files on ranks 2--4. Safe
  // squares one, two or three squares behind a friendly pawn are counted
  // twice. Finally, the space bonus is multiplied by a weight. The aim is to
  // improve play on game opening.

  template<Tracing T> template<Color Us>
  Score Evaluation<T>::space() const {

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);
    constexpr Bitboard SpaceMask =
      Us == WHITE ? CenterFiles & (Rank2BB | Rank3BB | Rank4BB)
                  : CenterFiles & (Rank7BB | Rank6BB | Rank5BB);

    bool pawnsOnly = !(pos.pieces(Us) ^ pos.pieces(Us, PAWN));

    if (pos.non_pawn_material() < SpaceThreshold && !pos.captures_to_hand() && !pawnsOnly)
        return SCORE_ZERO;

    // Find the available squares for our pieces inside the area defined by SpaceMask
    Bitboard safe =   SpaceMask
                   & ~pos.pieces(Us, PAWN, SHOGI_PAWN)
                   & ~attackedBy[Them][PAWN]
                   & ~attackedBy[Them][SHOGI_PAWN];

    if (pawnsOnly)
        safe = pos.pieces(Us, PAWN) & ~attackedBy[Them][ALL_PIECES];

    // Find all squares which are at most three squares behind some friendly pawn
    Bitboard behind = pos.pieces(Us, PAWN, SHOGI_PAWN);
    behind |= (Us == WHITE ? behind >> NORTH : behind << NORTH);
    behind |= (Us == WHITE ? behind >> (2 * NORTH) : behind << (2 * NORTH));


    int bonus = popcount(safe) + popcount(behind & safe);
    int weight = pos.count<ALL_PIECES>(Us) - 2 * pe->open_files();

    Score score = make_score(bonus * weight * weight / 16, 0);

    if (T)
        Trace::add(SPACE, Us, score);

    return score;
  }


  // Evaluation::variant() computes variant-specific evaluation bonuses for a given side.

  template<Tracing T> template<Color Us>
  Score Evaluation<T>::variant() const {

    constexpr Color Them = (Us == WHITE ? BLACK : WHITE);

    Score score = SCORE_ZERO;

    // Capture the flag
    if (pos.capture_the_flag(Us))
    {
        bool isKingCTF = pos.capture_the_flag_piece() == KING;
        Bitboard ctfPieces = pos.pieces(Us, pos.capture_the_flag_piece());
        int scale = pos.count(Us, pos.capture_the_flag_piece());
        while (ctfPieces)
        {
            Square s1 = pop_lsb(&ctfPieces);
            Bitboard target_squares = pos.capture_the_flag(Us);
            while (target_squares)
            {
                Square s2 = pop_lsb(&target_squares);
                int dist =  distance(s1, s2)
                          + (isKingCTF ? popcount(pos.attackers_to(s2) & pos.pieces(Them)) : 0)
                          + !!(pos.pieces(Us) & s2);
                score += make_score(2500, 2500) / (1 + scale * dist * (!isKingCTF || pos.checking_permitted() ? dist : 1));
            }
        }
    }

    // nCheck
    if (pos.max_check_count())
    {
        int remainingChecks = pos.max_check_count() - pos.checks_given(Us);
        assert(remainingChecks > 0);
        score += make_score(3000, 1000) / (remainingChecks * remainingChecks);
    }

    // Connect-n
    if (pos.connect_n() > 0)
    {
        for (Direction d : {NORTH, NORTH_EAST, EAST, SOUTH_EAST, SOUTH, SOUTH_WEST, WEST, NORTH_WEST})
        {
            // Bonus for uninterrupted rows
            Bitboard b = pos.pieces(Us);
            for (int i = 1; i < pos.connect_n() && b; i++)
            {
                score += make_score(100, 100) * popcount(b) * i * i / (pos.connect_n() - i);
                b &= shift(-d, shift(d, shift(d, b)) & ~pos.pieces(Them) & pos.board_bb());
            }
            // Bonus for rows containing holes
            b = pos.pieces(Us);
            for (int i = 1; i < pos.connect_n() && b; i++)
            {
                score += make_score(50, 50) * popcount(b) * i * i / (pos.connect_n() - i);
                b &= shift(-d, shift(d, shift(d, b)) & ~pos.pieces(Them) & pos.board_bb()) | shift(d, shift(d, b) & ~pos.pieces());
            }
        }
    }

    if (T)
        Trace::add(VARIANT, Us, score);

    return score;
  }


  // Evaluation::initiative() computes the initiative correction value
  // for the position. It is a second order bonus/malus based on the
  // known attacking/defending status of the players.

  template<Tracing T>
  Score Evaluation<T>::initiative(Value eg) const {

    // No initiative bonus for extinction variants
    if (pos.extinction_value() != VALUE_NONE || pos.captures_to_hand() || pos.connect_n())
      return SCORE_ZERO;

    int outflanking = !pos.count<KING>(WHITE) || !pos.count<KING>(BLACK) ? 0
                     :  distance<File>(pos.square<KING>(WHITE), pos.square<KING>(BLACK))
                      - distance<Rank>(pos.square<KING>(WHITE), pos.square<KING>(BLACK));

    bool pawnsOnBothFlanks =   (pos.pieces(PAWN) & QueenSide)
                            && (pos.pieces(PAWN) & KingSide);

    // Compute the initiative bonus for the attacking side
    int complexity =   8 * outflanking
                    +  8 * pe->pawn_asymmetry()
                    + 12 * pos.count<PAWN>()
                    + 16 * pawnsOnBothFlanks
                    + 48 * !pos.non_pawn_material()
                    -136 ;

    // Now apply the bonus: note that we find the attacking side by extracting
    // the sign of the endgame value, and that we carefully cap the bonus so
    // that the endgame score will never change sign after the bonus.
    int v = ((eg > 0) - (eg < 0)) * std::max(complexity, -abs(eg));

    if (T)
        Trace::add(INITIATIVE, make_score(0, v));

    return make_score(0, v);
  }


  // Evaluation::scale_factor() computes the scale factor for the winning side

  template<Tracing T>
  ScaleFactor Evaluation<T>::scale_factor(Value eg) const {

    Color strongSide = eg > VALUE_DRAW ? WHITE : BLACK;
    int sf = me->scale_factor(pos, strongSide);

    // If scale is not already specific, scale down the endgame via general heuristics
    if (sf == SCALE_FACTOR_NORMAL && !pos.captures_to_hand())
    {
        if (pos.opposite_bishops())
        {
            // Endgame with opposite-colored bishops and no other pieces is almost a draw
            if (   pos.non_pawn_material(WHITE) == BishopValueMg
                && pos.non_pawn_material(BLACK) == BishopValueMg)
                sf = 31;

            // Endgame with opposite-colored bishops, but also other pieces. Still
            // a bit drawish, but not as drawish as with only the two bishops.
            else
                sf = 46;
        }
        else
            sf = std::min(40 + 7 * pos.count<PAWN>(strongSide), sf);
    }

    return ScaleFactor(sf);
  }


  // Evaluation::value() is the main function of the class. It computes the various
  // parts of the evaluation and returns the value of the position from the point
  // of view of the side to move.

  template<Tracing T>
  Value Evaluation<T>::value() {

    assert(!pos.checkers());
    assert(!pos.is_immediate_game_end());

    // Probe the material hash table
    me = Material::probe(pos);

    // If we have a specialized evaluation function for the current material
    // configuration, call it and return.
    if (me->specialized_eval_exists())
        return me->evaluate(pos);

    // Initialize score by reading the incrementally updated scores included in
    // the position object (material + piece square tables) and the material
    // imbalance. Score is computed internally from the white point of view.
    Score score = pos.psq_score();
    if (T)
        Trace::add(MATERIAL, score);
    score += me->imbalance() + pos.this_thread()->contempt;

    // Probe the pawn hash table
    pe = Pawns::probe(pos);
    score += pe->pawn_score(WHITE) - pe->pawn_score(BLACK);

    // Main evaluation begins here

    initialize<WHITE>();
    initialize<BLACK>();

    // Pieces should be evaluated first (populate attack tables).
    // For unused piece types, we still need to set attack bitboard to zero.
    for (PieceType pt = KNIGHT; pt < KING; ++pt)
        score += pieces<WHITE>(pt) - pieces<BLACK>(pt);

    // Evaluate pieces in hand once attack tables are complete
    if (pos.piece_drops())
        for (PieceType pt = PAWN; pt < KING; ++pt)
            score += hand<WHITE>(pt) - hand<BLACK>(pt);

    score += (mobility[WHITE] - mobility[BLACK]) * (1 + pos.captures_to_hand() + pos.must_capture());

    score +=  king<   WHITE>() - king<   BLACK>()
            + threats<WHITE>() - threats<BLACK>()
            + passed< WHITE>() - passed< BLACK>()
            + space<  WHITE>() - space<  BLACK>()
            + variant<WHITE>() - variant<BLACK>();

    score += initiative(eg_value(score));

    // Interpolate between a middlegame and a (scaled by 'sf') endgame score
    ScaleFactor sf = scale_factor(eg_value(score));
    Value v =  mg_value(score) * int(me->game_phase())
             + eg_value(score) * int(PHASE_MIDGAME - me->game_phase()) * sf / SCALE_FACTOR_NORMAL;

    v /= int(PHASE_MIDGAME);

    // In case of tracing add all remaining individual evaluation terms
    if (T)
    {
        Trace::add(IMBALANCE, me->imbalance());
        Trace::add(PAWN, pe->pawn_score(WHITE), pe->pawn_score(BLACK));
        Trace::add(MOBILITY, mobility[WHITE], mobility[BLACK]);
        Trace::add(TOTAL, score);
    }

    return  (pos.side_to_move() == WHITE ? v : -v) // Side to move point of view
           + Eval::tempo_value(pos);
  }

} // namespace


/// tempo_value() returns the evaluation offset for the side to move

Value Eval::tempo_value(const Position& pos) {
  return Tempo * (1 + 4 * pos.captures_to_hand());
}


/// evaluate() is the evaluator for the outer world. It returns a static
/// evaluation of the position from the point of view of the side to move.

Value Eval::evaluate(const Position& pos) {
  return Evaluation<NO_TRACE>(pos).value();
}


/// trace() is like evaluate(), but instead of returning a value, it returns
/// a string (suitable for outputting to stdout) that contains the detailed
/// descriptions and values of each evaluation term. Useful for debugging.

std::string Eval::trace(const Position& pos) {

  std::memset(scores, 0, sizeof(scores));

  pos.this_thread()->contempt = SCORE_ZERO; // Reset any dynamic contempt

  Value v = Evaluation<TRACE>(pos).value();

  v = pos.side_to_move() == WHITE ? v : -v; // Trace scores are from white's point of view

  std::stringstream ss;
  ss << std::showpoint << std::noshowpos << std::fixed << std::setprecision(2)
     << "     Term    |    White    |    Black    |    Total   \n"
     << "             |   MG    EG  |   MG    EG  |   MG    EG \n"
     << " ------------+-------------+-------------+------------\n"
     << "    Material | " << Term(MATERIAL)
     << "   Imbalance | " << Term(IMBALANCE)
     << "  Initiative | " << Term(INITIATIVE)
     << "       Pawns | " << Term(PAWN)
     << "     Knights | " << Term(KNIGHT)
     << "     Bishops | " << Term(BISHOP)
     << "       Rooks | " << Term(ROOK)
     << "      Queens | " << Term(QUEEN)
     << "    Mobility | " << Term(MOBILITY)
     << " King safety | " << Term(KING)
     << "     Threats | " << Term(THREAT)
     << "      Passed | " << Term(PASSED)
     << "       Space | " << Term(SPACE)
     << "     Variant | " << Term(VARIANT)
     << " ------------+-------------+-------------+------------\n"
     << "       Total | " << Term(TOTAL);

  ss << "\nTotal evaluation: " << to_cp(v) << " (white side)\n";

  return ss.str();
}
