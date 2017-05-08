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

#include <iostream>
#include "bit_board.h"

const BitBoard
MaskTable[kBoardSquare] =
{
  BitBoard(1ULL <<  0, 0), BitBoard(1ULL <<  1, 0), BitBoard(1ULL <<  2, 0),  BitBoard(1ULL <<  3, 0), BitBoard(1ULL <<  4, 0), BitBoard(1ULL <<  5, 0), BitBoard(1ULL <<  6, 0), BitBoard(1ULL <<  7, 0), BitBoard(1ULL <<  8, 0),
  BitBoard(1ULL <<  9, 0), BitBoard(1ULL << 10, 0), BitBoard(1ULL << 11, 0),  BitBoard(1ULL << 12, 0), BitBoard(1ULL << 13, 0), BitBoard(1ULL << 14, 0), BitBoard(1ULL << 15, 0), BitBoard(1ULL << 16, 0), BitBoard(1ULL << 17, 0),
  BitBoard(1ULL << 18, 0), BitBoard(1ULL << 19, 0), BitBoard(1ULL << 20, 0),  BitBoard(1ULL << 21, 0), BitBoard(1ULL << 22, 0), BitBoard(1ULL << 23, 0), BitBoard(1ULL << 24, 0), BitBoard(1ULL << 25, 0), BitBoard(1ULL << 26, 0),
  BitBoard(1ULL << 27, 0), BitBoard(1ULL << 28, 0), BitBoard(1ULL << 29, 0),  BitBoard(1ULL << 30, 0), BitBoard(1ULL << 31, 0), BitBoard(1ULL << 32, 0), BitBoard(1ULL << 33, 0), BitBoard(1ULL << 34, 0), BitBoard(1ULL << 35, 0),
  BitBoard(1ULL << 36, 0), BitBoard(1ULL << 37, 0), BitBoard(1ULL << 38, 0),  BitBoard(1ULL << 39, 0), BitBoard(1ULL << 40, 0), BitBoard(1ULL << 41, 0), BitBoard(1ULL << 42, 0), BitBoard(1ULL << 43, 0), BitBoard(1ULL << 44, 0),
  BitBoard(1ULL << 45, 0), BitBoard(1ULL << 46, 0), BitBoard(1ULL << 47, 0),  BitBoard(1ULL << 48, 0), BitBoard(1ULL << 49, 0), BitBoard(1ULL << 50, 0), BitBoard(1ULL << 51, 0), BitBoard(1ULL << 52, 0), BitBoard(1ULL << 53, 0),
  BitBoard(1ULL << 54, 0), BitBoard(1ULL << 55, 0), BitBoard(1ULL << 56, 0),  BitBoard(1ULL << 57, 0), BitBoard(1ULL << 58, 0), BitBoard(1ULL << 59, 0), BitBoard(1ULL << 60, 0), BitBoard(1ULL << 61, 0), BitBoard(1ULL << 62, 0),
  BitBoard(0, 1ULL <<  0), BitBoard(0, 1ULL <<  1), BitBoard(0, 1ULL <<  2),  BitBoard(0, 1ULL <<  3), BitBoard(0, 1ULL <<  4), BitBoard(0, 1ULL <<  5), BitBoard(0, 1ULL <<  6), BitBoard(0, 1ULL <<  7), BitBoard(0, 1ULL <<  8),
  BitBoard(0, 1ULL <<  9), BitBoard(0, 1ULL << 10), BitBoard(0, 1ULL << 11),  BitBoard(0, 1ULL << 12), BitBoard(0, 1ULL << 13), BitBoard(0, 1ULL << 14), BitBoard(0, 1ULL << 15), BitBoard(0, 1ULL << 16), BitBoard(0, 1ULL << 17),
};

const BitBoard
FileMaskTable[kNumberOfFile] =
{
  BitBoard(((1ULL << 0) | (1ULL <<  9) | (1ULL << 18) | (1ULL << 27) | (1ULL << 36) | (1ULL << 45) | (1ULL << 54)), ((1ULL << 0) | (1ULL <<  9))),
  BitBoard(((1ULL << 1) | (1ULL << 10) | (1ULL << 19) | (1ULL << 28) | (1ULL << 37) | (1ULL << 46) | (1ULL << 55)), ((1ULL << 1) | (1ULL << 10))),
  BitBoard(((1ULL << 2) | (1ULL << 11) | (1ULL << 20) | (1ULL << 29) | (1ULL << 38) | (1ULL << 47) | (1ULL << 56)), ((1ULL << 2) | (1ULL << 11))),
  BitBoard(((1ULL << 3) | (1ULL << 12) | (1ULL << 21) | (1ULL << 30) | (1ULL << 39) | (1ULL << 48) | (1ULL << 57)), ((1ULL << 3) | (1ULL << 12))),
  BitBoard(((1ULL << 4) | (1ULL << 13) | (1ULL << 22) | (1ULL << 31) | (1ULL << 40) | (1ULL << 49) | (1ULL << 58)), ((1ULL << 4) | (1ULL << 13))),
  BitBoard(((1ULL << 5) | (1ULL << 14) | (1ULL << 23) | (1ULL << 32) | (1ULL << 41) | (1ULL << 50) | (1ULL << 59)), ((1ULL << 5) | (1ULL << 14))),
  BitBoard(((1ULL << 6) | (1ULL << 15) | (1ULL << 24) | (1ULL << 33) | (1ULL << 42) | (1ULL << 51) | (1ULL << 60)), ((1ULL << 6) | (1ULL << 15))),
  BitBoard(((1ULL << 7) | (1ULL << 16) | (1ULL << 25) | (1ULL << 34) | (1ULL << 43) | (1ULL << 52) | (1ULL << 61)), ((1ULL << 7) | (1ULL << 16))),
  BitBoard(((1ULL << 8) | (1ULL << 17) | (1ULL << 26) | (1ULL << 35) | (1ULL << 44) | (1ULL << 53) | (1ULL << 62)), ((1ULL << 8) | (1ULL << 17)))
};

const BitBoard
RankMaskTable[kNumberOfRank] =
{
  BitBoard((0x1FFULL << 0), 0), BitBoard((0x1FFULL << 9), 0), BitBoard((0x1FFULL << 18), 0), BitBoard((0x1FFULL << 27), 0), BitBoard((0x1FFULL << 36), 0), BitBoard((0x1FFULL << 45), 0), BitBoard((0x1FFULL << 54), 0),
  BitBoard(0, (0x1FFULL << 0)), BitBoard(0, (0x1FFULL << 9))
};

const BitBoard
Right45MaskTable[17] =
{
  BitBoard(1ULL, 0),
  BitBoard(((1ULL << 1) | (1ULL << 9)), 0),
  BitBoard(((1ULL << 2) | (1ULL << 10) | (1ULL << 18)), 0),
  BitBoard(((1ULL << 3) | (1ULL << 11) | (1ULL << 19) | (1ULL << 27)), 0),
  BitBoard(((1ULL << 4) | (1ULL << 12) | (1ULL << 20) | (1ULL << 28) | (1ULL << 36)), 0),
  BitBoard(((1ULL << 5) | (1ULL << 13) | (1ULL << 21) | (1ULL << 29) | (1ULL << 37) | (1ULL << 45)), 0),
  BitBoard(((1ULL << 6) | (1ULL << 14) | (1ULL << 22) | (1ULL << 30) | (1ULL << 38) | (1ULL << 46) | (1ULL << 54)), 0),
  BitBoard(((1ULL << 7) | (1ULL << 15) | (1ULL << 23) | (1ULL << 31) | (1ULL << 39) | (1ULL << 47) | (1ULL << 55)), 1ULL),
  BitBoard(((1ULL << 8) | (1ULL << 16) | (1ULL << 24) | (1ULL << 32) | (1ULL << 40) | (1ULL << 48) | (1ULL << 56)), ((1ULL << 1) | (1ULL << 9))),
  BitBoard((              (1ULL << 17) | (1ULL << 25) | (1ULL << 33) | (1ULL << 41) | (1ULL << 49) | (1ULL << 57)), ((1ULL << 2) | (1ULL << 10))),
  BitBoard((                             (1ULL << 26) | (1ULL << 34) | (1ULL << 42) | (1ULL << 50) | (1ULL << 58)), ((1ULL << 3) | (1ULL << 11))),
  BitBoard((                                            (1ULL << 35) | (1ULL << 43) | (1ULL << 51) | (1ULL << 59)), ((1ULL << 4) | (1ULL << 12))),
  BitBoard((                                                           (1ULL << 44) | (1ULL << 52) | (1ULL << 60)), ((1ULL << 5) | (1ULL << 13))),
  BitBoard((                                                                          (1ULL << 53) | (1ULL << 61)), ((1ULL << 6) | (1ULL << 14))),
  BitBoard(                                                                                          (1ULL << 62) , ((1ULL << 7) | (1ULL << 15))),
  BitBoard(                                                                                                      0, ((1ULL << 8) | (1ULL << 16))),
  BitBoard(                                                                                                      0,                (1ULL << 17))
};

const BitBoard
Left45MaskTable[17] =
{
  BitBoard( (1ULL << 8), 0),
  BitBoard(((1ULL << 7) | (1ULL << 17)), 0),
  BitBoard(((1ULL << 6) | (1ULL << 16) | (1ULL << 26)), 0),
  BitBoard(((1ULL << 5) | (1ULL << 15) | (1ULL << 25) | (1ULL << 35)), 0),
  BitBoard(((1ULL << 4) | (1ULL << 14) | (1ULL << 24) | (1ULL << 34) | (1ULL << 44)), 0),
  BitBoard(((1ULL << 3) | (1ULL << 13) | (1ULL << 23) | (1ULL << 33) | (1ULL << 43) | (1ULL << 53)), 0),
  BitBoard(((1ULL << 2) | (1ULL << 12) | (1ULL << 22) | (1ULL << 32) | (1ULL << 42) | (1ULL << 52) | (1ULL << 62)), 0),
  BitBoard(((1ULL << 1) | (1ULL << 11) | (1ULL << 21) | (1ULL << 31) | (1ULL << 41) | (1ULL << 51) | (1ULL << 61)),  (1ULL << 8)),
  BitBoard(((1ULL << 0) | (1ULL << 10) | (1ULL << 20) | (1ULL << 30) | (1ULL << 40) | (1ULL << 50) | (1ULL << 60)), ((1ULL << 7) | (1ULL << 17))),
  BitBoard((              (1ULL <<  9) | (1ULL << 19) | (1ULL << 29) | (1ULL << 39) | (1ULL << 49) | (1ULL << 59)), ((1ULL << 6) | (1ULL << 16))),
  BitBoard((                             (1ULL << 18) | (1ULL << 28) | (1ULL << 38) | (1ULL << 48) | (1ULL << 58)), ((1ULL << 5) | (1ULL << 15))),
  BitBoard((                                            (1ULL << 27) | (1ULL << 37) | (1ULL << 47) | (1ULL << 57)), ((1ULL << 4) | (1ULL << 14))),
  BitBoard((                                                           (1ULL << 36) | (1ULL << 46) | (1ULL << 56)), ((1ULL << 3) | (1ULL << 13))),
  BitBoard((                                                                          (1ULL << 45) | (1ULL << 55)), ((1ULL << 2) | (1ULL << 12))),
  BitBoard(                                                                                          (1ULL << 54) , ((1ULL << 1) | (1ULL << 11))),
  BitBoard(                                                                                                      0, ( 1ULL       | (1ULL << 10))),
  BitBoard(                                                                                                      0,                (1ULL <<  9))
};

const BitBoard
PromotableMaskTable[kNumberOfColor] =
{
  BitBoard(0x7FFFFFFULL, 0), BitBoard(0x7FC0000000000000ULL, 0x3FFFFULL)
};

const BitBoard
NotPromotableMaskTable[kNumberOfColor] =
{
  BitBoard(0x7FFFFFFFF8000000ULL, 0x3FFFFULL), BitBoard(0x3FFFFFFFFFFFFFULL, 0)
};

const BitBoard
MustPromoteMaskTable[kNumberOfColor] =
{
  BitBoard(0x3FFFFULL, 0), BitBoard(0, 0x3FFFFULL)
};

const BitBoard
KnightDropableMaskTable[kNumberOfColor] =
{
  BitBoard(0x7FFFFFFFFFFC0000ULL, 0x3FFFFULL), BitBoard(0x7FFFFFFFFFFFFFFFULL, 0)
};

const BitBoard
LanceDropableMaskTable[kNumberOfColor] =
{
  BitBoard(0x7FFFFFFFFFFFFE00ULL, 0x3FFFFULL), BitBoard(0x7FFFFFFFFFFFFFFFULL, 0x1FFULL)
};

BitBoard
RookMaskTable[kBoardSquare];
BitBoard
BishopMaskTable[kBoardSquare];
BitBoard
LanceMaskTable[kNumberOfColor][kBoardSquare];

BitBoard
PawnAttacksTable[kNumberOfColor][kBoardSquare];
BitBoard
SilverAttacksTable[kNumberOfColor][kBoardSquare];
BitBoard
GoldAttacksTable[kNumberOfColor][kBoardSquare];
BitBoard
KingAttacksTable[kBoardSquare];
BitBoard
KnightAttacksTable[kNumberOfColor][kBoardSquare];
BitBoard *
LanceAttacksTable[kNumberOfColor][kBoardSquare];
BitBoard *
RookAttacksTable[kBoardSquare];
BitBoard *
BishopAttacksTable[kBoardSquare];
BitBoard
RookStepAttacksTable[kBoardSquare];
BitBoard
BishopStepAttacksTable[kBoardSquare];
BitBoard
BlackLanceTable[2304];
BitBoard 
WhiteLanceTable[2304];
BitBoard 
RookTable[512000];
BitBoard 
BishopTable[20224];

Direction 
DirectionTable[kBoardSquare][kBoardSquare];

BitBoard 
BetweenTable[kBoardSquare][kBoardSquare];

BitBoard
PawnDropableTable[512][kNumberOfColor];

namespace
{
inline void 
set_bit(BitBoard &b, int rank, int file)
{
  if (rank >= kRank1 && rank <= kRank9 && file >= kFile1 && file <= kFile9)
    b ^= MaskTable[rank * kNumberOfFile + file];
}

void
map_bit(BitBoard &b, const BitBoard &m, uint32_t bit)
{
  int i = 0;
  BitBoard mask = m;
  b.init();
  while (mask.test())
  {
    Square sq = mask.pop_bit();
    if (bit & (1 << i))
      b ^= MaskTable[sq];
    ++i;
  }
}

void 
set_pawn_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file);
  PawnAttacksTable[kBlack][rank * kNumberOfFile + file] = b;

  b.init();
  set_bit(b, rank + 1, file);
  PawnAttacksTable[kWhite][rank * kNumberOfFile + file] = b;
}

void 
set_silver_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file - 1);
  set_bit(b, rank - 1, file + 1);
  set_bit(b, rank + 1, file - 1);
  set_bit(b, rank + 1, file + 1);
  set_bit(b, rank - 1, file);
  SilverAttacksTable[kBlack][rank * kNumberOfFile + file] = b;

  b.init();
  set_bit(b, rank - 1, file - 1);
  set_bit(b, rank - 1, file + 1);
  set_bit(b, rank + 1, file - 1);
  set_bit(b, rank + 1, file + 1);
  set_bit(b, rank + 1, file);
  SilverAttacksTable[kWhite][rank * kNumberOfFile + file] = b;
}

void 
set_gold_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file - 1);
  set_bit(b, rank - 1, file + 1);
  set_bit(b, rank - 1, file);
  set_bit(b, rank + 1, file);
  set_bit(b, rank, file + 1);
  set_bit(b, rank, file - 1);
  GoldAttacksTable[kBlack][rank * kNumberOfFile + file] = b;

  b.init();
  set_bit(b, rank + 1, file - 1);
  set_bit(b, rank + 1, file + 1);
  set_bit(b, rank + 1, file);
  set_bit(b, rank - 1, file);
  set_bit(b, rank, file + 1);
  set_bit(b, rank, file - 1);
  GoldAttacksTable[kWhite][rank * kNumberOfFile + file] = b;
}

void
set_king_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file - 1);
  set_bit(b, rank - 1, file + 1);
  set_bit(b, rank - 1, file);
  set_bit(b, rank + 1, file - 1);
  set_bit(b, rank + 1, file + 1);
  set_bit(b, rank + 1, file);
  set_bit(b, rank, file + 1);
  set_bit(b, rank, file - 1);
  KingAttacksTable[rank * kNumberOfFile + file] = b;
}

void
set_knight_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 2, file - 1);
  set_bit(b, rank - 2, file + 1);
  KnightAttacksTable[kBlack][rank * kNumberOfFile + file] = b;

  b.init();
  set_bit(b, rank + 2, file - 1);
  set_bit(b, rank + 2, file + 1);
  KnightAttacksTable[kWhite][rank * kNumberOfFile + file] = b;
}

void
set_rook_step_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file);
  set_bit(b, rank + 1, file);
  set_bit(b, rank, file + 1);
  set_bit(b, rank, file - 1);
  RookStepAttacksTable[rank * kNumberOfFile + file] = b;
}

void
set_bishop_step_attacks(int rank, int file)
{
  BitBoard b;
  b.init();
  set_bit(b, rank - 1, file - 1);
  set_bit(b, rank - 1, file + 1);
  set_bit(b, rank + 1, file - 1);
  set_bit(b, rank + 1, file + 1);
  BishopStepAttacksTable[rank * kNumberOfFile + file] = b;
}

void
set_lance_mask(int rank, int file)
{
  BitBoard b;
  b.init();

  for (int i = rank - 1; i > kRank1; --i)
    set_bit(b, i, file);
  LanceMaskTable[kBlack][rank * kNumberOfFile + file] = b;

  b.init();
  for (int i = rank + 1; i < kRank9; ++i)
    set_bit(b, i, file);
  LanceMaskTable[kWhite][rank * kNumberOfFile + file] = b;
}

void
set_rook_mask(int rank, int file)
{
  BitBoard b;
  b.init();

  for (int i = rank - 1; i > kRank1; --i)
    set_bit(b, i, file);

  for (int i = rank + 1; i < kRank9; ++i)
    set_bit(b, i, file);

  for (int i = file - 1; i > kFile1; --i)
    set_bit(b, rank, i);

  for (int i = file + 1; i < kFile9; ++i)
    set_bit(b, rank, i);

  RookMaskTable[rank * kNumberOfFile + file] = b;
}

void
set_bishop_mask(int rank, int file)
{
  BitBoard b;
  b.init();

  if (file <= rank)
  {
    for (int i = 1; file - i > 0; ++i)
      set_bit(b, rank - i, file - i);

    for (int i = 1; rank + i < 8; ++i)
      set_bit(b, rank + i, file + i);
  }
  else
  {
    for (int i = 1; rank - i > 0; ++i)
      set_bit(b, rank - i, file - i);

    for (int i = 1; file + i < 8; ++i)
      set_bit(b, rank + i, file + i);
  }

  if (file + rank >= 8)
  {
    for (int i = 1; file + i < 8; ++i)
      set_bit(b, rank - i, file + i);

    for (int i = 1; rank + i < 8; ++i)
      set_bit(b, rank + i, file - i);
  }
  else
  {
    for (int i = 1; rank - i > 0; ++i)
      set_bit(b, rank - i, file + i);

    for (int i = 1; file - i > 0; ++i)
      set_bit(b, rank + i, file - i);
  }
  BishopMaskTable[rank * kNumberOfFile + file] = b;
}

void
set_lance_attacks(int rank, int file)
{
  BitBoard b;
  BitBoard occupied;
  int table_size = 0;
  const int lance_bits[2][9] =
  { 
    {0, 0, 1, 2, 3, 4, 5, 6, 7},
    {7, 6, 5, 4, 3, 2, 1, 0, 0}
  };

  for (int bit = 0; bit < (1 << lance_bits[kBlack][rank]); ++bit)
  {
    b.init();
    map_bit(occupied, LanceMaskTable[kBlack][rank * kNumberOfFile + file], bit);

    for (int i = 1; rank - i >= 0; ++i)
    {
      set_bit(b, rank - i, file);
      if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file]).test())
        break;
    }
    LanceAttacksTable[kBlack][rank * kNumberOfFile + file][occupied.magic_index(LanceMaskTable[kBlack][rank * kNumberOfFile + file])] = b;
    ++table_size;
  }
  
  if (rank != 8 || file != 8)
    LanceAttacksTable[kBlack][rank * kNumberOfFile + file + 1] = LanceAttacksTable[kBlack][rank * kNumberOfFile + file] + table_size;

  table_size = 0;
  for (int bit = 0; bit < (1 << lance_bits[kWhite][rank]); ++bit)
  {
    b.init();
    map_bit(occupied, LanceMaskTable[kWhite][rank * kNumberOfFile + file], bit);
    for (int i = 1; rank + i <= 8; ++i)
    {
      set_bit(b, rank + i, file);
      if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file]).test())
        break;
    }
    LanceAttacksTable[kWhite][rank * kNumberOfFile + file][occupied.magic_index(LanceMaskTable[kWhite][rank * kNumberOfFile + file])] = b;
    ++table_size;
  }

  if (rank != 8 || file != 8)
    LanceAttacksTable[kWhite][rank * kNumberOfFile + file + 1] = LanceAttacksTable[kWhite][rank * kNumberOfFile + file] + table_size;
}

void
set_rook_attacks(int rank, int file)
{
  BitBoard b;
  int table_size = 0;
  const int rook_bits[] = 
  {
    14, 13, 13, 13, 13, 13, 13, 13, 14,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    13, 12, 12, 12, 12, 12, 12, 12, 13,
    14, 13, 13, 13, 13, 13, 13, 13, 14
  };
  
  for (uint32_t bit = 0; bit < (1U << rook_bits[rank * kNumberOfFile + file]); ++bit)
  {
    BitBoard occupied;
    map_bit(occupied, RookMaskTable[rank * kNumberOfFile + file], bit);
    b.init();

    for (int i = 1; rank - i >= 0; ++i)
    {
      set_bit(b, rank - i, file);
      if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file]).test())
        break;
    }

    for (int i = 1; rank + i <= 8; ++i)
    {
      set_bit(b, rank + i, file);
      if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file]).test())
        break;
    }

    for (int i = 1; file - i >= 0; ++i)
    {
      set_bit(b, rank, file - i);
      if ((occupied & MaskTable[rank * kNumberOfFile + file - i]).test())
        break;
    }

    for (int i = 1; file + i <= 8; ++i)
    {
      set_bit(b, rank, file + i);
      if ((occupied & MaskTable[rank * kNumberOfFile + file + i]).test())
        break;
    }
    RookAttacksTable[rank * kNumberOfFile + file][occupied.magic_index(RookMaskTable[rank * kNumberOfFile + file])] = b;
    ++table_size;
  }

  if (rank != 8 || file != 8)
    RookAttacksTable[rank * kNumberOfFile + file + 1] = RookAttacksTable[rank * kNumberOfFile + file] + table_size;
}

void
set_bishop_attacks(int rank, int file)
{
  BitBoard b;
  const int bishop_bits[] = 
  {
    7,  6,  6,  6,  6,  6,  6,  6,  7,
    6,  6,  6,  6,  6,  6,  6,  6,  6,
    6,  6,  8,  8,  8,  8,  8,  6,  6,
    6,  6,  8, 10, 10, 10,  8,  6,  6,
    6,  6,  8, 10, 12, 10,  8,  6,  6,
    6,  6,  8, 10, 10, 10,  8,  6,  6,
    6,  6,  8,  8,  8,  8,  8,  6,  6,
    6,  6,  6,  6,  6,  6,  6,  6,  6,
    7,  6,  6,  6,  6,  6,  6,  6,  7
  };
  
  int table_size = 0;
  for (uint32_t bit = 0; bit < (1U << bishop_bits[rank * kNumberOfFile + file]); ++bit)
  {
    BitBoard occupied;
    map_bit(occupied, BishopMaskTable[rank * kNumberOfFile + file], bit);
    b.init();
    if (file <= rank)
    {
      for (int i = 1; file - i >= 0; ++i)
      {
        set_bit(b, rank - i, file - i);
        if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file - i]).test())
          break;
      }

      for (int i = 1; rank + i <= 8; ++i)
      {
        set_bit(b, rank + i, file + i);
        if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file + i]).test())
          break;
      }
    }
    else
    {
      for (int i = 1; rank - i >= 0; ++i)
      {
        set_bit(b, rank - i, file - i);
        if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file - i]).test())
          break;
      }

      for (int i = 1; file + i <= 8; ++i)
      {
        set_bit(b, rank + i, file + i);
        if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file + i]).test())
          break;
      }
    }   

    if (file + rank >= 8)
    {
      for (int i = 1; file + i <= 8; ++i)
      {
        set_bit(b, rank - i, file + i);
        if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file + i]).test())
          break;
      }

      for (int i = 1; rank + i <= 8; ++i)
      {
        set_bit(b, rank + i, file - i);
        if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file - i]).test())
          break;
      }
    }
    else
    {
      for (int i = 1; rank - i >= 0; ++i)
      {
        set_bit(b, rank - i, file + i);
        if ((occupied & MaskTable[(rank - i) * kNumberOfFile + file + i]).test())
          break;
      }

      for (int i = 1; file - i >= 0; ++i)
      {
        set_bit(b, rank + i, file - i);
        if ((occupied & MaskTable[(rank + i) * kNumberOfFile + file - i]).test())
          break;
      }
    }
    BishopAttacksTable[rank * kNumberOfFile + file][occupied.magic_index(BishopMaskTable[rank * kNumberOfFile + file])] = b;
    ++table_size;
  }

  if (rank != 8 || file != 8)
    BishopAttacksTable[rank * kNumberOfFile + file + 1] = BishopAttacksTable[rank * kNumberOfFile + file] + table_size;
}

void 
set_pawn_dropable()
{
  for (uint16_t i = 0; i < 512; ++i)
  {
    BitBoard b(0, 0);
    for (int j = 0; j < 9; ++j)
    {
      if (((i >> j) & 1) == 0)
        b = b | FileMaskTable[j];
    }
    BitBoard b_black = b;
    b_black.not_and(RankMaskTable[kRank1]);
    PawnDropableTable[i][kBlack] = b_black;
    BitBoard b_white = b;
    b_white.not_and(RankMaskTable[kRank9]);
    PawnDropableTable[i][kWhite] = b_white;
  }
}

void
initialize_attacks()
{
  BishopAttacksTable[0] = BishopTable;
  RookAttacksTable[0] = RookTable;
  LanceAttacksTable[kBlack][0] = BlackLanceTable;
  LanceAttacksTable[kWhite][0] = WhiteLanceTable;

  for (int rank = 0; rank < kNumberOfRank; rank++)
  {
    for (int file = 0; file < kNumberOfFile; file++)
    {
      set_pawn_attacks(rank, file);
      set_gold_attacks(rank, file);
      set_king_attacks(rank, file);
      set_knight_attacks(rank, file);
      set_silver_attacks(rank, file);
      set_bishop_step_attacks(rank, file);
      set_rook_step_attacks(rank, file);

      set_lance_mask(rank, file);
      set_rook_mask(rank, file);
      set_bishop_mask(rank, file);

      set_lance_attacks(rank, file);
      set_rook_attacks(rank, file);
      set_bishop_attacks(rank, file);
    }
  }
  set_pawn_dropable();
}

void 
initialize_direction()
{
  for (int from = 0; from < kBoardSquare; from++)
  {
    File from_file = FilePositionTable[from];
    Rank from_rank = RankPositionTable[from];
    for (int to = 0; to < kBoardSquare; to++)
    {
      File to_file = FilePositionTable[to];
      Rank to_rank = RankPositionTable[to];

      if (from == to)
        DirectionTable[from][to] = kDirMisc;
      else if (from_file == to_file)
        DirectionTable[from][to] = kDirFile;
      else if (from_rank == to_rank)
        DirectionTable[from][to] = kDirRank;
      else if (static_cast<int>(to_file - from_file) == static_cast<int>(from_rank - to_rank))
        DirectionTable[from][to] = kDirRight45;
      else if (static_cast<int>(to_file - from_file) == static_cast<int>(to_rank - from_rank))
        DirectionTable[from][to] = kDirLeft45;
      else
        DirectionTable[from][to] = kDirMisc;
    }
  }

  for (int from = 0; from < kBoardSquare; from++)
  {
    for (int to = 0; to < kBoardSquare; to++)
    {
      switch (DirectionTable[from][to])
      {
      case kDirRank:
      case kDirFile:
        BetweenTable[from][to] = rook_attack(MaskTable[to], static_cast<Square>(from)) & rook_attack(MaskTable[from], static_cast<Square>(to));
        break;
      case kDirRight45:
      case kDirLeft45:
        BetweenTable[from][to] = bishop_attack(MaskTable[to], static_cast<Square>(from)) & bishop_attack(MaskTable[from], static_cast<Square>(to));
        break;
      default:
        BetweenTable[from][to].init();
        break;
      }
    }
  }
}
} // namespace

void 
BitBoard::initialize()
{
  initialize_attacks();
  initialize_direction();
}

void
BitBoard::print() const
{
  int i, j;
  int num, index;
  char table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I'};
  uint64_t mask = 1;

  std::cout << "  9 8 7 6 5 4 3 2 1" << std::endl;
  for (i = 0; i < 9; i++)
  {
    std::cout << table[i];
    if (mask == (1ULL << (9 * 7)))
      mask = 1;
    if (i < 7)
      index = 0;
    else
      index = 1;
    for (j = 0; j < 9; j++)
    {
      num = (this->u64_[index] & mask) ? 1 : 0;
      std::cout << " " << num;
      mask = mask << 1;
    }
    std::cout << std::endl;
  }
}
