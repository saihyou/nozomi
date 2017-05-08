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

#ifndef _BIT_BOARD_H_
#define _BIT_BOARD_H_

#include <smmintrin.h>
#include <nmmintrin.h>
#include <stdint.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#include "types.h"

class BitBoard;

extern const BitBoard
MaskTable[kBoardSquare];
extern BitBoard
RookMaskTable[kBoardSquare];
extern BitBoard
BishopMaskTable[kBoardSquare];
extern BitBoard
LanceMaskTable[kNumberOfColor][kBoardSquare];

extern const BitBoard
Left45MaskTable[17];
extern const BitBoard
Right45MaskTable[17];
extern const BitBoard
RankMaskTable[kNumberOfRank];
extern const BitBoard
FileMaskTable[kNumberOfFile];

extern BitBoard
PawnAttacksTable[kNumberOfColor][kBoardSquare];
extern BitBoard
SilverAttacksTable[kNumberOfColor][kBoardSquare];
extern BitBoard
GoldAttacksTable[kNumberOfColor][kBoardSquare];
extern BitBoard
KingAttacksTable[kBoardSquare];
extern BitBoard
KnightAttacksTable[kNumberOfColor][kBoardSquare];
extern BitBoard *
LanceAttacksTable[kNumberOfColor][kBoardSquare];
extern BitBoard *
RookAttacksTable[kBoardSquare];
extern BitBoard *
BishopAttacksTable[kBoardSquare];
extern BitBoard
FileAttacksTable[kBoardSquare][128];
extern BitBoard
RankAttacksTable[kBoardSquare][128];
extern BitBoard
Left45AttacksTable[kBoardSquare][128];
extern BitBoard
Right45AttacksTable[kBoardSquare][128];
extern BitBoard
RookStepAttacksTable[kBoardSquare];
extern BitBoard
BishopStepAttacksTable[kBoardSquare];


extern Direction
DirectionTable[kBoardSquare][kBoardSquare];

extern BitBoard
BetweenTable[kBoardSquare][kBoardSquare];

extern BitBoard
PawnDropableTable[512][kNumberOfColor];
extern const BitBoard
KnightDropableMaskTable[kNumberOfColor];
extern const BitBoard
LanceDropableMaskTable[kNumberOfColor];

extern const BitBoard
PromotableMaskTable[kNumberOfColor];
extern const BitBoard
MustPromoteMaskTable[kNumberOfColor];
extern const  BitBoard
NotPromotableMaskTable[kNumberOfColor];

constexpr File
FilePositionTable[kBoardSquare] =
{
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
  kFile1, kFile2, kFile3, kFile4, kFile5, kFile6, kFile7, kFile8, kFile9,
};

constexpr Rank
RankPositionTable[kBoardSquare] =
{
  kRank1, kRank1, kRank1, kRank1, kRank1, kRank1, kRank1, kRank1, kRank1,
  kRank2, kRank2, kRank2, kRank2, kRank2, kRank2, kRank2, kRank2, kRank2,
  kRank3, kRank3, kRank3, kRank3, kRank3, kRank3, kRank3, kRank3, kRank3,
  kRank4, kRank4, kRank4, kRank4, kRank4, kRank4, kRank4, kRank4, kRank4,
  kRank5, kRank5, kRank5, kRank5, kRank5, kRank5, kRank5, kRank5, kRank5,
  kRank6, kRank6, kRank6, kRank6, kRank6, kRank6, kRank6, kRank6, kRank6,
  kRank7, kRank7, kRank7, kRank7, kRank7, kRank7, kRank7, kRank7, kRank7,
  kRank8, kRank8, kRank8, kRank8, kRank8, kRank8, kRank8, kRank8, kRank8,
  kRank9, kRank9, kRank9, kRank9, kRank9, kRank9, kRank9, kRank9, kRank9
};

constexpr int
Left45MaskIndexTable[kBoardSquare] =
{
  8,  7,  6,  5,  4,  3,  2,  1,  0,
  9,  8,  7,  6,  5,  4,  3,  2,  1,
  10,  9,  8,  7,  6,  5,  4,  3,  2,
  11, 10,  9,  8,  7,  6,  5,  4,  3,
  12, 11, 10,  9,  8,  7,  6,  5,  4,
  13, 12, 11, 10,  9,  8,  7,  6,  5,
  14, 13, 12, 11, 10,  9,  8,  7,  6,
  15, 14, 13, 12, 11, 10,  9,  8,  7,
  16, 15, 14, 13, 12, 11, 10,  9,  8
};

constexpr int
Right45MaskIndexTable[kBoardSquare] =
{
  0,  1,  2,  3,  4,  5,  6,  7,  8,
  1,  2,  3,  4,  5,  6,  7,  8,  9,
  2,  3,  4,  5,  6,  7,  8,  9, 10,
  3,  4,  5,  6,  7,  8,  9, 10, 11,
  4,  5,  6,  7,  8,  9, 10, 11, 12,
  5,  6,  7,  8,  9, 10, 11, 12, 13,
  6,  7,  8,  9, 10, 11, 12, 13, 14,
  7,  8,  9, 10, 11, 12, 13, 14, 15,
  8,  9, 10, 11, 12, 13, 14, 15, 16
};

class BitBoard
{
public:
  BitBoard() {}

  BitBoard(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, b.board_);
  }

  BitBoard(uint64_t val0, uint64_t val1)
  {
    this->u64_[0] = val0;
    this->u64_[1] = val1;
  }

  BitBoard &
  operator=(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, b.board_);
    return *this;
  }

  __m128i
  board() const
  {
    return board_;
  }

  uint64_t
  to_uint64() const
  {
    return this->u64_[0] | this->u64_[1];
  }

  bool
  test() const
  {
    return !_mm_testz_si128(board_, _mm_set1_epi8(-1));
  }

  bool
  contract(const BitBoard &b) const
  {
    return !_mm_testz_si128(board_, b.board());
  }

  uint64_t
  popcount() const
  {
    return _mm_popcnt_u64(this->u64_[0]) + _mm_popcnt_u32(this->u32_[2]);
  }

  uint64_t
  magic_index(const BitBoard &mask) const
  {
    return _pext_u64(to_uint64(), mask.to_uint64());
  }

  void
  init()
  {
    board_ = _mm_setzero_si128();
  }

  BitBoard
  operator~() const
  {
    BitBoard tmp;
    const BitBoard b(0x7fffffffffffffffLLU, 0x3ffffU);
    _mm_store_si128(&tmp.board_, _mm_andnot_si128(this->board_, b.board_));
    return tmp;
  }

  BitBoard
  operator&=(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, _mm_and_si128(this->board_, b.board_));
    return *this;
  }

  BitBoard
  operator|=(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, _mm_or_si128(board_, b.board()));
    return *this;
  }

  BitBoard
  operator^=(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, _mm_xor_si128(this->board_, b.board_));
    return *this;
  }

  void
  and_or(const BitBoard &b1, const BitBoard &b2)
  {
    _mm_store_si128(&this->board_, _mm_or_si128(board_, _mm_and_si128(b1.board_, b2.board_)));
  }

  void
  not_and(const BitBoard &b)
  {
    _mm_store_si128(&this->board_, _mm_andnot_si128(b.board_, this->board_));
  }

  void
  xor_bit(int sq)
  {
    *this ^= MaskTable[sq];
  }

  void
  set(int index, uint64_t value)
  {
    this->u64_[index] = value;
  }

  void
  set(__m128i m)
  {
    _mm_store_si128(&this->board_, m);
  }

  uint64_t
  operator[](int index) const
  {
    return this->u64_[index];
  }

  BitBoard
  operator&(const BitBoard &b) const
  {
    return BitBoard(*this) &= b;
  }

  BitBoard
  operator|(const BitBoard &b) const
  {
    return BitBoard(*this) |= b;
  }

  BitBoard
  operator^(const BitBoard &b) const
  {
    return BitBoard(*this) ^= b;
  }

#if defined (_MSC_VER)
  uint32_t
  first_one() const
  {
    unsigned long index;

    if (_BitScanForward64(&index, this->u64_[0]))
      return index;

    _BitScanForward(&index, this->u32_[2]);
    return 63 + index;
  }

  uint32_t
  last_one() const
  {
    unsigned long index;

    if (_BitScanReverse(&index, this->u32_[2]))
      return 63 + index;

    _BitScanReverse64(&index, this->u64_[0]);
    return index;
  }
#else
  uint32_t
  first_one() const
  {
    if (u64_[0] > 0)
      return __builtin_ctzll(u64_[0]);

    return __builtin_ctz(u32_[2]) + 63;
  }

  uint32_t
  last_one() const
  {
    if (u32_[2] > 0)
      return 94 - __builtin_clz(this->u32_[2]);

    return 63 - __builtin_clzll(u64_[0]);
  }
#endif

  Square
  pop_bit()
  {
    uint32_t bit = first_one();
    xor_bit(bit);
    return static_cast<Square>(bit);
  }

  void
  print() const;

  static void
  initialize();

private:
  union
  {
    uint32_t u32_[4];
    uint64_t u64_[2];
    __m128i board_;
  };
};

inline BitBoard
lance_attack(const BitBoard &occupied, Color color, Square sq)
{
  const BitBoard b(occupied & LanceMaskTable[color][sq]);
  return LanceAttacksTable[color][sq][b.magic_index(LanceMaskTable[color][sq])];
}

inline BitBoard
bishop_attack(const BitBoard &occupied, Square sq)
{
  const BitBoard b(occupied & BishopMaskTable[sq]);
  return BishopAttacksTable[sq][b.magic_index(BishopMaskTable[sq])];
}

inline BitBoard
rook_attack(const BitBoard &occupied, Square sq)
{
  const BitBoard b(occupied & RookMaskTable[sq]);
  return RookAttacksTable[sq][b.magic_index(RookMaskTable[sq])];
}

inline BitBoard
horse_attack(const BitBoard &occupied, Square sq)
{
  return bishop_attack(occupied, sq) | KingAttacksTable[sq];
}

inline BitBoard
dragon_attack(const BitBoard &occupied, Square sq)
{
  return rook_attack(occupied, sq) | KingAttacksTable[sq];
}

inline BitBoard
pawn_attack(Color color, const BitBoard &piece)
{
  return
    (color == kBlack)
    ?
    BitBoard((piece[0] >> 9 & 0x7fffffffffffffffLLU) | (piece[1] & 0x1ff) << 54, piece[1] >> 9)
    :
    BitBoard(piece[0] << 9, (piece[0] & 0x7fc0000000000000LLU) >> 54 | piece[1] << 9);
}

// s1, s2, s3が縦or横or斜めの一直線上に並んでいるか
inline bool
aligned(Square s1, Square s2, Square s3)
{
  return DirectionTable[s1][s2] != kDirMisc && DirectionTable[s1][s2] == DirectionTable[s1][s3];
}

#endif
