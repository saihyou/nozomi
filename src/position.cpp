/*
  nozomi, a USI shogi playing engine
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

#include <random>
#include <map>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <string.h>
#include "position.h"
#include "misc.h"
#include "transposition_table.h"

using std::string;

constexpr int kHandPieceMax = 7;

using namespace Eval;

namespace Zobrist
{
uint64_t tables[kNumberOfColor][kPieceTypeMax][kBoardSquare];
// 配列の0番目を使わないので+1する
uint64_t hands[kNumberOfColor][kHandPieceMax + 1];
uint64_t side;
uint64_t exclusion;
} // namespace Zobrist

namespace
{
struct PieceLetters : public std::map<char, Piece>
{
  PieceLetters()
  {
    operator[]('K') = kBlackKing;
    operator[]('k') = kWhiteKing;
    operator[]('G') = kBlackGold;
    operator[]('g') = kWhiteGold;
    operator[]('S') = kBlackSilver;
    operator[]('s') = kWhiteSilver;
    operator[]('N') = kBlackKnight;
    operator[]('n') = kWhiteKnight;
    operator[]('L') = kBlackLance;
    operator[]('l') = kWhiteLance;
    operator[]('P') = kBlackPawn;
    operator[]('p') = kWhitePawn;
    operator[]('R') = kBlackRook;
    operator[]('r') = kWhiteRook;
    operator[]('B') = kBlackBishop;
    operator[]('b') = kWhiteBishop;
    operator[]('.') = kEmpty;
  }
};

PieceLetters piece_letters;

constexpr Square
PieceTypeToSquareHandTable[kNumberOfColor][kPieceTypeMax] =
{
  {
    kSquareHand, 
    kBlackHandPawn, 
    kBlackHandLance, 
    kBlackHandKnight, 
    kBlackHandSilver,
    kBlackHandBishop, 
    kBlackHandRook, 
    kBlackHandGold, 
    kSquareHand,
    kBlackHandPawn,
    kBlackHandLance,
    kBlackHandKnight,
    kBlackHandSilver,
    kBlackHandBishop,
    kBlackHandRook
  },
  {
    kSquareHand,
    kWhiteHandPawn,
    kWhiteHandLance,
    kWhiteHandKnight,
    kWhiteHandSilver,
    kWhiteHandBishop,
    kWhiteHandRook,
    kWhiteHandGold,
    kSquareHand,
    kWhiteHandPawn,
    kWhiteHandLance,
    kWhiteHandKnight,
    kWhiteHandSilver,
    kWhiteHandBishop,
    kWhiteHandRook
  }
};

void 
initialize_zobrist()
{
  std::mt19937_64 engine(0);
  Zobrist::side = 1;
  Zobrist::exclusion = engine() & ~1;
  for (PieceType piece_type = kOccupied; piece_type < kPieceTypeMax; ++piece_type)
  {
    for (Color color = kBlack; color < kNumberOfColor; ++color)
    {
      for (Square sq = k9A; sq < kBoardSquare; ++sq)
      {
        Zobrist::tables[color][piece_type][sq] = engine() & ~1;
      }
    }
  }

  for (Color color = kBlack; color < kNumberOfColor; ++color)
  {
    for (int i = 0; i < kHandPieceMax; i++)
    {
      Zobrist::hands[color][i + 1] = engine() & ~1;
    }
  }
}

inline bool 
can_promote(Color color, uint32_t from, uint32_t to)
{
  if (color == kBlack)
    return (to < k9D || from < k9D) ? true : false;
  else
    return (to > k1F || from > k1F) ? true : false;
}
} // namespace

CheckInfo::CheckInfo(const Position &pos)
{
  Color enemy = ~pos.side_to_move();
  Square king = pos.square_king(enemy);
  const BitBoard occupied = pos.occupied();

  pinned = pos.pinned_pieces(pos.side_to_move());
  discover_check_candidates = pos.discovered_check_candidates();

  check_squares[kPawn] = PawnAttacksTable[enemy][king];
  check_squares[kLance] = lance_attack(occupied, enemy, king);
  check_squares[kKnight] = KnightAttacksTable[enemy][king];
  check_squares[kSilver] = SilverAttacksTable[enemy][king];
  check_squares[kBishop] = bishop_attack(occupied, king);
  check_squares[kRook] = rook_attack(occupied, king);
  check_squares[kGold] = GoldAttacksTable[enemy][king];
  check_squares[kKing].init();
  check_squares[kPromotedPawn] = check_squares[kGold];
  check_squares[kPromotedLance] = check_squares[kGold];
  check_squares[kPromotedKnight] = check_squares[kGold];
  check_squares[kPromotedSilver] = check_squares[kGold];
  check_squares[kHorse] = check_squares[kBishop] | KingAttacksTable[king];
  check_squares[kDragon] = check_squares[kRook] | KingAttacksTable[king];
}

void 
Position::initialize()
{
  initialize_zobrist();
}

void 
Position::set(const std::string &sfen, Thread *t)
{
  char token;
  int sq = 0;
  bool is_promote = false;
  Piece piece = kEmpty;
  std::istringstream ss(sfen);

  clear();

  ss >> std::noskipws;

  while ((ss >> token) && !isspace(token))
  {
    if (token == '+')
    {
      is_promote = true;
      continue;
    }

    if (piece_letters.find(token) != piece_letters.end())
    {
      piece = piece_letters[token];
      if (is_promote)
        piece = piece + kFlagPromoted;
      put_piece(piece, static_cast<Square>(sq));

      sq++;
      is_promote = false;
    }
    else if (isdigit(token))
    {
      sq += (token - '0');
    }
    else if (token == '/')
    {
      // do nothing
    }
  }

  while (ss >> token && !isspace(token))
  {
    if (token == 'b')
    {
      side_to_move_ = kBlack;
    }
    else
    {
      side_to_move_ = kWhite;
      state_->board_key += Zobrist::side;
    }
  }

  int piece_num = 1;
  while (ss >> token && !isspace(token))
  {
    if (token == '-')
      continue;

    if (isdigit(token))
    {
      if (token == '1')
      {
        ss >> token;
        piece_num = 10 + int(token - '0');
      }
      else
      {
        piece_num = int(token - '0');
      }
    }
    else
    {
      Piece p = piece_letters[token];
      PieceType t = type_of(p);
      Color c = color_of(p);
      for (int i = 0; i < piece_num; i++)
      {
        add_hand(hand_[c], t);
        state_->hand_key += Zobrist::hands[c][t];
      }
      piece_num = 1;
    }
  }

  ss >> game_ply_;

  this_thread_ = t;
  state_->hand_black = hand_[kBlack];
  state_->material = compute_material();
  state_->checkers_bb = attacks_to(square_king_[side_to_move_], ~side_to_move_, occupied());
  if (state_->checkers_bb.test())
    state_->continuous_checks[~side_to_move_] = 1; 

  int list_index = 0;
  for (int i = 1; i <= number_of(hand_[kBlack], kPawn); ++i)
  {
    state_->black_kpp_list[list_index]         = kFHandPawn + i;
    state_->white_kpp_list[list_index]         = kEHandPawn + i;
    state_->kpp_list_index[kBlackHandPawn + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kPawn); ++i)
  {
    state_->black_kpp_list[list_index]         = kEHandPawn + i;
    state_->white_kpp_list[list_index]         = kFHandPawn + i;
    state_->kpp_list_index[kWhiteHandPawn + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kLance); ++i)
  {
    state_->black_kpp_list[list_index]          = kFHandLance + i;
    state_->white_kpp_list[list_index]          = kEHandLance + i;
    state_->kpp_list_index[kBlackHandLance + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kLance); ++i)
  {
    state_->black_kpp_list[list_index]          = kEHandLance + i;
    state_->white_kpp_list[list_index]          = kFHandLance + i;
    state_->kpp_list_index[kWhiteHandLance + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kKnight); ++i)
  {
    state_->black_kpp_list[list_index]           = kFHandKnight + i;
    state_->white_kpp_list[list_index]           = kEHandKnight + i;
    state_->kpp_list_index[kBlackHandKnight + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kKnight); ++i)
  {
    state_->black_kpp_list[list_index]           = kEHandKnight + i;
    state_->white_kpp_list[list_index]           = kFHandKnight + i;
    state_->kpp_list_index[kWhiteHandKnight + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kSilver); ++i)
  {
    state_->black_kpp_list[list_index]           = kFHandSilver + i;
    state_->white_kpp_list[list_index]           = kEHandSilver + i;
    state_->kpp_list_index[kBlackHandSilver + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kSilver); ++i)
  {
    state_->black_kpp_list[list_index]           = kEHandSilver + i;
    state_->white_kpp_list[list_index]           = kFHandSilver + i;
    state_->kpp_list_index[kWhiteHandSilver + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kGold); ++i)
  {
    state_->black_kpp_list[list_index]         = kFHandGold + i;
    state_->white_kpp_list[list_index]         = kEHandGold + i;
    state_->kpp_list_index[kBlackHandGold + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kGold); ++i)
  {
    state_->black_kpp_list[list_index]         = kEHandGold + i;
    state_->white_kpp_list[list_index]         = kFHandGold + i;
    state_->kpp_list_index[kWhiteHandGold + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kBishop); ++i)
  {
    state_->black_kpp_list[list_index]           = kFHandBishop + i;
    state_->white_kpp_list[list_index]           = kEHandBishop + i;
    state_->kpp_list_index[kBlackHandBishop + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kBishop); ++i)
  {
    state_->black_kpp_list[list_index]           = kEHandBishop + i;
    state_->white_kpp_list[list_index]           = kFHandBishop + i;
    state_->kpp_list_index[kWhiteHandBishop + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kBlack], kRook); ++i)
  {
    state_->black_kpp_list[list_index]         = kFHandRook + i;
    state_->white_kpp_list[list_index]         = kEHandRook + i;
    state_->kpp_list_index[kBlackHandRook + i] = list_index;
    ++list_index;
  }

  for (int i = 1; i <= number_of(hand_[kWhite], kRook); ++i)
  {
    state_->black_kpp_list[list_index]         = kEHandRook + i;
    state_->white_kpp_list[list_index]         = kFHandRook + i;
    state_->kpp_list_index[kWhiteHandRook + i] = list_index;
    ++list_index;
  }

  for (int i = 0; i < kBoardSquare; i++)
  {
    if (squares_[i] != kEmpty && squares_[i] != kBlackKing && squares_[i] != kWhiteKing)
    {
      state_->kpp_list_index[i] = list_index;
      state_->black_kpp_list[list_index] = PieceToIndexBlackTable[squares_[i]] + i;
      state_->white_kpp_list[list_index] = PieceToIndexWhiteTable[squares_[i]] + inverse((Square)i);
      ++list_index;
    }
  }
}

Position &
Position::operator=(const Position &pos)
{
  memcpy(this, &pos, sizeof(Position));
  start_state_ = *state_;
  state_ = &start_state_;
  nodes_searched_ = 0;
  return *this;
}

void 
Position::clear()
{
  memset(this, 0, sizeof(Position));

  for (int color = 0; color < kNumberOfColor; color++)
  {
    for (int type = 0; type < kPieceTypeMax; type++)
    {
      piece_board_[color][type].init();
    }
    hand_[color] = kHandZero;
    square_king_[color] = k9A;
  }
  memset(squares_, kEmpty, kBoardSquare);
  side_to_move_ = kBlack;
  state_ = &start_state_;
  nodes_searched_ = 0;
  game_ply_ = 0;
}

void
Position::do_move(Move m, StateInfo &new_state)
{
  do_move(m, new_state, gives_check(m, CheckInfo(*this)));
}

void 
Position::do_move(Move m, StateInfo &new_state, bool gives_check)
{
  ++nodes_searched_;
  ++game_ply_;

  uint64_t board_key = state_->board_key;
  uint64_t hand_key = state_->hand_key;
  Square from = move_from(m);
  Square to = move_to(m);

  std::memcpy(&new_state, state_, offsetof(StateInfo, list_index_capture));
  new_state.previous = state_;
  state_ = &new_state;

  ++state_->pilies_from_null;
  
  Color us = side_to_move_;
  board_key ^= Zobrist::side;

  if (from >= kBoardSquare)
  {
    const PieceType drop = to_drop_piece_type(from);
    assert(drop != kOccupied);
    piece_board_[us][drop].xor_bit(to);
    squares_[to] = make_piece(drop, us);
    piece_board_[us][kOccupied].xor_bit(to);
    sub_hand(hand_[us], drop);
    hand_key -= Zobrist::hands[us][drop];
    board_key += Zobrist::tables[us][drop][to];

    assert(to < kBoardSquare);
    int hand_num   = number_of(hand_[us], drop) + 1;
    int list_index = state_->kpp_list_index[PieceTypeToSquareHandTable[us][drop] + hand_num];
    assert(list_index < 38);
    state_->black_kpp_list[list_index] = PieceToIndexBlackTable[squares_[to]] + to;
    state_->white_kpp_list[list_index] = PieceToIndexWhiteTable[squares_[to]] + inverse(to);
    state_->kpp_list_index[to]         = list_index;
    state_->list_index_move            = list_index;
  }
  else
  {
    const PieceType piece_move = move_piece_type(m);
    const bool is_promote = move_is_promote(m);
    BitBoard set_clear = MaskTable[from] | MaskTable[to];
    piece_board_[us][kOccupied] ^= set_clear;
    squares_[from] = kEmpty;
    if (is_promote)
    {
      piece_board_[us][piece_move].xor_bit(from);
      piece_board_[us][piece_move + kFlagPromoted].xor_bit(to);
      squares_[to] = make_piece(piece_move + kFlagPromoted, us);
      board_key -= Zobrist::tables[us][piece_move][from];
      board_key += Zobrist::tables[us][piece_move + kFlagPromoted][to];
      state_->material += 
        (us == kBlack) 
        ? 
        PromotePieceValueTable[piece_move] 
        : 
        -PromotePieceValueTable[piece_move];
    }
    else
    {
      piece_board_[us][piece_move] ^= set_clear;
      squares_[to] = make_piece(piece_move, us);
      board_key -= Zobrist::tables[us][piece_move][from];
      board_key += Zobrist::tables[us][piece_move][to];
      if (piece_move == kKing)
        square_king_[us] = static_cast<Square>(to);
    }

    const PieceType piece_capture = move_capture(m);
    if (piece_capture != kPieceNone)
    {
      const Color enemy = ~us;
      piece_board_[enemy][piece_capture].xor_bit(to);
      add_hand(hand_[us], piece_capture);
      piece_board_[enemy][kOccupied].xor_bit(to);
      board_key -= Zobrist::tables[enemy][piece_capture][to];
      hand_key += Zobrist::hands[us][piece_capture & 0x7];
      state_->material +=
        (us == kBlack)
        ?
        ExchangePieceValueTable[piece_capture]
        :
        -ExchangePieceValueTable[piece_capture];

      int captured_index = state_->kpp_list_index[to];
      int hand_num       = number_of(hand_[us], piece_capture);
      state_->black_kpp_list[captured_index] = PieceTypeToBlackHandIndexTable[us][piece_capture] + hand_num;
      state_->white_kpp_list[captured_index] = PieceTypeToWhiteHandIndexTable[us][piece_capture] + hand_num;
      state_->kpp_list_index[PieceTypeToSquareHandTable[us][piece_capture] + hand_num] = captured_index;
      state_->list_index_capture = captured_index;
    }

    if (piece_move != kKing)
    {
      int kpp_index = state_->kpp_list_index[from];
      assert(kpp_index < 38);
      state_->kpp_list_index[to] = kpp_index;
      state_->black_kpp_list[kpp_index] = PieceToIndexBlackTable[squares_[to]] + to;
      state_->white_kpp_list[kpp_index] = PieceToIndexWhiteTable[squares_[to]] + inverse(to);
      state_->list_index_move = kpp_index;
    }
  }

  state_->board_key = board_key;
  state_->hand_key = hand_key;
  state_->hand_black = hand_[kBlack];
  side_to_move_ = ~side_to_move_;
  if (gives_check)
  {
    state_->continuous_checks[us]++;
    state_->checkers_bb = attacks_to(square_king_[side_to_move_], ~side_to_move_, occupied());
  }
  else
  {
    state_->continuous_checks[us] = 0;
    state_->checkers_bb.init();
  }
}

void 
Position::do_null_move(StateInfo &new_state)
{
  memcpy(&new_state, state_, sizeof(StateInfo));
  new_state.previous = state_;
  state_ = &new_state;

  state_->board_key ^= Zobrist::side;
  prefetch(TT.first_entry(key()));

  side_to_move_ = ~side_to_move_;
}

void 
Position::undo_null_move()
{
  state_ = state_->previous;
  side_to_move_ = ~side_to_move_;
}

void 
Position::undo_move(Move move)
{
  side_to_move_ = ~side_to_move_;
  --game_ply_;

  Square from = move_from(move);
  Square to = move_to(move);
  if (from >= kBoardSquare)
  {
    PieceType drop = to_drop_piece_type(from);
    assert(drop != kOccupied);
    piece_board_[side_to_move_][drop].xor_bit(to);
    add_hand(hand_[side_to_move_], drop);
    squares_[to] = kEmpty;
    piece_board_[side_to_move_][kOccupied].xor_bit(to);
  }
  else
  {
    PieceType piece_move = move_piece_type(move);
    bool is_promote = move_is_promote(move);
    BitBoard set_clear = MaskTable[from] | MaskTable[to];
    piece_board_[side_to_move_][kOccupied] ^= set_clear;
    if (is_promote)
    {
      piece_board_[side_to_move_][piece_move].xor_bit(from);
      piece_board_[side_to_move_][piece_move + kFlagPromoted].xor_bit(to);
      squares_[from] = make_piece(piece_move, side_to_move_);
    }
    else
    {
      piece_board_[side_to_move_][piece_move] ^= set_clear;
      squares_[from] = make_piece(piece_move, side_to_move_);
      if (piece_move == kKing)
        square_king_[side_to_move_] = static_cast<Square>(from);
    }

    PieceType piece_capture = move_capture(move);
    if (piece_capture != kPieceNone)
    {
      Color enemy = ~side_to_move_;
      piece_board_[enemy][piece_capture].xor_bit(to);
      sub_hand(hand_[side_to_move_], piece_capture);
      squares_[to] = make_piece(piece_capture, enemy);
      piece_board_[enemy][kOccupied].xor_bit(to);
    }
    else
    {
      squares_[to] = kEmpty;
    }
  }
  state_ = state_->previous;
}

bool 
Position::gives_check(Move m, const CheckInfo &ci) const
{
  PieceType type = move_piece_type(m);
  Square to = move_to(m);
  Color enemy = ~side_to_move_;
  BitBoard bb = MaskTable[to];
  Square from = move_from(m);

  if (from >= kBoardSquare)
  {
    type = to_drop_piece_type(from);
    bb &= ci.check_squares[type];
    return bb.test();
  }
  else
  {
    if (move_is_promote(m))
      type = type + kFlagPromoted;
    bb &= ci.check_squares[type];

    // 直接の王手
    if (bb.test())
      return true;

    // 駒が動くことによって王手がかかる場合
    if
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
      &&
      !aligned(from, to, square_king_[enemy]) 
    )
      return true;
  }
  return false;
}

bool
Position::gives_mate_by_drop_pawn(Square sq) const
{
  Color color = side_to_move_;
  // 歩の打った場所で王手がかかるか
  if (color == kBlack)
  {
    if (squares_[sq - 9] != kWhiteKing)
      return false;
  }
  else
  {
    if (squares_[sq + 9] != kBlackKing)
      return false;
  }

  // 王が動く手を調べる
  Color enemy = ~color;
  BitBoard occupy = occupied();
  occupy.xor_bit(sq);
  BitBoard movable = KingAttacksTable[square_king_[enemy]] & ~piece_board_[enemy][kOccupied];
  do
  {
    Square to = movable.pop_bit();
    // 敵の王が動けるならばその時点で打ち歩づめではない
    if (!is_attacked(to, enemy, occupy))
      return false;
  }
  while (movable.test());

  // 玉以外の駒が歩をとることができるか
  // 香車は前にしか行けないため、王と歩の間にいない限り取ることができないので調べない
  occupy = occupied();
  BitBoard sum = piece_board_[enemy][kKnight] & KnightAttacksTable[color][sq];
  sum.and_or(piece_board_[enemy][kSilver], SilverAttacksTable[color][sq]);
  sum.and_or(total_gold(enemy), GoldAttacksTable[color][sq]);
  sum.and_or(bishop_horse(enemy), bishop_attack(occupy, sq));
  sum.and_or(rook_dragon(enemy), rook_attack(occupy, sq));
  sum.and_or((piece_board_[enemy][kHorse] | piece_board_[enemy][kDragon]), KingAttacksTable[sq]);
  BitBoard pinned = pinned_pieces(enemy);
  while (sum.test())
  {
    Square from = sum.pop_bit();
    // 空き王手になるならその駒は動かせない
    if (!is_king_discover(from, sq, enemy, pinned))
      return false;
  }

  return true;
}


inline bool 
Position::is_pawn_exist(Square sq, Color color) const
{
  return (piece_board_[color][kPawn] & FileMaskTable[FilePositionTable[sq]]).test();
}

BitBoard
Position::check_blockers(Color c, Color king_color, const BitBoard &occupied) const
{
  BitBoard result;
  result.init();
  Square king_square = square_king_[king_color];
  BitBoard pinners = piece_board_[~king_color][kLance] & LanceAttacksTable[king_color][king_square][0];
  pinners.and_or(rook_dragon(~king_color), RookAttacksTable[king_square][0]);
  pinners.and_or(bishop_horse(~king_color), BishopAttacksTable[king_square][0]);

  while (pinners.test())
  {
    Square sq = pinners.pop_bit();
    const BitBoard b = BetweenTable[king_square][sq] & occupied;
    if (_mm_popcnt_u64(b.to_uint64()) == 1)
      result |= b & piece_board_[c][kOccupied];
  }
  return result;
}

bool
Position::pseudo_legal(Move m) const
{
  Square from = move_from(m);
  Square to = move_to(m);

  if (m == kMoveNone)
    return false;

  if (from >= kBoardSquare)
  {
    // drop先がemptyではない場合は打てないので違法手
    if (squares_[to] != kEmpty)
      return false;

    PieceType drop = to_drop_piece_type(from);
    if (!has_hand(hand_[side_to_move_], drop))
      return false;

    if (drop == kPawn)
    {
      if (gives_mate_by_drop_pawn(to))
        return false;

      if (is_pawn_exist(to, side_to_move_))
        return false;
    }

    if (in_check())
    {
      BitBoard target = state_->checkers_bb;
      Square sq = target.pop_bit();
      // 両王手の場合
      if (target.test())
        return false;
      target = BetweenTable[sq][square_king_[side_to_move_]];
      if (!target.contract(MaskTable[to]))
        return false;
    }
  }
  else
  {
    PieceType type = move_piece_type(m);
    Piece piece = make_piece(type, side_to_move_);

    if (type == kPieceNone || squares_[from] != piece)
      return false;

    if (side_to_move_ == kBlack)
    {
      if (squares_[to] != kEmpty && squares_[to] < kFlagWhite)
        return false;
    }
    else
    {
      if (squares_[to] != kEmpty && squares_[to] > kFlagWhite)
        return false;
    }

    PieceType capture = move_capture(m);
    if (capture == kPieceNone)
    {
      if (squares_[to] != kEmpty)
        return false;
    }
    else
    {
      if (capture == kKing)
        return false;

      if (make_piece(capture, ~side_to_move_) != squares_[to])
        return false;
    }

    if (move_is_promote(m))
    {
      if (!can_promote(side_to_move_, from, to))
        return false;
    }

    BitBoard bb;
    switch (type)
    {
    case kPawn:
      bb = PawnAttacksTable[side_to_move_][from] & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kLance:
      bb = lance_attack(occupied(), side_to_move_, from) & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kKnight:
      bb = KnightAttacksTable[side_to_move_][from] & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kSilver:
      bb = SilverAttacksTable[side_to_move_][from] & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kGold:
    case kPromotedPawn:
    case kPromotedLance:
    case kPromotedKnight:
    case kPromotedSilver:
      bb = GoldAttacksTable[side_to_move_][from] & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kRook:
      bb = rook_attack(occupied(), from) & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kDragon:
      bb = (rook_attack(occupied(), from) | KingAttacksTable[from]) & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kBishop:
      bb = bishop_attack(occupied(), from) & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kHorse:
      bb = (bishop_attack(occupied(), from) | KingAttacksTable[from]) & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    case kKing:
      bb = KingAttacksTable[from] & MaskTable[to];
      if (!bb.test())
        return false;
      break;
    default:
      break;
    }


    if (in_check())
    {
      if (type == kKing)
      {
        BitBoard oc = occupied();
        oc.xor_bit(from);
        if (is_attacked(to, side_to_move_, oc))
          return false;
      }
      else
      {
        BitBoard target = state_->checkers_bb;
        Square sq = target.pop_bit();
        // 両王手
        if (target.test())
          return false;
        
        target = BetweenTable[sq][square_king_[side_to_move_]] | state_->checkers_bb;
        if (!target.contract(MaskTable[to]))
          return false;
      }
    }
  }
  return true;
}

bool
Position::validate() const
{
  // 1段目にblackの歩と香と桂がないこと
  if
  (
    (
      (
        piece_board_[kBlack][kPawn]
        |
        piece_board_[kBlack][kLance]
        |
        piece_board_[kBlack][kKnight]
      )
      & 
      RankMaskTable[kRank1]
    ).test()
  )
    return false;

  // 2段目にblackの桂がないこと
  if ((piece_board_[kBlack][kKnight] & RankMaskTable[kRank2]).test())
    return false;

  // 9段目にwhiteの歩と香と桂がないこと
  if
  (
    (
      (
        piece_board_[kWhite][kPawn]
        |
        piece_board_[kWhite][kLance]
        |
        piece_board_[kWhite][kKnight]
      )
      &
      RankMaskTable[kRank9]
    ).test()
  )
    return false;  

  // 8段目にwhiteの桂がないこと
  if ((piece_board_[kWhite][kKnight] & RankMaskTable[kRank8]).test())
    return false;

  // 2歩になっていないこと
  for (File f = kFile1; f <= kFile9; ++f)
  {
    for (Color c = kBlack; c <= kWhite; ++c)
    {
      if ((piece_board_[c][kPawn] & FileMaskTable[f]).popcount() > 1)
        return false;
    }
  }

  // 敵側の玉がとられないこと
  if (is_attacked(square_king_[~side_to_move_], ~side_to_move_, occupied()))
    return false;

  return true;
}

void 
Position::put_piece(Piece piece, Square sq)
{
  Color color = (piece < kFlagWhite) ? kBlack : kWhite;
  squares_[sq] = piece;
  piece_board_[color][kOccupied].xor_bit(sq);
  piece_board_[color][piece & 0xF].xor_bit(sq);
  state_->board_key += Zobrist::tables[color][piece & 0xf][sq];
  if (piece == kBlackKing)
    square_king_[kBlack] = sq;

  if (piece == kWhiteKing)
    square_king_[kWhite] = sq;
}

int 
Position::compute_material() const
{
  int num;
  int material = 0;

  num = static_cast<int>(piece_board_[kBlack][kPawn].popcount()) + number_of(hand_[kBlack], kPawn);
  num -= static_cast<int>(piece_board_[kWhite][kPawn].popcount()) + number_of(hand_[kWhite], kPawn);
  material += num * PieceValueTable[kPawn];

  num = static_cast<int>(piece_board_[kBlack][kLance].popcount()) + number_of(hand_[kBlack], kLance);
  num -= static_cast<int>(piece_board_[kWhite][kLance].popcount()) + number_of(hand_[kWhite], kLance);
  material += num * PieceValueTable[kLance];

  num = static_cast<int>(piece_board_[kBlack][kKnight].popcount()) + number_of(hand_[kBlack], kKnight);
  num -= static_cast<int>(piece_board_[kWhite][kKnight].popcount()) + number_of(hand_[kWhite], kKnight);
  material += num * PieceValueTable[kKnight];

  num = static_cast<int>(piece_board_[kBlack][kSilver].popcount()) + number_of(hand_[kBlack], kSilver);
  num -= static_cast<int>(piece_board_[kWhite][kSilver].popcount()) + number_of(hand_[kWhite], kSilver);
  material += num * PieceValueTable[kSilver];

  num = static_cast<int>(piece_board_[kBlack][kGold].popcount()) + number_of(hand_[kBlack], kGold);
  num -= static_cast<int>(piece_board_[kWhite][kGold].popcount()) + number_of(hand_[kWhite], kGold);
  material += num * PieceValueTable[kGold];

  num = static_cast<int>(piece_board_[kBlack][kBishop].popcount()) + number_of(hand_[kBlack], kBishop);
  num -= static_cast<int>(piece_board_[kWhite][kBishop].popcount()) + number_of(hand_[kWhite], kBishop);
  material += num * PieceValueTable[kBishop];

  num = static_cast<int>(piece_board_[kBlack][kRook].popcount()) + number_of(hand_[kBlack], kRook);
  num -= static_cast<int>(piece_board_[kWhite][kRook].popcount()) + number_of(hand_[kWhite], kRook);
  material += num * PieceValueTable[kRook];

  num = static_cast<int>(piece_board_[kBlack][kPromotedPawn].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kPromotedPawn].popcount());
  material += num * PieceValueTable[kPromotedPawn];

  num = static_cast<int>(piece_board_[kBlack][kPromotedLance].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kPromotedLance].popcount());
  material += num * PieceValueTable[kPromotedLance];

  num = static_cast<int>(piece_board_[kBlack][kPromotedKnight].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kPromotedKnight].popcount());
  material += num * PieceValueTable[kPromotedKnight];

  num = static_cast<int>(piece_board_[kBlack][kPromotedSilver].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kPromotedSilver].popcount());
  material += num * PieceValueTable[kPromotedSilver];

  num = static_cast<int>(piece_board_[kBlack][kHorse].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kHorse].popcount());
  material += num * PieceValueTable[kHorse];

  num = static_cast<int>(piece_board_[kBlack][kDragon].popcount());
  num -= static_cast<int>(piece_board_[kWhite][kDragon].popcount());
  material += num * PieceValueTable[kDragon];

  return material;
}

template<int piece_type> 
FORCE_INLINE PieceType 
min_attacker
(
  const Position *pos,
  Square to,
  Color defend_side,
  const BitBoard &defenders,
  BitBoard &attackers,
  BitBoard &occupied
)
{
  const static PieceType min_table[] = 
    {kPawn, kLance, kKnight, kPromotedPawn, kPromotedLance, kSilver, kPromotedKnight, kPromotedSilver, kGold, kBishop, kRook, kHorse, kDragon, kKing};
  BitBoard b = defenders & pos->pieces(min_table[piece_type], defend_side);
  if (!b.test())
    return min_attacker<piece_type + 1>(pos, to, defend_side, defenders, attackers, occupied);

  Square sq = static_cast<Square>(b.first_one());
  occupied.xor_bit(sq);

  BitBoard attacks;
  switch (DirectionTable[to][sq])
  {
  case kDirFile:
    attacks = pos->rook_dragon(defend_side);
    attacks.and_or(pos->pieces(kLance, defend_side), LanceAttacksTable[~defend_side][to][0]);
    attacks &= (rook_attack(occupied, to) & FileMaskTable[FilePositionTable[to]]);
    attackers = attackers | attacks;
    break;
  case kDirRank:
    attacks = pos->rook_dragon(defend_side);
    attacks &= (rook_attack(occupied, to) & RankMaskTable[RankPositionTable[to]]);
    attackers = attackers | attacks;
    break;
  case kDirLeft45:
    attacks = pos->bishop_horse(defend_side);
    attacks &= (bishop_attack(occupied, to) & Left45MaskTable[Left45MaskIndexTable[to]]);
    attackers = attackers | attacks;
    break;
  case kDirRight45:
    attacks = pos->bishop_horse(defend_side);
    attacks &= (bishop_attack(occupied, to) & Right45MaskTable[Right45MaskIndexTable[to]]);
    attackers = attackers | attacks;
    break;
  default:
    break;
  }
  attackers = attackers & occupied;
  return min_table[piece_type];
}

template<>
FORCE_INLINE PieceType 
min_attacker<13>
(
  const Position *pos,
  Square to,
  Color defend_side,
  const BitBoard &defenders,
  BitBoard &attackers,
  BitBoard &occupied
)
{
  return kKing;
}

bool
Position::see_ge(Move m, Value v, Color c) const
{
  Square    to = move_to(m);
  Square    from = move_from(m);
  PieceType next_victim;
  Color     side_to_move = ~c;
  BitBoard  occupied = this->occupied();
  Value     balance;
  BitBoard  side_to_move_attackers;
  BitBoard  attackers;

  if (from < kBoardSquare)
  {
    occupied.xor_bit(from);
    balance = static_cast<Value>(ExchangePieceValueTable[move_capture(m)]);
    next_victim = move_piece_type(m);

    if (balance < v)
      return false;

    if (next_victim == kKing)
      return true;

    attackers = attacks_to(to, side_to_move, occupied);
    if (!attackers.test())
      return true;

    balance -= static_cast<Value>(ExchangePieceValueTable[next_victim]);

    if (balance >= v)
      return true;
  }
  else
  {
    next_victim = to_drop_piece_type(from);
    balance = kValueZero;
    if (balance < v)
      return false;

    attackers = attacks_to(to, side_to_move, occupied);
    if (!attackers.test())
      return true;

    balance -= static_cast<Value>(ExchangePieceValueTable[next_victim]);

    if (balance >= v)
      return true;

    occupied.xor_bit(to);
  }
  attackers = (attackers | attacks_to(to, ~side_to_move, occupied)) & occupied;

  bool relative_side_to_move = true;

  while (true)
  {
    side_to_move_attackers = attackers & piece_board_[side_to_move][kOccupied];
#if 0
    if (!(state_->pinners_for_king[side_to_move] & ~occupied).test())
      side_to_move_attackers &= ~state_->pinners_for_king[side_to_move];
#endif
    if (!side_to_move_attackers.test())
      return relative_side_to_move;

    next_victim = min_attacker<0>(this, to, side_to_move, side_to_move_attackers, attackers, occupied);

    if (next_victim == kKing)
      return relative_side_to_move == (attackers & piece_board_[~side_to_move][kOccupied]).test();

    balance += relative_side_to_move
      ?
      ExchangePieceValueTable[next_victim]
      :
      -ExchangePieceValueTable[next_victim];

    relative_side_to_move = !relative_side_to_move;
    if (relative_side_to_move == (balance >= v))
      return relative_side_to_move;

    side_to_move = ~side_to_move;
  }
}


uint64_t
Position::key_after(Move m) const
{
  uint64_t board_key = state_->board_key;
  uint64_t hand_key = state_->hand_key;
  Square from = move_from(m);
  Square to = move_to(m);
  Color us = side_to_move_;

  board_key ^= Zobrist::side;

  if (from >= kBoardSquare)
  {
    PieceType drop = to_drop_piece_type(from);
    hand_key -= Zobrist::hands[us][drop];
    board_key += Zobrist::tables[us][drop][to];
  }
  else
  {
    PieceType piece_move = move_piece_type(m);
    bool is_promote = move_is_promote(m);
    if (is_promote)
    {
      board_key -= Zobrist::tables[us][piece_move][from];
      board_key += Zobrist::tables[us][piece_move + kFlagPromoted][to];
    }
    else
    {
      board_key -= Zobrist::tables[us][piece_move][from];
      board_key += Zobrist::tables[us][piece_move][to];
    }

    PieceType piece_capture = move_capture(m);
    if (piece_capture != kPieceNone)
    {
      Color enemy = ~us;
      board_key -= Zobrist::tables[enemy][piece_capture][to];
      hand_key += Zobrist::hands[us][piece_capture & 0x7];
    }
  }

  return board_key + hand_key;
}

uint64_t 
Position::exclusion_key() const
{
  return (state_->board_key + state_->hand_key) ^ Zobrist::exclusion;
}


Repetition 
Position::in_repetition() const
{
  StateInfo *state = state_;
  for (int i = 2; i <= state_->pilies_from_null; i += 2)
  {
    state = state->previous->previous;
    if (state->board_key == state_->board_key)
    {
      if (state->hand_key == state_->hand_key)
      {
        if (state_->continuous_checks[side_to_move_] * 2 >= i)
          return kPerpetualCheckLose;
        else if (state_->continuous_checks[~side_to_move_] * 2 >= i)
          return kPerpetualCheckWin;
        else
          return kRepetition;
      }
      else
      {
        if (is_hand_equal_or_win(state->hand_black, state_->hand_black))
          return kBlackWinRepetition;
        else if (is_hand_equal_or_win(state_->hand_black, state->hand_black))
          return kBlackLoseRepetition;
      }
    }
  }

  return kNoRepetition;
}

std::ostream &
operator<<(std::ostream &os, const Position &pos)
{
  const char *print_name[] =
  {
    "  ", "P ", "L ", "N ", "S ", "B ", "R ", "G ", "K ", "P+", "L+", "N+", "S+", "H ", "D ", "",
    "  ", "p ", "l ", "n ", "s ", "b ", "r ", "g ", "k ", "p+", "l+", "n+", "s+", "h ", "d ", ""
  };
  const string rank_name("abcdefghi");

  os << "\n   9    8    7    6    5    4    3    2    1\n";
  os <<   " +----+----+----+----+----+----+----+----+----+\n";

  int place = 0;
  for (int y = 0; y < 9; y++)
  {
    for (int x = 0; x < 9; x++)
    {
      os << " | " << print_name[pos.squares_[place]];
      ++place;
    }
    os << " | " << rank_name[y] << "\n +----+----+----+----+----+----+----+----+----+\n";
  }
  os << "SideToMove : ";
  if (pos.side_to_move() == kBlack)
    os << "Black";
  else
    os << "White";

  return os;
}

void
Position::print() const
{
  std::cout << *this << std::endl;
}

bool
Position::is_decralation_win() const
{
  Color us = side_to_move_;
  // 宣言する者の玉が入玉している
  if (us == kBlack)
  {
    if (square_king_[kBlack] > k1C)
      return false;
  }
  else
  {
    if (square_king_[kWhite] < k9G)
      return false;
  }

  // 宣言する者の敵陣にいる駒は、玉を除いて10枚以上である
  if
  (
    ((piece_board_[us][kOccupied] ^ piece_board_[us][kKing]) & PromotableMaskTable[us]).popcount()
    <
    10
  )
    return false;

  // 宣言する者の玉に王手がかかっていない
  if (in_check())
    return false;

  BitBoard large_pieces = piece_board_[us][kBishop] | piece_board_[us][kRook] | piece_board_[us][kDragon] | piece_board_[us][kHorse];
  BitBoard small_pieces = (piece_board_[us][kOccupied] ^ piece_board_[us][kKing]);
  small_pieces.not_and(large_pieces);
  large_pieces &= PromotableMaskTable[us];
  small_pieces &= PromotableMaskTable[us];

  int large_score =
    static_cast<int>(large_pieces.popcount())
    +
    number_of(hand_[us], kBishop)
    +
    number_of(hand_[us], kRook);
  int small_score = 
    static_cast<int>(small_pieces.popcount())
    +
    number_of(hand_[us], kPawn)
    +
    number_of(hand_[us], kLance)
    +
    number_of(hand_[us], kKnight)
    +
    number_of(hand_[us], kSilver)
    +
    number_of(hand_[us], kGold);
  
  int score = small_score + 5 * large_score;
  if (us == kBlack)
    return score >= 28;
  else
    return score >= 27;
}