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

#ifndef _MOVE_H_
#define _MOVE_H_

#include <stdint.h>
#include <string>
#include <immintrin.h>
#include "types.h"

class Position;

// 移動先
// xxxxxxxx xxxxxxxx x1111111
//
// 移動元 駒を打つ場合は、81を加算
// xxxxxxxx xx111111 1xxxxxxx
//
// 成り
// xxxxxxxx x1xxxxxx xxxxxxxx
//
// 移動する駒
// xxxxx111 1xxxxxxx xxxxxxxx
//
// 取得する駒
// x1111xxx xxxxxxxx xxxxxxxx
enum Move : uint32_t
{
  kMoveNone = 0,
  kMoveNull = 0x800000,
  kPromoted = 1U << 14
};

struct ExtMove 
{
  Move  move;
  int   value;

  operator Move() const
  {
    return move;
  }

  void
  operator=(Move m)
  {
    move = m;
  }
};

inline Move 
move_init(Square from, Square to, PieceType piece, PieceType capture, bool is_promote)
{
    return 
      Move
      (
        (static_cast<uint32_t>(from)       << 7)
        |
        (static_cast<uint32_t>(to)         << 0)
        |
        (static_cast<uint32_t>(piece)      << 15)
        |
        (static_cast<uint32_t>(capture)    << 19)
        |
        (static_cast<uint32_t>(is_promote) << 14)
      );
}

inline Move
move_init(Square to, PieceType drop)
{
  return 
    Move
    (
      (static_cast<uint32_t>(to) << 0)
      |
      ((static_cast<uint32_t>(drop) + kBoardSquare - 1) << 7)
    );
}

inline Square
move_from(Move m)
{
  return Square((m >> 7) & 0x007fU);
}

inline Square 
move_to(Move m)
{
  return Square((m >> 0) & 0x007fU);
}

inline PieceType 
move_piece_type(Move m)
{
  return PieceType((m >> 15) & 0x000fU);
}

inline Piece
move_piece(Move m, Color c)
{
  const Square    from = move_from(m);
  const PieceType type =
    (from >= kBoardSquare)
    ?
    to_drop_piece_type(from)
    :
    move_piece_type(m);

  return Piece(c << 4 | type);
}

inline PieceType
move_capture(Move m)
{
  return PieceType((m >> 19) & 0x000fU);
}

inline bool
move_is_promote(Move m)
{
  return (m & kPromoted) != 0;
}

inline bool
move_is_capture(Move m)
{
  return ((m >> 19) & 0x000fU) != 0;
}

inline bool
move_is_capture_or_promotion(Move m)
{
  return move_is_promote(m) || move_is_capture(m);
}

inline bool 
is_ok(Move m)
{
  return move_from(m) != move_to(m);
}

inline bool 
operator<(const ExtMove& f, const ExtMove& s) 
{
  return f.value < s.value;
}

constexpr uint32_t
HandShiftTable[kPieceTypeMax] =
{
  0,  // None
  0,  // Pawn
  6,  // Lance
  10, // Knight
  14, // Silver
  22, // Bishop
  25, // Rook
  18, // Gold
  0,  // King
  0,  // ProPawn
  6,  // ProLance
  10, // ProKnight
  14, // ProSilver
  22, // Horse
  25  // Dragon
};

// Handの構造はAperyを参考にしている
// xxxxxxxx xxxxxxxx xxxxxxxx xxx11111  Pawn
// xxxxxxxx xxxxxxxx xxxxxxx1 11xxxxxx  Lance
// xxxxxxxx xxxxxxxx xxx111xx xxxxxxxx  Knight
// xxxxxxxx xxxxxxx1 11xxxxxx xxxxxxxx  Silver
// xxxxxxxx xxx111xx xxxxxxxx xxxxxxxx  Gold
// xxxxxxxx 11xxxxxx xxxxxxxx xxxxxxxx  Bishop
// xxxxx11x xxxxxxxx xxxxxxxx xxxxxxxx  Rook
enum Hand : uint32_t
{
  kHandZero   = 0,
  kHandPawn   = 1 << HandShiftTable[kPawn],
  kHandLance  = 1 << HandShiftTable[kLance],
  kHandKnight = 1 << HandShiftTable[kKnight],
  kHandSilver = 1 << HandShiftTable[kSilver],
  kHandGold   = 1 << HandShiftTable[kGold],
  kHandBishop = 1 << HandShiftTable[kBishop],
  kHandRook   = 1 << HandShiftTable[kRook]
};

constexpr uint32_t
HandMaskTable[kPieceTypeMax] =
{
  0,         // None
  0x1fUL << HandShiftTable[kPawn],
  0x7UL  << HandShiftTable[kLance],
  0x7UL  << HandShiftTable[kKnight],
  0x7UL  << HandShiftTable[kSilver],
  0x3UL  << HandShiftTable[kBishop],
  0x3UL  << HandShiftTable[kRook],
  0x7UL  << HandShiftTable[kGold],
  0,
  0x1fUL << HandShiftTable[kPromotedPawn],
  0x7UL  << HandShiftTable[kPromotedLance],
  0x7UL  << HandShiftTable[kPromotedKnight],
  0x7UL  << HandShiftTable[kPromotedSilver],
  0x3UL  << HandShiftTable[kHorse],
  0x3UL  << HandShiftTable[kDragon]
};

constexpr Hand
PieceTypeToHandTable[kPieceTypeMax] =
{
  kHandZero,   // None
  kHandPawn,   // Pawn
  kHandLance,  // Lance
  kHandKnight, // Knight
  kHandSilver, // Silver
  kHandBishop, // Bishop
  kHandRook,   // Rook
  kHandGold,   // Gold
  kHandZero,   // King
  kHandPawn,   // ProPawn
  kHandLance,  // ProLance
  kHandKnight, // ProKnight
  kHandSilver, // ProSilver
  kHandBishop, // Horse
  kHandRook    // Dragon
};

enum HandType : uint32_t
{
  kHandPawnExist = (HandMaskTable[kPawn] + (1 << HandShiftTable[kPawn])),
  kHandLanceExist = (HandMaskTable[kLance] + (1 << HandShiftTable[kLance])),
  kHandKnightExist = (HandMaskTable[kKnight] + (1 << HandShiftTable[kKnight])),
  kHandSilverExist = (HandMaskTable[kSilver] + (1 << HandShiftTable[kSilver])),
  kHandGoldExist = (HandMaskTable[kGold] + (1 << HandShiftTable[kGold])),
  kHandBishopExist = (HandMaskTable[kBishop] + (1 << HandShiftTable[kBishop])),
  kHandRookExist = (HandMaskTable[kRook] + (1 << HandShiftTable[kRook])),
  kHandTypeMask  = kHandPawnExist | kHandLanceExist | kHandKnightExist | kHandSilverExist | kHandGoldExist | kHandBishopExist | kHandRookExist
};

constexpr uint32_t
kHandBorrowMask =
#if 1
  153231904U;
#else
  (
    (HandMaskTable[kPawn]   + (1 << HandShiftTable[kPawn]))
    |
    (HandMaskTable[kLance]  + (1 << HandShiftTable[kLance]))
    |
    (HandMaskTable[kKnight] + (1 << HandShiftTable[kKnight]))
    |
    (HandMaskTable[kSilver] + (1 << HandShiftTable[kSilver]))
    |
    (HandMaskTable[kGold]   + (1 << HandShiftTable[kGold]))
    |
    (HandMaskTable[kBishop] + (1 << HandShiftTable[kBishop]))
    |
    (HandMaskTable[kRook]   + (1 << HandShiftTable[kRook]))
  );
#endif

inline bool
has_hand(Hand h, PieceType p)
{
  return (h & HandMaskTable[p]) != kHandZero;
}

inline bool
has_hand_except_pawn(Hand h)
{
  return (h >> HandShiftTable[kLance]) != kHandZero;
}

inline HandType
extract_piece_without_pawn(Hand h)
{
  return HandType((h + 0x6DDDDC0) & kHandTypeMask);
}

inline int
number_of(Hand h, PieceType p)
{
  return (h & HandMaskTable[p]) >> HandShiftTable[p];
}

inline void
add_hand(Hand &h, PieceType p)
{
  h = static_cast<Hand>(h + PieceTypeToHandTable[p]);
}

inline void
sub_hand(Hand &h, PieceType p)
{
  h = static_cast<Hand>(h - PieceTypeToHandTable[p]);
}

inline bool
is_hand_equal_or_win(Hand ref, Hand targ)
{
#if 1
  return ((targ - ref) & kHandBorrowMask) == 0;
#else
  return 
    (
      number_of(targ, kPawn) >= number_of(ref, kPawn)
      &&
      number_of(targ, kLance) >= number_of(ref, kLance)
      &&
      number_of(targ, kKnight) >= number_of(ref, kKnight)
      &&
      number_of(targ, kSilver) >= number_of(ref, kSilver)
      &&
      number_of(targ, kGold) >= number_of(ref, kGold)
      &&
      number_of(targ, kBishop) >= number_of(ref, kBishop)
      &&
      number_of(targ, kRook) >= number_of(ref, kRook)
    );
#endif
}

#endif
