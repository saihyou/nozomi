/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

  This code is based on Stockfish (Chess playing engin).
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef _MOVE_GENERATOR_H_
#define _MOVE_GENERATOR_H_

#include <algorithm>

#include "types.h"
#include "move.h"

enum GenType
{
  kCaptures,
  kQuiets,
  kEvasions,
  kNonEvasions,
  kChecks,
  kQuietChecks,
  kLegalForSearch,
  kLegal
};

class Position;

template<GenType>
ExtMove *
generate(const Position &pos, ExtMove *mlist);

Move
search_mate1ply(Position &pos);

template<GenType T>
struct MoveList
{
  explicit MoveList(const Position& pos)
  :
  cur(mlist), last(generate<T>(pos, mlist))
  {
    last->move = kMoveNone;
  }

  void
  operator++()
  {
    ++cur;
  }

  Move
  operator*() const
  {
    return cur->move;
  }

  const ExtMove *
  begin() const
  {
    return mlist;
  }

  const ExtMove *
  end() const
  {
    return last;
  }

  size_t
  size() const
  {
    return last - mlist;
  }

  bool
  contains(Move m) const
  {
    return std::find(begin(), end(), m) != end();
  }

  Move
  operator[](int n)
  {
    return mlist[n];
  }

private:
  ExtMove mlist[kMaxMoves];
  ExtMove *cur;
  ExtMove *last;
};

#endif
