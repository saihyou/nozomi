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

#ifndef _LEARN_H_
#define _LEARN_H_

#include <vector>
#include <atomic>
#include <set>
#include <algorithm>
#include <mutex>
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "move_generator.h"

struct BoardPosition
{
  BoardPosition() : x(9), y(9) {}
  BoardPosition(Square sq)
  {
    x = sq % 9;
    y = sq / 9;
  }

  Square
  square() const
  {
    return Square(y * 9 + x);
  }

  Square
  inverse_square() const
  {
    int inverse_x = kFile9 - x;
    return Square(y * 9 + inverse_x);
  }

  Square
  lower_square() const
  {
    int lower_x = x;
    if (x > kFile5)
      lower_x = kFile9 - x;
    return Square(y * 9 + lower_x);
  }

  Square
  inverse_black_white() const
  {
    return Square(kBoardSquare - 1 - square());
  }

  int x;
  int y;
};

struct KingPosition : BoardPosition
{
  KingPosition(Square sq)
  {
    x = sq % 9;
    if (x > kFile5)
    {
      x = kFile9 - x;
      swap = true;
    }
    y = sq / 9;
  }
  bool swap = false;
};

const int
KPPIndexTable[] =
{
  Eval::kFHandPawn,
  Eval::kEHandPawn,
  Eval::kFHandLance,
  Eval::kEHandLance,
  Eval::kFHandKnight,
  Eval::kEHandKnight,
  Eval::kFHandSilver,
  Eval::kEHandSilver,
  Eval::kFHandGold,
  Eval::kEHandGold,
  Eval::kFHandBishop,
  Eval::kEHandBishop,
  Eval::kFHandRook,
  Eval::kEHandRook,
  Eval::kFPawn,
  Eval::kEPawn,
  Eval::kFLance,
  Eval::kELance,
  Eval::kFKnight,
  Eval::kEKnight,
  Eval::kFSilver,
  Eval::kESilver,
  Eval::kFGold,
  Eval::kEGold,
  Eval::kFBishop,
  Eval::kEBishop,
  Eval::kFHorse,
  Eval::kEHorse,
  Eval::kFRook,
  Eval::kERook,
  Eval::kFDragon,
  Eval::kEDragon,
  Eval::kFEEnd
};

inline int
kpp_index_begin(int i)
{
  return *(std::upper_bound(std::begin(KPPIndexTable), std::end(KPPIndexTable), i) - 1);
}

inline int
inverse_file_kpp_index(int i)
{
  if (i < Eval::kFEHandEnd)
    return i;

  const int begin = kpp_index_begin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<int>(begin + pos.inverse_square());
}

inline int
lower_file_kpp_index(int i)
{
  if (i < Eval::kFEHandEnd)
    return i;

  const int begin = kpp_index_begin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<int>(begin + pos.lower_square());
}


struct KppIndex
{
  KppIndex(Square k, int in_i, int in_j)
  {
    if (in_i == in_j)
    {
      i = 0;
      j = 0;
      king = kBoardSquare;
      return;
    }

    if (in_j < in_i)
      std::swap(in_i, in_j);
    KingPosition kp(k);
    if (kp.swap)
    {
      in_i = inverse_file_kpp_index(in_i);
      in_j = inverse_file_kpp_index(in_j);
      if (in_j < in_i)
        std::swap(in_i, in_j);
    }
    else if (kp.x == kFile5)
    {
      if (in_i >= Eval::kFPawn)
      {
        const int begin = kpp_index_begin(in_i);
        const BoardPosition i_pos(static_cast<Square>(in_i - begin));
        if (i_pos.x > kFile5)
        {
          in_i = begin + i_pos.inverse_square();
          in_j = inverse_file_kpp_index(in_j);
        }
        else if (i_pos.x == kFile5)
        {
          in_j = lower_file_kpp_index(in_j);
        }
        if (in_j < in_i)
          std::swap(in_i, in_j);
      }
    }
    i = in_i;
    j = in_j;
    king = kp.square();
  }

  Square king;
  int i;
  int j;
};

#endif
