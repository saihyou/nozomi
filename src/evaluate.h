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

#ifndef _EVALUATE_H_
#define _EVALUATE_H_

#include <array>

#include "types.h"
#include "move.h"

class Position;
struct SearchStack;

namespace Eval
{
enum KPPIndex : int16_t
{
  kFHandPawn = 0,
  kEHandPawn = kFHandPawn + 19,
  kFHandLance = kEHandPawn + 19,
  kEHandLance = kFHandLance + 5,
  kFHandKnight = kEHandLance + 5,
  kEHandKnight = kFHandKnight + 5,
  kFHandSilver = kEHandKnight + 5,
  kEHandSilver = kFHandSilver + 5,
  kFHandGold = kEHandSilver + 5,
  kEHandGold = kFHandGold + 5,
  kFHandBishop = kEHandGold + 5,
  kEHandBishop = kFHandBishop + 3,
  kFHandRook = kEHandBishop + 3,
  kEHandRook = kFHandRook + 3,
  kFEHandEnd = kEHandRook + 3,

  kFPawn = kFEHandEnd,
  kEPawn = kFPawn + 81,
  kFLance = kEPawn + 81,
  kELance = kFLance + 81,
  kFKnight = kELance + 81,
  kEKnight = kFKnight + 81,
  kFSilver = kEKnight + 81,
  kESilver = kFSilver + 81,
  kFGold = kESilver + 81,
  kEGold = kFGold + 81,
  kFBishop = kEGold + 81,
  kEBishop = kFBishop + 81,
  kFHorse = kEBishop + 81,
  kEHorse = kFHorse + 81,
  kFRook = kEHorse + 81,
  kERook = kFRook + 81,
  kFDragon = kERook + 81,
  kEDragon = kFDragon + 81,
  kFEEnd = kEDragon + 81,
  kFENone = kFEEnd
};

inline KPPIndex
operator+(KPPIndex d1, int i)
{
  return KPPIndex(int(d1) + i);
}

constexpr KPPIndex
PieceToIndexBlackTable[kPieceMax] =
{
  kFENone,  // kEmpty
  kFPawn,   // kBlackPawn
  kFLance,  // kBlackLance
  kFKnight, // kBlackKnight
  kFSilver, // kBlackSilver
  kFBishop, // kBlackBishop
  kFRook,   // kBlackRook
  kFGold,   // kBlackGold
  kFENone,  // kBlackKing
  kFGold,   // kBlackPromotedPawn
  kFGold,   // kBlackPromotedLance
  kFGold,   // kBlackPromotedKnight
  kFGold,   // kBlackPromotedSilver
  kFHorse,  // kBlackHorse
  kFDragon, // kBlackDragon
  kFENone,  // 15
  kFENone,  // kFlagWhite
  kEPawn,   // kWhitePawn
  kELance,  // kWhiteLance
  kEKnight, // kWhiteKnight
  kESilver, // kWhiteSilver
  kEBishop, // kWhiteBishop
  kERook,   // kWhiteRook
  kEGold,   // kWhiteGold
  kFENone,  // kWhiteKing
  kEGold,   // kWhitePromotedPawn
  kEGold,   // kWhitePromotedLance
  kEGold,   // kWhitePromotedKnight
  kEGold,   // kWhitePromotedSilver
  kEHorse,  // kWhiteHorse
  kEDragon  // kWhiteDragon
};

constexpr KPPIndex
PieceToIndexWhiteTable[kPieceMax] =
{
  kFENone,  // kEmpty
  kEPawn,   // kBlackPawn
  kELance,  // kBlackLance
  kEKnight, // kBlackKnight
  kESilver, // kBlackSilver
  kEBishop, // kBlackBishop
  kERook,   // kBlackRook
  kEGold,   // kBlackGold
  kFENone,  // kBlackKing
  kEGold,   // kBlackPromotedPawn
  kEGold,   // kBlackPromotedLance
  kEGold,   // kBlackPromotedKnight
  kEGold,   // kBlackPromotedSilver
  kEHorse,  // kBlackHorse
  kEDragon, // kBlackDragon
  kFENone,  // 15
  kFENone,  // kFlagWhite
  kFPawn,   // kWhitePawn
  kFLance,  // kWhiteLance
  kFKnight, // kWhiteKnight
  kFSilver, // kWhiteSilver
  kFBishop, // kWhiteBishop
  kFRook,   // kWhiteRook
  kFGold,   // kWhiteGold
  kFENone,  // kWhiteKing
  kFGold,   // kWhitePromotedPawn
  kFGold,   // kWhitePromotedLance
  kFGold,   // kWhitePromotedKnight
  kFGold,   // kWhitePromotedSilver
  kFHorse,  // kWhiteHorse
  kFDragon  // kWhiteDragon
};

constexpr KPPIndex
PieceTypeToBlackHandIndexTable[kNumberOfColor][kPieceTypeMax] =
{
  {
    kFEHandEnd,
    kFHandPawn,
    kFHandLance,
    kFHandKnight,
    kFHandSilver,
    kFHandBishop,
    kFHandRook,
    kFHandGold,
    kFEHandEnd,
    kFHandPawn,
    kFHandLance,
    kFHandKnight,
    kFHandSilver,
    kFHandBishop,
    kFHandRook
  },
  {
    kFEHandEnd,
    kEHandPawn,
    kEHandLance,
    kEHandKnight,
    kEHandSilver,
    kEHandBishop,
    kEHandRook,
    kEHandGold,
    kFEHandEnd,
    kEHandPawn,
    kEHandLance,
    kEHandKnight,
    kEHandSilver,
    kEHandBishop,
    kEHandRook
  }
};

constexpr KPPIndex
PieceTypeToWhiteHandIndexTable[kNumberOfColor][kPieceTypeMax] =
{
  {
    kFEHandEnd,
    kEHandPawn,
    kEHandLance,
    kEHandKnight,
    kEHandSilver,
    kEHandBishop,
    kEHandRook,
    kEHandGold,
    kFEHandEnd,
    kEHandPawn,
    kEHandLance,
    kEHandKnight,
    kEHandSilver,
    kEHandBishop,
    kEHandRook
  },
  {
    kFEHandEnd,
    kFHandPawn,
    kFHandLance,
    kFHandKnight,
    kFHandSilver,
    kFHandBishop,
    kFHandRook,
    kFHandGold,
    kFEHandEnd,
    kFHandPawn,
    kFHandLance,
    kFHandKnight,
    kFHandSilver,
    kFHandBishop,
    kFHandRook
  }
};


constexpr int 
KPPHandIndex[8] =
{
  0,  // None
  0,  // Pawn
  2,  // Lance
  4,  // Knight
  6,  // Silver
  10, // Bishop
  12, // Rook
  8   //Gold
};

enum PieceValue
{
  kPawnValue      = 88,
  kLanceValue     = 238,
  kKnightValue    = 259,
  kSilverValue    = 370,
  kGoldValue      = 448,
  kProSilverValue = 488,
  kProLanceValue  = 493,
  kProKnightValue = 518,
  kProPawnValue   = 551,
  kBishopValue    = 565,
  kRookValue      = 637,
  kHorseValue     = 831,
  kDragonValue    = 954,
  kKingValue      = 15000
};

constexpr int
PieceValueTable[kPieceTypeMax] =
{
  0,
  kPawnValue,
  kLanceValue,
  kKnightValue,
  kSilverValue,
  kBishopValue,
  kRookValue,
  kGoldValue,
  kKingValue,
  kProPawnValue,
  kProLanceValue,
  kProKnightValue,
  kProSilverValue,
  kHorseValue,
  kDragonValue
};

constexpr int
PromotePieceValueTable[7] =
{
  0,
  PieceValueTable[kPromotedPawn]   - PieceValueTable[kPawn],
  PieceValueTable[kPromotedLance]  - PieceValueTable[kLance],
  PieceValueTable[kPromotedKnight] - PieceValueTable[kKnight],
  PieceValueTable[kPromotedSilver] - PieceValueTable[kSilver],
  PieceValueTable[kHorse]          - PieceValueTable[kBishop],
  PieceValueTable[kDragon]         - PieceValueTable[kRook]
};

constexpr int
ExchangePieceValueTable[kPieceTypeMax] =
{
  0,
  PieceValueTable[kPawn] * 2,
  PieceValueTable[kLance] * 2,
  PieceValueTable[kKnight] * 2,
  PieceValueTable[kSilver] * 2,
  PieceValueTable[kBishop] * 2,
  PieceValueTable[kRook] * 2,
  PieceValueTable[kGold] * 2,
  0,
  PieceValueTable[kPromotedPawn] + PieceValueTable[kPawn],
  PieceValueTable[kPromotedLance] + PieceValueTable[kLance],
  PieceValueTable[kPromotedKnight] + PieceValueTable[kKnight],
  PieceValueTable[kPromotedSilver] + PieceValueTable[kSilver],
  PieceValueTable[kHorse] + PieceValueTable[kBishop],
  PieceValueTable[kDragon] + PieceValueTable[kRook]
};

constexpr int kTableSize = 65536;

struct EvalParts
{
  Value black_kpp;
  Value white_kpp;
  Value kkpt;
};

struct Entry
{
  Key       key;
  EvalParts parts;
};

struct HashTable
{
  Entry *
  operator[](Key key)
  {
    return &table[(uint32_t)key & (kTableSize - 1)];
  }

private:
  std::array<Entry, kTableSize> table;
};

constexpr int
kListNum = 38;

constexpr int
kFvScale = 32;

inline Square
inverse(Square sq)
{
  return static_cast<Square>(kBoardSquare - 1 - sq);
}

bool
init();

Value 
evaluate(const Position &pos, SearchStack *ss);

Value
calc_kkpt_value(const Position &pos);

extern int16_t KPP[kBoardSquare][kFEEnd][kFEEnd];
extern int16_t KKPT[kBoardSquare][kBoardSquare][kFEEnd][kNumberOfColor];

} // namespace Eval

#endif
