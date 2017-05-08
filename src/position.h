/*
  nozomi, a USI shogi playing engine derived from Stockfish (chess playing engin)
  Copyright (C) 2016 Yuhei Ohmori

  nozomi is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  nozomi is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _POSITION_H_
#define _POSITION_H_

#include <cstddef>
#include <stdint.h>
#include <iostream>
#include <sstream>
#include <stack>
#include <memory>
#include "bit_board.h"
#include "move.h"
#include "evaluate.h"

enum Repetition
{
  kNoRepetition,
  kRepetition,
  kPerpetualCheckWin,
  kPerpetualCheckLose,
  kBlackWinRepetition,
  kBlackLoseRepetition
};

class Thread;

struct CheckInfo
{
  explicit CheckInfo(const Position &);

  BitBoard discover_check_candidates;
  BitBoard pinned;
  BitBoard check_squares[kPieceTypeMax];
};

struct StateInfo
{
  int material;
  int pilies_from_null;
  int continuous_checks[kNumberOfColor];
  uint8_t  kpp_list_index[kSquareHand];
  Eval::KPPIndex black_kpp_list[Eval::kListNum];
  Eval::KPPIndex white_kpp_list[Eval::kListNum];
  uint8_t list_index_move;
  uint8_t list_index_capture;

  uint64_t board_key;
  uint64_t hand_key;
  Hand     hand_black;
  BitBoard checkers_bb;
  StateInfo *previous;
};

class Position
{
  friend std::ostream& operator<<(std::ostream&, const Position&);

public:
  Position() {}
  
  Position(const Position& p, Thread *t) 
  { 
    *this = p;
    this_thread_ = t;
  }

  Position(const std::string &f, Thread *t)
  {
    set(f, t);
  }

  Position &
  operator=(const Position &);

  static void 
  initialize();

  // Text input/output
  void 
  set(const std::string &sfen, Thread *t);

  // Position representation
  BitBoard 
  pieces(PieceType type, Color color) const;
  PieceType 
  piece_type(Square sq) const;
  Square 
  square_king(Color color) const;
  Hand
  hand(Color color) const;
  Piece 
  square(Square sq) const;

  // Properties of moves
  bool 
  is_attacked(Square sq, Color color, const BitBoard &occupied) const;
  bool
  is_king_discover(Square from, Square to, Color color, const BitBoard &pinned) const;

  bool 
  in_check() const;
  bool 
  gives_check(Move m, const CheckInfo &ci) const;
  bool
  gives_mate_by_drop_pawn(Square sq) const;

  // Doing and undoing moves
  void
  do_move(Move m, StateInfo &new_state, bool gives_check);
  void 
  do_move(Move m, StateInfo &new_state);
  void
  do_null_move(StateInfo &new_state);
  void 
  undo_move(Move m);
  void 
  undo_null_move();

  void
  move_temporary(Square from, Square to, PieceType type, PieceType capture);
  void
  move_with_promotion_temporary(Square from, Square to, PieceType type, PieceType capture);


  Color 
  side_to_move() const;
  uint64_t 
  nodes_searched() const;
  void 
  set_nodes_searched(uint64_t value);
  int 
  game_ply() const;
  Thread *
  this_thread() const;

  BitBoard 
  occupied() const;
  BitBoard 
  rook_dragon(Color color) const;
  BitBoard 
  bishop_horse(Color color) const;
  BitBoard 
  total_gold(Color color) const;
  BitBoard 
  horse_dragon_king(Color color) const;

  BitBoard 
  attacks_to(Square sq, Color color, const BitBoard &occupied) const;

  uint64_t
  key() const;
  uint64_t
  key_after(Move m) const;
  int 
  material() const;
  bool
  see_ge(Move m, Value v) const;
  bool
  see_ge_reverse_move(Move m, Value v) const;

  Repetition 
  in_repetition() const;
  uint64_t 
  exclusion_key() const;
  int 
  continuous_checks(Color c) const;

  Eval::KPPIndex *
  black_kpp_list() const;
  Eval::KPPIndex *
  white_kpp_list() const;
  Eval::KPPIndex *
  prev_black_kpp_list() const;
  Eval::KPPIndex *
  prev_white_kpp_list() const;
  uint8_t
  list_index_capture() const;
  uint8_t
  list_index_move() const;

  void
  print() const;

  BitBoard
  pinned_pieces(Color c) const;
  BitBoard
  pinned_pieces(Color c, const BitBoard &occupied) const;
  BitBoard
  discovered_check_candidates() const;
  BitBoard
  checkers_bitboard() const;
  bool
  legal(Move m, BitBoard &pinned) const;
  bool
  pseudo_legal(Move m) const;
  bool
  validate() const;
  bool
  is_decralation_win() const;

private:
  void 
  clear();

  void 
  put_piece(Piece piece, Square sq);
  int 
  compute_material() const;
  bool 
  is_pawn_exist(Square sq, Color color) const;
  BitBoard
  check_blockers(Color c, Color king_color, const BitBoard &occupied) const;
  bool
  see_ge(Move m, Value v, Color c) const;
 
  BitBoard   piece_board_[kNumberOfColor][kPieceTypeMax];
  Hand       hand_[kNumberOfColor];
  Piece      squares_[kBoardSquare];
  Square     square_king_[kNumberOfColor];
  Color      side_to_move_;
  StateInfo  start_state_;
  uint64_t   nodes_searched_;
  StateInfo *state_;
  int        game_ply_;
  Thread    *this_thread_;
};

inline bool
Position::see_ge(Move m, Value v) const
{
  return see_ge(m, v, side_to_move_);
}

inline bool
Position::see_ge_reverse_move(Move m, Value v) const
{
  const Square to = move_from(m);
  if (to >= kBoardSquare)
    return v >= kValueZero;

  const Square from = move_to(m);
  // 本来ならcaptureも考慮すべきかもだけどnon captureの場面でしか呼ばれないので無視する
  return see_ge(move_init(from, to, move_piece_type(m), kPieceNone, false), v, ~side_to_move_);
}

inline int
Position::continuous_checks(Color c) const
{
  return state_->continuous_checks[c];
}

inline uint64_t
Position::nodes_searched() const
{
  return nodes_searched_;
}

inline void 
Position::set_nodes_searched(uint64_t value)
{
  nodes_searched_ = value;
}

inline int 
Position::game_ply() const
{
  return game_ply_;
}

inline BitBoard 
Position::pieces(PieceType type, Color color) const
{
  return piece_board_[color][type];
}

inline PieceType
Position::piece_type(Square sq) const
{
  return static_cast<PieceType>(squares_[sq] & 0xF);
}

inline Square
Position::square_king(Color color) const
{
  return static_cast<Square>(square_king_[color]);
}

inline Color
Position::side_to_move() const
{
  return side_to_move_;
}

inline Hand
Position::hand(Color color) const
{
  return hand_[color];
}

inline Piece
Position::square(Square sq) const
{
  return squares_[sq];
}

inline BitBoard
Position::occupied() const
{
  return piece_board_[kBlack][kOccupied] | piece_board_[kWhite][kOccupied];
}

inline BitBoard
Position::rook_dragon(Color color) const
{
  return piece_board_[color][kRook] | piece_board_[color][kDragon];
}

inline BitBoard
Position::bishop_horse(Color color) const
{
  return piece_board_[color][kBishop] | piece_board_[color][kHorse];
}

inline BitBoard
Position::total_gold(Color color) const
{
  return piece_board_[color][kGold] | piece_board_[color][kPromotedPawn] | piece_board_[color][kPromotedKnight] | piece_board_[color][kPromotedLance] | piece_board_[color][kPromotedSilver];
}

inline BitBoard
Position::horse_dragon_king(Color color) const
{
  return piece_board_[color][kHorse] | piece_board_[color][kDragon] | piece_board_[color][kKing];
}

inline bool
Position::is_king_discover(Square from, Square to, Color color, const BitBoard &pinned) const
{
  return (pinned & MaskTable[from]).test() && !aligned(from, to, square_king_[color]);
}

inline bool 
Position::in_check() const
{
  return state_->checkers_bb.test();
}

inline BitBoard
Position::checkers_bitboard() const
{
  return state_->checkers_bb;
}

inline uint64_t
Position::key() const
{
  return state_->board_key + state_->hand_key;
}

inline int
Position::material() const
{
  return state_->material;
}

inline Thread *
Position::this_thread() const
{
  return this_thread_;
}

// sqに利いている駒を列挙する
FORCE_INLINE
BitBoard
Position::attacks_to(Square sq, Color color, const BitBoard &occupied) const
{
  Color enemy = ~color;
  BitBoard bb = piece_board_[color][kPawn] & PawnAttacksTable[enemy][sq];

  bb.and_or(piece_board_[color][kLance], lance_attack(occupied, enemy, sq));
  bb.and_or(piece_board_[color][kKnight], KnightAttacksTable[enemy][sq]);
  bb.and_or(piece_board_[color][kSilver], SilverAttacksTable[enemy][sq]);
  bb.and_or(total_gold(color), GoldAttacksTable[enemy][sq]);
  bb.and_or(horse_dragon_king(color), KingAttacksTable[sq]);
  bb.and_or(bishop_horse(color), bishop_attack(occupied, sq));
  bb.and_or(rook_dragon(color), rook_attack(occupied, sq));

  return bb;
}

// sqに対して敵の駒のききがあるか
FORCE_INLINE
bool
Position::is_attacked(Square sq, Color color, const BitBoard &occupied) const
{
  Color enemy = ~color;
  BitBoard bb = piece_board_[enemy][kPawn] & PawnAttacksTable[color][sq];

  bb.and_or(piece_board_[enemy][kLance], lance_attack(occupied, color, sq));
  bb.and_or(piece_board_[enemy][kKnight], KnightAttacksTable[color][sq]);
  bb.and_or(piece_board_[enemy][kSilver], SilverAttacksTable[color][sq]);
  bb.and_or(total_gold(enemy), GoldAttacksTable[color][sq]);
  bb.and_or(horse_dragon_king(enemy), KingAttacksTable[sq]);
  bb.and_or(bishop_horse(enemy), bishop_attack(occupied, sq));
  bb.and_or(rook_dragon(enemy), rook_attack(occupied, sq));

  return bb.test();
}

inline BitBoard
Position::pinned_pieces(Color c) const
{
  return check_blockers(c, c, occupied());
}

inline BitBoard
Position::pinned_pieces(Color c, const BitBoard &occupied) const
{
  return check_blockers(c, c, occupied);
}

inline BitBoard
Position::discovered_check_candidates() const
{
  return check_blockers(side_to_move_, ~side_to_move_, occupied());
}

inline bool
Position::legal(Move m, BitBoard &pinned) const
{
  Square from = move_from(m);

  if (from >= kBoardSquare)
    return true;

  PieceType type = move_piece_type(m);
  Square to = move_to(m);
  if (type == kKing)
  {
    BitBoard oc = occupied();
    oc.xor_bit(from);
    return !is_attacked(to, side_to_move_, oc);
  }

  return !pinned.test() || !((pinned & MaskTable[from]).test()) || aligned(from, to, square_king_[side_to_move_]);
}

inline Eval::KPPIndex *
Position::black_kpp_list() const
{
  return state_->black_kpp_list;
}

inline Eval::KPPIndex *
Position::white_kpp_list() const
{
  return state_->white_kpp_list;
}

inline Eval::KPPIndex *
Position::prev_black_kpp_list() const
{
  return state_->previous->black_kpp_list;
}

inline Eval::KPPIndex *
Position::prev_white_kpp_list() const
{
  return state_->previous->white_kpp_list;
}

inline uint8_t
Position::list_index_capture() const
{
  return state_->list_index_capture;
}

inline uint8_t
Position::list_index_move() const
{
  return state_->list_index_move;
}

inline void
Position::move_temporary(Square from, Square to, PieceType type, PieceType capture)
{
  const BitBoard  set_clear = MaskTable[from] | MaskTable[to];
  piece_board_[side_to_move_][kOccupied] ^= set_clear;
  piece_board_[side_to_move_][type] ^= set_clear;
  if (capture != kPieceNone)
  {
    Color enemy = ~side_to_move_;
    piece_board_[enemy][capture].xor_bit(to);
    piece_board_[enemy][kOccupied].xor_bit(to);
  }
}

inline void
Position::move_with_promotion_temporary(Square from, Square to, PieceType type, PieceType capture)
{
  const BitBoard  set_clear = MaskTable[from] | MaskTable[to];
  piece_board_[side_to_move_][kOccupied] ^= set_clear;
  piece_board_[side_to_move_][type].xor_bit(from);
  piece_board_[side_to_move_][type + kFlagPromoted].xor_bit(to);
  if (capture != kPieceNone)
  {
    Color enemy = ~side_to_move_;
    piece_board_[enemy][capture].xor_bit(to);
    piece_board_[enemy][kOccupied].xor_bit(to);
  }
}

#endif
